// Free camera during melee attacks.
//
// RE5 films physical attacks (A on a staggered enemy, finishers, kicks, stomps, the co-op help attack, the context
// knife attacks) with a camera baked into the attack motion: uPawn::UpdateActionCamera (0xB56310) latches it when
// a motion with camera tracks starts and registers it on the camera controller, which then skips the normal QFPS
// camera (and the mod's orbit with it) until the motion ends, so the right stick does nothing. The game has a
// one-shot "no action camera for this motion" flag (ch+0x10C1); the mod sets it for its own player's melee motions,
// so the normal camera keeps running and the right stick keeps turning it. Enemy grabs work the same way: the
// grabbing enemy puts the player into an event act (action 0x80000000, the act at ch+0x10F8) and plays held motions
// from its own motion list on him, and those carry the camera tracks, so GrabCamera sets the same flag for them.
// Event acts of doors, ladders, co-op objects, boats and scripted scenes (their source is not an enemy), the kills,
// the story bosses' QTE scenes, knock-downs and the dying state keep the game's camera.
#include "common.h"
#include "re5.h"
#include "config.h"
#include "camera.h"
#include "damage.h"
#include "melee.h"
#include "kbinput.h"
#include "MinHook.h"

using namespace re5;

namespace {

constexpr uint32_t kFnUpdateActionCamera = 0x00B56310;  // thiscall(pawn), every frame from the pawn update
constexpr uint32_t PawnMotionState = 0x388;            // u16, slot-0 motion state: & 2 = started this frame
constexpr uint32_t PawnSkipActionCamera = 0x10C1;      // u8, one-shot: the next new motion gets no action camera

typedef void(__fastcall* PFN_Pawn)(uint32_t pawn, void* edx);
PFN_Pawn oUpdateActionCamera = nullptr;
// The game's action camera is what locks the view during a quick knife slash; while the mod owns the camera we set
// this one-shot flag so the new motion gets no action camera.
void __fastcall HookUpdateActionCamera(uint32_t pawn, void* edx) {
    const Config& c = Cfg();
    // Cheap gates first: most frames are not starting a motion on a local camera character.
    if (c.enabled && (c.freeCamera || c.meleeOnly) && pawn && (At<uint16_t>(pawn, PawnMotionState) & 2) &&
        IsCamCharacter(pawn) && At<uint32_t>(pawn, ch::NetControl) == 0 && FreeCameraAction(pawn)) {
        // The keyboard/mouse gate keeps the action camera for players whose KB layer is not active that frame,
        // which is exactly when the slash felt locked.  For a knife action the mod wants the camera in any case.
        const uint32_t id = At<uint32_t>(pawn, ch::ActionId);
        if (!KbMouse(pawn) || KbLayerFor(pawn) || (id >= aid::KnifeBegin && id <= aid::KnifeAttack))
            At<uint8_t>(pawn, PawnSkipActionCamera) = 1;
    }
    oUpdateActionCamera(pawn, edx);
}

}  // namespace

// Player action ids (ch+0x10E0, table 0x113AF80): 0x30-0x4D attack_* (hook, upper, straight, kick, reverse knuckle,
// finishers, stomp, neck breaker, context knife attacks, combos, lariat, knee bazooka), 0x6F co-op help attack,
// 0xAC/0xAD attacks on em10. Not the taunt (0x2F), the knife stance (0x22-0x27), dodges, grabs or damage.
bool IsMeleeAction(uint32_t id) { return aidc::Melee(id); }

namespace {

// cEventAct* while the action is 0x80000000 (+8 the player, +0xC the source), in the order the game's own test 0xB56600
// reads them: enemy grabs (em10 back hold) sit in the first.
constexpr uint32_t kPawnEventActs[] = {0x10F8, 0x1100};
// Class descriptors (DTI: +0x10 the parent), from GetDTI (vtable slot 0x10, "mov eax, dti; ret").
constexpr uint32_t kDtiEnemy = 0x011F4F4C;
constexpr uint32_t kDtiEnemyPart[] = {0x011EBE10, 0x011EC7FC};  // uEm30::Tentacle, uEm32::Tongue (grab for their enemy)
// Bosses, giants and story characters: their grabs and holds are filmed scenes (and some of them kills). Wesker
// em80/em81/ema2, em82, em83, em84 and its core, em87, em8c, em8e, uEmNpc; Uroboros em40 (em41) with its parts, arm and
// core, em43, em44, the giant em45 with em4501/em4502, em46 and its tentacles emc0-emc6.
constexpr uint32_t kDtiBoss[] = {0x011F0498, 0x011F1260, 0x011F19A4, 0x011F1F14, 0x011F29A0, 0x011F2AC0,
                                 0x011F2B2C, 0x011F2EB0, 0x011F4C10, 0x011ECE74, 0x011ED360, 0x011ED72C,
                                 0x011ED750, 0x011EDF50, 0x011EE3B4, 0x011EE880, 0x011EEB74, 0x011EEBE0,
                                 0x011EED08, 0x011F3018, 0x011F3234, 0x011F32A0, 0x011F3498, 0x011F35DC,
                                 0x011F3648, 0x011F3864};
// Kill grabs keep the game's death camera (event act vtables): em20 ExecKill, em22 Cutting / CutsDown, em30
// NeckBiteFinTH, em32 MountFinish, em34 AtkDeath, em43 CatchKill, emc0 Finish, em66 crocodile SwimBiteHit / RaftBiteHit.
constexpr uint32_t kKillActs[] = {0x00F74210, 0x00F756E0, 0x00F7570C, 0x00F76F44, 0x00F78B70,
                                  0x00F7A6CC, 0x00F80EF4, 0x00F92DD4, 0x00F88030, 0x00F8805C};
// em10 PlagaEat (a Majini's Plaga head bites): a kill only for a player who is already dying (0xC26BC0 sets
// ch+0x2D98 bit 1 then); otherwise he gets up again.
constexpr uint32_t kPlagaEat = 0x0103BAA4;

uint32_t DtiOf(uint32_t obj) {
    if (!obj || !Readable((void*)obj, 4)) return 0;
    const uint32_t vt = At<uint32_t>(obj, 0);
    if (!Readable((void*)(vt + 0x10), 4)) return 0;
    const uint32_t fn = At<uint32_t>(vt, 0x10);
    if (!Readable((void*)fn, 6) || At<uint8_t>(fn, 0) != 0xB8 || At<uint8_t>(fn, 5) != 0xC3) return 0;
    return At<uint32_t>(fn, 1);
}

bool KindOf(uint32_t dti, uint32_t base) {
    for (int i = 0; dti && i < 32; ++i) {
        if (dti == base) return true;
        if (!Readable((void*)(dti + 0x10), 4)) return false;
        dti = At<uint32_t>(dti, 0x10);
    }
    return false;
}

}  // namespace

bool InEnemyGrab(uint32_t pawn) {
    if (At<uint32_t>(pawn, ch::ActionId) != 0x80000000) return false;
    uint32_t ev = 0;
    for (uint32_t off : kPawnEventActs) {
        const uint32_t p = At<uint32_t>(pawn, off);
        if (p && Readable((void*)p, 0x1C) && At<uint32_t>(p, 8) == pawn) {
            ev = p;
            break;
        }
    }
    if (!ev) return false;
    const uint32_t act = At<uint32_t>(ev, 0);
    for (uint32_t k : kKillActs)
        if (act == k) return false;
    if (act == kPlagaEat && (At<uint32_t>(pawn, 0x2D98) & 2)) return false;
    const uint32_t dti = DtiOf(At<uint32_t>(ev, 0xC));
    if (!dti) return false;
    for (uint32_t b : kDtiBoss)
        if (KindOf(dti, b)) return false;
    if (KindOf(dti, kDtiEnemy)) return true;
    for (uint32_t p : kDtiEnemyPart)
        if (KindOf(dti, p)) return true;
    return false;
}

bool FreeCameraAction(uint32_t pawn) {
    const Config& c = Cfg();
    const uint32_t id = At<uint32_t>(pawn, ch::ActionId);
    // With the free camera on, the mod owns the view for EVERY attack, not for a list of ids that has to be right.
    // The old rule named only the knife attack id (0x27) and, before that, only 0x30-0x4D, so whether the view stayed
    // free depended on which id the game happened to start; that is why the quick knife could not be turned.  This
    // range covers the knife stance/attacks (0x22-0x27) and the attack_* family (0x30-0x4D, incl. the co-op help
    // attack 0x6F and the two em10 attacks), and nothing else: taunt (0x2F), dodges, grabs and damage stay stock.
    // One int compare first: the enemy-grab DTI tables are only walked when GrabCamera is on.
    const bool attackAction = (id >= aid::KnifeBegin && id <= aid::KnifeAttack) || IsMeleeAction(id);
    if (attackAction && IsMeleeAction(id) && c.stockMeleeCamera) return false;
    if (attackAction && (c.freeCamera || c.meleeCamera)) return true;
    if (c.grabCamera && InEnemyGrab(pawn)) return true;
    return false;
}

void InitMeleeHooks() {
    static const BYTE sig[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x80, 0xBE, 0xC0, 0x10, 0x00, 0x00, 0x00};
    if (SafeEqual((void*)kFnUpdateActionCamera, sig, sizeof(sig)) &&
        MH_CreateHook((void*)kFnUpdateActionCamera, (void*)HookUpdateActionCamera, (void**)&oUpdateActionCamera) == MH_OK &&
        MH_QueueEnableHook((void*)kFnUpdateActionCamera) == MH_OK)
        Log("hook melee action camera at %08x", kFnUpdateActionCamera);
    else
        Log("hook melee action camera failed");
}
