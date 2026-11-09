// Crosshair instead of the laser sight when playing with a gamepad (and with keyboard/mouse, MouseAim=RE3).
//
// With mouse and keyboard RE5 draws a four-piece crosshair (HUD cockpit uIdCockpit00, reticle type 3) at the
// screen projection of a world point picked by the mouse aim. With a pad it shows only the laser. The mod turns
// the same crosshair on in pad mode. The local player's own laser is made invisible (black in an additive blend);
// the partner's laser stays as it is.
//
// Aim like mouse mode (and RE3): the crosshair sits on the camera's aim ray (the screen centre, ch+0x2870,
// computed by the game every frame for pad players too) and shots converge on what that ray hits. The game
// already computes the converged shot for every local player (FUN_00B69280: muzzle -> camera-ray hit, into
// ch+0x2700); for a pad it then overwrites it with the barrel line (FUN_00B68E10), so the mod puts the converged
// shot back. Bullets land under the crosshair at any distance and the barrel's breathing sway no longer moves
// them. The game's laser hit dot is switched off, and the colour (the game only sets it for mouse players) is
// set by the mod: red when the aim ray is on an enemy.
#include "common.h"
#include "re5.h"
#include "config.h"
#include "crosshair.h"
#include "camera.h"
#include "kbinput.h"
#include "damage.h"
#include "net.h"
#include <string.h>
#include <math.h>
#include "MinHook.h"

using namespace re5;

namespace {

constexpr uint32_t kFnCockpitReticle = 0x009F2280;  // thiscall uIdCockpit00 reticle update, no args
constexpr uint32_t kInputSysPtr = 0x011B2158;       // +0x20 player index, per-player block 0x8C74 at +0x38
constexpr uint32_t kGameCfgPtr = 0x0123457C;        // +0x3085: force mouse/keyboard mode
constexpr uint32_t kLaserSightVt = 0x00FBBD00;      // uLaserSight

// uIdCockpit00
constexpr uint32_t CkCharacter = 0x1A0;
constexpr uint32_t CkVisible = 0x370;   // u8, drawn this frame
constexpr uint32_t CkShowReq = 0x373;   // u8, copied to the visible flag (+0x370) once per frame, then cleared
constexpr uint32_t CkType = 0x375;      // u8 reticle type, 3 = mouse crosshair at a projected world point
// character
constexpr uint32_t ChCockpit = 0x2640;
constexpr uint32_t ChLaser = 0x26DC;     // -> uLaserSight
constexpr uint32_t ChAimPoint = 0x2870;  // float3, world point the type-3 crosshair is drawn at
constexpr uint32_t ChCursor = 0x2910;    // float2, mouse cursor; the type-3 reticle writes it to sPad+0x608
// uLaserSight colours: own beam, dot, dot glow (vector4 each)
constexpr uint32_t kLaserColors[3] = {0x270, 0x2A0, 0x2C0};
const float kLaserDefaults[3][4] = {{2.0f, 0, 0, 1.0f}, {1.0f, 0, 0, 0.2f}, {1.0f, 0, 0, 0.186f}};
// Four more colour entries of the laser object (16 bytes each), hidden with the three above.
constexpr uint32_t kLaserExtra[4] = {0x250, 0x260, 0x280, 0x2B0};
const float kLaserExtraDefaults[4][4] = {{1.3f, 0, 0, 1.0f}, {1.3f, 0, 0, 1.0f}, {1.3f, 0, 0, 1.0f}, {1.2f, 0, 0, 0}};

constexpr uint32_t kFnLaserRaycast = 0x00B68E10;  // thiscall(ch) -> 0 none, 1 enemy shape, 2 prop, 3 static
constexpr uint32_t kFnCameraAimRay = 0x00B69280;  // thiscall(ch) -> same codes for the camera ray; sets 0x2700
constexpr uint32_t ChShot = 0x2700;     // float3, the shot flies from the muzzle toward this point
constexpr uint32_t CkColour = 0x374;    // u8: 0 grey, 1 green, 2 red
constexpr uint32_t ChAimRayObj = 0x2890;
constexpr uint32_t ChScope = 0x2998;    // u8, sniper scope view

typedef void(__fastcall* PFN_Reticle)(uint32_t cockpit, void* edx);
typedef int(__fastcall* PFN_LaserRaycast)(uint32_t ch, void* edx);
PFN_Reticle oReticle = nullptr;
PFN_LaserRaycast oLaserRaycast = nullptr;
PFN_LaserRaycast oCameraAimRay = nullptr;

struct AimInfo {
    uint32_t ch = 0;
    int hit = 0;          // what the camera aim ray hit (0 none, 1 enemy, 2 prop, 3 static)
    uint32_t weapon = 0;  // inventory entry of the gun in hand, and its rounds last frame
    int rounds = -1;
    uint32_t used = 0;    // GetTickCount() of the last lookup
};
AimInfo g_aim[8];

// Entries of characters that are gone are never released, so the table fills up over restarts. Take a free entry or
// else the one unused for longest; reusing one fixed entry made two local players take it from each other.
AimInfo& AimFor(uint32_t ch) {
    const uint32_t now = GetTickCount();
    for (auto& a : g_aim)
        if (a.ch == ch) { a.used = now; return a; }
    AimInfo* slot = nullptr;
    for (auto& a : g_aim)
        if (!a.ch) { slot = &a; break; }
    if (!slot) {
        slot = &g_aim[0];
        for (auto& a : g_aim)
            if ((uint32_t)(now - a.used) > (uint32_t)(now - slot->used)) slot = &a;
    }
    *slot = AimInfo{};
    slot->ch = ch;
    slot->used = now;
    return *slot;
}

struct HiddenLaser {
    uint32_t laser = 0;
    float saved[3][4];
    float savedX[4][4];
};
HiddenLaser g_hidden[4];

uint32_t Rd(uint32_t a) { return Readable((void*)a, 4) ? *(uint32_t*)a : 0; }

bool IsLaser(uint32_t laser) { return laser && Readable((void*)laser, 0x300) && Rd(laser) == kLaserSightVt; }

// The character is played with the pad right now. The game switches to keyboard/mouse by itself as soon as one is
// used (0xB5C4F0); the per-player option at 0xCEE1B0 that the mod used to read is the keyboard/mouse aim style, not
// the device. Scripted cursor sequences (+0x3085) keep the game's own aim.
bool PadDevice(uint32_t ch) {
    const uint32_t cfg = Rd(kGameCfgPtr);
    if (cfg && Readable((void*)(cfg + 0x3085), 1) && At<uint8_t>(cfg, 0x3085)) return false;
    return !KbMouse(ch);
}

// Keyboard/mouse with the mod's controls and the centred mouse aim (the game's aim style 0): the same crosshair as
// on the pad. The game itself converges keyboard shots on the camera ray.
bool KbCrosshair(uint32_t ch) {
    const uint32_t cfg = Rd(kGameCfgPtr);
    if (cfg && Readable((void*)(cfg + 0x3085), 1) && At<uint8_t>(cfg, 0x3085)) return false;
    return Cfg().mouseAimRE3 && KbLayerFor(ch);
}

void Restore(HiddenLaser& h) {
    if (IsLaser(h.laser))
    {
        for (int i = 0; i < 3; ++i) memcpy((void*)(h.laser + kLaserColors[i]), h.saved[i], 16);
        for (int i = 0; i < 4; ++i) memcpy((void*)(h.laser + kLaserExtra[i]), h.savedX[i], 16);
    }
    h.laser = 0;
}

void HideLaser(uint32_t laser) {
    if (!IsLaser(laser)) return;
    HiddenLaser* h = nullptr;
    for (auto& e : g_hidden)
        if (e.laser == laser) h = &e;
    if (!h) {
        for (auto& e : g_hidden)
            if (!e.laser || !IsLaser(e.laser)) { h = &e; break; }
        if (!h) return;
        h->laser = laser;
        for (int i = 0; i < 3; ++i) {
            memcpy(h->saved[i], (void*)(laser + kLaserColors[i]), 16);
            const float* s = h->saved[i];
            if (s[0] == 0 && s[1] == 0 && s[2] == 0) memcpy(h->saved[i], kLaserDefaults[i], 16);
        }
        for (int i = 0; i < 4; ++i) {
            memcpy(h->savedX[i], (void*)(laser + kLaserExtra[i]), 16);
            const float* s = h->savedX[i];
            if (s[0] == 0 && s[1] == 0 && s[2] == 0) memcpy(h->savedX[i], kLaserExtraDefaults[i], 16);
        }
    }
    for (int i = 0; i < 3; ++i) memset((void*)(laser + kLaserColors[i]), 0, 16);
    for (int i = 0; i < 4; ++i) memset((void*)(laser + kLaserExtra[i]), 0, 16);
}

void ShowLaser(uint32_t laser) {
    for (auto& e : g_hidden)
        if (e.laser == laser) Restore(e);
}

// The gun is up (raising, aiming, firing). The aim flag ch+0x2760 drops for a frame or more while a shot recoils,
// so the action decides.
// Rounds in the magazine of the gun in hand (inventory ch+0x21A0: 24 entries of 0x30 at +4; +4 item id,
// +8 count, +0x1C 1 = in hand, +0x29 valid), as the game's FUN_00C4B700(0xE) finds it.
bool GunInHand(uint32_t ch, uint32_t* entry, int* rounds) {
    const uint32_t inv = ch + 0x21A0;
    for (int i = 0; i < 24; ++i) {
        const uint32_t e = inv + 4 + i * 0x30;
        if (Rd(e + 0x1C) == 1 && At<uint8_t>(e, 0x29) == 1 && (Rd(e + 4) & 0xFF00) == 0x100) {
            *entry = e;
            *rounds = (int)Rd(e + 8);
            return true;
        }
    }
    return false;
}

// Crosshair size. The reticle update draws the four pieces of the type-3 crosshair at the sprite scale 1.5 (type 2:
// 1.0) and moves them out by (spread - 1) x 15 px; both numbers come from constants other code shares (0x10BCD64,
// 0x10BCE20). The two loads in the reticle update are pointed at the mod's own copies, once, on the game thread and
// only when a size other than 1 is asked for; the copies hold the game's numbers except while the update draws the
// mod's crosshair.
constexpr uint32_t kSiteScale = 0x009F2A4E;   // movss xmm0, [0x010BCD64]  (1.5, type 3 piece scale)
constexpr uint32_t kSiteSpread = 0x009F29E1;  // mulss xmm0, [0x010BCE20]  (15.0 px per unit of spread)
float g_reticleScale = 1.5f;
float g_reticleSpread = 15.0f;
int g_sizePatched = 0;  // 0 not yet, 1 patched, -1 unexpected code (size stays the game's)

void PatchReticleSize() {
    static const BYTE scaleOp[] = {0xF3, 0x0F, 0x10, 0x05, 0x64, 0xCD, 0x0B, 0x01};
    static const BYTE spreadOp[] = {0xF3, 0x0F, 0x59, 0x05, 0x20, 0xCE, 0x0B, 0x01};
    g_sizePatched = -1;
    if (!SafeEqual((void*)kSiteScale, scaleOp, sizeof(scaleOp)) ||
        !SafeEqual((void*)kSiteSpread, spreadOp, sizeof(spreadOp)) || !Readable((void*)0x010BCD64, 4) ||
        !Readable((void*)0x010BCE20, 4) || At<float>(0x010BCD64, 0) != 1.5f || At<float>(0x010BCE20, 0) != 15.0f) {
        Log("crosshair size: unexpected code, left alone");
        return;
    }
    const uint32_t first = kSiteSpread, len = kSiteScale + sizeof(scaleOp) - kSiteSpread;
    DWORD old;
    if (!VirtualProtect((void*)first, len, PAGE_EXECUTE_READWRITE, &old)) {
        Log("crosshair size: VirtualProtect failed (%lu), size left alone", GetLastError());
        return;
    }
    At<uint32_t>(kSiteScale, 4) = (uint32_t)(uintptr_t)&g_reticleScale;
    At<uint32_t>(kSiteSpread, 4) = (uint32_t)(uintptr_t)&g_reticleSpread;
    VirtualProtect((void*)first, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)first, len);
    g_sizePatched = 1;
    Log("crosshair size: reticle scale at %08x, spread at %08x", kSiteScale, kSiteSpread);
}

// A shot spreads the crosshair like the game's mouse crosshair does (cockpit +0x388 spread target, +0x394 hold
// time; the four pieces move out by (spread - 1) * 15 px x CrosshairSize and spring back).
void KickOnShot(uint32_t ch, uint32_t cockpit) {
    AimInfo& a = AimFor(ch);
    uint32_t entry = 0;
    int rounds = -1;
    if (!GunInHand(ch, &entry, &rounds)) {
        a.weapon = 0;
        return;
    }
    if (entry == a.weapon && rounds >= 0 && a.rounds > rounds) {
        const float kick = Cfg().crosshairKick;
        if (kick > 0.0f) {
            float& target = At<float>(cockpit, 0x388);
            target = (target > 1.0f ? target : 1.0f) + kick;
            if (target > 1.0f + 2.5f * kick) target = 1.0f + 2.5f * kick;
            At<float>(cockpit, 0x394) = 0.12f;
        }
    }
    a.weapon = entry;
    a.rounds = rounds;
}

bool GunUp(uint32_t ch) {
    const uint32_t act = Rd(ch + ch::CurAction);
    switch (Rd(act)) {
        case plact::ReadyBegin: case plact::ReadyLoop: case plact::Fire:
            return true;
    }
    return false;
}

// A plain gun in hand: not the knife (its actions reuse the gun classes) and not a scoped rifle (weapon table
// 0x11B5170 + index*0x140, +0x98), which the game fires along the barrel in mouse mode too.
bool PlainGun(uint32_t ch) {
    const uint32_t id = Rd(ch + ch::ActionId);
    if (id >= aid::KnifeBegin && id <= aid::KnifeAttack) return false;
    uint32_t entry = 0;
    int rounds = 0;
    if (!GunInHand(ch, &entry, &rounds)) return false;
    return Rd(0x011B5170 + (Rd(entry + 4) & 0xFF) * 0x140 + 0x98) == 0;
}

bool CrosshairOn(uint32_t ch) {
    const Config& c = Cfg();
    // oReticle: without the reticle hook there is no crosshair to aim with, so the laser stays the game's.
    return c.enabled && c.crosshair && oReticle && Rd(ch + ch::NetControl) == 0 && (PadDevice(ch) || KbCrosshair(ch)) &&
           GunAimHeld(ch) &&
           GunUp(ch) && !At<uint8_t>(ch, ChScope) && Rd(ch + ChAimRayObj) != 0 && PlainGun(ch);
}

// The barrel line: start ch+0x2710, end ch+0x2720, copied into the laser object at +0x210/+0x220. For the grenade
// launcher (0x10C) and the Gatling gun (0x115) the crosshair is drawn where the line ends (HookReticle).
constexpr uint32_t kItemGrenadeLauncher = 0x10C;
constexpr uint32_t kItemGatlingGun = 0x115;
constexpr uint32_t ChLaserStart = 0x2710;
constexpr uint32_t ChLaserEnd = 0x2720;
float g_barrel[3] = {};     // end of the barrel line at the last laser raycast, for the launcher or the Gatling gun
uint32_t g_barrelCh = 0;
bool BarrelWeaponInHand(uint32_t ch) {
    uint32_t entry = 0;
    int rounds = 0;
    if (!GunInHand(ch, &entry, &rounds)) return false;
    const uint32_t item = Rd(entry + 4);
    return item == kItemGrenadeLauncher || item == kItemGatlingGun;
}

// The camera aim ray: what it hits colours the crosshair.
int __fastcall HookCameraAimRay(uint32_t ch, void* edx) {
    const int r = oCameraAimRay(ch, edx);
    if (Readable((void*)ch, 0x3000) && CrosshairOn(ch)) AimFor(ch).hit = r;
#if RE3C_DEV
    if (Readable((void*)ch, 0x3000) && Rd(ch + ch::NetControl) == 0) Bridge()->reserved[14] = (uint32_t)r;  // 1 = enemy
#endif
    return r;
}

// The barrel-line raycast overwrites the converged shot for pads and only lights the laser hit dot otherwise:
// keep the converged shot and the dot off.
int __fastcall HookLaserRaycast(uint32_t ch, void* edx) {
    if (!Readable((void*)ch, 0x3000)) return oLaserRaycast(ch, edx);
    float aim[3];
    if (Rd(ch + ch::NetControl) == 2 && NetRemoteAim(ch, aim)) {
        // The partner's copy: fire where the partner's crosshair points, as their own game does.
        const int r = oLaserRaycast(ch, edx);
        const float* m = (const float*)(ch + 0x26F0);
        float* shot = (float*)(ch + ChShot);
        float d[3] = {aim[0] - m[0], aim[1] - m[1], aim[2] - m[2]};
        const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1.0f)
            for (int i = 0; i < 3; ++i) shot[i] = m[i] + d[i] / len * 5000.0f;
        return r;
    }
    // Aim follows the camera.  For a pad (and for the keyboard's own aim) the game fires along the BARREL, and with
    // the free camera the barrel no longer points where the player looks: that is the "it aims in front of the
    // character" behaviour.  While aiming a plain gun, the shot point is put on the camera ray instead - the same ray
    // the mod's centred crosshair uses - so the shot goes where the camera looks.
    // NOTE: this must NOT return early - returning here skipped the code below that shortens the laser beam, which is
    // why a pad player aiming at an enemy saw the laser AND the crosshair at the same time.
    bool camRay = false;
    float camPoint[3] = {};
    if (Rd(ch + ch::NetControl) == 0 && At<uint8_t>(ch, ChScope) == 0 && GunUp(ch) && PlainGun(ch)) {
        float rayEye[3], rayDir[3];
        if (AimCameraRay(rayEye, rayDir)) {
            camRay = true;
            for (int i = 0; i < 3; ++i) camPoint[i] = rayEye[i] + rayDir[i] * 5000.0f;
        }
    }
    const bool showCross = CrosshairOn(ch);
    if (!camRay && !showCross) return oLaserRaycast(ch, edx);
    float shot[3];
    memcpy(shot, (void*)(ch + ChShot), 12);
    oLaserRaycast(ch, edx);
    memcpy((void*)(ch + ChShot), camRay ? camPoint : shot, 12);
    if (!showCross) return 0;  // the laser sight keeps its beam
    const float* start = (const float*)(ch + ChLaserStart);
    float* end = (float*)(ch + ChLaserEnd);
    if (BarrelWeaponInHand(ch)) {
        // End of the barrel line, used by HookReticle for the crosshair of these two weapons.
        memcpy(g_barrel, end, 12);
        g_barrelCh = ch;
    }
    // The beam is drawn from that line. It is kept in its direction and made half a millimetre long, because with the
    // colours hidden a long white beam still showed in dark places.
    const float d[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len > 1.0f) {
        for (int i = 0; i < 3; ++i) end[i] = start[i] + d[i] / len * 0.05f;
        const uint32_t laser = Rd(ch + ChLaser);
        if (IsLaser(laser)) memcpy((void*)(laser + 0x220), end, 12);
    }
    return 0;
}

void __fastcall HookReticle(uint32_t cockpit, void* edx) {
    g_reticleScale = 1.5f;  // the game's crosshair (another player's, the mouse cursor style) keeps its size
    g_reticleSpread = 15.0f;
    const uint32_t ch = Rd(cockpit + CkCharacter);
    uint32_t aimCh = 0;  // the character whose crosshair point is moved for this draw (grenade launcher)
    float aimSaved[3] = {};
    if (ch && Readable((void*)ch, 0x3000) && Rd(ch + ch::NetControl) == 0 && Rd(ch + ChCockpit) == cockpit) {
        const Config& c = Cfg();
        const uint32_t laser = Rd(ch + ChLaser);
        const bool kb = KbCrosshair(ch);
        if (c.enabled && c.crosshair && (PadDevice(ch) || kb)) {
            HideLaser(laser);
            if (CrosshairOn(ch)) {
                At<uint8_t>(cockpit, CkColour) = AimFor(ch).hit == 1 ? 2 : 1;
                if (g_barrelCh == ch && BarrelWeaponInHand(ch)) {
                    // Launcher or Gatling gun: the crosshair is drawn at the end of the barrel line, for this draw only.
                    memcpy(aimSaved, (void*)(ch + ChAimPoint), 12);
                    memcpy((void*)(ch + ChAimPoint), g_barrel, 12);
                    aimCh = ch;
                }
                // The game's own spread on a shot (0xB5B160, the same for both devices) is none for handguns.
                KickOnShot(ch, cockpit);
                // CrosshairKick=0: no opening at all, also not the game's own for shotguns and magnums (its fire
                // action sets a target up to 3.0 every frame; the spring runs only in the update below).
                if (c.crosshairKick <= 0.0f) {
                    At<float>(cockpit, 0x388) = 1.0f;
                    At<float>(cockpit, 0x394) = 0.0f;
                }
                // Type 3 also stores this character's cursor point as where the mouse cursor reappears (sPad+0x608):
                // keep it where it is (in the centred aim the game does not use the point).
                const uint32_t spad = Rd(kSPadPtr);
                if (spad && Readable((void*)(spad + 0x608), 8)) {
                    At<float>(ch, ChCursor) = (float)At<int32_t>(spad, 0x608);
                    At<float>(ch, ChCursor + 4) = (float)At<int32_t>(spad, 0x60C);
                }
                At<uint8_t>(cockpit, CkType) = 3;
                At<uint8_t>(cockpit, CkShowReq) = 1;
            }
            // The cockpit draws what was asked for the frame before (0x9ECBB0 copies the request into the visible
            // flag just before this update), so the size goes with the crosshair on screen, also in the frame after
            // the aim ends. A type-3 crosshair here is the mod's: the game asks for none with a pad, and type 2 in
            // the centred keyboard aim.
            if (c.crosshairSize != 1.0f && At<uint8_t>(cockpit, CkType) == 3 && At<uint8_t>(cockpit, CkVisible)) {
                if (!g_sizePatched) PatchReticleSize();
                g_reticleScale = 1.5f * c.crosshairSize;
                g_reticleSpread = 15.0f * c.crosshairSize;
            }
        } else {
            ShowLaser(laser);
        }
    }
    oReticle(cockpit, edx);
    if (aimCh) memcpy((void*)(aimCh + ChAimPoint), aimSaved, 12);
}

// The character's reticle request (thiscall(character), plain ret; called by the gun actions and the knife actions):
// it sets the cockpit's request (0x9ED350: +0x373 = 1, +0x375 = type), which the cockpit copies into its visible
// flag (0x9ECBB0) before the reticle update runs. Keyboard/mouse asks for the centred reticle (type 2) while the
// knife is out too (with a gamepad there is none); KnifeCrosshair=0 takes that request back right away.
constexpr uint32_t kFnReticleRequest = 0x00B69970;
typedef void(__fastcall* PFN_ReticleRequest)(uint32_t ch, void* edx);
PFN_ReticleRequest oReticleRequest = nullptr;

void __fastcall HookReticleRequest(uint32_t ch, void* edx) {
    oReticleRequest(ch, edx);
    if (!Readable((void*)ch, 0x3000) || Rd(ch + ch::NetControl) != 0) return;
    const Config& c = Cfg();
    const uint32_t id = Rd(ch + ch::ActionId);
    if (c.enabled && !c.meleeOnly && !c.knifeCrosshair && id >= aid::KnifeBegin && id <= aid::KnifeAttack &&
        KbMouse(ch)) {
        const uint32_t cockpit = Rd(ch + ChCockpit);
        if (cockpit && Readable((void*)(cockpit + CkShowReq), 1)) At<uint8_t>(cockpit, CkShowReq) = 0;
    }
}

// Direct horizontal aim. While a pad player aims, the game's aim function (cPadEx 0xB86CD0) first moves only the
// arms inside a horizontal zone (mWaistAngleYOrg +0x4B4, applied as waist twist +0x450) before the body and the
// camera turn (aim yaw +0x454). With the crosshair at the screen centre that zone feels dead, so the horizontal
// part the game gives the arms this frame goes to the aim yaw instead.
constexpr uint32_t kFnPadAim = 0x00B86CD0;  // thiscall(cPadEx, character, 4 floats), ret 0x14
typedef void(__fastcall* PFN_PadAim)(uint32_t padEx, void* edx, uint32_t ch, float a, float b, float c, float d);
PFN_PadAim oPadAim = nullptr;

//
// Aim speed. The pad turns the aim at fixed rates (sParameter +0x2D0 pitch, +0x2D4 yaw, 57.9 deg/s) times the
// game's Aim Speed option (0.8..1.5) after the dead zone, the ramp and the inversion; the mod's AimSpeedX/Y scale
// those two rates for the local pad player for the duration of the call (nothing else reads them meanwhile). Still
// stick = still aim; the scope's own zoom slow-down stays.
//
// Releasing the stick: the game's ramp (cPadEx+0x4B0) runs down over ~0.2 s after the stick is back in the dead
// zone and keeps feeding a stick-up vector, so the aim crept upward after every full push; the ramp is dropped as
// soon as the aim stick is neutral.
constexpr uint32_t kParamPtr = 0x011B27B4;    // sParameter
constexpr uint32_t kOptionsPtr = 0x012340A4;  // +0x1042C: per-player options (stride 0xF4)
constexpr uint32_t kFnControlType = 0x00C42960;  // thiscall(options, player) -> 0..3 = control type A..D

// Types B and D aim with the right stick (the 4 floats are left angle/magnitude, right angle/magnitude).
bool AimWithRightStick(uint32_t ch) {
    const uint32_t sys = Rd(kOptionsPtr);
    const uint32_t opts = sys && Readable((void*)(sys + 0x1042C), 4) ? Rd(sys + 0x1042C) : 0;
    if (!opts || !Readable((void*)opts, 0x400)) return true;
    const int type = ((int(__fastcall*)(uint32_t, void*, int))kFnControlType)(opts, nullptr, At<uint8_t>(ch, ch::NetSlot));
    return type == 1 || type == 3;
}

void __fastcall HookPadAim(uint32_t padEx, void* edx, uint32_t ch, float a, float b, float c, float d) {
    const Config& cfg = Cfg();
    const bool localPad = cfg.enabled && padEx && Readable((void*)padEx, 0x4E8) && ch && Readable((void*)ch, 0x3000) &&
                          Rd(ch + ch::NetControl) == 0 && PadDevice(ch);
    // Keyboard/mouse: the game's mouse aim turns the camera at once while the body lags in the arm-only zone
    // (+0x4B4, up to 7 degrees) and catches up in steps, which shows as trembling; the same direct turn as on the pad.
    const bool kbAim = cfg.enabled && padEx && Readable((void*)padEx, 0x4E8) && ch && Readable((void*)ch, 0x3000) &&
                       Rd(ch + ch::NetControl) == 0 && KbCrosshair(ch);
    // Also in wall cover: the arm zone (+0x4B4) is wider there, and its sway made the crosshair wait for the stick to
    // get past it before the aim moved.
    const bool direct = (localPad || kbAim) && cfg.directHorizontal && CrosshairOn(ch);
    const uint32_t prm = localPad && (cfg.aimSpeedX != 1.0f || cfg.aimSpeedY != 1.0f) ? Rd(kParamPtr) : 0;
    const bool scale = prm && Readable((void*)(prm + 0x2D0), 8);
    float pitchRate = 0.0f, yawRate = 0.0f;
    if (scale) {
        pitchRate = At<float>(prm, 0x2D0);
        yawRate = At<float>(prm, 0x2D4);
        At<float>(prm, 0x2D0) = pitchRate * cfg.aimSpeedY;
        At<float>(prm, 0x2D4) = yawRate * cfg.aimSpeedX;
    }
    if (localPad && !cfg.meleeOnly && (AimWithRightStick(ch) ? d : b) == 0.0f) At<float>(padEx, 0x4B0) = 0.0f;
    if (direct) At<float>(padEx, 0x4B4) = 0.0f;
    oPadAim(padEx, edx, ch, a, b, c, d);
    // Keyboard/mouse (any aim style): the game's aim sway (+0x4A0 pitch, +0x4A4 yaw) goes into the aim camera itself
    // (ch+0x2908, the aim matrix yaw), later this frame; with Breathing=0 the mouse aim stays as steady as the pad's.
    if (!cfg.breathing && cfg.enabled && padEx && Readable((void*)padEx, 0x4E8) && ch && Readable((void*)ch, 0x3000) &&
        Rd(ch + ch::NetControl) == 0 && KbLayerFor(ch)) {
        At<float>(padEx, 0x4A0) = 0.0f;
        At<float>(padEx, 0x4A4) = 0.0f;
    }
    if (scale) {
        At<float>(prm, 0x2D0) = pitchRate;
        At<float>(prm, 0x2D4) = yawRate;
    }
    if (direct) {
        const float h = At<float>(padEx, 0x4B4);
        At<float>(padEx, 0x454) += h;
        At<float>(padEx, 0x450) -= h;
        At<float>(padEx, 0x4B4) = 0.0f;
    }
}

}  // namespace

bool AimsWithPad(uint32_t ch) { return PadDevice(ch); }

bool PadAimsWithRightStick(uint32_t ch) { return AimWithRightStick(ch); }

// The local pad player aims a plain gun (crosshair or laser sight): the aim camera lag applies.
bool PadGunAimed(uint32_t ch) {
    return Readable((void*)ch, 0x3000) && Rd(ch + ch::NetControl) == 0 && PadDevice(ch) && GunAimHeld(ch) &&
           GunUp(ch) && !At<uint8_t>(ch, ChScope) && PlainGun(ch);
}

bool LocalAimPoint(uint32_t ch, float* out) {
    if (!Readable((void*)ch, 0x3000)) return false;
    // Pad: the mod's crosshair; keyboard/mouse: the game's own centred mouse aim. Both shoot along the camera ray,
    // whose hit is ch+0x2860.
    const bool keyboardAim = KbLayerFor(ch) && GunAimHeld(ch) && GunUp(ch) && !At<uint8_t>(ch, ChScope) &&
                             Rd(ch + ChAimRayObj) != 0 && PlainGun(ch);
    if (!keyboardAim && !CrosshairOn(ch)) return false;
    memcpy(out, (void*)(ch + 0x2860), 12);
    return true;
}

void InitCrosshairHooks() {
    static const BYTE sig[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0xB4, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57};
    if (SafeEqual((void*)kFnCockpitReticle, sig, sizeof(sig)) &&
        MH_CreateHook((void*)kFnCockpitReticle, (void*)HookReticle, (void**)&oReticle) == MH_OK &&
        MH_QueueEnableHook((void*)kFnCockpitReticle) == MH_OK)
        Log("hook cockpit reticle at %08x", kFnCockpitReticle);
    else
        Log("hook cockpit reticle failed");
    static const BYTE sigCamRay[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8B, 0xF1};
    if (HookSite((void*)kFnCameraAimRay, sigCamRay, sizeof(sigCamRay)) &&
        MH_CreateHook((void*)kFnCameraAimRay, (void*)HookCameraAimRay, (void**)&oCameraAimRay) == MH_OK &&
        MH_QueueEnableHook((void*)kFnCameraAimRay) == MH_OK)
        Log("hook camera aim ray at %08x", kFnCameraAimRay);
    else
        Log("hook camera aim ray failed");
    static const BYTE sigPadAim[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x64, 0x53, 0x56, 0x57};
    if (HookSite((void*)kFnPadAim, sigPadAim, sizeof(sigPadAim)) &&
        MH_CreateHook((void*)kFnPadAim, (void*)HookPadAim, (void**)&oPadAim) == MH_OK &&
        MH_QueueEnableHook((void*)kFnPadAim) == MH_OK)
        Log("hook pad aim (direct horizontal, aim speed) at %08x", kFnPadAim);
    else
        Log("hook pad aim failed");
    static const BYTE sigRay[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00, 0x53, 0x56, 0x8B, 0xF1};
    if (SafeEqual((void*)kFnLaserRaycast, sigRay, sizeof(sigRay)) &&
        MH_CreateHook((void*)kFnLaserRaycast, (void*)HookLaserRaycast, (void**)&oLaserRaycast) == MH_OK &&
        MH_QueueEnableHook((void*)kFnLaserRaycast) == MH_OK)
        Log("hook laser raycast at %08x", kFnLaserRaycast);
    else
        Log("hook laser raycast failed");
    // push ecx / push esi / mov esi,ecx / cmp byte [esi+2998h],1 / jz ... ; the tail: mov ecx,[esi+2640h] -> 0x9ED350
    static const BYTE sigReq[] = {0x51, 0x56, 0x8B, 0xF1, 0x80, 0xBE, 0x98, 0x29, 0x00, 0x00, 0x01, 0x0F, 0x84};
    static const BYTE sigReqTail[] = {0x8B, 0x8E, 0x40, 0x26, 0x00, 0x00, 0xC6, 0x86, 0x18, 0x29, 0x00, 0x00, 0x01, 0xE8};
    if (HookSite((void*)kFnReticleRequest, sigReq, sizeof(sigReq)) &&
        SafeEqual((void*)0x00B69A98, sigReqTail, sizeof(sigReqTail)) &&
        MH_CreateHook((void*)kFnReticleRequest, (void*)HookReticleRequest, (void**)&oReticleRequest) == MH_OK &&
        MH_QueueEnableHook((void*)kFnReticleRequest) == MH_OK)
        Log("hook reticle request at %08x", kFnReticleRequest);
    else
        Log("hook reticle request failed (KnifeCrosshair off)");
}
