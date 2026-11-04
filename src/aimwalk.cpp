// Walking while aiming (gun or knife) and while reloading.
//
// RE5's aim action keeps the character planted. The mod plays the weapon set's own walk/strafe/back clips in the
// last motion slots (6 and 7), masked to the pelvis and legs, so the upper body keeps the aim/fire/reload pose:
// uModel blends slots in order and each later slot overrides earlier ones per joint by the joint weights its
// clip wrote into the slot's pose buffer; zeroing those weights for every joint above the pelvis turns the
// walk clip into a lower-body layer. The clip's root motion moves the character and the game's collision pass
// keeps it out of walls.
//
// Directions: two clips play at once and the engine blends them (joints and root motion) by slot weight, as the game
// does with its own lean clips: slot 6 the forward or back walk at weight 1, slot 7 the strafe left or right at a
// weight from the stick angle, both masked to the lower body and kept at the same point of their step. The hips
// stay as the clips have them in every direction (with one clip turned toward a diagonal, the hips swung by up to
// 90 degrees whenever the nearest axis changed). Slot 6 normally holds the face; its requests wait meanwhile. A
// reload or switch on the run plays the run clip alone in slot 7. The same layer is played on the co-op partner's
// copy of the character (net.cpp).
//
// The upper-body mask is applied in the player's bind callback (uPlayer vt+0x7C, 0xB798B0), which the engine calls
// right after a new clip is bound to a slot and before the frame is evaluated; masking only in the blend would
// leave the first frame of each new clip full-body.
//
// Knife attacks (action 0x27) start their clips through setMotion with a fixed speed; the mod scales it, and the
// hit windows and the combo timing follow because they come from the clip's frames.
#include "common.h"
#include "re5.h"
#include "config.h"
#include "aimwalk.h"
#include "balance.h"
#include <math.h>
#include "MinHook.h"

using namespace re5;

typedef bool(__fastcall* PFN_SetMotion)(uint32_t model, void* edx, uint32_t slot, uint32_t motionNo, float inter,
                                        float startFrame, float speed, uint32_t flags);
typedef void(__fastcall* PFN_Blend)(uint32_t model, void* edx);
typedef void(__fastcall* PFN_BindSlot)(uint32_t model, void* edx, uint32_t slot);
static PFN_Blend oBlend = nullptr;
static PFN_BindSlot oBindSlot = nullptr;
static const uint32_t kFnPlayerBindSlot = 0x00B798B0;  // uPlayer vt+0x7C: thiscall(slot), after a clip is bound
static PFN_SetMotion oSetMotion = nullptr;
static bool g_ownCall = false;
static bool g_restoreHook = false;  // HookMotionUpdate installed: ShapeWaist saves the blend's values for it

static const uint32_t kLegSlot = 7;  // the last slot overrides all others
static const uint32_t kSlotFB = 6;   // forward/back clip of the directional blend (normally the face)
static const float kPi = 3.14159265f;

struct LegLayer {
    uint32_t ch = 0;
    bool active = false;
    uint16_t clip = 0xFFFF;
    uint32_t goodBank = 0;   // last motion set in which the strafe/back clips existed
    uint32_t bankFixStance = 0xFFFFFFFF;  // stance -> motion set substitution that was found to work
    uint32_t bankFix = 0;
    uint16_t savedMotion = 0xFFFF;
    float savedFrame = 0;
    float rate = 1.0f;
    float twist = 0;        // pelvis yaw, radians (+ = toward the character's left)
    float twistTarget = 0;
    float twistVel = 0;
    float k = 0;            // how much of the waist shaping applies: follows the clip fading in and out
    double lastShape = 0;
    float lastRel[4] = {0, 0, 0, 1};  // last spine1 -> pelvis rotation (keeps the waist blend on one side)
    float strike = 0;         // normalised time of the left heel strike of the clip in slot 7 (LeftStrike)
    float stopPhase = -1.0f;  // where in its step the layer stopped (relative to the left heel strike)
    double stopTime = 0;
    int root = -1;            // joint 0 (hip height): from the walk/run clip during a reload or switch on the move
    float rootW = 0, rootTarget = 0;
    float thetaF = 0, thetaV = 0;  // smoothed walking direction (keyboard)
    double lastTick = 0;           // partner copy: last twist/hip-height step
    // The blend's own pelvis/spine values before ShapeWaist changed them, put back before the engine's next motion
    // update copies the joints as the fade-from pose of a newly set clip (HookMotionUpdate).
    bool saveValid = false;
    uint32_t saveJoints = 0;
    float savePelvis[4], saveS1[4], saveS2[4];
    // Directional blend: slot 6 forward/back at weight 1 + slot 7 strafe left/right at weight w.
    bool two = false;
    bool back = false, right = false;
    uint16_t clipFB = 0xFFFF;
    float strikeFB = 0;
    float w = 0;
    float compatTwist = 0;  // sent to v1.7 partners, which play one clip turned toward the direction
    bool faceHeld = false;  // slot 6 borrowed; the face clip the game asked for meanwhile goes back at the end
    bool runGait = false;   // the run clip plays (with hysteresis against the walk blend)
    uint32_t noBlendBank = 0xFFFFFFFF;  // weapon set without the strafe/back clips: one clip only
    uint8_t walkBank = 0;   // walk bank before a reload, kept for characters of kind 4 and up (see AimWalkUpdate)
    double startTime = 0;   // when the layer became active
    // The game's own last request for the stance slots 3-5 while the layer replaced or skipped it (clip, speed),
    // put back when the layer ends if the slot still holds the replacement.
    uint16_t req[3] = {0xFFFF, 0xFFFF, 0xFFFF};
    float reqSpeed[3] = {1.0f, 1.0f, 1.0f};
    uint16_t savedFace = 0xFFFF;
    float savedFaceFrame = 0;
    float lowT = 0;         // how long the stick has been below the stop threshold
    float lastTheta = 0;
    int axis = 0;           // 0 forward, 1 left, 2 back, 3 right
    float lastPos[3] = {};  // parent-local, like ch+0x30 (see ch::Parent)
    bool havePos = false;
    uint32_t lastParent = 0;
    int32_t lastParentNo = 0;
    uint32_t maskFor = 0;
    uint32_t jointCount = 0;
    int pelvis = -1, spine1 = -1, spine2 = -1;
    uint8_t lower[256] = {};
    // remote copies: position/yaw follow the partner's packets
    bool remote = false;
    double lastPacket = 0;
    float from[3] = {}, to[3] = {};
    double fromT = 0, toT = 0;
    float yaw = 0;
};
static LegLayer g_layers[4];

static double Now() {
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static float Wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

static void MaskSlot(const LegLayer& L, uint32_t slot);  // applied from SetMotion, defined further down

static bool SetMotion(uint32_t ch, uint32_t slot, uint32_t no, float inter, float start = 0.0f, float speed = 1.0f) {
    g_ownCall = true;
    bool r = (oSetMotion ? oSetMotion : (PFN_SetMotion)kFnSetMotion)(ch, nullptr, slot, no, inter, start, speed, 0);
    g_ownCall = false;
    // Mask the freshly bound clip right away.  The bind callback (0xB798B0) does this before the frame is evaluated,
    // but on machines where RE5Fix patched that entry the hook cannot install, and then the first frame of every new
    // clip is full-body - the character jumps for one frame exactly when it starts moving (reported in the scope).
    if (r && (slot == kLegSlot || slot == kSlotFB)) {
        for (auto& L : g_layers) {
            if (L.active && L.ch == ch && L.jointCount) {
                MaskSlot(L, slot);
                break;
            }
        }
    }
    return r;
}

static uint32_t SlotObj(uint32_t ch, uint32_t slot) { return ch + ch::Motion0 + slot * ch::MotionStride; }

// Footsteps come from the walk clip's motion events, played by the character's motion-sound unit (uSoundMotionSe,
// vtable 0x0101F164, at ch+0x1460; +0x20 its model, +0x2C one bit per motion slot allowed to play sounds). The
// player's init sets that mask to 0x09 (slots 0 and 3) at 0xB6FE0F and the check is at 0xB8EBAB, so the layer's
// slot 7 walked silently; its bit is set while a layer plays (the engine picks the surface sound itself).
static bool g_stepSounds = false;
// bits: 0x80 = slot 7, 0x40 = slot 6 (only the clip with the larger share steps audibly in the blend), 0 = none.
static void LegLayerSounds(uint32_t ch, uint8_t bits) {
    const uint32_t u = ch + 0x1460;
    if (!g_stepSounds || !Readable((void*)u, 0x30) || At<uint32_t>(u, 0) != 0x0101F164 || At<uint32_t>(u, 0x20) != ch)
        return;
    uint8_t& m = At<uint8_t>(u, 0x2C);
    m = (uint8_t)((m & ~0xC0) | bits);
}

static float Frac(float x) { return x - floorf(x); }

// Normalised time of the left heel strike of the clip bound to a slot, from its sound events (setMotion points
// slot+0xC4 at the LMT entry's event collection 2: u16 remap[32], +0x40 count, +0x44 {u32 bits, u32 frames}[];
// footstep IDs 0 walkL, 2 runL, 4 kaitenL, 0x0C walk_backL). Chris's handgun set: walk/run/strafe left 0.02, back
// 0.52, strafe right 0.50; Sheva's and the odd sets differ, which is why it is read per clip.
static float LeftStrike(uint32_t ch, uint32_t slot, float fallback) {
    const uint32_t so = SlotObj(ch, slot);
    const uint32_t c2 = At<uint32_t>(so, 0xC4);
    const float len = At<float>(so, 0x1C);
    if (!c2 || len < 2.0f || !Readable((void*)c2, 0x48)) return fallback;
    const uint32_t n = At<uint32_t>(c2, 0x40), ev = At<uint32_t>(c2, 0x44);
    if (!n || n > 512 || !Readable((void*)ev, n * 8)) return fallback;
    uint32_t f = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t bits = At<uint32_t>(ev, i * 8);
        for (int b = 0; bits && b < 32; ++b)
            if (bits >> b & 1) {
                const uint16_t id = At<uint16_t>(c2, b * 2);
                if (id == 0 || id == 2 || id == 4 || id == 0x0C) return Frac((float)f / len);
            }
        f += At<uint32_t>(ev, i * 8 + 4);
    }
    return fallback;
}

// The local player's own walk or run (slot 0 clip 0x01-0x06) and where it was in its step (relative to the left
// heel strike), sampled every frame.
struct WalkSample {
    uint32_t ch = 0;
    float phase = 0;
    double time = 0;
    uint8_t bank = 0;  // motion bank of that clip
};
static WalkSample g_walk[4];  // one per local player (split screen)

static WalkSample* WalkFor(uint32_t ch, bool create) {
    for (auto& w : g_walk)
        if (w.ch == ch) return &w;
    if (!create) return nullptr;
    WalkSample* oldest = &g_walk[0];
    for (auto& w : g_walk)
        if (w.time < oldest->time) oldest = &w;
    *oldest = WalkSample{};
    oldest->ch = ch;
    return oldest;
}

static LegLayer* FindLayer(uint32_t ch, bool create) {
    for (auto& l : g_layers)
        if (l.ch == ch) return &l;
    if (!create) return nullptr;
    // A layer still fading out is busy, unless its character stopped being drawn (gone with a chapter restart).
    const double now = Now();
    for (auto& l : g_layers)
        if (!l.active && (l.k <= 0.0f || now - l.lastShape > 1.0)) { l = LegLayer{}; l.ch = ch; return &l; }
    return nullptr;
}

// Joints at or below the pelvis (MT joint id 0x0F) are the lower body.
static void BuildMask(LegLayer& L) {
    const uint32_t ch = L.ch;
    const uint32_t n = At<uint32_t>(ch, 0x314), joints = At<uint32_t>(ch, 0x318);
    if (L.maskFor == ch && L.jointCount == (n > 256 ? 256 : n)) return;
    L.maskFor = ch;
    L.jointCount = n > 256 ? 256 : n;
    L.pelvis = L.spine1 = L.spine2 = L.root = -1;
    memset(L.lower, 0, sizeof(L.lower));
    if (!Readable((void*)joints, L.jointCount * 0x90)) { L.jointCount = 0; return; }
    for (uint32_t j = 0; j < L.jointCount; ++j) {
        const uint8_t id = At<uint8_t>(joints, j * 0x90 + 7);
        if (id == 0x0F && L.pelvis < 0) L.pelvis = (int)j;
        if (id == 0x00 && L.root < 0) L.root = (int)j;
        if (id == 0x01 && L.spine1 < 0) L.spine1 = (int)j;
        if (id == 0x02 && L.spine2 < 0) L.spine2 = (int)j;
    }
    if (L.pelvis < 0) { L.jointCount = 0; return; }
    // The waist shaping needs spine2 to be the child of spine1 and both spine1 and the pelvis on the root.
    if (L.spine1 < 0 || L.spine2 < 0 || At<uint8_t>(joints, L.spine2 * 0x90 + 5) != L.spine1 ||
        At<uint8_t>(joints, L.spine1 * 0x90 + 5) != At<uint8_t>(joints, L.pelvis * 0x90 + 5))
        L.spine1 = L.spine2 = -1;
    for (uint32_t j = 0; j < L.jointCount; ++j) {
        uint32_t k = j;
        for (int depth = 0; depth < 64 && k < L.jointCount; ++depth) {
            if ((int)k == L.pelvis) { L.lower[j] = 1; break; }
            const uint8_t parent = At<uint8_t>(joints, k * 0x90 + 5);
            if (parent == 0xFF || parent == k) break;
            k = parent;
        }
    }
}

static void MaskSlot(const LegLayer& L, uint32_t slot) {
    const uint32_t buf = At<uint32_t>(SlotObj(L.ch, slot), 0xD8);
    if (!Readable((void*)buf, L.jointCount * 0x60)) return;
    for (uint32_t j = 0; j < L.jointCount; ++j) {
        if (L.lower[j]) continue;
        At<float>(buf, j * 0x60 + 0x0C) = 0.0f;
        // The root's position track is the hip height: the walk/run's own bob during a reload or switch on the move
        // (the aim keeps the stance's level hips; the aim camera follows the model, not this joint).
        At<float>(buf, j * 0x60 + 0x1C) = (int)j == L.root ? L.rootW : 0.0f;
        At<float>(buf, j * 0x60 + 0x2C) = 0.0f;
    }
}

static void MaskUpperBody(const LegLayer& L) {
    MaskSlot(L, kLegSlot);
    if (L.two) MaskSlot(L, kSlotFB);
}

// Called by the engine right after a clip is bound to a slot, before the frame is evaluated.
static void __fastcall HookBindSlot(uint32_t model, void* edx, uint32_t slot) {
    oBindSlot(model, edx, slot);
    if (slot != kLegSlot && slot != kSlotFB) return;
    for (auto& L : g_layers) {
        if (!L.active || L.ch != model) continue;
        if (slot == kLegSlot && At<uint16_t>(SlotObj(model, kLegSlot), 4) == L.clip) MaskSlot(L, kLegSlot);
        if (slot == kSlotFB && L.two && At<uint16_t>(SlotObj(model, kSlotFB), 4) == L.clipFB) MaskSlot(L, kSlotFB);
    }
}

// Quaternions (x, y, z, w); a joint's rotation in its parent's frame is parent * local.
struct Quat { float x, y, z, w; };
static Quat QMul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static Quat QConj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
static float QNorm(const Quat& q) { return sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }
static Quat QNlerp(const Quat& a, Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = {-b.x, -b.y, -b.z, -b.w};
    Quat r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const float n = QNorm(r);
    return n > 1e-6f ? Quat{r.x / n, r.y / n, r.z / n, r.w / n} : a;
}
#if RE3C_DEV
static float QAngleDeg(const Quat& a, const Quat& b) {
    const float d = fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) / (QNorm(a) * QNorm(b) + 1e-9f);
    return 2.0f * acosf(d > 1.0f ? 1.0f : d) * 57.29578f;
}
#endif

// Share of the waist bend spine1 takes over from the hips; spine2 keeps the chest (arms, gun, head) on aim.
static const float kWaistShare = 0.6f;

// After the blend: turn the legs toward the real walking direction and spread the bend between the walking hips
// and the aiming chest over the two spine joints.
//
// The aim pose is itself two layers in the original game (upper-body aim clips in slots 0-2, lower-body stance clips
// in slots 3-5, made to meet at the waist within ~3 degrees). The walk clip's pelvis is 10-40 degrees away from the
// aimed spine (it swings with every step, the leg twist adds to it), and the pelvis and spine1 both sit on the root
// at one point, so all of it showed as one kink there. Spine1 now turns most of the way toward the pelvis (in the
// walk clip itself the two stay within ~10 degrees) and spine2 takes the rest, so spine2 and everything above it keep
// their exact aim. The shaping fades in and out with the clip.
static void ShapeWaist(LegLayer& L) {
    const double now = Now();
    if (!L.active && L.lastShape > 0 && now - L.lastShape > 1.0) L.k = 0.0f;  // not drawn for a while: done
    float dt = L.lastShape > 0 ? (float)(now - L.lastShape) : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    L.lastShape = now;
    const float step = dt / 0.13f;  // the clip fades over 8 frames
    L.k = L.active ? (L.k + step > 1.0f ? 1.0f : L.k + step) : (L.k - step < 0.0f ? 0.0f : L.k - step);
    if (!L.active && L.k <= 0.0f) {
        L.twist = L.twistTarget = L.twistVel = 0;
        return;
    }
    // Still the skeleton the mask was built for (a fading layer's address may belong to another model by now).
    const uint32_t joints = At<uint32_t>(L.ch, 0x318);
    const uint32_t count = At<uint32_t>(L.ch, 0x314);
    if (L.pelvis < 0 || (count > 256 ? 256 : count) != L.jointCount ||
        !Readable((void*)joints, L.jointCount * 0x90) || At<uint8_t>(joints, L.pelvis * 0x90 + 7) != 0x0F ||
        (L.spine1 >= 0 && (At<uint8_t>(joints, L.spine1 * 0x90 + 7) != 0x01 ||
                           At<uint8_t>(joints, L.spine2 * 0x90 + 7) != 0x02))) {
        L.k = 0.0f;
        return;
    }
    if (g_restoreHook) {
        L.saveValid = true;
        L.saveJoints = joints;
        memcpy(L.savePelvis, (void*)(joints + L.pelvis * 0x90 + 0x20), 16);
        if (L.spine1 >= 0) {
            memcpy(L.saveS1, (void*)(joints + L.spine1 * 0x90 + 0x20), 16);
            memcpy(L.saveS2, (void*)(joints + L.spine2 * 0x90 + 0x20), 16);
        }
    }
    const float t = L.twist * L.k;
    Quat* pelvis = (Quat*)(joints + L.pelvis * 0x90 + 0x20);
    if (fabsf(t) > 0.001f) {
        const Quat tw = {0.0f, sinf(t * 0.5f), 0.0f, cosf(t * 0.5f)};
        *pelvis = QMul(tw, *pelvis);  // pre-rotate about the vertical (the root is aligned with the model)
    }

    const uint32_t devOff = DevMode() ? Bridge()->reserved[24] : 0;  // dev: switch parts off for comparisons
    if (L.spine1 < 0 || (devOff & 1)) return;
    Quat* s1 = (Quat*)(joints + L.spine1 * 0x90 + 0x20);
    Quat* s2 = (Quat*)(joints + L.spine2 * 0x90 + 0x20);
    const Quat chest = QMul(*s1, *s2);  // spine2 in the root's frame
#if RE3C_DEV
    Bridge()->reserved[25] = (uint32_t)(QAngleDeg(*pelvis, *s1) * 100.0f);
#endif
    float share = kWaistShare;
    if (DevMode() && Bridge()->reserved[27]) share = *(float*)&Bridge()->reserved[27];
    // spine1 -> pelvis as a rotation, kept on the same side as last frame: when the bend passes 180 degrees (knife
    // stance looking down) the shortest way would flip and the waist would snap to the other side.
    Quat rel = QMul(QConj(*s1), *pelvis);
    if (rel.x * L.lastRel[0] + rel.y * L.lastRel[1] + rel.z * L.lastRel[2] + rel.w * L.lastRel[3] < 0.0f)
        rel = {-rel.x, -rel.y, -rel.z, -rel.w};
    L.lastRel[0] = rel.x;
    L.lastRel[1] = rel.y;
    L.lastRel[2] = rel.z;
    L.lastRel[3] = rel.w;
    const float u = share * L.k;
    Quat part = {rel.x * u, rel.y * u, rel.z * u, 1.0f + (rel.w - 1.0f) * u};  // nlerp(identity, rel, u)
    const float pn = QNorm(part);
    if (pn < 1e-6f) return;
    part = {part.x / pn, part.y / pn, part.z / pn, part.w / pn};
    const Quat n1 = QMul(*s1, part);
    Quat n2 = QMul(QConj(n1), chest);
    const float nn = QNorm(n2);
    if (nn < 0.5f) return;
    *s1 = n1;
    *s2 = {n2.x / nn, n2.y / nn, n2.z / nn, n2.w / nn};
#if RE3C_DEV
    Bridge()->reserved[26] = (uint32_t)(QAngleDeg(*pelvis, *s1) * 100.0f);
#endif
}

static bool IsPlayerCharacter(uint32_t model);

#if RE3C_DEV
// Dev trace (tools/trace.py): one row per frame for the first local player, taken right after its model blend. v: 0 real dt (s), 1 game frame step (ch+0x10), 2-4 position, 5 body yaw (rad),
// 6-8 slot 7 clip / frame / frame count, 9-11 slot 0 clip / frame / frame count, 12 the layer's twist x fade (rad),
// 13 the pelvis's final local yaw (rad, model frame), 14 action id. trace_on == 3 (legs): 2-5 the final local rotation
// angle (deg) of the left knee, right knee, left thigh, right thigh (joints 0x11, 0x15, 0x10, 0x14), 12 slot 0's fade
// left (+0x10), 13 slot 7's.
static void TraceFrame(uint32_t character) {
    static double last = 0;
    const double now = Now();
    const double dt = last > 0 ? now - last : 0.0;
    last = now;
    BridgeShared* br = Bridge();
    if (!br || (br->trace_on != 1 && br->trace_on != 3) || !Readable((void*)character, 0x3000)) return;
    TraceRow& r = br->trace[br->trace_head % 256];
    r.frame = br->kbm.kbm_frame;
    r.v[0] = (float)dt;
    r.v[1] = At<float>(character, 0x10);
    for (int i = 0; i < 3; ++i) r.v[2 + i] = At<float>(character, ch::Pos + 4 * i);
    r.v[5] = 2.0f * atan2f(At<float>(character, ch::Quat + 4), At<float>(character, ch::Quat + 12));
    for (int k = 0; k < 2; ++k) {
        const uint32_t s = character + ch::Motion0 + (k ? 0 : 7) * ch::MotionStride;
        r.v[6 + 3 * k] = (float)(At<uint32_t>(s, 4) & 0xFFFF);
        r.v[7 + 3 * k] = At<float>(s, 0x18);
        r.v[8 + 3 * k] = At<float>(s, 0x1C);
    }
    r.v[12] = r.v[13] = 0.0f;
    for (const auto& L : g_layers)
        if (L.ch == character && (L.active || L.k > 0.0f)) r.v[12] = L.twist * L.k;
    const uint32_t joints = At<uint32_t>(character, 0x318);
    const uint32_t count = At<uint32_t>(character, 0x314);
    if (joints && count && count < 512 && Readable((void*)joints, count * 0x90)) {
        for (uint32_t j = 0; j < count; ++j)
            if (At<uint8_t>(joints, j * 0x90 + 7) == 0x0F) {
                // The final local rotation (the pelvis sits on the root, which is aligned with the model).
                const float x = At<float>(joints, j * 0x90 + 0x20), y = At<float>(joints, j * 0x90 + 0x24);
                const float z = At<float>(joints, j * 0x90 + 0x28), w = At<float>(joints, j * 0x90 + 0x2C);
                r.v[13] = atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
                break;
            }
    }
    if (br->trace_on == 3) {
        static const uint8_t kIds[4] = {0x11, 0x15, 0x10, 0x14};
        for (int i = 0; i < 4; ++i) {
            r.v[2 + i] = -1.0f;
            if (!joints || !count || count >= 512 || !Readable((void*)joints, count * 0x90)) continue;
            for (uint32_t j = 0; j < count; ++j)
                if (At<uint8_t>(joints, j * 0x90 + 7) == kIds[i]) {
                    const Quat q = At<Quat>(joints, j * 0x90 + 0x20);
                    const float n = QNorm(q), w = n > 1e-6f ? fabsf(q.w) / n : 1.0f;
                    r.v[2 + i] = 2.0f * acosf(w > 1.0f ? 1.0f : w) * 57.29578f;
                    break;
                }
        }
        r.v[12] = At<float>(character + ch::Motion0, 0x10);
        r.v[13] = At<float>(character + ch::Motion0 + 7 * ch::MotionStride, 0x10);
    }
    r.v[14] = (float)At<uint32_t>(character, ch::ActionId);
    br->trace_head++;
}
#endif

// The engine fades a newly set clip from a copy of the joints taken by the next motion update (0x59D0B0 copies
// +0x20/+0x40/+0x30 into the slot buffer; the blend 0x588230 pulls toward it by slot+0x28). That copy is last frame's
// final pose, which already holds ShapeWaist's twist and waist shaping, so every clip change in any slot (direction
// change, shot, reload, aim-turn start/stop) added them a second time for the length of the fade. The blend's own
// values go back first. Hooked at the thunk 0x8305A0 (vt+0x5C of uBioModel and every class derived from it: players,
// enemies, items), so the model is checked to be the layer's player with the same skeleton.
typedef void(__fastcall* PFN_MotionUpdate)(uint32_t model, void* edx);
static PFN_MotionUpdate oMotionUpdate = nullptr;
static const uint32_t kFnMotionUpdateThunk = 0x008305A0;  // jmp 0x59D0B0

static bool AnyLayerBusy() {
    for (const auto& L : g_layers)
        if (L.active || L.k > 0.0f || L.saveValid || L.remote) return true;
    return false;
}

static void __fastcall HookMotionUpdate(uint32_t model, void* edx) {
    // Runs on every model motion update. Skip the layer scan when nothing is walking.
    if (g_restoreHook && AnyLayerBusy()) {
        for (auto& L : g_layers) {
            if (L.ch != model || !L.saveValid) continue;
            L.saveValid = false;
            const uint32_t joints = At<uint32_t>(model, 0x318);
            const uint32_t count = At<uint32_t>(model, 0x314);
            if (joints != L.saveJoints || L.pelvis < 0 || (count > 256 ? 256 : count) != L.jointCount ||
                !Readable((void*)joints, L.jointCount * 0x90) || At<uint8_t>(joints, L.pelvis * 0x90 + 7) != 0x0F ||
                (L.spine1 >= 0 && (At<uint8_t>(joints, L.spine1 * 0x90 + 7) != 0x01 ||
                                   At<uint8_t>(joints, L.spine2 * 0x90 + 7) != 0x02)) ||
                !IsPlayerCharacter(model) || (DevMode() && (Bridge()->reserved[24] & 256)))
                break;
            memcpy((void*)(joints + L.pelvis * 0x90 + 0x20), L.savePelvis, 16);
            if (L.spine1 >= 0) {
                memcpy((void*)(joints + L.spine1 * 0x90 + 0x20), L.saveS1, 16);
                memcpy((void*)(joints + L.spine2 * 0x90 + 0x20), L.saveS2, 16);
            }
            break;
        }
    }
    oMotionUpdate(model, edx);
}

static void __fastcall HookBlend(uint32_t model, void* edx) {
    // Runs on every model blend in the game. Bail out before scanning when no leg layer is live.
    if (!AnyLayerBusy()) {
        oBlend(model, edx);
        return;
    }
    LegLayer* layer = nullptr;
    uint32_t shield[2] = {};  // slots 7 and 6 of a walking layer, marked as fading for this blend (see below)
    float shieldFade[2] = {}, shieldFactor[2] = {};
    uint32_t openBuf[2] = {}, openCount = 0;
    uint8_t opened[2][256];  // lower-body joints whose buffer weights were opened for this blend (see below)
    for (auto& L : g_layers) {
        if (L.ch != model || (!L.active && L.k <= 0.0f)) continue;
        if (!IsPlayerCharacter(model)) {  // its character is gone and this address is another model by now
            L = LegLayer{};
            break;
        }
        layer = &L;
        if (!L.active) break;  // fading out: the clip is already the game's again
        {
            const uint32_t n = At<uint32_t>(model, 0x314);
            if ((n > 256 ? 256 : n) != L.jointCount) {  // another skeleton at this address: start over
                L = LegLayer{};
                layer = nullptr;
                break;
            }
        }
        MaskUpperBody(L);  // the bind callback already did it; this keeps it if the game re-binds weights
        // Other slots' fades (a shot or a return to the aim restarts slot 0, which fades every joint) pulled the
        // walking legs toward that frozen copy for up to 10 frames: the blend scales an earlier slot's fade by
        // (1 - weight) of a later slot that is itself fading, so slot 7 is marked as fading with a zero factor (its own
        // fade then does nothing) while it holds the legs.
        for (int k = 0; k < 2 && !(DevMode() && (Bridge()->reserved[24] & 64)); ++k) {
            const uint32_t so = SlotObj(model, k ? kSlotFB : kLegSlot);
            if (k && !L.two) break;
            if (At<uint16_t>(so, 4) == (k ? L.clipFB : L.clip) && At<float>(so, 0x10) <= 0.0f) {
                shield[k] = so;
                shieldFade[k] = At<float>(so, 0x10);
                shieldFactor[k] = At<float>(so, 0x28);
                At<float>(so, 0x10) = 1e-4f;
                At<float>(so, 0x28) = 0.0f;
                // Lower-body joints without a track in the walk clips (Chris: the leg helpers 0x42-0x45 and
                // 0x6C-0x71) have weight 0 in this slot's buffer, so the mark above left them to the other slot's
                // fade while the rest of the leg walked on. Open them for this blend only (the fade pass alone reads
                // the buffer weights).
                const uint32_t buf = At<uint32_t>(so, 0xD8);
                memset(opened[k], 0, sizeof(opened[k]));
                if (buf && Readable((void*)buf, L.jointCount * 0x60) && !(DevMode() && (Bridge()->reserved[24] & 2048))) {
                    openBuf[k] = buf;
                    for (uint32_t j = 0; j < L.jointCount; ++j) {
                        float* w = (float*)(buf + j * 0x60);
                        if (L.lower[j] && w[3] == 0.0f && w[7] == 0.0f && w[11] == 0.0f) {
                            w[3] = w[7] = w[11] = 1.0f;
                            opened[k][j] = 1;
                            ++openCount;
                        }
                    }
                }
            }
        }
        if (L.remote && L.toT > 0 && Now() - L.lastPacket < 0.5) {
            // Interpolate between the last two packets (one packet of delay keeps it smooth).
            const double t = Now() - (L.toT - L.fromT > 0 ? L.toT - L.fromT : 0.066);
            float a = L.toT > L.fromT ? (float)((t - L.fromT) / (L.toT - L.fromT)) : 1.0f;
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            float* pos = (float*)(model + ch::Pos);
            pos[0] = L.from[0] + (L.to[0] - L.from[0]) * a;
            pos[1] = L.from[1] + (L.to[1] - L.from[1]) * a;
            pos[2] = L.from[2] + (L.to[2] - L.from[2]) * a;
            float* q = (float*)(model + ch::Quat);
            q[0] = 0; q[1] = sinf(L.yaw * 0.5f); q[2] = 0; q[3] = cosf(L.yaw * 0.5f);
        }
        break;
    }
    oBlend(model, edx);
    for (int k = 0; k < 2; ++k)
        if (shield[k]) {
            At<float>(shield[k], 0x10) = shieldFade[k];
            At<float>(shield[k], 0x28) = shieldFactor[k];
            if (openBuf[k] && openCount)
                for (uint32_t j = 0; j < layer->jointCount; ++j)
                    if (opened[k][j]) {
                        float* w = (float*)(openBuf[k] + j * 0x60);
                        w[3] = w[7] = w[11] = 0.0f;
                    }
        }
    if (layer) ShapeWaist(*layer);
#if RE3C_DEV
    if (Bridge() && Bridge()->trace_on) {
        const uint32_t mgr = Readable((void*)kPlayerMgrPtr, 4) ? At<uint32_t>(kPlayerMgrPtr, 0) : 0;
        if (mgr && Readable((void*)(mgr + 0x24), 4) && At<uint32_t>(mgr, 0x24) == model) TraceFrame(model);
    }
#endif
}

static bool IsPlayerCharacter(uint32_t model) {
    const uint32_t mgr = Readable((void*)kPlayerMgrPtr, 4) ? At<uint32_t>(kPlayerMgrPtr, 0) : 0;
    if (!mgr || !Readable((void*)(mgr + 0x24), 16)) return false;
    for (int i = 0; i < 4; ++i)
        if (At<uint32_t>(mgr, 0x24 + i * 4) == model) return true;
    return false;
}

// Where each walk clip is in its step: the normalised time at the middle of the right foot's planted half
// (forward, strafe left, back, strafe right; the same in all weapon sets), and the point whose feet are closest to
// the aim stance's, to start from. Measured from the clips' toe targets.
static const float kStartPhase[4] = {0.82f, 0.21f, 0.88f, 0.75f};
// Left heel strike when a clip has no footstep events (the standard sets' values; see LeftStrike).
static const float kStrike[4] = {0.02f, 0.02f, 0.52f, 0.50f};

// Walk clip per axis (forward, strafe left, back, strafe right).
static const uint8_t kAxisClip[4] = {0x01, 0x0C, 0x07, 0x0D};

static int ClipAxis(uint16_t clip) {
    switch (clip & 0xFF) {
        case 0x01: case 0x02: case 0x04: return 0;  // walk, its lean variant, run
        case 0x0C: return 1;
        case 0x07: return 2;
        case 0x0D: return 3;
    }
    return -1;
}

// Where the layer is in its step, relative to the left heel strike (-1 when no clip of the layer is bound).
static float LayerRel(const LegLayer& L) {
    if (L.two && L.clipFB != 0xFFFF) {
        const uint32_t s6 = SlotObj(L.ch, kSlotFB);
        if (At<uint16_t>(s6, 4) == L.clipFB && At<float>(s6, 0x1C) > 1.0f)
            return Frac(At<float>(s6, 0x18) / At<float>(s6, 0x1C) - L.strikeFB);
    }
    const uint32_t s7 = SlotObj(L.ch, kLegSlot);
    if (L.clip != 0xFFFF && ClipAxis(L.clip) >= 0 && At<uint16_t>(s7, 4) == L.clip && At<float>(s7, 0x1C) > 1.0f)
        return Frac(At<float>(s7, 0x18) / At<float>(s7, 0x1C) - L.strike);
    return -1.0f;
}

// Slot 6 back to the face the game asked for last (with a short fade); the blend ends.
static void ReleaseFace(LegLayer& L, float inter = 10.0f) {
    if (L.faceHeld && L.ch && Readable((void*)L.ch, 0x1000) && IsPlayerCharacter(L.ch))
        SetMotion(L.ch, kSlotFB, L.savedFace, inter, L.savedFace != 0xFFFF ? L.savedFaceFrame : 0.0f);
    L.faceHeld = false;
    L.two = false;
    L.clipFB = 0xFFFF;
}

// While a leg layer owns slot 7 the game's own requests for that slot (it uses it for small upper-body
// accents on action changes) are remembered for the restore instead of interrupting the legs.
static bool __fastcall HookSetMotion(uint32_t model, void* edx, uint32_t slot, uint32_t no, float inter, float start,
                                     float speed, uint32_t flags) {
    // Knife attack: its swing clips go into slots 0-2 (centre/up/down aim variants). The partner's copy gets the
    // same speed, so both games show the same swing.
    // Fire clips follow FireRate (balance.cpp scales the re-fire timer the same way); walking and running clips
    // follow MoveWalkSpeed / MoveRunSpeed, and the stop clip keeps the speed it stops from.
    if (!g_ownCall && slot <= 2 && Cfg().enabled && Readable((void*)(model + ch::ActionId), 4) &&
        IsPlayerCharacter(model)) {
        const uint32_t id = At<uint32_t>(model, ch::ActionId);
        const uint32_t act = At<uint32_t>(model, ch::CurAction);
        const uint32_t vt = Readable((void*)act, 0x18) ? At<uint32_t>(act, 0) : 0;
        if (id == aid::KnifeAttack) {
            if (Cfg().knifeSpeed > 0.2f) speed *= Cfg().knifeSpeed;
        } else if (vt == plact::Fire) {
            speed *= FireRateFor(model);
        } else if (id == 0 && slot == 0 && vt && At<uint8_t>(act, 0x14) == 0) {
            speed *= MoveSpeedFor(model, At<uint32_t>(model, ch::PrevActionId));
        } else {
            speed *= MoveSpeedFor(model, id);
        }
    }
    if (!g_ownCall && slot == kLegSlot) {
        for (auto& L : g_layers) {
            if (L.active && L.ch == model) {
                L.savedMotion = (uint16_t)no;
                L.savedFrame = start;
                return true;
            }
        }
    }
    if (!g_ownCall && slot == kSlotFB) {  // the face (and the aim squint) while slot 6 walks
        for (auto& L : g_layers) {
            if (L.faceHeld && L.ch == model) {
                L.savedFace = (uint16_t)no;
                L.savedFaceFrame = start;
                return true;
            }
        }
    }
    // Turning the aim swaps the lower-body stance clips (slots 3-5) for stepping-turn clips whose root moves a few
    // cm; under the walking legs that only shifted the hips. While walking the stance clips stay.
    // The knife attack does the same with its own lower-body set (0x3F-0x41, also 0x48-0x4A).
    if (!g_ownCall && slot >= 3 && slot <= 5) {
        for (auto& L : g_layers) {
            if (L.active && L.ch == model) {
                L.req[slot - 3] = (uint16_t)no;
                L.reqSpeed[slot - 3] = speed;
                break;
            }
        }
    }
    if (!g_ownCall && slot >= 3 && slot <= 5 && (no & 0xFF) >= 0x3F && (no & 0xFF) <= 0x4A && (no & 0xFF) != 0x3C &&
        !(DevMode() && (Bridge()->reserved[24] & 16))) {
        for (auto& L : g_layers) {
            if (L.active && L.ch == model) {
                static const uint8_t kStance[3] = {0x3D, 0x3E, 0x3C};
                no = (no & 0xFF00) | kStance[slot - 3];
                break;
            }
        }
    }
    // While the legs walk, the game restarts the stance in slots 3-5 at every aim-turn start/stop (0xC850E0 follows
    // ch+0x2DC0, which the mouse toggles between 0 and not 0 many times a second) and twice per shot; each restart
    // faded the legs from a frozen pose. The same clip again restarts without a fade. (Keeping the playing clip
    // instead left it out of step with slot 0 after a shot, and the game then stopped solving the knees: they stayed
    // bent as at the shot until the aim ended.)
    if (!g_ownCall && slot >= 3 && slot <= 5 && !(DevMode() && (Bridge()->reserved[24] & 128))) {
        for (auto& L : g_layers) {
            if (L.active && L.ch == model) {
                if (At<uint16_t>(SlotObj(model, slot), 4) == (uint16_t)no) inter = 0.0f;
                break;
            }
        }
    }
    const bool r = oSetMotion(model, edx, slot, no, inter, start, speed, flags);
    // The game's walk or run (clips 0x01-0x06 in slots 0-2) right after the leg layer: start it with the same foot
    // planted where the legs were, so the feet do not jump when the aim, a reload or a switch ends.
    if (r && !g_ownCall && slot <= 2 && (no & 0xFF) >= 1 && (no & 0xFF) <= 6) {
        for (auto& L : g_layers) {
            if (L.ch != model || L.remote) continue;
            float rel = L.active ? LayerRel(L) : -1.0f;
            if (!L.active && L.stopPhase >= 0.0f && Now() - L.stopTime < 0.3) rel = L.stopPhase;
            const uint32_t so = SlotObj(model, slot);
            if (rel >= 0.0f && At<float>(so, 0x1C) > 1.0f) {
                At<float>(so, 0x18) = Frac(rel + LeftStrike(model, slot, kStrike[0])) * At<float>(so, 0x1C);
                At<float>(so, 0x2C) = At<float>(so, 0x18) + At<float>(model, 0x10) * speed;
            }
            break;
        }
    }
    return r;
}

bool AimWalkState(uint32_t ch) {
    // Knife: ready begin/loop/end, the switches between gun and knife aim, the attack (they reuse gun classes).
    const uint32_t id = At<uint32_t>(ch, ch::ActionId);
    if (id >= aid::KnifeBegin && id <= aid::KnifeAttack) return true;
    const uint32_t act = Readable((void*)(ch + ch::CurAction), 4) ? At<uint32_t>(ch, ch::CurAction) : 0;
    if (!Readable((void*)act, 4)) return false;
    switch (At<uint32_t>(act, 0)) {
        case plact::ReadyBegin: case plact::ReadyLoop: case plact::Fire: case plact::Reload: case plact::ReadyEnd:
            return true;
    }
    return false;
}

static bool Switching(uint32_t ch) {
    const uint32_t act = Readable((void*)(ch + ch::CurAction), 4) ? At<uint32_t>(ch, ch::CurAction) : 0;
    if (!Readable((void*)act, 4)) return false;
    const uint32_t vt = At<uint32_t>(act, 0);
    return vt == plact::WeaponChange || vt == plact::WeaponChangeReady;
}

static bool Reloading(uint32_t ch) {
    const uint32_t act = Readable((void*)(ch + ch::CurAction), 4) ? At<uint32_t>(ch, ch::CurAction) : 0;
    return Readable((void*)act, 4) && At<uint32_t>(act, 0) == plact::Reload;
}

// Character kind from the game's function at 0x715B90 (the same as IsWesker in game.cpp). -1 without the expected code.
static int CharacterKind(uint32_t ch) {
    static int ok = 0;
    if (!ok) {
        static const BYTE sig[] = {0x8B, 0x4C, 0x24, 0x04, 0x8B, 0x01, 0x8B, 0x50, 0x10,
                                   0xFF, 0xD2, 0x8B, 0x40, 0x20, 0xC1, 0xE8, 0x08};
        ok = SafeEqual((void*)0x00715B90, sig, sizeof(sig)) ? 1 : -1;
    }
    return ok > 0 ? (int)((uint32_t(__cdecl*)(uint32_t))0x00715B90)(ch) : -1;
}

bool LegLayerState(uint32_t ch) { return AimWalkState(ch) || Switching(ch); }

static void StopLayer(LegLayer& L) {
    if (!L.active) return;
    L.active = false;
    const uint32_t ch = L.ch;
    L.stopPhase = -1.0f;
    L.stopTime = Now();
    if (ch && Readable((void*)ch, 0x1000) && IsPlayerCharacter(ch)) {  // never into a character that is gone
        L.stopPhase = LayerRel(L);
        LegLayerSounds(ch, 0);
        // A layer that ran only a frame or two (the game turned the action down after a preStart) ends without a
        // fade: a fade would pull the walking legs toward that frozen pose.
        const bool young = Now() - L.startTime < 0.1;
        ReleaseFace(L, young ? 0.0f : 10.0f);
        if (L.savedMotion != 0xFFFF)
            SetMotion(ch, kLegSlot, L.savedMotion, young ? 0.0f : 8.0f, L.savedFrame);
        else
            SetMotion(ch, kLegSlot, 0xFFFF, young ? 0.0f : 8.0f);
        // The stance slots: the clip the game last asked for (a stepping turn kept out while walking).
        for (uint32_t s = 3; s <= 5; ++s) {
            const uint16_t want = L.req[s - 3];
            if (want != 0xFFFF && At<uint16_t>(SlotObj(ch, s), 4) != want)
                SetMotion(ch, s, want, 10.0f, 0.0f, L.reqSpeed[s - 3]);
        }
    }
    for (int i = 0; i < 3; ++i) L.req[i] = 0xFFFF;
    L.runGait = false;
    L.clip = 0xFFFF;
    L.two = false;
    L.faceHeld = false;
    L.clipFB = 0xFFFF;
    L.twistTarget = 0;  // the twist and the waist shaping fade out with the clip (ShapeWaist)
    L.lowT = 0;
    L.havePos = false;
    L.rootW = L.rootTarget = 0;
}

static bool PlayLayer(LegLayer& L, uint16_t clip, float rate) {
    const uint32_t ch = L.ch;
    BuildMask(L);
    if (!L.jointCount) return false;
    if (!L.active) {
        const uint32_t s = SlotObj(ch, kLegSlot);
        L.savedMotion = At<uint16_t>(s, 4);
        L.savedFrame = At<float>(s, 0x18);
        L.active = true;
        L.startTime = Now();
        L.clip = 0xFFFF;
    }
    float relFromBlend = -1.0f;
    if (L.two) {  // from the directional blend to one clip (the run): the step goes on from where the blend was
        relFromBlend = LayerRel(L);
        ReleaseFace(L);
        L.clip = 0xFFFF;
    }
    LegLayerSounds(ch, 0x80);
    const uint32_t s = SlotObj(ch, kLegSlot);
    if (L.clip != clip || At<uint16_t>(s, 4) != clip) {
        // A change of direction fades the legs over a few frames with the engine's slot fade and starts the new
        // clip with the same foot planted at the same point of its step; starting to walk fades in from the stance
        // at the point of the step closest to it.
        const bool devOff = DevMode() && (Bridge()->reserved[24] & 2);
        const int newAxis = devOff ? -1 : ClipAxis(clip);
        // Where in the step to start, relative to the left heel strike: the clip playing now (a change of direction
        // or of walk/run keeps the same foot at the same point of its step), or the game's walk/run it comes from.
        float rel = -1.0f;
        const WalkSample* w = newAxis >= 0 ? WalkFor(ch, false) : nullptr;
        if (w && Now() - w->time < 0.3) rel = w->phase;
        const bool gaitChange = L.clip != 0xFFFF && ClipAxis(L.clip) == 0 && newAxis == 0 &&
                                ((L.clip & 0xFF) == 0x04) != ((clip & 0xFF) == 0x04);
        const float layerRel = relFromBlend >= 0.0f ? relFromBlend : LayerRel(L);
        if (layerRel >= 0.0f) rel = layerRel;
        const float inter = gaitChange ? 12.0f : 8.0f;
        if (!SetMotion(ch, kLegSlot, clip, inter, 0.0f, rate)) {
            const uint16_t fwd = (uint16_t)((clip & 0xFF00) | 0x01);
            if (clip == fwd || !SetMotion(ch, kLegSlot, fwd, inter, 0.0f, rate)) {
                StopLayer(L);
                return false;
            }
            clip = fwd;
        }
        L.clip = clip;
        const int axis = ClipAxis(clip);
        L.strike = LeftStrike(ch, kLegSlot, axis >= 0 ? kStrike[axis] : 0.0f);
        float phase = -1.0f;
        if (newAxis >= 0) phase = rel >= 0.0f ? Frac(rel + L.strike) : kStartPhase[newAxis];
        const float frameMax = At<float>(s, 0x1C);
        if (phase >= 0.0f && frameMax > 1.0f) {
            At<float>(s, 0x18) = phase * frameMax;
            At<float>(s, 0x2C) = At<float>(s, 0x18) + At<float>(ch, 0x10) * rate;  // next frame
        }
    }
    At<float>(s, 0x24) = rate;  // playback speed: the clip's root motion follows it
    L.rate = rate;
    return true;
}

// The directional blend: fb (forward 0x01 / back 0x07) in slot 6 at weight 1, lr (strafe left 0x0C / right 0x0D) in
// slot 7 at weight w; base = playback rate at full stick. Both clips run at the same rate of step cycles, so their
// feet stay together (slot 7's speed is trimmed toward slot 6's phase), and diagonals step a little faster to make up
// for the shorter blended stride.
static bool PlayBlend(LegLayer& L, uint16_t fb, uint16_t lr, float w, float base) {
    const uint32_t ch = L.ch;
    BuildMask(L);
    if (!L.jointCount) return false;
    const uint32_t s6 = SlotObj(ch, kSlotFB), s7 = SlotObj(ch, kLegSlot);
    float rel = LayerRel(L);
    if (rel < 0.0f) {
        const WalkSample* ws = WalkFor(ch, false);
        if (ws && Now() - ws->time < 0.3) rel = ws->phase;
    }
    if (!L.active) {
        L.savedMotion = At<uint16_t>(s7, 4);
        L.savedFrame = At<float>(s7, 0x18);
        L.active = true;
        L.startTime = Now();
        L.clip = 0xFFFF;
    }
    if (!L.faceHeld) {
        L.savedFace = At<uint16_t>(s6, 4);
        L.savedFaceFrame = At<float>(s6, 0x18);
        L.faceHeld = true;
        L.clipFB = 0xFFFF;
    }
    L.two = true;
    const bool fbBack = (fb & 0xFF) == 0x07, lrRight = (lr & 0xFF) == 0x0D;
    if (L.clipFB != fb || At<uint16_t>(s6, 4) != fb) {
        if (!SetMotion(ch, kSlotFB, fb, 8.0f, 0.0f, 1.0f)) {
            StopLayer(L);
            return false;
        }
        L.clipFB = fb;
        L.strikeFB = LeftStrike(ch, kSlotFB, kStrike[fbBack ? 2 : 0]);
        const float len = At<float>(s6, 0x1C);
        if (len > 1.0f) {
            const float ph = rel >= 0.0f ? Frac(rel + L.strikeFB) : kStartPhase[fbBack ? 2 : 0];
            At<float>(s6, 0x18) = ph * len;
            At<float>(s6, 0x2C) = At<float>(s6, 0x18) + At<float>(ch, 0x10);
            if (rel < 0.0f) rel = Frac(ph - L.strikeFB);
        }
    }
    const float lenA = At<float>(s6, 0x1C);
    const float relA = lenA > 1.0f ? Frac(At<float>(s6, 0x18) / lenA - L.strikeFB) : 0.0f;
    if (L.clip != lr || At<uint16_t>(s7, 4) != lr) {
        if (!SetMotion(ch, kLegSlot, lr, 8.0f, 0.0f, 1.0f)) {
            StopLayer(L);
            return false;
        }
        L.clip = lr;
        L.strike = LeftStrike(ch, kLegSlot, kStrike[lrRight ? 3 : 1]);
        const float len = At<float>(s7, 0x1C);
        if (len > 1.0f) {
            At<float>(s7, 0x18) = Frac(relA + L.strike) * len;
            At<float>(s7, 0x2C) = At<float>(s7, 0x18) + At<float>(ch, 0x10);
        }
    }
    const float lenB = At<float>(s7, 0x1C);
    if (lenA > 1.0f && lenB > 1.0f) {
        const float rA = base * (fbBack ? 0.88f : 1.0f), rB = base;  // the back clip covers 14% more ground
        float cps = (1.0f - w) * rA / lenA + w * rB / lenB;          // step cycles per motion frame
        cps *= sqrtf(1.0f / sqrtf((1.0f - w) * (1.0f - w) + w * w));
        const float relB = Frac(At<float>(s7, 0x18) / lenB - L.strike);
        float err = relB - relA;
        err -= floorf(err + 0.5f);
        float trim = err * 6.0f;
        trim = trim > 0.3f ? 0.3f : (trim < -0.3f ? -0.3f : trim);
        At<float>(s6, 0x24) = cps * lenA;
        At<float>(s7, 0x24) = cps * lenB * (1.0f - trim);
    }
    At<float>(s6, 0x14) = 1.0f;
    At<float>(s7, 0x14) = w;  // the engine lerps the joints and the root motion by it
    L.w = w;
    L.rate = base;
    LegLayerSounds(ch, w >= 0.5f ? 0x80 : 0x40);
    return true;
}

// ------------------------------------------------------------------------------------------------ local player

// pace = clip playback rate at full stick (chosen by the caller). run: a reload or switch on the run plays the run
// clip; bob: the hips follow the walk/run clip's height; smooth: the direction is smoothed (keyboard's 45-degree steps).
bool AimWalkUpdate(uint32_t ch, float stickX, float stickY, bool walkAllowed, float pace, float dtSec, bool run,
                   bool bob, bool smooth, bool preStart) {
    const float mag = sqrtf(stickX * stickX + stickY * stickY);
    // Layers of local characters that left the game (chapter restart) end without touching their memory. Each
    // local player (split screen) has its own layer.
    for (auto& l : g_layers)
        if (l.active && !l.remote && l.ch != ch && !IsPlayerCharacter(l.ch)) l = LegLayer{};
    LegLayer* L = FindLayer(ch, false);
    const float dt = dtSec > 0 ? (dtSec < 0.1f ? dtSec : 0.1f) : 0.033f;
    {
        const uint32_t s0 = SlotObj(ch, 0);
        const uint16_t m0 = At<uint16_t>(s0, 4);
        if ((m0 & 0xFF) >= 1 && (m0 & 0xFF) <= 6 && m0 != 0xFFFF && At<float>(s0, 0x1C) > 1.0f) {
            WalkSample* w = WalkFor(ch, true);
            w->phase = Frac(At<float>(s0, 0x18) / At<float>(s0, 0x1C) - LeftStrike(ch, 0, kStrike[0]));
            w->time = Now();
            w->bank = (uint8_t)(m0 >> 8);
        }
    }
    // preStart: the aim, the knife or a reload was asked for this frame while walking; the game starts that action
    // after this pad update and sets its lower-body stance in slots 3-5 straight away, which the legs would show for
    // one frame before the layer took over.
    if (!walkAllowed || !(LegLayerState(ch) || preStart)) {
        if (L) StopLayer(*L);
        return false;
    }
    // Start at 0.3 of the stick; keep walking until it stays below 0.22 for 0.1 s (no flicker at the edge).
    const bool walking = L && L->active && !L->remote;
    bool low = false;
    if (mag < (walking ? 0.22f : 0.3f)) {
        if (!walking) {
            if (L) StopLayer(*L);
            return false;
        }
        L->lowT += dt;
        if (L->lowT >= 0.1f) {
            StopLayer(*L);
            return false;
        }
        low = true;
    } else if (L) {
        L->lowT = 0;
    }
    if (!L) L = FindLayer(ch, true);
    if (!L) return false;
    L->remote = false;

    // Direction in the character's frame: 0 = forward, +pi/2 = left (the character's +X is its left). Keyboard
    // directions come in 45-degree steps: a critically damped angle (about 0.15 s) turns them into a smooth change,
    // except for a reversal, which is taken at once.
    const float raw = low ? L->lastTheta : atan2f(-stickX, stickY);
    L->lastTheta = raw;
    if (!smooth || !L->active || fabsf(Wrap(raw - L->thetaF)) > kPi * 0.75f) {
        L->thetaF = raw;
        L->thetaV = 0.0f;
    } else {
        const float w = 25.0f, x0 = Wrap(L->thetaF - raw), c = L->thetaV + w * x0, e = expf(-w * dt);
        L->thetaF = Wrap(raw + (x0 + c * dt) * e);
        L->thetaV = (L->thetaV - w * c * dt) * e;
    }
    const float theta = L->thetaF;
    // Nearest axis, with hysteresis so the clip doesn't flap at 45 degrees.
    static const float axisAngle[4] = {0.0f, kPi / 2, kPi, -kPi / 2};
    if (!L->active || fabsf(Wrap(theta - axisAngle[L->axis])) > kPi * 0.3f) {
        int best = 0;
        for (int i = 1; i < 4; ++i)
            if (fabsf(Wrap(theta - axisAngle[i])) < fabsf(Wrap(theta - axisAngle[best]))) best = i;
        L->axis = best;
    }
    const float delta = Wrap(theta - axisAngle[L->axis]);  // within +-54 degrees

    // Clip of the current weapon's motion set (bank = high byte of the stance in slot 0):
    // 0x01 walk forward (the game's straight walk; 0x02/0x03 are its lean-left/right variants), 0x0C strafe left,
    // 0x07 walk back, 0x0D strafe right, 0x04 run (straight). A weapon switch plays from bank 6 (the new weapon, until
    // it is swapped into bank 5 halfway) or bank 1 (grenades, cover): the walk of the weapon in hand, which the engine
    // rebinds to the new one at the swap.
    const uint16_t stance = At<uint16_t>(SlotObj(ch, 0), 4);
    uint32_t bank = stance == 0xFFFF || Switching(ch) ? 0x05 : (stance >> 8);
    // A stance whose motion set has no locomotion clips (the game switches to a knife-only set for some characters) is
    // substituted once, not retried every frame: retrying is what made the run restart (the stutter).
    if (L->bankFixStance == stance && L->bankFix) bank = L->bankFix;
    // Characters of kind 4 and up: during a reload the walk bank stays the one used before the reload.
    if (CharacterKind(ch) >= 4) {
        if (Reloading(ch)) {
            const WalkSample* ws = WalkFor(ch, false);
            const uint32_t keep = L->walkBank ? L->walkBank : (ws ? ws->bank : 0);
            if (keep) bank = keep;
        } else if (stance != 0xFFFF && !Switching(ch)) {
            L->walkBank = (uint8_t)bank;
        }
    }
    // With the knife out the game swaps the motion set to the knife's own bank, which has no strafe/back clips for some
    // characters (Barry in Mercenaries Reunion), so the blend failed and the run restarted every frame.  The walk bank
    // that was in use before the knife is kept instead.
    {
        const uint32_t aidN = At<uint32_t>(ch, ch::ActionId);
        if (aidN >= 0x22 && aidN <= 0x24 && L->walkBank) bank = L->walkBank;
    }
    const bool runGait = run && L->axis == 0 && fabsf(delta) < (L->runGait ? 0.5f : 0.3f);
    L->runGait = runGait;
    uint16_t want = (uint16_t)((bank << 8) | (runGait ? 0x04 : kAxisClip[L->axis]));
    if (DevMode() && Bridge()->reserved[22]) want = (uint16_t)Bridge()->reserved[22];  // dev: force a clip

    const uint32_t devOff = DevMode() ? Bridge()->reserved[24] : 0;  // dev: switch parts off for comparisons
    const float m = mag > 1.0f ? 1.0f : mag;
    float rate = runGait ? pace : pace * (0.5f + 0.5f * m);
    const bool blend = !runGait && !(devOff & 32) && !(DevMode() && Bridge()->reserved[22]) && bank != L->noBlendBank;
    if (!blend && !runGait && L->axis == 2) rate *= 0.88f;  // the back clip covers 14% more ground per frame
    // Hip height from the clip during a reload/switch on the move, eased in and out with the clip fade.
    L->rootTarget = bob ? 1.0f : 0.0f;
    {
        const float st = dt / 0.13f, d = L->rootTarget - L->rootW;
        L->rootW += d > st ? st : (d < -st ? -st : d);
    }
    if (devOff & 8) rate *= cosf(delta);
    L->compatTwist = delta;
    if (blend) {
        // Forward/back flips where the strafe has nearly all the weight, left/right where it has almost none.
        const float c = cosf(theta), s = sinf(theta);
        if (!L->two) {
            L->back = c < 0.0f;
            L->right = s < 0.0f;
        } else {
            if (L->back ? c > 0.10f : c < -0.10f) L->back = !L->back;
            if (L->right ? s > 0.10f : s < -0.10f) L->right = !L->right;
        }
        const float w = fabsf(s) / (fabsf(s) + fabsf(c) + 1e-6f);
        const uint16_t fb = (uint16_t)((bank << 8) | (L->back ? 0x07 : 0x01));
        const uint16_t lr = (uint16_t)((bank << 8) | (L->right ? 0x0D : 0x0C));
        if (!PlayBlend(*L, fb, lr, w, rate)) {
            // PROBE: which weapon set / clip set refused the blend (the Barry knife stutter and the Excella melee one).
            {
                static double lastLog = 0;
                const double now = Now();
                if (now - lastLog > 1.0) {
                    lastLog = now;
                    Log("blendfail: aid=%02X stance=%04X bank=%02X fb=%04X lr=%04X noBlend=%02X good=%02X",
                        At<uint32_t>(ch, ch::ActionId), stance, (uint32_t)bank, (uint32_t)fb, (uint32_t)lr,
                        (uint32_t)L->noBlendBank, (uint32_t)L->goodBank);
                }
            }
            // This character/stage has no strafe or back clips in this motion set (Barry's knife set is one example).
            // Retry with the last set that did have them, instead of dropping the layer for this frame - dropping it is
            // what restarted the run every frame and produced the stutter.
            const uint32_t fallback = L->goodBank ? L->goodBank : (uint32_t)L->walkBank;
            const bool ok = fallback && fallback != bank &&
                            PlayBlend(*L, (uint16_t)((fallback << 8) | (fb & 0xFF)),
                                      (uint16_t)((fallback << 8) | (lr & 0xFF)), w, rate);
            if (ok) { L->bankFixStance = stance; L->bankFix = fallback; }
            L->noBlendBank = bank;  // do not try this set again for this stance
            if (!ok) return false;
        } else {
            L->goodBank = bank;
        }
    } else if (!PlayLayer(*L, want, rate)) {
        // The run clip (bank | 0x04) does not exist in this motion set either - with the knife out the game switches to
        // a set that only holds the knife stances, so running with the knife dropped the layer every frame (the stutter).
        {
            static double lastLog2 = 0;
            const double now = Now();
            if (now - lastLog2 > 1.0) {
                lastLog2 = now;
                Log("layerfail: aid=%02X stance=%04X bank=%02X want=%04X good=%02X",
                    At<uint32_t>(ch, ch::ActionId), stance, (uint32_t)bank, (uint32_t)want, (uint32_t)L->goodBank);
            }
        }
        // The motion set derived from the stance does not necessarily hold this character's locomotion clips (the game
        // switches the whole set while the knife is out).  The set the game itself uses when the character walks is the
        // reliable one, so it is tried first, then the last good set, then the walk bank.
        const WalkSample* wsFb = WalkFor(ch, false);
        const uint32_t cand[3] = { (uint32_t)(wsFb ? wsFb->bank : 0), L->goodBank, (uint32_t)L->walkBank };
        bool placed = false;
        for (int ci = 0; ci < 3 && !placed; ++ci) {
            const uint32_t b2 = cand[ci];
            if (!b2 || b2 == bank || b2 == cand[(ci + 1) % 3]) continue;
            if (PlayLayer(*L, (uint16_t)((b2 << 8) | (want & 0xFF)), rate)) {
                L->goodBank = b2;
                L->noBlendBank = bank;
                L->bankFixStance = stance;
                L->bankFix = b2;
                placed = true;
            }
        }
        if (!placed) return false;
    } else {
        L->goodBank = bank;
    }

    // One clip (the run): the legs turn toward the real direction with a critically damped spring (exact step, so
    // any frame rate is stable). The blend already walks the stick's direction; a twist left from the run eases out.
    L->twistTarget = blend ? 0.0f : delta;
    if (devOff & 4) {
        const float maxStep = 6.0f * dt;
        const float d = L->twistTarget - L->twist;
        L->twist += d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d);
        L->twistVel = 0;
    } else {
        const float w = 18.0f;
        const float x0 = L->twist - L->twistTarget, v0 = L->twistVel;
        const float c = v0 + w * x0, e = expf(-w * dt);
        L->twist = L->twistTarget + (x0 + c * dt) * e;
        L->twistVel = (v0 - w * c * dt) * e;
    }

    // The clip walks along its axis at the legs' own pace; the step is turned to where the stick points, so the
    // feet keep up with the ground on diagonals. Pos and the yaw below are both relative to the platform the
    // character stands on (if any); stepping onto or off a platform changes that frame, so the last position is
    // dropped then. Steps far off the stick direction (pushed along a wall, hit) are left as they are.
    float* pos = (float*)(ch + ch::Pos);
    const float* q = (const float*)(ch + ch::Quat);
    if (At<uint32_t>(ch, ch::Parent) != L->lastParent || At<int32_t>(ch, ch::ParentNo) != L->lastParentNo) {
        L->lastParent = At<uint32_t>(ch, ch::Parent);
        L->lastParentNo = At<int32_t>(ch, ch::ParentNo);
        L->havePos = false;
    }
    const float yaw = 2.0f * atan2f(q[1], q[3]);
    if (L->havePos && (devOff & 8) && fabsf(delta) > 0.05f) {
        const float ax = yaw + axisAngle[L->axis];
        const float along = (pos[0] - L->lastPos[0]) * sinf(ax) + (pos[2] - L->lastPos[2]) * cosf(ax);
        if (along > 0.0f && along < 60.0f) {
            const float side = along * tanf(delta);
            pos[0] += sinf(ax + kPi / 2) * side;
            pos[2] += cosf(ax + kPi / 2) * side;
        }
    } else if (L->havePos) {
        const float dx = pos[0] - L->lastPos[0], dz = pos[2] - L->lastPos[2];
        const float len = sqrtf(dx * dx + dz * dz);
        if (len > 0.05f && len < 60.0f) {
            const float turn = Wrap(yaw + theta - atan2f(dx, dz));  // toward +turn = counter-clockwise from above
            if (fabsf(turn) < kPi * 0.4f) {
                const float c = cosf(turn), s = sinf(turn);
                pos[0] = L->lastPos[0] + dx * c + dz * s;
                pos[2] = L->lastPos[2] - dx * s + dz * c;
            }
        }
    }
    memcpy(L->lastPos, pos, 12);
    L->havePos = true;
    return true;
}

bool AimWalkLocalState(uint32_t ch, uint16_t* clip, float* rate, float* twist, uint8_t* blend, bool* bob) {
    LegLayer* L = FindLayer(ch, false);
    if (!L || !L->active || L->remote) return false;
    *clip = L->clip;
    *rate = L->rate;
    *twist = L->twist;
    *blend = 0;
    *bob = L->rootTarget > 0.5f;
    if (L->two) {
        // v1.8 partners blend the same two clips; older ones play the nearest axis's clip (with its hysteresis)
        // turned toward the walking direction, at the rate they expect for it, as before.
        *blend = (uint8_t)(0x80 | (L->back ? 0x40 : 0) | (L->right ? 0x20 : 0) | (uint8_t)(L->w * 31.0f + 0.5f));
        *clip = (uint16_t)((L->clipFB & 0xFF00) | kAxisClip[L->axis]);
        *twist = L->compatTwist;
        if (L->axis == 2) *rate = L->rate * 0.88f;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ partner copy
void AimWalkRemote(uint32_t ch, bool walking, uint16_t clip, float rate, float twist, const float* pos, float yaw,
                   uint8_t blend, bool bob) {
    if (blend & 0x80) {
        twist = 0.0f;  // the packet's twist is for v1.7 partners; the blend walks the direction itself
        if ((clip & 0xFF) == 0x07) rate /= 0.88f;
    }
    LegLayer* L = FindLayer(ch, walking);
    if (!L) return;
    if (!walking) {
        if (L->remote) StopLayer(*L);
        return;
    }
    if (!LegLayerState(ch)) {  // the partner's copy is not in an aim action (yet): nothing to layer onto
        StopLayer(*L);
        return;
    }
    const double now = Now();
    if (!L->active || !L->remote || now - L->lastPacket > 0.5) {
        memcpy(L->from, pos, 12);
        L->fromT = now - 0.066;
    } else {
        memcpy(L->from, L->to, 12);
        L->fromT = L->toT;
    }
    memcpy(L->to, pos, 12);
    L->toT = now;
    L->yaw = yaw;
    L->lastPacket = now;
    if (!L->remote || !L->active) {
        L->twist = twist;
        L->twistVel = 0.0f;
    }
    L->remote = true;
    L->twistTarget = twist;  // packets come every 0.066 s: the twist follows them smoothly (AimWalkRemoteTick)
    // Hip height as on the partner's own screen (a reload or switch on the move; v1.7 never sends it).
    L->rootTarget = bob ? 1.0f : 0.0f;
    if (blend & 0x80) {
        L->back = (blend & 0x40) != 0;
        L->right = (blend & 0x20) != 0;
        L->twistTarget = 0.0f;
        const uint16_t bank = (uint16_t)(clip & 0xFF00);
        PlayBlend(*L, (uint16_t)(bank | (L->back ? 0x07 : 0x01)), (uint16_t)(bank | (L->right ? 0x0D : 0x0C)),
                  (float)(blend & 31) / 31.0f, rate);
    } else {
        PlayLayer(*L, clip, rate);
    }
}

void AimWalkRemoteTick() {
    const double now = Now();
    for (auto& L : g_layers) {
        if (!L.active || !L.remote) continue;
        if (!IsPlayerCharacter(L.ch)) {
            L = LegLayer{};  // the copy is gone (game over, chapter change)
            continue;
        }
        if (now - L.lastPacket > 0.5 || !Readable((void*)L.ch, 0x1000) || !LegLayerState(L.ch)) {
            StopLayer(L);
            continue;
        }
        float dt = L.lastTick > 0 ? (float)(now - L.lastTick) : 0.0f;
        if (dt > 0.1f) dt = 0.1f;
        L.lastTick = now;
        const float w = 18.0f, x0 = L.twist - L.twistTarget, c = L.twistVel + w * x0, e = expf(-w * dt);
        L.twist = L.twistTarget + (x0 + c * dt) * e;
        L.twistVel = (L.twistVel - w * c * dt) * e;
        const float st = dt / 0.13f, d = L.rootTarget - L.rootW;
        L.rootW += d > st ? st : (d < -st ? -st : d);
    }
}

void InitAimWalkHooks() {
    static const BYTE sig[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0xD4, 0x01, 0x00, 0x00};
    if (!SafeEqual((void*)kFnModelBlend, sig, sizeof(sig))) {
        Log("hook uModel blend: signature mismatch");
        return;
    }
    if (MH_CreateHook((void*)kFnModelBlend, (void*)HookBlend, (void**)&oBlend) == MH_OK &&
        MH_QueueEnableHook((void*)kFnModelBlend) == MH_OK)
        Log("hook uModel blend at %08x", kFnModelBlend);
    static const BYTE sigBind[] = {0x83, 0x7C, 0x24, 0x04, 0x07, 0x56, 0x8B, 0xF1, 0x0F, 0x85};
    if (HookSite((void*)kFnPlayerBindSlot, sigBind, sizeof(sigBind)) &&
        MH_CreateHook((void*)kFnPlayerBindSlot, (void*)HookBindSlot, (void**)&oBindSlot) == MH_OK &&
        MH_QueueEnableHook((void*)kFnPlayerBindSlot) == MH_OK)
        Log("hook player bind-slot callback at %08x", kFnPlayerBindSlot);
    else
        Log("hook player bind-slot callback failed");
    static const BYTE sigSet[] = {0x8B, 0x54, 0x24, 0x04, 0x53, 0x8B, 0xD9, 0x3B, 0x93, 0x74, 0x03, 0x00, 0x00};
    if (SafeEqual((void*)kFnSetMotion, sigSet, sizeof(sigSet)) &&
        MH_CreateHook((void*)kFnSetMotion, (void*)HookSetMotion, (void**)&oSetMotion) == MH_OK &&
        MH_QueueEnableHook((void*)kFnSetMotion) == MH_OK)
        Log("hook uModel::setMotion at %08x", kFnSetMotion);
    else
        Log("hook uModel::setMotion failed");
    // The motion update's player thunk (jmp 0x59D0B0), see HookMotionUpdate.
    static const BYTE sigMotion[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x10, 0x53, 0x55, 0x56,
                                     0x57, 0x8B, 0xF9, 0x33, 0xED, 0x39, 0xAF, 0x80, 0x0A, 0x00, 0x00};
    if (Readable((void*)kFnMotionUpdateThunk, 5) && At<uint8_t>(kFnMotionUpdateThunk, 0) == 0xE9 &&
        kFnMotionUpdateThunk + 5 + At<int32_t>(kFnMotionUpdateThunk, 1) == 0x0059D0B0 &&
        SafeEqual((void*)0x0059D0B0, sigMotion, sizeof(sigMotion)) &&
        MH_CreateHook((void*)kFnMotionUpdateThunk, (void*)HookMotionUpdate, (void**)&oMotionUpdate) == MH_OK &&
        MH_QueueEnableHook((void*)kFnMotionUpdateThunk) == MH_OK) {
        g_restoreHook = true;
        Log("hook player motion update at %08x", kFnMotionUpdateThunk);
    } else {
        Log("hook player motion update failed");
    }
    // Footsteps of the leg layer: the player's mask init and the per-slot check (see LegLayerSounds).
    static const BYTE sigMaskInit[] = {0xC6, 0x86, 0x8C, 0x14, 0x00, 0x00, 0x09};
    static const BYTE sigMaskTest[] = {0x8A, 0x46, 0x2C, 0xBA, 0x01, 0x00, 0x00, 0x00, 0xD3, 0xE2, 0x84, 0xD0};
    g_stepSounds = SafeEqual((void*)0x00B6FE0F, sigMaskInit, sizeof(sigMaskInit)) &&
                   SafeEqual((void*)0x00B8EBAB, sigMaskTest, sizeof(sigMaskTest));
    Log(g_stepSounds ? "aim-walk footsteps on" : "aim-walk footsteps: signature mismatch, off");
}
