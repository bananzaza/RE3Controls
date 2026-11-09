// Free camera, RE3 style.
//
// Outside the aim the mod draws its own orbit camera: a pivot at the character's shoulder height, the eye behind
// it at the yaw/pitch the right stick chose and shifted over the right (or left) shoulder, so the character
// stands left of the centre. Looking up brings the camera down and closer instead of into the floor; looking down
// raises it. Nothing re-centres by itself.
//
// The game keeps its own cameras for aiming and for its look-at moments (locate-partner button, co-op action
// points); the mod blends into and out of them. The game's own right-stick look-around (QFPS+0x1D0, read from the
// raw pad by 0x8454E0) is switched off while the orbit camera runs, so nothing the game stored (the aim pitch,
// say) can leave the camera low afterwards.
//
// The camera controller (uCameraCtrl) runs a lag camera on the QFPS output. In world-space mode (aiming, looking
// at the partner) that lag camera turns the up vector into a signed roll, so every eye/target the mod writes ends
// with an up vector built exactly like the rig builds it.
#include "common.h"
#include "re5.h"
#include "config.h"
#include "camera.h"
#include "crosshair.h"
#include <math.h>
#include <string.h>
#include "MinHook.h"

using namespace re5;

static FreeCam g_cams[4];  // by player slot
FreeCam& CamFor(uint32_t character) { return g_cams[At<uint8_t>(character, ch::NetSlot) & 3]; }
bool IsCamCharacter(uint32_t character) {
    // Check known camera slots first so we almost never VirtualQuery random addresses.
    if (!character) return false;
    for (int i = 0; i < 4; ++i)
        if (g_cams[i].character == character) return Readable((void*)character, 0x3000);
    return false;
}

static inline float Rad(float deg) { return deg * 0.01745329252f; }

float DefaultCameraPitch() { return Rad(Cfg().cameraDefaultPitch); }

float WrapPi(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// ------------------------------------------------------------------------------------------------ moving platforms
// On a boat, lift, moving floor or conveyor the game parents the character to that object (uCoord mpParent +0x18,
// joint +0x1C): Pos/Quat become relative to the parent's joint matrix (row vectors, row 3 = translation), exactly
// as the game's own camera controller reads them (0x842251). Without a parent everything below is the identity.
typedef const float*(__fastcall* PFN_GetJointWmat)(uint32_t self, void* edx, int32_t no);

static const float* ParentMatrix(uint32_t character) {
    const uint32_t par = At<uint32_t>(character, ch::Parent);
    if (!par || !Readable((void*)par, 0x100)) return nullptr;
    const uint32_t kind = At<uint32_t>(par, 4) & 7;  // the engine's own "live parent" test (0x48ABA0)
    if (kind != 1 && kind != 2) return nullptr;
    const int32_t no = At<int32_t>(character, ch::ParentNo);
    const float* m = (const float*)(par + 0xA0);
    if (no >= 0) {
        const uint32_t vt = At<uint32_t>(par, 0);
        if (!Readable((void*)vt, 0x48)) return nullptr;
        m = ((PFN_GetJointWmat)At<uint32_t>(vt, 0x44))(par, nullptr, no);
    }
    return Readable(m, 64) ? m : nullptr;
}

static void ToWorld(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i) out[i] = p[0] * m[i] + p[1] * m[4 + i] + p[2] * m[8 + i] + m[12 + i];
}

// Heading of the platform's forward (z) axis, in the same convention as the character's yaw.
static float MatrixYaw(const float* m) {
    return m[8] * m[8] + m[10] * m[10] > 1e-6f ? atan2f(m[8], m[10]) : 0.0f;
}

void CharacterWorldPos(uint32_t character, float* out) {
    const float* p = (const float*)(character + ch::Pos);
    if (const float* m = ParentMatrix(character)) {
        ToWorld(m, p, out);
    } else {
        memcpy(out, p, 12);
    }
}

static float BaseYaw(uint32_t character) {
    const float* m = ParentMatrix(character);
    return m ? MatrixYaw(m) : 0.0f;
}

float CharacterYaw(uint32_t character) {
    const float* q = (const float*)(character + ch::Quat);
    return WrapPi(2.0f * atan2f(q[1], q[3]) + BaseYaw(character));
}

void SetCharacterYaw(uint32_t character, float yaw) {
    yaw = WrapPi(yaw - BaseYaw(character));
    float* q = (float*)(character + ch::Quat);
    q[0] = 0.0f;
    q[1] = sinf(yaw * 0.5f);
    q[2] = 0.0f;
    q[3] = cosf(yaw * 0.5f);
}

static double NowSec() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

typedef void(__fastcall* PFN_CamUpdate)(uint32_t self, void* edx, uint32_t arg);
static PFN_CamUpdate oQfpsUpdate = nullptr;
static PFN_CamUpdate oCtrlUpdate = nullptr;

// up = normalize((d x Y) x d), as the rig builds it.
static void RebuildUp(const float* eye, const float* tgt, float* up) {
    const float dx = tgt[0] - eye[0], dy = tgt[1] - eye[1], dz = tgt[2] - eye[2];
    const float ux = -dx * dy, uy = dx * dx + dz * dz, uz = -dz * dy;
    const float len = sqrtf(ux * ux + uy * uy + uz * uz);
    if (len < 1e-6f) return;
    up[0] = ux / len;
    up[1] = uy / len;
    up[2] = uz / len;
}

// ------------------------------------------------------------------------------------------------ rig poses
// Pose block (local character space, +X = the character's left): +0 eye xyz, +0x10 target xyz, +0x24 fov.
// The ctrl points QFPS+0x2A4/+0x2A8/+0x2AC (normal rig at look +1/0/-1) and +0x2B0/+0x2B4/+0x2B8 (aim rig) at its
// resource tables every frame; some areas use inline poses instead (+0x3E1: normal at +0x2C0/+0x2F0/+0x320,
// +0x3E0: aim at +0x350/+0x380/+0x3B0). Mirroring X (copies for one update, put back after it) moves the game's
// cameras (aim, look-at) to the left shoulder; the game's own wall handling then works on the mirrored poses.
static const uint32_t kPosePtrs = 0x2A4;
static const uint32_t kInlinePoses[6] = {0x2C0, 0x2F0, 0x320, 0x350, 0x380, 0x3B0};  // 3 normal, 3 aim

struct PoseMirror {
    uint32_t self = 0;
    uint32_t savedPtr[6] = {};
    float buf[6][10];
    bool inlineSaved[6] = {};

    static void Flip(float* p) {
        p[0] = -p[0];
        p[4] = -p[4];
    }

    void Apply(uint32_t q) {
        self = q;
        for (int i = 0; i < 6; ++i) {
            const uint32_t p = At<uint32_t>(q, kPosePtrs + i * 4);
            savedPtr[i] = 0;
            if (!Readable((void*)p, 0x28)) continue;
            savedPtr[i] = p;
            memcpy(buf[i], (void*)p, 0x28);
            Flip(buf[i]);
            At<uint32_t>(q, kPosePtrs + i * 4) = (uint32_t)(uintptr_t)buf[i];
        }
        const bool inlineNormal = At<uint8_t>(q, 0x3E1) != 0, inlineAim = At<uint8_t>(q, 0x3E0) != 0;
        for (int i = 0; i < 6; ++i) {
            inlineSaved[i] = i < 3 ? inlineNormal : inlineAim;
            if (inlineSaved[i]) Flip((float*)(q + kInlinePoses[i]));
        }
    }

    void Restore() {
        if (!self) return;
        for (int i = 0; i < 6; ++i) {
            if (savedPtr[i]) At<uint32_t>(self, kPosePtrs + i * 4) = savedPtr[i];
            if (inlineSaved[i]) Flip((float*)(self + kInlinePoses[i]));
        }
        self = 0;
    }
};

// ------------------------------------------------------------------------------------------------ aim distance, fov
// AimDistance uses the game's own aim pull-back, QFPS+0x67C (mReadyCameraOfsLength): with the aim rig the rig
// evaluation (0x84705A) moves the eye along its final eye->target line by that length (negative = back), after the
// pitch blend, so the view, the screen centre and the camera aim ray keep their direction (the aim ray is cast from
// the final camera); the wall sweep afterwards still pulls the eye in. The camera controller writes it every frame
// before the QFPS update (0, or -70 for the keyboard's cursor aim style). FieldOfView / AimFieldOfView add to the
// rig's output fov (QFPS+0x1A0, mFovAim, rewritten from the poses each update; the controller's lag and projection
// take it from there). The sniper scope and the rocket launchers use another camera (uCameraWep) and never get here.
constexpr uint32_t kReadyOfs = 0x67C;
constexpr uint32_t kAimRigId = 0x30C4;  // ctrl: the aim rig the character asked for (0xB74040, by the item in hand)

// Gun emplacements, jeep guns and torpedoes (aim rigs 0x11-0x13, which the handheld Gatling gun 0x115 shares): their
// aim view stays the game's.
static bool MountedGunAim(uint32_t ch, uint32_t rig) {
    if (rig < 0x11 || rig > 0x13) return false;
    const uint32_t inv = ch + 0x21A0;
    for (int i = 0; i < 24; ++i) {
        const uint32_t e = inv + 4 + i * 0x30;
        if (At<uint32_t>(e, 0x1C) == 1 && At<uint8_t>(e, 0x29) == 1) return At<uint32_t>(e, 4) != 0x115;
    }
    return true;
}

struct ViewSettings {
    float back = 0.0f, fov = 0.0f, aimFov = 0.0f;
};

// The local player's own camera, outside Mode=MeleeCamera; false when there is nothing to change.
static bool ViewSettingsFor(uint32_t q, ViewSettings* out) {
    const Config& c = Cfg();
    *out = ViewSettings{};
    if (!c.enabled || c.meleeOnly ||
        (c.aimDistance <= 0.0f && c.fieldOfView == 0.0f && c.aimFieldOfView == 0.0f && ZoomFovExtra() == 0.0f) ||
        !Readable((void*)q, 0x680))
        return false;
    const uint32_t ch = At<uint32_t>(q, cam::Parent);
    if (!ch || !Readable((void*)ch, 0x3000) || At<uint32_t>(ch, ch::NetControl) != 0 || At<uint8_t>(ch, ch::Scope))
        return false;
    const uint32_t ctrl = q - 0xE00;
    const uint32_t rig = Readable((void*)ctrl, 0x3100) && At<uint32_t>(ctrl, 0) == 0x00F6CA30 ? At<uint32_t>(ctrl, kAimRigId) : 0;
    const bool mounted = MountedGunAim(ch, rig);
    out->back = mounted ? 0.0f : c.aimDistance;
    out->fov = c.fieldOfView;
    out->aimFov = mounted ? 0.0f : c.aimFieldOfView + ZoomFovExtra();  // plain-gun zoom (F / C) adds here
    return true;
}

// Around the QFPS update: the pull-back before it, the fov after it.
struct ViewEdit {
    uint32_t self = 0;
    float savedOfs = 0.0f;
    ViewSettings v;

    void Before(uint32_t q) {
        if (!ViewSettingsFor(q, &v)) return;
        self = q;
        savedOfs = At<float>(q, kReadyOfs);
        if (v.back > 0.0f) At<float>(q, kReadyOfs) = savedOfs - v.back;
    }

    void After() {
        if (!self) return;
        At<float>(self, kReadyOfs) = savedOfs;
        const float add = At<uint8_t>(self, cam::AimRig) == 1 && !At<uint8_t>(self, cam::LookAt) ? v.aimFov : v.fov;
        float& fov = At<float>(self, cam::FovAim);
        if (add != 0.0f && fov > 1.0f) {
            fov += add;
            if (fov < 10.0f) fov = 10.0f;
            if (fov > 120.0f) fov = 120.0f;
        }
        self = 0;
    }
};

// Wall collision escapes toward one ear (ctrl+0x30B6: 0 right, 1 left); it follows the shoulder.
static void SetEscapeEar(uint32_t q, int idx, bool left) {
    static uint32_t ctrlSeen[4] = {};
    static uint8_t base[4] = {};
    const uint32_t ctrl = q - 0xE00;
    if (!Readable((void*)ctrl, 0x3100) || At<uint32_t>(ctrl, 0) != 0x00F6CA30) return;
    if (ctrl != ctrlSeen[idx]) {
        ctrlSeen[idx] = ctrl;
        base[idx] = At<uint8_t>(ctrl, 0x30B6);
    }
    At<uint8_t>(ctrl, 0x30B6) = left ? (uint8_t)(base[idx] ^ 1) : base[idx];
}

// ------------------------------------------------------------------------------------------------ orbit camera
struct OrbitState {
    uint32_t camera = 0;          // the QFPS camera the orbit last drew into
    float pivotY = 0;
    bool havePivot = false;
    float shoulder = 1.0f;        // +1 right, -1 left (eased when swapped)
    bool lastOwn = false;
    bool haveLast = false;
    float lastEye[3] = {}, lastTgt[3] = {};
    float fromEye[3] = {}, fromTgt[3] = {};
    double blendStart = -1;
    double lastTime = 0;
    bool returning = false;       // Mode=MeleeCamera: turning back to where the game's camera looks
    bool returned = false;        // ... done: the game's camera until the next melee attack
    double returnStart = 0;
    // the platform the character stood on last frame (to carry the camera along with it)
    uint32_t basePar = 0;
    int32_t baseNo = 0;
    float baseM[16] = {};
};
static OrbitState g_orbits[4];  // by player slot, as the free cameras

static const float kBlendSec = 0.3f;

// A point that moved with the platform from matrix a to matrix b (rows: axes, row 3: translation).
static void CarryPoint(const float* a, const float* b, float* v) {
    float l[3];
    for (int r = 0; r < 3; ++r) {
        const float* ax = a + r * 4;
        const float n = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2];
        l[r] = n > 1e-12f ? ((v[0] - a[12]) * ax[0] + (v[1] - a[13]) * ax[1] + (v[2] - a[14]) * ax[2]) / n : 0.0f;
    }
    ToWorld(b, l, v);
}

// While the character rides a moving platform, everything the camera remembers in world space (the smoothed pivot
// height, the blend start, last frame's eye) moves with the platform, and the orbit turns with it, as the game's
// own camera does. Nothing is carried across a change of platform (attach, detach, teleport).
static void FollowPlatform(FreeCam& fc, OrbitState& o) {
    const uint32_t ch = fc.character;
    const float* m = ParentMatrix(ch);
    const uint32_t par = m ? At<uint32_t>(ch, ch::Parent) : 0;
    const int32_t no = m ? At<int32_t>(ch, ch::ParentNo) : 0;
    if (m && par == o.basePar && no == o.baseNo) {
        const float* a = o.baseM;
        float before[3], after[3];
        ToWorld(a, (const float*)(ch + ch::Pos), before);
        ToWorld(m, (const float*)(ch + ch::Pos), after);
        o.pivotY += after[1] - before[1];
        CarryPoint(a, m, o.fromEye);
        CarryPoint(a, m, o.fromTgt);
        CarryPoint(a, m, o.lastEye);
        CarryPoint(a, m, o.lastTgt);
        if (Cfg().followPlatform) fc.yaw = WrapPi(fc.yaw + WrapPi(MatrixYaw(m) - MatrixYaw(a)));
    }
    o.basePar = par;
    o.baseNo = no;
    if (m) memcpy(o.baseM, m, 64);
}

static void Orbit(const FreeCam& fc, OrbitState& o, float dt, float* eye, float* tgt) {
    const Config& c = Cfg();
    float pos[3];
    CharacterWorldPos(fc.character, pos);
    // Pivot at shoulder height; its height follows the character smoothly (stairs, slopes).
    const float wantY = pos[1] + c.cameraHeight;
    if (!o.havePivot || fabsf(o.pivotY - wantY) > 150.0f) o.pivotY = wantY;
    o.pivotY += (wantY - o.pivotY) * (1.0f - expf(-dt * 10.0f));
    o.havePivot = true;
    const float wantSide = fc.leftShoulder ? -1.0f : 1.0f;
    o.shoulder += (wantSide - o.shoulder) * (1.0f - expf(-dt * 8.0f));

    const float p = fc.pitch;
    const float cy = cosf(fc.yaw), sy = sinf(fc.yaw), cp = cosf(p), sp = sinf(p);
    const float fwd[3] = {sy * cp, -sp, cy * cp};
    const float right[3] = {-cy, 0.0f, sy};
    // Looking up: the camera comes closer and nearer to the character's axis instead of digging into the floor.
    const float up01 = p < 0.0f ? (-p / Rad(40.0f) > 1.0f ? 1.0f : -p / Rad(40.0f)) : 0.0f;
    const float dist = c.cameraDistance * (1.0f - 0.35f * up01);
    const float side = c.cameraShoulder * o.shoulder * (1.0f - 0.25f * up01);
    for (int i = 0; i < 3; ++i) eye[i] = (i == 1 ? o.pivotY : pos[i]) - fwd[i] * dist + right[i] * side;
    const float floorY = pos[1] + 30.0f;
    if (eye[1] < floorY) eye[1] = floorY;
    for (int i = 0; i < 3; ++i) tgt[i] = eye[i] + fwd[i] * 400.0f;
}

// ------------------------------------------------------------------------------------------------ hooks
// The slot of the local player whose free camera this QFPS camera is, or -1.
static int OurCamera(uint32_t q) {
    if (!Cfg().enabled || !Readable((void*)q, 0x680)) return -1;
    const uint32_t ch = At<uint32_t>(q, cam::Parent);
    return IsCamCharacter(ch) ? (At<uint8_t>(ch, ch::NetSlot) & 3) : -1;
}

// uCameraCtrl::update: it picks the lag-camera mode from the aim flag before running the QFPS update, so the mod's
// reload-on-the-move clears the flag here (the character raised the gun without the player aiming).
static void __fastcall HookCtrlUpdate(uint32_t ctrl, void* edx, uint32_t dt) {
    const Config& c = Cfg();
    // noAimRig only matters with the full free camera; MeleeCamera never sets it.
    // The scope is left to the game (see game.cpp): no lookV write here.
    if (c.enabled && c.freeCamera && !c.meleeOnly) {
        const int idx = OurCamera(ctrl + 0xE00);
        if (idx >= 0 && g_cams[idx].noAimRig) At<uint8_t>(ctrl, 0xE00 + cam::AimRig) = 0;
    }
    oCtrlUpdate(ctrl, edx, dt);
}

// Aim camera lag. While aiming, the controller's lag camera (mLAG1, ctrl+0x380) keeps tracking^(30*dt) of last
// frame's view, with tracking = QFPS+0x15C, which the QFPS update sets to sParameter+0x45C = 0.8 for a pad (about
// 0.14 s behind: ~8 deg at the default aim speed, ~16 deg at AimSpeedX=2, enough for the arm or body to swing into
// the crosshair), plus a 3-unit dead band. For the mouse the game itself ramps tracking to 0.05 within 0.5 s and sets
// the dead band to 0.1 (0xB752D1); the mod does the same for the pad player aiming a gun (crosshair or laser), with
// tracking = base^(1/AimCameraLag) so the time constant scales by AimCameraLag. Written after the QFPS update and
// before the controller reads it (0x8427A9).
static double g_aimTrackStart[4] = {-1, -1, -1, -1};

static void AimTracking(uint32_t q, int idx) {
    const Config& c = Cfg();
    const FreeCam& fc = g_cams[idx];
    double& trackStart = g_aimTrackStart[idx];
    // The pad's aim with the crosshair or the laser sight; the game's keyboard aim has its own tight tracking.
    const bool on = c.enabled && !c.meleeOnly && c.aimCameraLag < 1.0f && At<uint8_t>(q, cam::AimRig) == 1 &&
                    At<uint8_t>(q, 0x1B0) != 0 && fc.character && PadGunAimed(fc.character);
    if (!on) {
        trackStart = -1;
        return;
    }
    const double now = NowSec();
    if (trackStart < 0) trackStart = now;
    const uint32_t prm = Readable((void*)0x011B27B4, 4) ? At<uint32_t>(0x011B27B4, 0) : 0;
    float base = prm && Readable((void*)(prm + 0x45C), 4) ? At<float>(prm, 0x45C) : 0.8f;
    if (!(base > 0.0f && base < 1.0f)) base = 0.8f;
    const float want = c.aimCameraLag <= 0.001f ? 0.0f : powf(base, 1.0f / c.aimCameraLag);
    float t = (float)((now - trackStart) / 0.5);  // ease in like the game's mouse ramp
    if (t > 1.0f) t = 1.0f;
    At<float>(q, 0x15C) = base + (want - base) * t;
    const uint32_t ctrl = q - 0xE00;
    if (Readable((void*)ctrl, 0x610) && At<uint32_t>(ctrl, 0) == 0x00F6CA30) {
        At<float>(ctrl, 0x600) = 0.1f;  // LAG1 dead band for this frame, as for the mouse
        At<uint8_t>(ctrl, 0x604) = 1;
    }
}

// Camera ray while aiming, shared with crosshair.cpp through AimCameraRay().
static float g_aimEye[3] = {0, 0, 0}, g_aimDir[3] = {0, 0, 1};
static bool g_aimRayValid = false;
static float g_aimPitchLast = 0.0f;
float AimPitchLast() { return g_aimPitchLast; }
// Mouse vertical for the aim, fed by game.cpp (it owns the mouse deltas): when AimPitchFromMouse is on the mod drives
// the aim pitch and writes it into cam::LookV every frame.  Writing the RESULT each frame does not accumulate, unlike
// seeding the state on the press - that was the "every aim walks a little lower" bug.
static float g_aimMouseDY = 0.0f, g_aimMouseScale = 0.0f;
static bool g_aimMouseInvert = false;
void SetAimMouse(float dy, float scale, bool invert) { g_aimMouseDY = dy; g_aimMouseScale = scale; g_aimMouseInvert = invert; }
bool AimCameraRay(float eye[3], float dir[3]) {
    if (!g_aimRayValid) return false;
    for (int i = 0; i < 3; ++i) { eye[i] = g_aimEye[i]; dir[i] = g_aimDir[i]; }
    return true;
}
static bool g_lastAimRig[4] = {};
static int g_aimSeedFrames[4] = {};
// One compact line per aim (start, ~0.35 s in, end) with every field the aim could follow, so a single aim with the
// mouse moved up and down shows which one tracks the camera: camPitch = the view the player sees, shotY = the point the
// game aims/ shoots at (ch+0x2700), aim8/aimC = the two character angles, lookV = the game's vertical look channel.
static void AimTrace(uint32_t self, int idx, const char* when) {
    const uint32_t ch = At<uint32_t>(self, cam::Parent);
    const float* eye = (const float*)(self + cam::EyeAim);
    const float* tgt = (const float*)(self + cam::TargetAim);
    const float dx = tgt[0] - eye[0], dz = tgt[2] - eye[2];
    const float len = sqrtf(dx * dx + dz * dz);
    const float camPitch = len > 0.001f ? atan2f(eye[1] - tgt[1], len) * 57.3f : 0.0f;
    float a8 = 0.0f, aC = 0.0f, shotY = 0.0f, rayY = 0.0f;
    if (ch && Readable((void*)ch, 0x3000)) {
        a8 = At<float>(ch, 0x2908) * 57.3f;
        aC = At<float>(ch, 0x290C) * 57.3f;
        shotY = At<float>(ch, 0x2704);
        rayY = At<float>(ch, 0x2894);
    }
    Log("aimtrace[%s] camPitch=%+.1f lookV=%+.2f aim8=%+.1f aimC=%+.1f shotY=%+.0f rayY=%+.0f",
        when, camPitch, At<float>(self, cam::LookV), a8, aC, shotY, rayY);
}
static void __fastcall HookQfpsUpdate(uint32_t self, void* edx, uint32_t arg) {
    const Config& c = Cfg();
    if (!c.enabled) {
        oQfpsUpdate(self, edx, arg);
        return;
    }
    const int idx = OurCamera(self);
    if (!(c.freeCamera || c.meleeOnly) || idx < 0) {
        // Other characters' cameras (the online partner's) update too: only a camera losing its orbit (or the orbit
        // being switched off) drops what that orbit remembers.
        for (OrbitState& o : g_orbits)
            if (self == o.camera || !(c.freeCamera || c.meleeOnly)) {
                o.haveLast = false;
                o.returning = false;
                o.camera = 0;
            }
        ViewEdit view;
        view.Before(self);
        oQfpsUpdate(self, edx, arg);
        view.After();
        if (idx >= 0) AimTracking(self, idx);
        return;
    }
    FreeCam& fc = g_cams[idx];
    OrbitState& o = g_orbits[idx];
    // Looking at the partner (locate-partner button, co-op action points): the game turns the camera.
    const bool lookAt = At<uint8_t>(self, cam::LookAt) || At<uint8_t>(self, cam::LookAt + 1);
    // Mode=MeleeCamera: the orbit runs during melee attacks, then turns back toward where the game's camera looks
    // before handing over (a straight blend after a big swing would pass through the character).
    const double tNow = NowSec();
    if (!fc.initialised || tNow - fc.lastCameraUpdate > 0.5) {
        // After a gap in our camera's updates nothing from before it is carried over.
        o.returning = false;
        o.lastOwn = false;
        o.haveLast = false;
        o.returned = true;
        o.basePar = 0;
        o.baseNo = 0;
    }
    bool active = fc.active;
    if (c.meleeOnly) {
        active = fc.active && tNow - fc.activeTime < 0.2;
        if (active) {
            o.returning = false;
            o.returned = false;
        } else if (o.lastOwn && o.haveLast && !o.returning && !o.returned) {
            o.returning = true;
            o.returnStart = tNow;
            if (DevMode()) Log("dev: melee camera returning");
        }
        // Fast path: MeleeCamera and the orbit is idle. Skip pose mirror, FollowPlatform, Orbit, blends.
        // Still run the game's camera and keep a light track of its yaw/pitch so the next melee starts clean.
        if (!active && !o.returning && o.blendStart < 0 && !lookAt) {
            o.camera = self;
            oQfpsUpdate(self, edx, arg);
            float* eye = (float*)(self + cam::EyeAim);
            float* tgt = (float*)(self + cam::TargetAim);
            const float dx = tgt[0] - eye[0], dz = tgt[2] - eye[2];
            if (dx * dx + dz * dz >= 1.0f) {
                fc.yaw = atan2f(dx, dz);
                const float len = sqrtf(dx * dx + dz * dz);
                float pitch = atan2f(eye[1] - tgt[1], len);
                if (pitch > Rad(55.0f)) pitch = Rad(55.0f);
                if (pitch < Rad(-40.0f)) pitch = Rad(-40.0f);
                fc.pitch = pitch;
                fc.initialised = true;
                fc.lastCameraUpdate = tNow;
                memcpy(o.lastEye, eye, 12);
                memcpy(o.lastTgt, tgt, 12);
                o.haveLast = true;
                o.lastOwn = false;
            }
            return;
        }
    }
    o.camera = self;
    if (fc.noAimRig) At<uint8_t>(self, cam::AimRig) = 0;
    // AimCamera: keep the mod's own camera during the aim.  The game's aim camera is a separate camera with its own
    // pitch, so handing the view over on every aim press made the view jump to a direction the player had not chosen
    // (and writing our pitch into it to compensate drifted a little on every press).  Keeping the orbit camera means the
    // view simply does not move when aiming: the crosshair stays where the player was looking.
    const bool own = (active || (c.meleeOnly && o.returning)) && !lookAt &&
                     (At<uint8_t>(self, cam::AimRig) == 0 || c.aimCamera);
    if (!own) o.returning = false;
    uint8_t lookEnabled = 0;
    if (own) {
        // Keep the game's camera neutral underneath: its look-around reads the raw right stick (0x8454E0) and
        // is skipped while +0x2A0 is 0.
        lookEnabled = At<uint8_t>(self, cam::LookEnabled);
        At<uint8_t>(self, cam::LookEnabled) = 0;
        At<float>(self, cam::LookV) = 0.0f;
        At<float>(self, cam::LookH) = 0.0f;
    }
    PoseMirror mirror;
    // The game's own aim side is per character (in game, Chris aims with the character on one side, Sheva the other),
    // so a fixed rule is wrong for one of them.  It is measured instead, but only while NOT mirroring and only during
    // the aim, over three consecutive frames of the same sign, so the mirror cannot feed back into its own measurement
    // (that feedback made the pose x alternate +40/-40 in an earlier build).  Until it is known the aim is not
    // mirrored at all.
    {
        static uint8_t sideFrames[4] = {};
        static bool sideValue[4] = {};
        static uint32_t sideFor[4] = {};
        const bool rigNow = At<uint8_t>(self, cam::AimRig) == 1;
        // The measurement is remembered per character: repeating it on every aim made the first frames of each aim
        // run unmirrored and then snap over, which is the twitch that showed up now and then.
        if (sideFor[idx] != fc.character) {
            sideFor[idx] = fc.character;
            fc.aimSideKnown = false;
            sideFrames[idx] = 0;
        }
        if (rigNow) {
            if (!fc.aimSideKnown) {
                const uint32_t pp = At<uint32_t>(self, kPosePtrs + 3 * 4);  // first of the three aim poses
                const float* pose = Readable((void*)pp, 0x28) ? (const float*)pp : (const float*)(self + kInlinePoses[3]);
                const bool side = pose[0] < 0.0f;  // +X is the character's left: eye.x < 0 => character on the left
                if (sideFrames[idx] == 0 || side != sideValue[idx]) {
                    sideValue[idx] = side;
                    sideFrames[idx] = 1;
                } else if (sideFrames[idx] < 3) {
                    ++sideFrames[idx];
                }
                if (sideFrames[idx] >= 3) {
                    fc.gameCharLeft = side;
                    fc.aimSideKnown = true;
                    Log("shoulder: game aims with the character on the %s (pose x %+.1f)", side ? "left" : "right",
                        pose[0]);
                }
            }
            fc.mirrorAim = fc.aimSideKnown && (fc.charLeft != fc.gameCharLeft);
        } else {
            sideFrames[idx] = 0;
            fc.mirrorAim = false;  // never outside the aim: the measurement below must read the game's own pose
        }
    }
    // Never mirror in a scope: the scope is drawn by the weapon camera (uCameraWep) with its own poses, so flipping
    // the QFPS poses there is not what the player sees - it only fights the weapon camera and showed up as a twitch
    // when moving while scoped.
    if (fc.mirrorAim) {
        const uint32_t chM = At<uint32_t>(self, cam::Parent);
        const bool scopedM = chM && Readable((void*)chM, 0x3000) && At<uint8_t>(chM, ch::Scope) != 0;
        if (!scopedM) mirror.Apply(self);
    }
    // Aim pitch hand-off, measured from the aimdbg samples: the game's vertical look channel (cam::LookV) tracks
    // the view pitch as  lookV = -pitch / 55 deg  (camPitch -16.3 deg -> lookV +0.319, -27.6 -> +0.509).  Carrying
    // the orbit pitch here, before the game's own camera update, makes the aim start where the player was looking;
    // the earlier attempt used the opposite sign, which is why it looked unchanged.
    {
        const bool rigNow = At<uint8_t>(self, cam::AimRig) == 1;
        if (rigNow && !g_lastAimRig[idx]) {
            // Restored: the run that the user reported as "basically done" carried the pitch here, BEFORE the game's
            // camera update, with the measured sign.  Removing it (to chase a one-frame jump) is what made the aim
            // stop following the pitch for a plain gun.
            // REMOVED: the LookV seed.  Report (3440x1440, Sheva): aiming at the sky and pressing aim/cancel repeatedly
            // walked the aim from the sky down to the ground without moving the camera.  A seed that the game converts
            // back through its own scale loses a little pitch per aim when the two scales differ, and the loss adds up
            // over presses - exactly that drift.  The game keeps its aim pitch between aims, so no seed is needed for a
            // stable result; fc.pitch below only FOLLOWS the game's pitch, it no longer writes into it.
            g_aimSeedFrames[idx] = 0;
            // lookV alone does not hold: the game recomputes it every frame and the entry pitch was gone within
            // 0.35 s (trace: camPitch -14.9 at "in", +5.5 at "mid").  The character's aim angles do hold, so they
            // are seeded once as well, here, before the game's camera update uses them.
            const uint32_t chA = At<uint32_t>(self, cam::Parent);
            if (chA && Readable((void*)chA, 0x3000)) {
                // Absolute aim-pitch hand-off: the character's aim angles are set to the view pitch once per aim.  This
            // is an absolute write (not a delta and not the LookV channel), so repeated aims converge to the same value
            // instead of accumulating; the LookV write stays removed because that one WAS additive and drifted.
            if (chA && Readable((void*)chA, 0x3000)) {
                At<float>(chA, 0x2908) = fc.pitch;
                At<float>(chA, 0x290C) = fc.pitch;
            }
                // The aim POINT, not only the angles: the game derives the aim direction (and the scope's, and the
                // knife's, neither of which goes through the bullet convergence the plain gun now uses) from
                // ch+0x2870, a world point along the aim ray.  Put it on the free camera's ray at the moment the aim
                // starts, from the character's chest, so every aiming mode begins where the player was looking.
                float pos[3];
                CharacterWorldPos(chA, pos);
                const float cy2 = cosf(fc.pitch), sy2 = sinf(fc.pitch);
                const float dx2 = sinf(fc.yaw) * cy2, dy2 = -sy2, dz2 = cosf(fc.yaw) * cy2;
                At<float>(chA, 0x2870) = pos[0] + dx2 * 4000.0f;
                At<float>(chA, 0x2874) = pos[1] + 90.0f + dy2 * 4000.0f;
                At<float>(chA, 0x2878) = pos[2] + dz2 * 4000.0f;
                At<float>(chA, 0x2700) = At<float>(chA, 0x2870);
                At<float>(chA, 0x2704) = At<float>(chA, 0x2874);
                At<float>(chA, 0x2708) = At<float>(chA, 0x2878);
            }
            AimTrace(self, idx, "in");
        }
        // One more sample a moment later and one when the aim ends: together they show which field the game's aim
        // actually follows while the player moves the mouse.
        if (rigNow) {
            static double traceAt[4] = {};
            const double tNow = NowSec();
            if (traceAt[idx] == 0) traceAt[idx] = tNow;
            if (tNow - traceAt[idx] > 0.35 && g_lastAimRig[idx]) { traceAt[idx] = 0; AimTrace(self, idx, "mid"); }
        } else if (g_lastAimRig[idx]) {
            AimTrace(self, idx, "out");
        }
        g_lastAimRig[idx] = rigNow;
    }
    // The old post-update seed of ch+0x2908/+0x290C is gone.  Those are NOT the aim pitch: when they were written the
    // values accumulated (+91.7, +103.1 deg), i.e. they are angles that the game keeps adding to, and writing them for
    // the first frames of every aim is one of the things that made the character twitch when aiming started.
    g_aimSeedFrames[idx] = 0;
    ViewEdit view;
    view.Before(self);
    SetEscapeEar(self, idx, fc.leftShoulder);
    oQfpsUpdate(self, edx, arg);
    mirror.Restore();
    view.After();
    AimTracking(self, idx);
    if (own) At<uint8_t>(self, cam::LookEnabled) = lookEnabled;

    const double now = NowSec();
    float dt = o.lastTime > 0 ? (float)(now - o.lastTime) : 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    o.lastTime = now;
    float* eye = (float*)(self + cam::EyeAim);
    float* up = (float*)(self + cam::UpAim);
    float* tgt = (float*)(self + cam::TargetAim);
    // Cache the camera ray for the crosshair code: with the free camera the barrel no longer points where the player
    // looks, and the game fires along the barrel, so the shot is put on this ray instead (HookLaserRaycast).
    if (At<uint8_t>(self, cam::AimRig) == 1) {
        float d[3] = {tgt[0] - eye[0], tgt[1] - eye[1], tgt[2] - eye[2]};
        const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1.0f) {
            for (int i = 0; i < 3; ++i) {
                g_aimEye[i] = eye[i];
                g_aimDir[i] = d[i] / len;
            }
            g_aimRayValid = true;
        }
    } else {
        g_aimRayValid = false;
    }
    const float dx = tgt[0] - eye[0], dz = tgt[2] - eye[2];
    if (dx * dx + dz * dz < 1.0f) return;
    const float baseYaw = atan2f(dx, dz);
    // Leaving the aim: adopt the pitch the game ended on, otherwise the orbit snaps the view back to the pitch it
    // had before the aim (the "camera returns to the angle I set" report) after the player had aimed somewhere else.
    {
        const bool rigNow = At<uint8_t>(self, cam::AimRig) == 1;
        if (!rigNow && g_lastAimRig[idx]) {
            const float len0 = sqrtf(dx * dx + dz * dz);
            if (len0 > 0.001f) {
                float p = atan2f(eye[1] - tgt[1], len0);
                if (p > Rad(55.0f)) p = Rad(55.0f);
                if (p < Rad(-40.0f)) p = Rad(-40.0f);
                fc.pitch = p;
                Log("aim exit: adopted game pitch %+.1f deg", p * 57.3f);
            }
        }
        g_lastAimRig[idx] = rigNow;
    }
    if (!fc.initialised || now - fc.lastCameraUpdate > 0.5) {
        fc.yaw = baseYaw;
        fc.pitch = DefaultCameraPitch();
        fc.initialised = true;
        o.havePivot = false;
        o.haveLast = false;
        o.shoulder = fc.leftShoulder ? -1.0f : 1.0f;
    }
    fc.lastCameraUpdate = now;
    FollowPlatform(fc, o);
    // Dev inspection camera (bridge reserved[20] = angle around the character, [21] = distance).
    const float devDist = DevMode() ? *(float*)&Bridge()->reserved[21] : 0.0f;
    if (devDist > 0.0f && devDist < 5000.0f) {
        const float devAngle = *(float*)&Bridge()->reserved[20];
        float p[3];
        CharacterWorldPos(fc.character, p);
        const float a = CharacterYaw(fc.character) + devAngle;
        eye[0] = p[0] + sinf(a) * devDist;
        eye[1] = p[1] + 120.0f;
        eye[2] = p[2] + cosf(a) * devDist;
        tgt[0] = p[0];
        tgt[1] = p[1] + 90.0f;
        tgt[2] = p[2];
        RebuildUp(eye, tgt, up);
        return;
    }
    if (own) {
        if (o.returning) {
            // Turn toward the game's view (its pitch from the unmodified output), then let go.
            const float len = sqrtf(dx * dx + dz * dz);
            float basePitch = atan2f(eye[1] - tgt[1], len);
            if (basePitch > Rad(55.0f)) basePitch = Rad(55.0f);
            if (basePitch < Rad(-40.0f)) basePitch = Rad(-40.0f);
            const float k = 1.0f - expf(-dt / 0.12f);
            fc.yaw = WrapPi(fc.yaw + WrapPi(baseYaw - fc.yaw) * k);
            fc.pitch += (basePitch - fc.pitch) * k;
            if (DevMode())
                Log("dev: return yaw %.1f -> game %.1f, pitch %.1f -> %.1f, %.2f s", fc.yaw * 57.3f, baseYaw * 57.3f,
                    fc.pitch * 57.3f, basePitch * 57.3f, now - o.returnStart);
            if ((fabsf(WrapPi(baseYaw - fc.yaw)) < Rad(3.0f) && fabsf(basePitch - fc.pitch) < Rad(3.0f)) ||
                now - o.returnStart > 0.6) {
                o.returning = false;  // next frame: the game's camera, with the usual short blend
                o.returned = true;
            }
        }
        Orbit(fc, o, dt, eye, tgt);
    } else {
        fc.yaw = baseYaw;  // aiming, looking at the partner or scripted: the game's camera, followed by the orbit
        // Shoulder in cover.  Low cover and wall cover use the game's own camera (the orbit is off there), so the
        // wanted side has no orbit shoulder to move and the swap key looked dead.  The game's camera is shifted
        // sideways here instead: eye and target move together along the camera's right axis, which is exactly what a
        // shoulder swap looks like, without taking the cover camera away from the game.
        {
            const uint32_t chC = At<uint32_t>(self, cam::Parent);
            if (chC && Readable((void*)chC, 0x3000) && (At<uint32_t>(chC, 0x2D8C) & 0xF00) != 0) {
                const float ox = tgt[0] - eye[0], oz = tgt[2] - eye[2];
                const float l = sqrtf(ox * ox + oz * oz);
                if (l > 1.0f) {
                    const float s = fc.charLeft ? 22.0f : -22.0f;
                    const float rx = oz / l, rz = -ox / l;
                    eye[0] += rx * s;
                    eye[2] += rz * s;
                    tgt[0] += rx * s;
                    tgt[2] += rz * s;
                }
            }
        }
        // Follow the pitch too, not only the yaw.  While aiming the game owns the view and the mouse pitches it, but
        // fc.pitch stayed frozen at the pre-aim value: when the aim ended the orbit put that old pitch back -- the
        // "the camera returns to the angle it had before aiming" report.  Adopting the view pitch every frame keeps
        // the orbit in step, so handing the view back is seamless.
        {
            const float len = sqrtf(dx * dx + dz * dz);
            // Only adopt the view pitch when the camera is not looking almost straight up or down: with len near zero
            // the atan2 saturates and fc.pitch jumped to its clamps (+55 / -40), which then went into everything that
            // builds a direction from fc.pitch (the knife aim point showed it in the log).  A flat view always has a
            // horizontal distance of hundreds of units, so this guard only rejects degenerate frames.
            if (c.aimPitchFromMouse) {
                // The mod owns the aim pitch: the mouse feeds fc.pitch and the result goes into the look channel every
                // frame, so the aim sits exactly where the camera points and nothing accumulates between aims.
                float d = g_aimMouseDY * g_aimMouseScale * (g_aimMouseInvert ? -1.0f : 1.0f);
                fc.pitch += d;
                if (fc.pitch > Rad(55.0f)) fc.pitch = Rad(55.0f);
                if (fc.pitch < Rad(-40.0f)) fc.pitch = Rad(-40.0f);
                At<float>(self, cam::LookV) = -fc.pitch / Rad(55.0f);
                g_aimPitchLast = fc.pitch;
            } else if (len > 10.0f) {
                float vp = atan2f(eye[1] - tgt[1], len);
                if (vp > Rad(55.0f)) vp = Rad(55.0f);
                if (vp < Rad(-40.0f)) vp = Rad(-40.0f);
                fc.pitch = vp;
                g_aimPitchLast = vp;
            }
        }
        if (c.meleeOnly) {
            const float len = sqrtf(dx * dx + dz * dz);
            fc.pitch = atan2f(eye[1] - tgt[1], len);
            if (fc.pitch > Rad(55.0f)) fc.pitch = Rad(55.0f);
            if (fc.pitch < Rad(-40.0f)) fc.pitch = Rad(-40.0f);
        }
        // The per-frame write of ch+0x2908/+0x290C that was here is REMOVED again: rewriting the character's aim
        // angles every frame resets the game's own pitch integrator, so the pitch could no longer be adjusted while
        // aiming (reported straight after that build).  The game owns the aim pitch; the mod only seeds it once when
        // the aim starts and otherwise leaves it alone.
    }
    // Blend for a moment whenever the camera changes hands, from wherever the last frame left it.
    // Mode=MeleeCamera blends the hand-over to and from the game's camera itself. In the full mode the camera
    // controller's own smoothing covers aiming and the look-at moments (a blend of the mod's on top of it made the
    // view wobble for a few frames), as it always did in practice.
    if (c.meleeOnly && o.haveLast && own != o.lastOwn) {
        // A straight blend between two views that look very different ways would carry the eye through the
        // character: those cut instead.
        float a[3], b[3];
        for (int i = 0; i < 3; ++i) {
            a[i] = o.lastTgt[i] - o.lastEye[i];
            b[i] = tgt[i] - eye[i];
        }
        const float la = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const float lb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        if (la > 1e-3f && lb > 1e-3f && (a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / (la * lb) > 0.26f) {  // < 75 deg
            memcpy(o.fromEye, o.lastEye, 12);
            memcpy(o.fromTgt, o.lastTgt, 12);
            o.blendStart = now;
        } else {
            o.blendStart = -1;
        }
    }
    o.lastOwn = own;
    if (o.blendStart >= 0) {
        float t = (float)(now - o.blendStart) / kBlendSec;
        if (t >= 1.0f) {
            o.blendStart = -1;
        } else {
            t = t * t * (3.0f - 2.0f * t);
            for (int i = 0; i < 3; ++i) {
                eye[i] = o.fromEye[i] + (eye[i] - o.fromEye[i]) * t;
                tgt[i] = o.fromTgt[i] + (tgt[i] - o.fromTgt[i]) * t;
            }
        }
    }
    memcpy(o.lastEye, eye, 12);
    memcpy(o.lastTgt, tgt, 12);
    o.haveLast = true;
    if (DevMode()) {
        Bridge()->reserved[5] = self;
        Bridge()->reserved[7] = (own ? 1u : 0u) | (o.returning ? 2u : 0u) | (o.blendStart >= 0 ? 4u : 0u);
    }
    if (c.meleeOnly && !own && o.blendStart < 0) return;  // the game's own camera, untouched
    RebuildUp(eye, tgt, up);
}

void InitCameraHooks() {
    static const BYTE sig[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x51, 0x0F, 0x57, 0xC9, 0x53, 0x56};
    if (!SafeEqual((void*)kFnQfpsUpdate, sig, sizeof(sig))) {
        Log("hook QFPS update: signature mismatch");
        return;
    }
    if (MH_CreateHook((void*)kFnQfpsUpdate, (void*)HookQfpsUpdate, (void**)&oQfpsUpdate) == MH_OK &&
        MH_QueueEnableHook((void*)kFnQfpsUpdate) == MH_OK)
        Log("hook QFPS update at %08x", kFnQfpsUpdate);
    else
        Log("hook QFPS update failed");
    static const BYTE sigCtrl[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC, 0x74, 0x53, 0x56, 0x8B, 0xF1};
    if (HookSite((void*)kFnCameraCtrlUpdate, sigCtrl, sizeof(sigCtrl)) &&
        MH_CreateHook((void*)kFnCameraCtrlUpdate, (void*)HookCtrlUpdate, (void**)&oCtrlUpdate) == MH_OK &&
        MH_QueueEnableHook((void*)kFnCameraCtrlUpdate) == MH_OK)
        Log("hook camera ctrl update at %08x", kFnCameraCtrlUpdate);
    else
        Log("hook camera ctrl update failed");
}
