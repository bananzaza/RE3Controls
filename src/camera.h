#pragma once
#include <stdint.h>

// Free orbit camera shared between the input layer (game thread, cPad update) and the QFPS camera hook.
// Outside the aim the mod draws its own RE3-style orbit camera; aiming and the game's own look-at cameras stay
// the game's, with a short blend in between.
struct FreeCam {
    uint32_t character = 0;  // local player the camera belongs to
    float yaw = 0;           // world yaw the camera looks along (radians, same convention as the character)
    float pitch = 0;         // radians, + = looking down (camera above the shoulder), - = looking up
    bool leftShoulder = false;  // camera over the left shoulder (orbit only)
    bool mirrorAim = false;     // mirror the game's aim/look-at poses: only when the wanted side differs from the game's own
    bool charLeft = true;       // wanted side expressed as "the character stands on the left of the screen"
    bool gameCharLeft = true;   // measured once per aim: the game's own aim side puts the character on the left
    bool aimSideKnown = false;  // the measurement above was taken for this aim session
    bool active = false;     // the orbit camera runs this frame (not aiming, free camera on)
    bool initialised = false;
    bool noAimRig = false;   // reloading on the move: keep the normal camera while the gun is up
    double lastCameraUpdate = 0;
    double activeTime = 0;   // Mode=MeleeCamera: when the input layer last asked for the orbit (stale = vanilla)
};

// Each local player has a free camera of their own (split screen: each player has their own camera controller),
// by the player's slot (ch+NetSlot).
FreeCam& CamFor(uint32_t character);
// The character is the one its free camera was last driven for (a local player under the mod's controls).
bool IsCamCharacter(uint32_t character);
void InitCameraHooks();
float DefaultCameraPitch();  // radians, from the ini
// Extra fov (degrees, negative = closer) from the plain-gun zoom keys, 0 when off.
float ZoomFovExtra();
// Camera ray while aiming (eye + unit direction), used by the crosshair code so the shot follows the camera instead of
// the barrel line.  False when the game does not own the aim camera this frame.
bool AimCameraRay(float eye[3], float dir[3]);
// Pitch (radians) of the game's own aim camera the last time it was running; the aim start nudges the game's pitch
// towards the free camera through its own input using this as the "where the aim was" reference.
float AimPitchLast();
// Mouse vertical (raww units), the scale to radians and the invert flag, fed by game.cpp for the aim pitch.
void SetAimMouse(float dy, float scale, bool invert);

// yaw helpers: forward = (sin yaw, 0, cos yaw), right = (-cos yaw, 0, sin yaw)
// World facing of a character (on a moving boat or platform: its own yaw plus the platform's heading).
float CharacterYaw(uint32_t character);
void SetCharacterYaw(uint32_t character, float yaw);
// World position of a character (Pos is relative to the platform while it stands on one).
void CharacterWorldPos(uint32_t character, float* out);
float WrapPi(float a);
