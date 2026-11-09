#pragma once
#include <stdint.h>

// Native movement takeover (movehook.cpp).
//
// The engine drives player locomotion from a handful of action-update methods (walk / run / back / strafes / turns).
// This module hooks their entries, verifies the original prologue bytes first (so a different game build is rejected
// instead of patched blindly), and can then add a camera-relative step of its own.  Everything is passive unless
// [Movement] EngineMove=1, and every hook can be removed again.
void InitMovementHooks();
// True when the takeover is installed and the ini switch is on.
bool EngineMovementActive();
// Move the actor by `speedCm` per second for `dt`, in the world direction `yaw` (radians).  Returns false when
// the character is parented to a platform or the arguments are unusable.
bool EngineMoveStep(uint32_t character, float yaw, float speedCm, float dt);
