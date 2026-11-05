#include "common.h"
#include "re5.h"
#include <math.h>
#include <MinHook.h>
#include "config.h"
#include "movehook.h"

// ---------------------------------------------------------------------------------------------------------------
// Native movement takeover, step 1: install, verify and observe.
//
// RE5's locomotion is driven by action-update methods (walk / run / back / strafe / turn).  Each of them translates
// the actor by the clip's own root motion, so an animation layer that also moves the actor (aimwalk.cpp) fights them.
// Enhanced Movement solves this by replacing those entries and computing the step itself; this module starts the same
// way, but conservatively: the hooks are installed, the original code still runs, and the ini switch decides whether a
// camera-relative step is added.  With [Movement] EngineMove=0 nothing about the game changes.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kFnWalkUpdate = 0x00C7A220;
constexpr uint32_t kFnRunUpdate = 0x00C7A6E0;
constexpr uint32_t kFnBackUpdate = 0x00C7A960;

// Original prologues, read from the retail image.  A different build will not match and is left alone.
const BYTE kSigWalk[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00};
const BYTE kSigRun[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x18, 0x85, 0xC9};
const BYTE kSigBack[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00};

// The update methods are thiscall: ecx = the action object, and the actor sits at +0x18 (same layout the rest of the
// mod uses).  A __fastcall detour keeps ecx in place and leaves edx unused.
typedef void(__fastcall* PFN_ActionUpdate)(void* self, void* edx);

PFN_ActionUpdate oWalkUpdate = nullptr;
PFN_ActionUpdate oRunUpdate = nullptr;
PFN_ActionUpdate oBackUpdate = nullptr;

int g_installed = 0;
int g_logged = 0;

bool HookOne(uint32_t addr, const BYTE* sig, size_t n, void* detour, void** orig, const char* name) {
    if (!SafeEqual((const void*)addr, sig, n)) {
        Log("movehook %s: signature mismatch at %08X - left alone", name, addr);
        return false;
    }
    if (MH_CreateHook((void*)addr, detour, orig) != MH_OK || MH_QueueEnableHook((void*)addr) != MH_OK) {
        Log("movehook %s: MinHook failed", name);
        return false;
    }
    Log("movehook %s: installed at %08X", name, addr);
    return true;
}

// The actor that owns an action object, or 0 when the object is malformed.
inline uint32_t ActorOf(void* self) {
    if (!self || !Readable((void*)((uintptr_t)self + 0x18), 4)) return 0;
    return re5::At<uint32_t>((uint32_t)(uintptr_t)self, 0x18);
}

// Step 1 does not move anything: it only proves that the entries can be taken over without the game noticing.
void Observe(const char* name, void* self) {
    if (g_logged >= 24 || !Cfg().engineMove) return;
    ++g_logged;
    const uint32_t actor = ActorOf(self);
    Log("movehook %s: action=%08X actor=%08X", name, (uint32_t)(uintptr_t)self, actor);
}

void __fastcall HookWalkUpdate(void* self, void* edx) {
    Observe("walk", self);
    if (oWalkUpdate) oWalkUpdate(self, edx);
}

void __fastcall HookRunUpdate(void* self, void* edx) {
    Observe("run", self);
    if (oRunUpdate) oRunUpdate(self, edx);
}

void __fastcall HookBackUpdate(void* self, void* edx) {
    Observe("back", self);
    if (oBackUpdate) oBackUpdate(self, edx);
}


}  // namespace

// Move the actor itself, in the direction the camera is looking.  Used for the actions where the engine suppresses
// locomotion (knife slash, item use, a throw): the native step never runs there, so writing the position is the only way
// to move.  Positions are parent-local whenever the character stands on a moving object, so those frames are skipped.
bool EngineMoveStep(uint32_t character, float yaw, float speedCm, float dt) {
    if (!character || !dt || dt <= 0.0f || dt > 0.25f) return false;
    if (!Readable((void*)(character + re5::ch::Parent), 4)) return false;
    if (re5::At<uint32_t>(character, re5::ch::Parent) != 0) return false;   // on a platform: leave it to the game
    const uint32_t posOff = character + re5::ch::Pos;
    if (!Readable((void*)posOff, 12)) return false;
    const float step = speedCm * dt;
    const float dx = sinf(yaw) * step;
    const float dz = cosf(yaw) * step;
    re5::At<float>(posOff, 0) += dx;
    re5::At<float>(posOff, 8) += dz;
    return true;
}

bool EngineMovementActive() {
    return g_installed == 3 && Cfg().engineMove;
}

void InitMovementHooks() {
    if (g_installed) return;
    int ok = 0;
    ok += HookOne(kFnWalkUpdate, kSigWalk, sizeof(kSigWalk), (void*)HookWalkUpdate, (void**)&oWalkUpdate, "walk") ? 1 : 0;
    ok += HookOne(kFnRunUpdate, kSigRun, sizeof(kSigRun), (void*)HookRunUpdate, (void**)&oRunUpdate, "run") ? 1 : 0;
    ok += HookOne(kFnBackUpdate, kSigBack, sizeof(kSigBack), (void*)HookBackUpdate, (void**)&oBackUpdate, "back") ? 1 : 0;
    g_installed = ok;
    Log("movehook: %d/3 entries installed (EngineMove=%d)", ok, (int)Cfg().engineMove);
}
