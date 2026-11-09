// InventoryMove: walking and running while the inventory grid is open.
//
// The character does not stop because of the grid itself: the player's command builder (cPadEx::update 0xB8A3E0)
// asks "is the grid open?" (0xC69BF0, called at 0xB8AB82) right after reading the pad and, if so, only sets the
// neutral command and returns, so no walk/run command is built. The grid (0xC6C420, called at 0xB8CD65 after the
// builder in the same frame) reads the same pad: d-pad and left-stick direction bits move its cursor, Y/A/B/RT/RB
// drive it; it never reads the analog sticks.
//
// So for a walking player the mod gives the builder a stick-only copy of the pad (the left stick and A for running,
// game.cpp) and lets it go on (the gate below answers "not open" for that inventory), then puts the buttons back
// before the grid runs (the wrapper below), without the left-stick directions: the cursor uses the d-pad then.
// Item use and equip requests from the grid reach the builder before the gate, so they work as in the original.
// Network partners never run the builder (their copy just receives walk/run actions). On keyboard/mouse the builder's
// own key reads past the gate (aim, X, T) are filtered meanwhile (kbinput.cpp) and the mouse cursor stays shown.
#include "common.h"
#include "re5.h"
#include "inventory.h"

using namespace re5;

namespace {

constexpr uint32_t kFnGridOpen = 0x00C69BF0;    // thiscall bool(inventory), plain ret
constexpr uint32_t kFnGridUpdate = 0x00C6C420;  // thiscall void(inventory; character, pad), ret 8
constexpr uint32_t kSiteBuilder = 0x00B8AB7C;   // mov ecx,[esi+2654h] ; call 0xC69BF0 ; test al,al ; je
constexpr uint32_t kSiteGrid = 0x00B8CD5D;      // lea eax,[edi+708h] ; push eax ; push esi ; call 0xC6C420

struct Stash {
    uint32_t pad = 0, inv = 0;
    bool valid = false;      // set by InventoryStash, used once by the grid wrapper
    uint32_t on, trg, rel, rep, trgRep;
    float lTrig, rTrig;
    uint32_t prevView = 0;   // the builder's bits last frame (its own edges)
    bool hadView = false;
};
Stash g_stash[4];
int g_state = 0;  // 0 = not tried, 1 = patched, -1 = unavailable
int g_stale = 0;

bool __fastcall BuilderGate(uint32_t inv) {
    for (const auto& s : g_stash)
        if (s.valid && s.inv == inv) return false;  // walking: the builder goes on with the stick-only pad
    return ((bool(__fastcall*)(uint32_t, void*))kFnGridOpen)(inv, nullptr);
}

void __fastcall GridUpdate(uint32_t inv, void* edx, uint32_t ch, uint32_t pad) {
    for (auto& s : g_stash)
        if (s.valid && s.pad == pad) {
            // The grid gets the buttons as the game built them (minus the left-stick directions); the stick values
            // stay as the builder saw them. Next frame PlayerPad::Update takes these bits as the previous state.
            At<uint32_t>(pad, pp::On) = s.on;
            At<uint32_t>(pad, pp::Trg) = s.trg;
            At<uint32_t>(pad, pp::Rel) = s.rel;
            At<uint32_t>(pad, pp::Rep) = s.rep;
            At<uint32_t>(pad, pp::TrgRep) = s.trgRep;
            At<float>(pad, pp::LTrig) = s.lTrig;
            At<float>(pad, pp::RTrig) = s.rTrig;
            s.valid = false;
            // Keyboard/mouse: the builder shows the mouse cursor (sPad+0x600 = 1) when it stops early for the grid
            // and hides it at its normal end (0xB8CABB/0xB8CAEA), which it now reaches; the stock early stop's 1 back.
            const uint32_t spad = *(uint32_t*)kSPadPtr;
            if (spad && Readable((void*)spad, 0x620) && Readable((void*)ch, 0x3000) &&
                At<uint8_t>(spad, spad::DeviceIsPad) == 0 &&
                At<int32_t>(spad, spad::KeyboardPlayer) == (int32_t)At<uint8_t>(ch, ch::NetSlot))
                At<uint8_t>(spad, 0x600) = 1;
        }
    ((void(__fastcall*)(uint32_t, void*, uint32_t, uint32_t))kFnGridUpdate)(inv, edx, ch, pad);
}

bool RetargetCall(uint32_t call, void* target) {
    DWORD old;
    if (!VirtualProtect((void*)call, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    At<int32_t>(call, 1) = (int32_t)((uint32_t)(uintptr_t)target - (call + 5));
    VirtualProtect((void*)call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)call, 5);
    return true;
}

// Both call sites or neither: the gate alone would leave the grid without its buttons.
void Install() {
    static const BYTE builder[] = {0x8B, 0x8E, 0x54, 0x26, 0x00, 0x00, 0xE8, 0x69, 0xF0, 0x0D, 0x00, 0x84, 0xC0, 0x74, 0x1F};
    static const BYTE grid[] = {0x8D, 0x87, 0x08, 0x07, 0x00, 0x00, 0x50, 0x56, 0xE8, 0xB6, 0xF6, 0x0D, 0x00};
    g_state = -1;
    if (!SafeEqual((void*)kSiteBuilder, builder, sizeof(builder)) || !SafeEqual((void*)kSiteGrid, grid, sizeof(grid))) {
        Log("inventory move: unexpected code at %08x/%08x, left off", kSiteBuilder, kSiteGrid);
        return;
    }
    // The wrapper first: on its own it changes nothing (no stash, no change).
    if (!RetargetCall(kSiteGrid + 8, (void*)GridUpdate)) return;
    if (!RetargetCall(kSiteBuilder + 6, (void*)BuilderGate)) {
        Log("inventory move: patch failed, left off");
        return;
    }
    g_state = 1;
    Log("inventory move: patched %08x and %08x", kSiteBuilder + 6, kSiteGrid + 8);
}

}  // namespace

bool InventoryMoveReady() { return g_state == 1 && g_stale < 3; }

void InventoryPadUpdated(uint32_t padObj) {
    // Patched from the game thread that runs the builder and the grid (this is called from inside the builder),
    // so the two calls never change under it.
    if (g_state == 0) Install();
    for (auto& s : g_stash) {
        if (s.pad != padObj) continue;
        if (s.valid) {
            // The grid did not run after the last stash: unexpected, so after a few times the feature stays off.
            s.valid = false;
            ++g_stale;
        }
        if (!s.hadView) s.prevView = 0;
        s.hadView = false;
    }
}

void InventoryStash(uint32_t padObj, uint32_t inv, uint32_t view, bool keepRight) {
    if (!InventoryMoveReady() || !Readable((void*)inv, 0x80)) return;
    Stash* s = nullptr;
    for (auto& e : g_stash)
        if (e.pad == padObj) s = &e;
    for (auto& e : g_stash)
        if (!s && !e.pad) s = &e;
    if (!s) {
        s = &g_stash[0];
        *s = Stash{};
    }
    s->pad = padObj;
    s->inv = inv;
    s->on = At<uint32_t>(padObj, pp::On);
    s->trg = At<uint32_t>(padObj, pp::Trg);
    s->rel = At<uint32_t>(padObj, pp::Rel);
    s->rep = At<uint32_t>(padObj, pp::Rep);
    s->trgRep = At<uint32_t>(padObj, pp::TrgRep);
    s->lTrig = At<float>(padObj, pp::LTrig);
    s->rTrig = At<float>(padObj, pp::RTrig);
    // What the builder sees: only walking (A to run, the right stick to turn without the free camera).
    At<uint32_t>(padObj, pp::On) = view;
    At<uint32_t>(padObj, pp::Trg) = view & ~s->prevView;
    At<uint32_t>(padObj, pp::Rel) = s->prevView & ~view;
    At<uint32_t>(padObj, pp::Rep) = 0;
    At<uint32_t>(padObj, pp::TrgRep) = view & ~s->prevView;
    if (!keepRight) {
        At<float>(padObj, pp::RAngle) = 0;
        At<float>(padObj, pp::RMag) = 0;
    }
    At<float>(padObj, pp::LTrig) = 0;
    At<float>(padObj, pp::RTrig) = 0;
    s->prevView = view;
    s->hadView = true;
    s->valid = true;
}
