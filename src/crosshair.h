#pragma once
#include <stdint.h>

// The mouse crosshair instead of the laser sight (gamepad, and keyboard/mouse with MouseAim=RE3).
// Runtime switch between the two aiming sights: -1 = follow the ini, 0 = the game's laser sight, 1 = the crosshair.
void SetCrosshairMode(int mode);
int CrosshairMode();
void InitCrosshairHooks();
// game.cpp: the local player holds the gun up (LT), as opposed to the knife.
bool GunAimHeld(uint32_t character);
// The local player's crosshair is up: the world point under it (the camera aim ray's hit).
bool LocalAimPoint(uint32_t character, float* out);
// The player aims with a gamepad (not the game's keyboard/mouse mode, not a scripted cursor scene).
bool AimsWithPad(uint32_t character);
// The local pad player aims a plain gun, with the crosshair or the laser sight.
bool PadGunAimed(uint32_t character);
// The player's control type aims with the right stick (B and D; there the left stick zooms in a scope).
bool PadAimsWithRightStick(uint32_t character);
