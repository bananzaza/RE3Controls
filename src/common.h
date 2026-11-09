#pragma once
#define WIN32_LEAN_AND_MEAN

// Developer build (build.sh dev): dev server, shared-memory bridge with a virtual pad, screenshots. Release builds
// leave all of it out.
#define RE3C_VERSION_STR "1.9.5mod"

#ifndef RE3C_DEV
#define RE3C_DEV 0
#endif
#include <windows.h>
#include <stdint.h>

// XInput types (declared here so we don't depend on an SDK xinput.h version).
struct XGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct XState {
    DWORD dwPacketNumber;
    XGamepad Gamepad;
};

enum : WORD {
    XB_DPAD_UP = 0x0001, XB_DPAD_DOWN = 0x0002, XB_DPAD_LEFT = 0x0004, XB_DPAD_RIGHT = 0x0008,
    XB_START = 0x0010, XB_BACK = 0x0020, XB_LTHUMB = 0x0040, XB_RTHUMB = 0x0080,
    XB_LB = 0x0100, XB_RB = 0x0200, XB_GUIDE = 0x0400,
    XB_A = 0x1000, XB_B = 0x2000, XB_X = 0x4000, XB_Y = 0x8000,
};

// Shared memory bridge used by the dev tools to drive a virtual pad and read mod status.
// Name: Local\RE3Controls_Bridge. Layout must match tools/bridge.py.
#define RE3C_BRIDGE_NAME "Local\\RE3Controls_Bridge"
#define RE3C_BRIDGE_MAGIC 0x43334552u  // 'RE3C'
#pragma pack(push, 1)
// One frame of the dev trace: frame = kbm_frame, v[] see aimwalk.cpp TraceFrame (player slot 0).
struct TraceRow {
    uint32_t frame;
    float v[15];
};
// Dev keyboard/mouse block (devinput.cpp, tools/kbm.py): written by the tools unless noted.
struct BridgeKbm {
    uint32_t kbd_mode;       // 0 off, 1 the virtual keys replace the real ones, 2 OR onto them
    uint32_t vk[8];          // virtual key bitmap, Windows VK codes (bit vk&31 of vk[vk>>5])
    uint32_t mouse_mode;     // 0 off, 1 the virtual buttons replace the real ones, 2 OR
    uint32_t mouse_buttons;  // bit 0 left, 1 right, 2 middle, 3 X1, 4 X2
    int32_t mouse_dx, mouse_dy, mouse_wheel;  // raw DirectInput counts per frame (wheel: 120 per notch)
    uint32_t mouse_frames;   // frames left to apply the deltas (0xFFFFFFFF = until changed); write it last
    int32_t cursor_x, cursor_y;  // cursor in render pixels (INT32_MIN = the centre of the render area)
    uint32_t pad_disconnect;  // 1 = XInput users 0/1 report "not connected"
    uint32_t window_active;   // 0 = the game's focus check always passes (dev default), 1 = real, 2 = never
    // written by the mod
    uint32_t kbm_frame;      // incremented on every sKeyboard update
    int32_t applied_dx, applied_dy;  // what was added to sMouse this frame (after MouseBaseSpeed)
    uint32_t last_device;    // sPad+0x5F0 (1 pad, 0 keyboard/mouse)
    uint32_t input_locked;   // sPad+0x5DC
    uint32_t reserved[16];
};
struct BridgeShared {
    uint32_t magic;
    uint32_t version;
    // written by tools
    uint32_t vpad_enabled;      // 1 = replace pad 0 with vpad, 2 = OR vpad onto real pad 0
    XGamepad vpad;
    uint32_t vpad_seq;
    // written by the mod
    uint32_t mod_frame;          // incremented on every XInputGetState(0)
    XGamepad last_real;          // last physical pad 0 state
    XGamepad last_out;           // last state handed to the game
    uint32_t hooks_ok;
    uint32_t shot_req;           // tools bump this to request a back-buffer screenshot
    uint32_t shot_done;          // mod sets this to shot_req when the file is written
    uint32_t reserved[62];
    BridgeKbm kbm;           // offset 316
    // Per-frame trace of the local player (dev, tools/trace.py); the mod writes rows while trace_on is set.
    uint32_t trace_on;       // offset 476
    uint32_t trace_head;     // rows written so far (ring of 256)
    TraceRow trace[256];
};
#pragma pack(pop)

void Log(const char* fmt, ...);

// True if [p, p+n) is committed readable memory.
// Hot path: called hundreds of times per frame. VirtualQuery is a kernel transition, so recent positive results are
// cached. Entries expire after a few milliseconds: memory is freed and committed all the time (chapter restarts,
// level loads), so a region that was readable a moment ago may be gone now, and a stale "yes" means reading freed
// memory. Negative results are never cached (the same region can be committed later). The table is shared by the
// game's worker threads (split screen), hence the spin lock around it.
inline bool Readable(const void* p, size_t n) {
    if (!p || n == 0) return false;
    const uintptr_t start = (uintptr_t)p;
    const uintptr_t end = start + n;
    if (end < start) return false;

    struct Region {
        uintptr_t base, end;
        DWORD tick;
    };
    static Region cache[16];
    static unsigned cache_i = 0;
    static volatile LONG lock = 0;
    const DWORD now = GetTickCount();

    while (InterlockedExchange(&lock, 1)) YieldProcessor();
    for (unsigned i = 0; i < 16; ++i) {
        const Region& r = cache[i];
        if (r.end == 0 || (DWORD)(now - r.tick) > 32) continue;
        if (start >= r.base && end <= r.end) {
            InterlockedExchange(&lock, 0);
            return true;
        }
    }
    InterlockedExchange(&lock, 0);

    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    // PAGE_EXECUTE is included on purpose: on x86 a user-mode execute-only page is still readable, and the game maps
    // some code pages that way.  Without it every SafeEqual() signature check on such a page failed and the hooks
    // behind it were skipped (one run showed camera ctrl, bind-slot, camera aim ray, pad aim and the reticle all
    // failing while the neighbouring functions hooked fine).
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE | PAGE_EXECUTE_READ |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & readable)) return false;
    const uintptr_t rbase = (uintptr_t)mbi.BaseAddress;
    const uintptr_t rend = rbase + mbi.RegionSize;

    while (InterlockedExchange(&lock, 1)) YieldProcessor();
    cache[cache_i++ & 15] = {rbase, rend, now};
    InterlockedExchange(&lock, 0);
    return end <= rend;
}
inline bool SafeEqual(const void* p, const void* sig, size_t n) {
    return Readable(p, n) && memcmp(p, sig, n) == 0;
}
// Hook-site check that tolerates an entry another component already hooked (RE5Fix's dinput8.dll patches code long
// before this mod runs).  A jump opcode at the entry means someone else owns the prologue; MinHook can still chain a
// detour on top of it, and skipping the hook silently degraded whole features (bind-slot = the leg layer, pad aim).
inline bool HookSite(const void* p, const void* sig, size_t n) {
    if (SafeEqual(p, sig, n)) return true;
    if (!Readable(p, 1)) {
        Log("hook site %08x: not readable", (unsigned)(uintptr_t)p);
        return false;
    }
    const uint8_t* b = (const uint8_t*)p;
    if (b[0] == 0xE9 || b[0] == 0xEB) return true;  // someone else hooked it: chain on top
    // Not our code at all: print what is there so the patch can be adapted instead of guessed.  The bytes before the
    // first mismatch are printed as well, because RE5Fix often leaves the prologue intact and changes a later byte.
    char want[3 * 16 + 1] = {}, got[3 * 16 + 1] = {};
    const size_t shown = n < 16 ? n : 16;
    for (size_t i = 0; i < shown; ++i) {
        const char* hex = "0123456789ABCDEF";
        want[i * 3] = hex[((const uint8_t*)sig)[i] >> 4];
        want[i * 3 + 1] = hex[((const uint8_t*)sig)[i] & 15];
        want[i * 3 + 2] = ' ';
        got[i * 3] = hex[b[i] >> 4];
        got[i * 3 + 1] = hex[b[i] & 15];
        got[i * 3 + 2] = ' ';
    }
    Log("hook site %08x differs: want %s / have %s", (unsigned)(uintptr_t)p, want, got);
    return false;
}
BridgeShared* Bridge();
void InitBridge();
void InitXInputProxy();
void InitD3D9Hooks();
const char* ModDir();
LONG XInputCallCount();  // calls the game made to the proxy's XInput exports
#if RE3C_DEV
bool DevMode();  // RE3C_dev.flag next to the DLL: dev server, virtual pad, screenshots
#else
constexpr bool DevMode() { return false; }
#endif
