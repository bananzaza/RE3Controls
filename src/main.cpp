#include "common.h"
#include <stdarg.h>
#include <stdio.h>
#include <share.h>
#include "MinHook.h"
#include "config.h"

static CRITICAL_SECTION g_logLock;
static FILE* g_log = nullptr;
static BridgeShared* g_bridge = nullptr;
static BridgeShared g_bridgeFallback = {};
static char g_modDir[MAX_PATH];

const char* ModDir() { return g_modDir; }

#if RE3C_DEV
bool DevMode() {
    static int cached = -1;
    if (cached < 0) {
        char path[MAX_PATH];
        wsprintfA(path, "%sRE3C_dev.flag", g_modDir);
        cached = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
    }
    return cached == 1;
}
#endif

void Log(const char* fmt, ...) {
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

BridgeShared* Bridge() { return g_bridge ? g_bridge : &g_bridgeFallback; }

// The bridge lets the dev tools drive a virtual pad: only in developer builds.
void InitBridge() {
#if RE3C_DEV
    HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(BridgeShared),
                                  RE3C_BRIDGE_NAME);
    if (!h) {
        Log("bridge: CreateFileMapping failed %lu", GetLastError());
        return;
    }
    g_bridge = (BridgeShared*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BridgeShared));
    if (!g_bridge) {
        Log("bridge: MapViewOfFile failed %lu", GetLastError());
        return;
    }
    g_bridge->magic = RE3C_BRIDGE_MAGIC;
    g_bridge->version = 1;
    Log("bridge: ready");
#endif
}

static void OpenLog(HMODULE self) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\') --n;
    path[n] = 0;
    // Absolute folder of the DLL: the profile API looks up a bare file name in the Windows folder, not here.
    strcpy_s(g_modDir, path);
    strcat_s(path, "RE3Controls.log");
    g_log = _fsopen(path, "w", _SH_DENYWR);
}

// re5dx9.exe is wrapped in Enigma Protector (inside the SteamStub layer). It decrypts the game while it starts and
// checks the image; a patch made in that window makes the game quit with "Program integrity check failed". So the
// mod touches nothing in the game until signals from inside this process say the game proper is running:
// 1. the code is decrypted (a known signature is readable),
// 2. the game's entry point has run (its CRT security cookie has been initialised: 0xE68A06 -> 0xE76877),
// 3. the game's own window exists (class MTFramework, this process - a window of another program with the same
//    title, e.g. Explorer showing the game folder, used to let the mod patch far too early),
// 4. the game polls the pad, and HookDelayMs has passed since the window appeared.
// It never patches on a timeout.
static bool GameCodeReady() {
    const BYTE sig[] = {0x8B, 0x44, 0x24, 0x04, 0xD9, 0x00};  // exe+43A560 camera SetLookAt prologue
    return SafeEqual((const void*)0x0083A560, sig, sizeof(sig));
}

static bool EntryPointPassed() {
    const uint32_t* cookie = (const uint32_t*)0x011AC150;  // __security_cookie, then its complement
    return Readable(cookie, 8) && cookie[0] != 0xBB40E64Eu && cookie[1] == ~cookie[0];
}

static BOOL CALLBACK FindOwnWindow(HWND h, LPARAM found) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    char cls[32];
    if (pid == GetCurrentProcessId() && GetClassNameA(h, cls, sizeof(cls)) && !strcmp(cls, "MTFramework")) {
        *(HWND*)found = h;
        return FALSE;
    }
    return TRUE;
}

static bool OwnGameWindow() {
    HWND h = nullptr;
    EnumWindows(FindOwnWindow, (LPARAM)&h);
    return h != nullptr;
}

static bool WaitFor(bool (*ready)(), DWORD timeoutMs) {
    const DWORD t0 = GetTickCount();
    while (!ready()) {
        if (GetTickCount() - t0 > timeoutMs) return false;
        Sleep(20);
    }
    return true;
}

void InitGameHooks();
void InitPadLock();
void InitGameHooks();
void RedirectGameXInputImports();
void InitDevServer();
void DevPatchWindowActive();

static DWORD WINAPI InitThread(LPVOID) {
#if RE3C_DEV
    InitBridge();
    InitDevServer();
#endif
    if (MH_Initialize() != MH_OK) Log("MinHook init failed");
    const DWORD t0 = GetTickCount();
    if (!WaitFor(GameCodeReady, 120000)) {
        Log("game code NOT detected (unknown exe build?) - the mod stays off");
        return 0;
    }
    Log("game code decrypted (+%lu ms)", GetTickCount() - t0);
    if (!WaitFor(EntryPointPassed, 120000)) {
        Log("the game's entry point was not reached - the mod stays off");
        return 0;
    }
    Log("game entry point passed (+%lu ms)", GetTickCount() - t0);
    if (!WaitFor(OwnGameWindow, 300000)) {
        Log("the game window did not appear - the mod stays off");
        return 0;
    }
    const DWORD tWindow = GetTickCount();
    Log("game window up (+%lu ms)", tWindow - t0);
    LoadConfig();
    const DWORD delay = Cfg().hookDelayMs;
    // The game polls the pad once its main loop runs; if it never does (no XInput at all), go on after 15 s.
    while (GetTickCount() - tWindow < delay || (XInputCallCount() == 0 && GetTickCount() - tWindow < 15000)) Sleep(20);
    Log("hooking the game now (+%lu ms, %ld pad polls so far)", GetTickCount() - t0, XInputCallCount());
    RedirectGameXInputImports();
#if RE3C_DEV
    DevPatchWindowActive();
    if (DevMode()) InitD3D9Hooks();  // back-buffer screenshots for the dev tools only
#endif
    InitPadLock();
    InitGameHooks();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_logLock);
        OpenLog(inst);
        Log("RE3Controls %s loading (pid %lu)", RE3C_DEV ? "dev build" : "v" RE3C_VERSION_STR, GetCurrentProcessId());
        InitXInputProxy();
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
