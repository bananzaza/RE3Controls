// Keyboard and mouse: the mod's RE3 controls for a player on keyboard/mouse.
//
// The game has its own complete keyboard/mouse layer and switches to it by itself (sPad+0x5F0 = 0, the player
// sPad+0x614 owns the keyboard), live, to whatever device was used last. The mod keeps the game in that mode -
// prompts, QTEs, the inventory, key icons, menus, cover, turrets and vehicles stay exactly as the game does them -
// and only takes over what it also takes over on the pad: moving relative to its camera, the camera itself, running,
// the one-button reload, the quick turn, walking while aiming.
//
// Keys: the mod asks the game's own key query (0x79A4D0, held) for the game's actions, so the player's bindings in
// the game's options apply (defaults: WASD, Shift run, C quick turn, RMB aim, Space knife, LMB fire, R reload,
// V partner, Q locate partner, M map, E inventory; F answers prompts natively). The middle mouse button puts the
// camera behind the character.
//
// The game also reads some keys itself inside the character code: the command builder adds WASD to its own copy of
// the left stick and keys 9/10 to the right one, turns WASD+run into commands, reads quick turn (6) and reload (0x12),
// and in the keyboard aim WASD turn the aim. While the mod drives the player those reads answer "not pressed" (the
// keys 9/10 only while the free camera turns the view, the aim's WASD only while WASD walk in the aim, reload and
// quick turn only while the mod's own versions are on); they are recognised by their return address (each one checked
// at install), so every other reader - prompts, QTEs, the inventory, partner orders, stage scripts, vehicles, the
// sniper scope - keeps working.
//
// Mouse: the game's own mouse fields in the character's PlayerPad (+0x20C/+0x210: raw counts x MouseBaseSpeed x the
// in-game sensitivity), which the game already zeroes while paused or in the background. Outside the aim they turn
// the mod's camera at exactly the angle per unit of the game's own mouse aim, so the camera and the aim feel the same;
// the game's own mouse turning of the body (+0x200/+0x204) is cleared. In the aim the game's keyboard aim style 0 runs
// as it is: a per-count yaw/pitch with no dead zone, the crosshair in the screen centre, shots converging on the camera
// ray (forced for the player even when the in-game option says "cursor").
//
// A pad player who nudges the mouse is not thrown into keyboard mode: a mouse-motion-only switch needs
// MouseSwitchCounts raw counts within about a quarter of a second (the game's own rule is 2 counts in one frame).
#include <cstdio>
#include "common.h"
#include "re5.h"
#include "config.h"
#include "kbinput.h"
#include <math.h>
#include "MinHook.h"

using namespace re5;

namespace {

constexpr uint32_t kFnKeyOn = 0x0079A4D0;   // stdcall bool(action, player), ret 8
constexpr uint32_t kFnKeyTrg = 0x0079A3E0;  // stdcall bool(action, player), ret 8
constexpr uint32_t kFnAimStyle = 0x00CEE1B0;  // thiscall int(options block; which), ret 4
constexpr uint32_t kFnDeviceSwitch = 0x0079BC50;  // thiscall void(sPad), once per frame before any character
constexpr uint32_t kKeyboardPtr = 0x01249C74;
constexpr uint32_t kMousePtr = 0x01249C78;
constexpr uint32_t kInfoManPtr = 0x011B2158;
constexpr uint32_t kParamPtr = 0x011B27B4;
constexpr uint32_t kRenderPtr = 0x012345D4;
constexpr uint32_t kMainPtr = 0x012340A4;
constexpr uint32_t kSceneFlagPtr = 0x0123457C;  // +0x3085: scripted scenes (DLC) that use the cursor aim

typedef bool(__stdcall* PFN_Key)(int action, int player);
typedef int(__fastcall* PFN_AimStyle)(uint32_t block, void* edx, int which);
typedef void(__fastcall* PFN_Switch)(uint32_t spad, void* edx);
PFN_Key oKeyOn = nullptr, oKeyTrg = nullptr;
PFN_AimStyle oAimStyle = nullptr;
PFN_Switch oSwitch = nullptr;

// Character-code key reads that drive the character (call site + 5 = return address), with the action they read.
struct Site { uint32_t call; int action; };
const Site kHeldSites[] = {
    // cPadEx::update 0xB8A3E0: WASD into its left stick, WASD + run as turn/back commands, reload
    {0xB8A7C0, 0}, {0xB8A7ED, 1}, {0xB8A81A, 2}, {0xB8A847, 3},
    {0xB8B577, 0}, {0xB8B5A4, 1}, {0xB8B5D1, 2}, {0xB8B5FE, 3},
    {0xB8B37D, 0x12}, {0xB8B3A4, 0x12},
};
// The builder's look left/right keys (9/10, unbound by default) into its right stick and the body yaw: only while
// the free camera turns the view (without it the game's own body turning stays, as with the mouse).
const Site kLookSites[] = {
    {0xB8A95F, 9}, {0xB8A98C, 10}, {0xB8AE93, 9}, {0xB8AED2, 10},
};
// The keyboard aim (0xB66DE0) and its helper (0xB85E50) turn the aim with WASD: only while WASD walk there (with
// GunWalk=0 / KnifeWalk=0 they turn the aim as in the game).
const Site kKeyAimSites[] = {
    {0xB67C22, 0}, {0xB67C3B, 1}, {0xB67C70, 0}, {0xB67C9D, 1}, {0xB67CD1, 2}, {0xB67CFE, 3},
    {0xB860BC, 0}, {0xB860E7, 1}, {0xB86112, 2}, {0xB8613D, 3},
};
const Site kPressSites[] = {
    {0xB8B20F, 6},  // quick turn
};
// Further builder reads that the stock code never reaches with the inventory open (it stops at the grid check): while
// the mod lets a keyboard player walk there, the right mouse button must stay the grid's (not raise the gun), X and T
// too.
const Site kInvHeldSites[] = {
    {0xB8B2CA, 0xC}, {0xB8B2E7, 0xC}, {0xB8B319, 0xC}, {0xB8B403, 0x27}, {0xB8B4C2, 0x26},
};
const Site kInvPressSites[] = {
    {0xB8B4A0, 0x26},
};
uint32_t g_heldRet[sizeof(kHeldSites) / sizeof(kHeldSites[0])];
uint32_t g_lookRet[sizeof(kLookSites) / sizeof(kLookSites[0])];
uint32_t g_keyAimRet[sizeof(kKeyAimSites) / sizeof(kKeyAimSites[0])];
uint32_t g_pressRet[sizeof(kPressSites) / sizeof(kPressSites[0])];
uint32_t g_invHeldRet[sizeof(kInvHeldSites) / sizeof(kInvHeldSites[0])];
uint32_t g_invPressRet[sizeof(kInvPressSites) / sizeof(kInvPressSites[0])];
int g_heldCount = 0, g_pressCount = 0, g_invHeldCount = 0, g_invPressCount = 0, g_lookCount = 0, g_keyAimCount = 0;

bool g_driving[4] = {};
DWORD g_drivingTick[4] = {};
bool g_invWalk[4] = {};
bool g_keyAimWalk[4] = {};  // WASD walk in the aim this frame (the game's key aim is off)
bool g_orbit[4] = {};       // the free camera owns the view (the game's look keys are off)

uint32_t Rd(uint32_t a) { return Readable((void*)a, 4) ? *(uint32_t*)a : 0; }

uint32_t SPad() { return Rd(kSPadPtr); }

int PlayerOf(int player) {
    if (player < 0) {
        const uint32_t spad = SPad();
        player = spad && Readable((void*)(spad + 0x614), 4) ? At<int32_t>(spad, 0x614) : -1;
    }
    return player >= 0 && player < 4 ? player : -1;
}

bool Driving(int player, bool inventory = false) {
    player = PlayerOf(player);
    return player >= 0 && g_driving[player] && (!inventory || g_invWalk[player]) &&
           GetTickCount() - g_drivingTick[player] < 500;
}

bool Listed(const uint32_t* list, int n, uint32_t ret) {
    for (int i = 0; i < n; ++i)
        if (list[i] == ret) return true;
    return false;
}

// The game's own reload (0x12) and quick turn (6) stay when the mod's versions are switched off.
bool __stdcall HookKeyOn(int action, int player) {
    const uint32_t ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
    if (Listed(g_heldRet, g_heldCount, ret) && Driving(player) && !(action == 0x12 && !Cfg().oneButtonReload))
        return false;
    if (Listed(g_lookRet, g_lookCount, ret) && Driving(player) && g_orbit[PlayerOf(player)]) return false;
    if (Listed(g_keyAimRet, g_keyAimCount, ret) && Driving(player) && g_keyAimWalk[PlayerOf(player)]) return false;
    if (Listed(g_invHeldRet, g_invHeldCount, ret) && Driving(player, true)) return false;
    const bool origAns = oKeyOn(action, player);
    // Read-only probe: what does the game itself answer for the aim (0xC) and fire (0xE) actions, and for how long after
    // the button is released.  Rate limited; changes no behaviour.
    if (action == 0xC || action == 0xE) {
        static int probeN = 0;
        static bool lmbPrev = false, rmbPrev = false;
        const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool released = (action == 0xC) ? (rmbPrev && !rmb) : (lmbPrev && !lmb);
        if (released || (++probeN % 30) == 0)
            Log("keyq: action=%X orig=%d rmb=%d lmb=%d%s", action, (int)origAns, (int)rmb, (int)lmb,
                released ? " <released>" : "");
        rmbPrev = rmb; lmbPrev = lmb;
    }
    return origAns;
}

bool __stdcall HookKeyTrg(int action, int player) {
    const uint32_t ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
    if (Listed(g_pressRet, g_pressCount, ret) && Driving(player) && !(action == 6 && !Cfg().quickTurn)) return false;
    if (Listed(g_invPressRet, g_invPressCount, ret) && Driving(player, true)) return false;
    return oKeyTrg(action, player);
}

bool SceneCursor() {
    const uint32_t p = Rd(kSceneFlagPtr);
    return p && Readable((void*)(p + 0x3085), 1) && At<uint8_t>(p, 0x3085) != 0;
}

int __fastcall HookAimStyle(uint32_t block, void* edx, int which) {
    const Config& c = Cfg();
    if (which == 0 && c.enabled && c.keyboardMouse && c.mouseAimRE3 && !c.meleeOnly && !SceneCursor()) return 0;
    return oAimStyle(block, edx, which);
}

// ---------------------------------------------------------------------------------------------------- device switch
double g_motion = 0;  // raw mouse counts, decaying over ~0.25 s
LARGE_INTEGER g_lastSwitch = {};

void __fastcall HookDeviceSwitch(uint32_t spad, void* edx) {
    const uint8_t before = Readable((void*)(spad + 0x5F0), 1) ? At<uint8_t>(spad, 0x5F0) : 1;
    oSwitch(spad, edx);
    const Config& c = Cfg();
    const uint32_t mouse = Rd(kMousePtr), kb = Rd(kKeyboardPtr);
    if (!mouse || !kb || !Readable((void*)(mouse + 0x20), 0x80) || !Readable((void*)(kb + 0x160), 0x20)) return;
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    const double dt = g_lastSwitch.QuadPart ? (double)(now.QuadPart - g_lastSwitch.QuadPart) / f.QuadPart : 0.0;
    g_lastSwitch = now;
    const float base = At<float>(mouse, 0x9C) > 0.01f ? At<float>(mouse, 0x9C) : 1.0f;
    const double raw = (fabs((double)At<int32_t>(mouse, 0x2C)) + fabs((double)At<int32_t>(mouse, 0x30))) / base;
    g_motion = g_motion * exp(-(dt > 1.0 ? 1.0 : dt) / 0.25) + raw;
    if (!c.enabled || !c.keyboardMouse || c.meleeOnly || c.mouseSwitchCounts <= 0 || before != 1 ||
        At<uint8_t>(spad, 0x5F0) != 0)
        return;
    // Switched to keyboard this frame. Keep the pad when only the mouse moved, a little, and a pad is there.
    const uint32_t* trg = (const uint32_t*)(kb + 0x160);
    bool key = false;
    for (int i = 0; i < 8; ++i) {
        uint32_t t = trg[i];
        if (i == 0) t &= ~(1u << 0x1B);  // Esc
        if (i == 1) t &= ~(1u << (0x24 - 32));  // Home
        key |= t != 0;
    }
    const bool click = At<uint32_t>(mouse, 0x40) != 0;
    const bool wheel = At<int32_t>(mouse, 0x34) != 0;
    bool pad = false;
    for (int i = 0; i < 2; ++i) {
        const uint32_t p = spad + spad::Pads + i * spad::PadStride;
        pad |= At<uint8_t>(p, 4) != 0 && At<uint8_t>(p, spad::PadType) != 0;
    }
    const bool locked = At<uint8_t>(spad, spad::InputLocked) != 0;
    if (pad && !key && !click && !wheel && (locked || g_motion < c.mouseSwitchCounts)) At<uint8_t>(spad, 0x5F0) = 1;
}

bool HookAt(uint32_t addr, const BYTE* sig, size_t n, void* detour, void** orig, const char* name) {
    if (!SafeEqual((void*)addr, sig, n) || MH_CreateHook((void*)addr, detour, orig) != MH_OK ||
        MH_QueueEnableHook((void*)addr) != MH_OK) {
        Log("keyboard: hook %s failed", name);
        return false;
    }
    return true;
}

int CollectSites(const Site* sites, int n, uint32_t fn, uint32_t* out) {
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const uint32_t s = sites[i].call;
        // call rel32 to the query, with the expected action pushed shortly before ("push imm8")
        const bool call = Readable((void*)(s - 12), 17) && At<uint8_t>(s, 0) == 0xE8 && s + 5 + At<int32_t>(s, 1) == fn;
        bool action = false;
        for (uint32_t a = s - 12; call && a + 1 < s; ++a)
            action |= At<uint8_t>(a, 0) == 0x6A && At<int8_t>(a, 1) == sites[i].action;
        if (call && action) out[k++] = s + 5;
        else Log("keyboard: unexpected code at %08x, left to the game", s);
    }
    return k;
}

}  // namespace

bool KbLayerFor(uint32_t ch) {
    const Config& c = Cfg();
    if (!c.enabled || !c.keyboardMouse || !Readable((void*)ch, 0x3000) || At<uint32_t>(ch, ch::NetControl) != 0)
        return false;
    const uint32_t spad = SPad();
    return spad && Readable((void*)spad, 0x620) && At<uint8_t>(spad, spad::DeviceIsPad) == 0 &&
           At<int32_t>(spad, spad::KeyboardPlayer) == (int32_t)At<uint8_t>(ch, ch::NetSlot);
}

void KbSetDriving(uint32_t player, bool driving, bool inventory, bool keyAimWalk, bool orbit) {
    if (player > 3) return;
    g_driving[player] = driving;
    g_invWalk[player] = driving && inventory;
    g_keyAimWalk[player] = keyAimWalk;
    g_orbit[player] = orbit;
    g_drivingTick[player] = GetTickCount();
}

bool KbRead(uint32_t ch, uint32_t padObj, KbFrame* f) {
    *f = KbFrame{};
    const uint32_t spad = SPad();
    if (!spad || !Readable((void*)spad, 0x620) || At<uint8_t>(spad, spad::InputLocked)) return false;
    const int p = At<uint8_t>(ch, ch::NetSlot);
    auto on = [&](int action) { return oKeyOn ? oKeyOn(action, p) : ((PFN_Key)kFnKeyOn)(action, p); };
    f->aim = on(0xC);
    f->knife = on(0xD);
    f->fire = on(0xE);
    f->run = on(4);
    f->reload = on(0x12);
    f->partner = on(0x10);
    f->locate = on(0xB);
    f->map = on(0x15);
    f->inventory = on(0x16);
    f->quickTurn = on(6);
    const float x = (on(3) ? 1.0f : 0.0f) - (on(2) ? 1.0f : 0.0f);
    const float y = (on(0) ? 1.0f : 0.0f) - (on(1) ? 1.0f : 0.0f);
    const float len = sqrtf(x * x + y * y);
    if (len > 0.0f) {
        f->moveX = x / len;
        f->moveY = y / len;
    }
    // Middle mouse button: the game cannot bind it; like the game's own mouse-button keys it needs the cursor in
    // the window (sPad+0x610).
    const uint32_t mouse = Rd(kMousePtr);
    if (mouse && Readable((void*)(mouse + 0x38), 4) && At<uint8_t>(spad, 0x610))
        f->cameraReset = (At<uint32_t>(mouse, 0x38) & 4) != 0;
    if (Readable((void*)(padObj + 0x200), 0x18)) {
        f->mouseX = At<float>(padObj, 0x20C);
        f->mouseY = At<float>(padObj, 0x210);
    }
    return true;
}

float MouseUnitRadians() {
    const uint32_t sp = Rd(kParamPtr), r = Rd(kRenderPtr);
    const float speed = sp && Readable((void*)(sp + 0x42C), 4) ? At<float>(sp, 0x42C) : 5.0f;
    const int w = r && Readable((void*)(r + 0x50), 4) ? At<int32_t>(r, 0x50) : 1280;
    return 0.02f * speed * 0.017453293f * 1.2f * ((float)(w > 0 ? w : 1280) / 1280.0f);
}

uint32_t MouseInvert() {
    const uint32_t im = Rd(kInfoManPtr);
    if (!im || !Readable((void*)(im + 0x20), 4)) return 0;
    const uint32_t idx = At<uint32_t>(im, 0x20);
    const uint32_t a = im + 0x9C + idx * 0x8C74;
    return idx < 4 && Readable((void*)a, 4) ? At<uint32_t>(a, 0) & 3 : 0;
}

bool JustActivated() {
    const uint32_t m = Rd(kMainPtr);
    return m && Readable((void*)(m + 0x1054C), 1) && At<uint8_t>(m, 0x1054C) != 0;
}

void KbSyncTargetYaw(uint32_t ch) {
    const float* q = (const float*)(ch + ch::Quat);
    At<float>(ch, 0x2A10) = 2.0f * atan2f(q[1], q[3]);
    At<uint8_t>(ch, 0x2A14) = 0;
}

void InitKeyboardHooks() {
    g_heldCount = CollectSites(kHeldSites, sizeof(kHeldSites) / sizeof(kHeldSites[0]), kFnKeyOn, g_heldRet);
    g_lookCount = CollectSites(kLookSites, sizeof(kLookSites) / sizeof(kLookSites[0]), kFnKeyOn, g_lookRet);
    g_keyAimCount = CollectSites(kKeyAimSites, sizeof(kKeyAimSites) / sizeof(kKeyAimSites[0]), kFnKeyOn, g_keyAimRet);
    g_pressCount = CollectSites(kPressSites, sizeof(kPressSites) / sizeof(kPressSites[0]), kFnKeyTrg, g_pressRet);
    g_invHeldCount = CollectSites(kInvHeldSites, sizeof(kInvHeldSites) / sizeof(kInvHeldSites[0]), kFnKeyOn, g_invHeldRet);
    g_invPressCount =
        CollectSites(kInvPressSites, sizeof(kInvPressSites) / sizeof(kInvPressSites[0]), kFnKeyTrg, g_invPressRet);
    static const BYTE sigKey[] = {0x8B, 0x4C, 0x24, 0x08, 0x85, 0xC9, 0xA1, 0x40, 0x9C, 0x24, 0x01, 0x7C, 0x08};
    static const BYTE sigStyle[] = {0x80, 0x7C, 0x24, 0x04, 0x00, 0x75, 0x13, 0xA1, 0x7C, 0x45, 0x23, 0x01};
    static const BYTE sigSwitch[] = {0x56, 0x8B, 0xF1, 0xC6, 0x86, 0x1C, 0x06, 0x00, 0x00, 0x00};
    const bool a = HookAt(kFnKeyOn, sigKey, sizeof(sigKey), (void*)HookKeyOn, (void**)&oKeyOn, "key held");
    const bool b = HookAt(kFnKeyTrg, sigKey, sizeof(sigKey), (void*)HookKeyTrg, (void**)&oKeyTrg, "key pressed");
    const bool s = HookAt(kFnAimStyle, sigStyle, sizeof(sigStyle), (void*)HookAimStyle, (void**)&oAimStyle, "aim style");
    const bool d = HookAt(kFnDeviceSwitch, sigSwitch, sizeof(sigSwitch), (void*)HookDeviceSwitch, (void**)&oSwitch,
                          "device switch");
    Log("keyboard: key reads %s (%d/%d sites), aim style %s, device switch %s", a && b ? "hooked" : "FAILED",
        g_heldCount + g_lookCount + g_keyAimCount + g_pressCount + g_invHeldCount + g_invPressCount,
        (int)(sizeof(kHeldSites) / sizeof(kHeldSites[0]) + sizeof(kLookSites) / sizeof(kLookSites[0]) +
              sizeof(kKeyAimSites) / sizeof(kKeyAimSites[0]) + sizeof(kPressSites) / sizeof(kPressSites[0]) +
              sizeof(kInvHeldSites) / sizeof(kInvHeldSites[0]) + sizeof(kInvPressSites) / sizeof(kInvPressSites[0])),
        s ? "hooked" : "-", d ? "hooked" : "-");
}
