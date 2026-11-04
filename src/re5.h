// Addresses and layouts for RE5 Steam 1.2.0 (build 11465250, image base 0x400000, no ASLR).
#pragma once
#include <stdint.h>

namespace re5 {

// Globals
constexpr uint32_t kPlayerMgrPtr = 0x011B27DC;  // -> player manager; +0x24 + slot*4 = character (4 slots)

constexpr uint32_t kSPadPtr = 0x01249C40;      // -> sPad (MT pad system)

// Functions
constexpr uint32_t kFnPlayerPadUpdate = 0x00C6E150;  // thiscall cPad::Update(), ECX = cPad
constexpr uint32_t kFnActionCheck = 0x00766F00;
constexpr uint32_t kFnQfpsUpdate = 0x00849EF0;
constexpr uint32_t kFnCameraCtrlUpdate = 0x00841CE0;  // thiscall uCameraCtrl::update(float dt), ret 4
constexpr uint32_t kFnModelBlend = 0x00588230;  // thiscall uModel::blend/update joints (no args), via thunk 0x8305C0
constexpr uint32_t kFnSetMotion = 0x0059E880;   // thiscall uModel::setMotion(slot, no, inter, start, speed, flags) ret 0x18  // thiscall uCameraQFPS::update(arg) (vtable slot 16), ret 4

// uCameraBase fields (uCameraQFPS = over-the-shoulder player camera, embedded in uCameraCtrl at +0xE00)
namespace cam {
constexpr uint32_t Parent = 0x140;     // mpParent (character)
constexpr uint32_t EyeAim = 0x170;     // mCameraAim, world
constexpr uint32_t UpAim = 0x180;      // mUpAim, direction
constexpr uint32_t TargetAim = 0x190;  // mTargetAim, world
constexpr uint32_t FovAim = 0x1A0;
constexpr uint32_t AimRig = 0x1B1;     // u8: 1 = aim (over-the-shoulder) rig; written every frame by the character
constexpr uint32_t LookAt = 0x1B3;     // u8: look at the partner (locate button, co-op action points); +0x1B4 = last
constexpr uint32_t LookV = 0x1D0;      // f32 vertical look -1..1 (blends the rig poses), the aim copies its pitch here
constexpr uint32_t LookH = 0x1D4;      // f32 horizontal look offset (radians)
constexpr uint32_t LookEnabled = 0x2A0;  // u8: the game's raw-right-stick look-around runs (0x8454E0)
}  // namespace cam      // thiscall ActionSlot::Check(cPad* pad, u32 slot) -> bool

// sPad: raw device pads
namespace spad {
constexpr uint32_t Pads = 0x34;          // pad[i] = sPad + 0x34 + i*0x2C0
constexpr uint32_t PadStride = 0x2C0;
constexpr uint32_t PadType = 0x07;       // u8, 0 = no device
constexpr uint32_t On = 0x15C;           // MT bits (see P_*)
constexpr uint32_t Rx = 0x174;           // s32 sticks, -32768..32767 (y up = +)
constexpr uint32_t Ry = 0x178;
constexpr uint32_t Lx = 0x17C;
constexpr uint32_t Ly = 0x180;
constexpr uint32_t InputLocked = 0x5DC;  // u8, all getters return 0 while set
constexpr uint32_t DeviceIsPad = 0x5F0;  // u8, 1 = pad, 0 = keyboard/mouse (last device used, 0x79BC50)
constexpr uint32_t KeyboardPlayer = 0x614;  // s32, the player the keyboard/mouse belongs to
}  // namespace spad

// sActionButton::ButtonMgr (one per player) action slots: context prompts and QTE buttons
namespace act {
constexpr uint32_t Request = 0x0C;     // -> request
constexpr uint32_t Glyph = 0x38;       // u32 prompt icon id
constexpr uint32_t ReqType = 0x20;     // index into the mask table at 0xF58E50 (1 = X, 2 = Y, 3 = A, 4 = B ...)
constexpr uint32_t ReqStrict = 0x6C;   // u8, QTE: any other button fails
constexpr uint32_t TypeX = 1;
constexpr uint32_t TypeA = 3;
constexpr uint32_t MaskTable = 0x00F58E50;  // u32[type]: pad bits a prompt type expects
constexpr uint32_t GlyphA = 70;        // icon ids from the table at 0xF590B8 (X = 69, A = 70, Y = 71, B = 73)
}  // namespace act

// Player "owner" objects (4, stride 0x2D0) embed a PlayerPad at +0x10. owner+8 = character slot.
constexpr uint32_t kOwnerPadOffset = 0x10;
constexpr uint32_t kOwnerSlot = 0x08;
// The character's own input PlayerPad: [character + ch::Controller] + kCharPadOffset; controller+0x18 = character.
constexpr uint32_t kCharPadOffset = 0x708;

// PlayerPad layout (after Update)
namespace pp {
constexpr uint32_t Enabled = 0x04;   // u8
constexpr uint32_t Mode = 0x08;      // u32: 0 physical pad, 1 remapped pad, 2 injected values
constexpr uint32_t PadIndex = 0x10;  // u32: sPad index, 0xFF = none (AI)
constexpr uint32_t On = 0x34;
constexpr uint32_t Old = 0x38;
constexpr uint32_t Trg = 0x3C;
constexpr uint32_t Rel = 0x40;
constexpr uint32_t Rep = 0x44;
constexpr uint32_t TrgRep = 0x48;
constexpr uint32_t LAngle = 0x1CC;   // float degrees, 0 = up, + = counter-clockwise (left)
constexpr uint32_t RAngle = 0x1D0;
constexpr uint32_t LMag = 0x1D4;     // float 0..1 (0 inside the 0.3 dead zone)
constexpr uint32_t RMag = 0x1D8;
constexpr uint32_t LTrig = 0x1DC;    // float 0..0.95
constexpr uint32_t RTrig = 0x1E0;
}  // namespace pp

// MT logical pad bits
enum : uint32_t {
    P_BACK = 0x1, P_L3 = 0x2, P_R3 = 0x4, P_START = 0x8,
    P_UP = 0x10, P_RIGHT = 0x20, P_DOWN = 0x40, P_LEFT = 0x80,
    P_LB = 0x100, P_RB = 0x200, P_LT = 0x400, P_RT = 0x800,
    P_Y = 0x1000, P_B = 0x2000, P_A = 0x4000, P_X = 0x8000,
    P_LS_UP = 0x10000, P_LS_RIGHT = 0x20000, P_LS_DOWN = 0x40000, P_LS_LEFT = 0x80000,
    P_RS_UP = 0x100000, P_RS_RIGHT = 0x200000, P_RS_DOWN = 0x400000, P_RS_LEFT = 0x800000,
    P_LS_DIRS = 0x0F0000, P_RS_DIRS = 0xF00000,
};

// Character (uPl* actor) fields
namespace ch {
constexpr uint32_t Flags = 0x04;     // & 7 == 1/2 -> controllable
// While the character stands on a moving object (boat, lift, moving floor, conveyor plate) the game parents it to
// that object (0xB626D0): Pos and Quat are then relative to the parent's joint matrix, not the world.
constexpr uint32_t Parent = 0x18;    // uCoord mpParent (0 = none)
constexpr uint32_t ParentNo = 0x1C;  // s32 parent joint (-1 = the parent's own world matrix +0xA0)
constexpr uint32_t Pos = 0x30;       // float3, parent-local when Parent != 0
constexpr uint32_t Quat = 0x40;      // float4 (x,y,z,w); facing yaw = 2*atan2(y,w); parent-local when Parent != 0
constexpr uint32_t Hp = 0x1364;      // int16
constexpr uint32_t PadIdx = 0x2DB1;  // u8, 0xFF = AI
constexpr uint32_t ActionId = 0x10E0;    // u32 current action id (1 relax, 2 walk, 5 run, 0x1C weapon_ready_loop ...)
constexpr uint32_t PrevActionId = 0x10E4;
constexpr uint32_t CurAction = 0x10F8;   // -> current cPlAction_pl_act_* object
constexpr uint32_t NetTimer0 = 0x17CC;    // f32 seconds until the next co-op heartbeat
constexpr uint32_t NetTimer1 = 0x17D0;
constexpr uint32_t NetSlot = 0x2D9C;      // u8 slot index used in co-op messages
constexpr uint32_t NetControl = 0x2DA8;   // 0 local pad, 1 AI, 2 driven by the network partner
constexpr uint32_t Scope = 0x2998;        // u8, sniper scope view
constexpr uint32_t Controller = 0x2D64;  // -> player controller (vtable 0x101EC08), +0x18 = character
constexpr uint32_t Motion0 = 0x380;  // 8 motion slots (count at +0x374), stride 0xE0; +4 motion no, +0x14 blend
constexpr uint32_t MotionStride = 0xE0;
}  // namespace ch

// cPlAction_pl_act_* vtables (class reflection dump)
namespace plact {
constexpr uint32_t Relax = 0x01044014, Wait = 0x01043FE8, Walk = 0x01044040, WalkL = 0x0104406C,
                   WalkR = 0x01044098, Run = 0x010440C4, Back = 0x010440F0, TurnL = 0x0104411C,
                   TurnR = 0x01044148, Turn180 = 0x01044174;
constexpr uint32_t ReadyBegin = 0x0104448C, ReadyLoop = 0x010444B8, ReadyEnd = 0x010444E4,
                   Reload = 0x01044540, Fire = 0x01044510;
// Weapon switch (action 0x20) and switch while aiming (0x21): a full-body clip 0x14 in slot 0, slots 1-7 cleared.
constexpr uint32_t WeaponChange = 0x0104456C, WeaponChangeReady = 0x01044598;
}  // namespace plact

// Player action ids (ch+0x10E0, table 0x113AF80). The knife states reuse the gun classes (ready loop, fire).
namespace aid {
constexpr uint32_t KnifeBegin = 0x22, KnifeLoop = 0x23, KnifeEnd = 0x24, KnifeFromGun = 0x25, KnifeToGun = 0x26,
                   KnifeAttack = 0x27;
}  // namespace aid

template <class T>
inline T& At(uint32_t base, uint32_t off) { return *reinterpret_cast<T*>(base + off); }

// ------------------------------------------------------------------------------------------------ action classes
// One place decides what a player action id means.  Every subsystem used to carry its own list and each of those lists
// was incomplete for some character (Excella's melee is 0xAC, which none of the old ranges covered).
namespace aidc {
inline bool Controllable(uint32_t id) { return id <= 0x09 || (id >= 0x1B && id <= 0x27); }
inline bool Locked(uint32_t id) { return !Controllable(id); }

// Melee / context attack: everything from 0x30 up, except the groups that are known to be something else
// (items, the plaga reaction, damage reactions).
inline bool Melee(uint32_t id) {
    if (id < 0x30 || id > 0xC8) return false;
    if (id >= 0x59 && id <= 0x68) return false;   // item get/put/use
    if (id == 0x85 || id == 0x86) return false;   // plaga stick in/out
    if (id >= 0x88 && id <= 0x9F) return false;   // damage / reactions
    return true;
}
}  // namespace aidc

}  // namespace re5
