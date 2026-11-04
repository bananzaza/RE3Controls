# RE3Controls for Resident Evil 5

This is a modification of the mod created by palecvzhope and is based on version 1.9.
The original mod can be found here: https://www.nexusmods.com/residentevil5goldedition/mods/1141

RE3Controls is a Resident Evil 5 mod that brings in Resident Evil 3 (2020) style controls: a free over-the-shoulder
camera, camera-relative movement, walking while aiming, a centred crosshair, one-button reload on the move, quick
turn, full keyboard/mouse support and more. 

Target: **Resident Evil 5 Gold Edition, Steam, version 1.2.0** (`re5dx9.exe`, 32-bit). The mod uses fixed addresses of
that build and checks the code bytes at each address before it hooks or patches anything; on any other build it
leaves that part alone and writes a line to its log.

## Building

Requirements: [zig](https://ziglang.org/) 0.13 or newer (it brings clang, lld and the mingw headers) and a bash (Git
Bash is fine). No Visual Studio or Windows SDK is needed.

```sh
bash build.sh                          # release build -> build/xinput1_3.dll
ZIG=/path/to/zig.exe bash build.sh     # if zig is not on PATH
```

`bash build.sh dev` makes a developer build (`build/dev/xinput1_3.dll`) with test tooling. It is not the release
build (see below).

## How it works

- **Loading.** The game imports `xinput1_3.dll`. The mod is a proxy with that name placed in the game folder: it
  loads the system XInput (`%WINDIR%\System32\XInput1_4.dll`, or `XInput9_1_0.dll` from the same folder) and forwards
  all XInput calls to it (`src/xinput_proxy.cpp`, exports in `src/xinput1_3.def`).
- **Hooks.** Game functions are hooked with [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4
  (`third_party/minhook`, unmodified, BSD 2-clause licence in `third_party/minhook/LICENSE.txt`). A few single
  instructions or call targets in the game's own code are patched in place with `VirtualProtect` (for example the aim
  sway and recoil gates, the colour filter switch, the crosshair size). Every hook and patch checks the expected
  bytes first.
- **Files.** It reads `RE3Controls.ini` next to the DLL, writes a default one if it is missing, and writes
  `RE3Controls.log` next to the DLL. It touches no other files and no registry keys.
- **Network.** None of its own. In online co-op it sends small sync packets through the game's own Steam P2P
  channel (`src/net.cpp`) so the partner sees the camera-relative movement and the aim point.
- **Nothing else.** No downloads, no other processes, no code injection into other programs, no obfuscation, no
  packing. The release build is stripped of debug symbols only to keep it small.

## Release and developer code

`RE3C_DEV` (set only by `bash build.sh dev`) guards all test tooling: a local TCP inspection server on 127.0.0.1, a
shared-memory bridge with a virtual gamepad, keyboard and mouse, screenshots and an auto-skip of cutscenes, all for
automated testing on the author's machine. The release build compiles without `src/devserver.cpp` and
`src/d3d9_hook.cpp`, `src/devinput.cpp` compiles to nothing, and `DevMode()` is a compile-time `false`, so none of
that code is in the published DLL.

## Files

| Path | Contents |
|---|---|
| `src/main.cpp` | DLL entry, log, start-up (waits for the game's code, then installs the hooks) |
| `src/xinput_proxy.cpp`, `src/xinput1_3.def` | The XInput proxy (forwards to the system XInput) |
| `src/config.cpp`, `src/config.h` | `RE3Controls.ini`: default text with comments, parsing |
| `src/game.cpp` | The controls: gamepad layout, free-camera movement, run, reload, quick turn, prompts |
| `src/camera.cpp` | The free orbit camera, aim camera distance and field of view |
| `src/aimwalk.cpp` | Walking while aiming (a lower-body animation layer) |
| `src/crosshair.cpp` | Centred crosshair, shot convergence, aim speed |
| `src/kbinput.cpp` | Keyboard and mouse support |
| `src/inventory.cpp` | Walking with the inventory open |
| `src/melee.cpp` | Free camera during melee attacks and enemy grabs |
| `src/damage.cpp`, `src/balance.cpp` | Optional damage, fire rate, move speed, shotgun spread, colour filter |
| `src/net.cpp` | Co-op sync packets |
| `src/re5.h`, `src/common.h` | Game structure offsets and helpers |
| `src/version.rc` | Version resource |
| `src/devserver.cpp`, `src/devinput.cpp`, `src/d3d9_hook.cpp` | Developer test tooling only (see above) |
| `docs/` | The user READMEs shipped in the release archive |
| `third_party/minhook/` | MinHook 1.3.4 |

## Licence

RE3Controls is released under the [MIT License](LICENSE): you may use, change and share this code, also in your own
mods, as long as the copyright notice stays with it. `third_party/minhook/` keeps its own BSD 2-clause licence
(`third_party/minhook/LICENSE.txt`).

---

---

## What this build changes compared with the original game

Everything below is a difference from a clean RE5 Gold Edition (Steam 1.2.0, 32-bit) with no mods.  Nothing here
changes the game's files; it is a single `xinput1_3.dll` next to `re5dx9.exe`.

### Camera and aim

| In this build | In the original game |
|---|---|
| **Free camera** while walking and while aiming; the mouse turns the view, the character follows | fixed over-the-shoulder camera that only turns with the character |
| **Shoulder swap at any time**, keyboard **B** (`[Controls] ShoulderKey`, any letter) or pad **RB** (L3 when `RunButton=RB`), including in cover and while walking | no swap at all |
| **Plain-gun zoom** while aiming: **F** zooms in, **C** zooms out (`[Zoom] ZoomFovStep=-14`, `ZoomMaxStep=2`) | one fixed zoom level per weapon |
| **Scoped walking**: move with the sniper scope raised (`[Zoom] ScopeWalk=1`) | the character is frozen while scoped |
| Aim pitch is inherited from the free camera and is not pulled back when the aim ends | aim pitch is fixed per stance |
| Shots follow the **camera ray**, including the laser sight | shots follow the weapon's own direction |
| Melee plays on the **game's own fixed camera** (`[Camera] StockMeleeCamera=1`) so the animations read correctly; the knife stance keeps the mod camera | all actions on the game camera |

### Movement

| In this build | In the original game |
|---|---|
| **Aim walking**: move while aiming a gun or holding the knife (`GunWalk=1`, `KnifeWalk=1`) | standing still while aiming |
| **Reload and weapon switch on the move** (`ReloadWalk=1`, `SwitchWalk=1`), including the game's own automatic reload when a magazine runs out | both force the character to stop |
| Steering continues while the gun goes down (`ReadyEnd`) and right after a shot | direction is locked for those actions |

### Weapons

| In this build | In the original game |
|---|---|
| **No frozen gun after a shot**: single-shot weapons and the last round of a magazine return control immediately (the fire action's own timer is held off) | the character stays locked in the fire stance for a while |
| **Weapon switching does not aim or reload by itself** | a switch can force the ready state |

### Knife

| In this build | In the original game |
|---|---|
| The **knife can be turned** while slashing, and the character keeps walking/running with the knife out (through the ordinary aim-walk path) | the knife locks the facing and the movement |
| Faster, movement-preserving knife stances | knife stance stops the character |

### Input robustness (no action at the wrong moment)

| In this build | In the original game / earlier builds |
|---|---|
| The **aim and the knife key do nothing** during melee, executions, grabs, vaults, climbs, damage reactions and scripted actions - for **every** character, because one classifier (`aidc::Locked`) decides instead of per-range lists | the mod used to turn the character toward the camera and draw the knife in the middle of those animations |
| The **character never raises the gun by itself**: the ready bit is written only when the player aims | older builds forced the ready state during a throw and after a reaction |
| Being hit does **not** trigger a reload or an aim | older builds re-sent the latched keys after a reaction |
| The thrown item is left entirely to the game: the character stands still and does not twitch during the recovery | older builds fought the throw animation |

### Known engine limits (not mod bugs)

- **Quick slash and item use cannot move.** `knife_attack` (0x27) and `item_use_my_*` (0x61/0x62) raise a movement lock
  inside the engine (`0xB7A8F0`: `or [ch+0x2D7C], 0x80000000` plus a cleared move vector).  Clearing that bit restores
  only the animation's own forward step; emulating the aim state changes the direction handling and the throw, so it is
  not done.  Holding the aim button still allows the slash to move, exactly as in the original game.
- The **sniper scope** keeps the game's own aim: its view is drawn by the weapon camera and the fields the mod can
  write do not reach it.
- The **laser sight** is drawn by the engine only on the pad path; a keyboard player gets the crosshair.
- Five hooks cannot be installed where RE5Fix has already patched the entries (camera ctrl, bind slot, camera aim ray,
  pad aim, reticle request).  The mod works without them.

---

## Configuration quick reference

The mod writes a fully commented `RE3Controls.ini` next to the DLL on the first run; the table below is the same list with the shipped defaults.  `Mode=MeleeCamera` keeps every other behaviour at the original game's.

### [Controls]

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `1` |  |
| `StockMeleeCamera` | `1` | Mode: Full = everything below; MeleeCamera = the original game, except that the camera stays free and turns with the right stick during melee attacks (of [Camera] 1 = melee/attack actions use the game's own fixed camera |
| `AimCamera` | `0` | EXPERIMENTAL, off: keep the mod's camera while aiming |
| `Mode` | `Full` | the free camera's behaviour (Mode=Full it uses Speed, PitchSpeed, InvertY, Distance, Height, Shoulder, FollowPlatform and GrabCamera; AimDistance, FieldOfView and AimFieldOfView stay the game's) |
| `RemapButtons` | `1` | 1 = the RE3 layout on the gamepad: A = interact (the game's "X" prompts show and take A; objects that keep checking the button after their prompt - held switches, co-op "push together" - also accept A held), X = reload, Back = map, L3 =... |
| `Run` | `Toggle` | Run: Toggle (click L3 once), Hold (hold L3) or Stock (hold A, as in the original game). It runs while standing, walking, running, turning or lowering the gun/knife; in cover, on ladders, in attacks, QTEs or Wesker's dash it does nothing... |
| `RunButton` | `L3` | RunButton: L3 or RB (with RemapButtons=1). RB swaps the two buttons: RB runs (and switches the walking pace while aiming), L3 locates the partner (and swaps the shoulder while aiming) |
| `InteractButton` | `A` | InteractButton (with RemapButtons=1, gamepad): A = the prompts (open, pick up, finishers) show and take A, as in RE3; X = they stay on X, as in the original game: X answers a prompt when one is on screen and reloads otherwise, and A has... |
| `OneButtonReload` | `1` | 1 = X reloads (keyboard: the game's reload key) from standing, walking, running, cover or the aim, without raising the gun. While a prompt or QTE that X answers is on screen, X only answers it; a press is ignored when the game cannot rel... |
| `QuickTurn` | `1` | 1 = B with the left stick pulled back (toward the camera with FreeCamera=1) turns you round (keyboard: the game's quick turn key), while standing, walking, running or lowering the gun or knife. In cover, on ladders, in attacks, grabs, or... |
| `WeskerDash` | `1` | Wesker's dash (The Mercenaries). 1 = as in the original game: R3 starts the dash when you are not aiming (it also puts the camera behind Wesker; each dash costs a little health); during the dash the right stick steers, A + the right stic... |
| `StickClicks` | `1` | the original game's stick-click moves on the gamepad. 1 = hold R3 and click L3 to taunt (any character, also while aiming; that L3 click then does not run, change the walking pace, locate the partner or swap the shoulder), and R3 while a... |
| `KnifeFromAim` | `1` | LB (knife) pressed while LT (aim) is held. 1 = as in the original game: the knife comes out straight from the gun aim (RT slashes) and releasing LB goes back to the gun aim; LT pressed while the knife is out still switches to the gun. 0... |
| `InventoryMove` | `1` | 1 = walk with the left stick (and run with L3) while the inventory is open; its cursor then moves with the d-pad only. Keyboard: the run key runs when you press it while moving; pressed while standing it confirms in the inventory, as in... |

### [Camera]

| Key | Default | Meaning |
|---|---|---|
| `StockMeleeCamera` | `1` |  |
| `AimCamera` | `0` |  |
| `FreeCamera` | `1` | 1 = the right stick turns the camera around Chris/Sheva and the left stick moves relative to the camera. Raising the gun or knife from standing, walking, running or lowering the gun first turns the character to where the camera looks; in... |
| `Speed` | `180` | 1 = the right stick turns the camera around Chris/Sheva and the left stick moves relative to the camera. Raising the gun or knife from standing, walking, running or lowering the gun first turns the character to where the camera looks |
| `PitchSpeed` | `90` | 1 = the right stick turns the camera around Chris/Sheva and the left stick moves relative to the camera. Raising the gun or knife from standing, walking, running or lowering the gun first turns the character to where the camera looks |
| `InvertY` | `0` | 1 = the right stick turns the camera around Chris/Sheva and the left stick moves relative to the camera. Raising the gun or knife from standing, walking, running or lowering the gun first turns the character to where the camera looks |
| `Distance` | `230` | camera like RE3: distance behind the shoulder, pivot height, shift over the shoulder (cm), default tilt in degrees (+ = looking down); R3 puts the camera back behind and at this tilt |
| `Height` | `150` | camera like RE3: distance behind the shoulder, pivot height, shift over the shoulder |
| `Shoulder` | `45` | camera like RE3: distance behind the shoulder, pivot height, shift over the shoulder |
| `Tilt` | `8` | camera like RE3: distance behind the shoulder, pivot height, shift over the shoulder |
| `AimDistance` | `15` | aiming: how much farther back the camera sits, in cm. It moves straight back along its own view, so the crosshair and where the shots land stay the same, and walls still push it in as in the game. 0 = the game's own aim camera, 50 = half... |
| `FieldOfView` | `0` | field of view in degrees, added to the game's own (45 outside the aim): 0 = as in the game, 15 = 60 degrees (wider), -10 = 35 (narrower); -15 to 40, beyond that counts as the nearest end, text = 0. Not with Mode=MeleeCamera. Default: 0 |
| `AimFieldOfView` | `0` | the same while aiming (the game's 33 to 40 degrees, by weapon): 0 = as in the game, 10 = 10 degrees wider; -15 to 40. Not in the scope view (sniper rifles, rocket launchers), on gun emplacements and vehicle guns, or with Mode=MeleeCamera... |
| `TurnSpeed` | `720` | the same while aiming |
| `BackStep` | `0` | moving toward the camera (left stick pulled back, S on the keyboard) without running, with FreeCamera=1: 0 = turn round and walk toward the camera, as in RE3 Remake (and RE2/RE4 Remake); 1 = step back while facing away from the camera, a... |
| `FollowPlatform` | `1` | on a turning boat or moving platform the camera turns with it, as the game's own camera does |
| `MeleeCamera` | `0` | melee attacks (A on a staggered enemy, finishers, kicks, stomps): 1 = no cinematic attack camera, the camera stays where you put it and the right stick keeps turning it (needs FreeCamera=1); 0 = the game's attack camera |
| `GrabCamera` | `0` | enemy grabs (an enemy holds you while you shake free, or until the partner frees you): 1 = no cinematic grab camera, the camera stays where you put it and the right stick (keyboard: the mouse) keeps turning it (needs FreeCamera=1; also w... |
| `CoverTurn` | `1` | crouched behind low cover, with FreeCamera=1. 1 = as in the original game (control type D): the right stick (keyboard: the mouse) also turns the crouching character left/right while it turns the camera. 0 = as in version 1.8: there it on... |

### [Aim]

| Key | Default | Meaning |
|---|---|---|
| `AimPointFromCamera` | `0` |  |
| `AimPointDistance` | `9999` |  |
| `AimPitchFromMouse` | `0` |  |
| `GunWalk` | `1` | walk while aiming a gun / with the knife readied / while reloading / while switching weapons (1 = walk; 0 = stand still as in the original game; the camera stays the mod's either way). Default: 1 for all four |
| `KnifeWalk` | `1` | walk while aiming a gun / with the knife readied / while reloading / while switching weapons |
| `ReloadWalk` | `1` | walk while aiming a gun / with the knife readied / while reloading / while switching weapons |
| `SwitchWalk` | `1` | walk while aiming a gun / with the knife readied / while reloading / while switching weapons |
| `ScopeWalk` | `1` | walk while looking through a scope (sniper rifles, rocket launchers). 1 = the left stick (keyboard: WASD) walks in the scope; to zoom hold RB (L3 with RunButton=RB) and push the left stick up / down - the legs stop while it is held. Keyb... |
| `WalkSpeed` | `1.125` | walking speed while aiming or holding the knife, 1.0 = the game's normal walk; the run button (L3 or RB) while aiming switches to FastWalkSpeed |
| `FastWalkSpeed` | `1.875` | fast walk (run button while aiming) |
| `KnifeSpeed` | `1.5` | knife attack speed (1.0 = the original game) |
| `Crosshair` | `1` | a neat crosshair instead of the laser sight (gamepad, and keyboard/mouse with MouseAim=RE3) |
| `CrosshairSize` | `1.0` | size of that crosshair: 1.0 = the normal size, 0.5 = half, 2.0 = twice as big; 0.25 to 4 (smaller values count as 0.25, larger ones as 4; 0, a negative number or text = 1.0, so 0 does not hide it: Crosshair=0 gives the laser). Its openin... |
| `DirectHorizontal` | `1` | horizontal crosshair aim turns the view and the body at once (stick or mouse); 0 = original arm-only aim zone |
| `StockMeleeCamera` | `1` | gamepad aim speed, horizontal / vertical, on top of the game's own Aim Speed option: 1.0 = unchanged, 2.0 = twice as fast, 0.5 = half (0.25 to 4). Also in the sniper scope (the game still slows the aim with the zoom). The free camera has... |
| `AimCamera` | `0` | gamepad aim speed, horizontal / vertical, on top of the game's own Aim Speed option: 1.0 = unchanged, 2.0 = twice as fast, 0.5 = half |
| `AimSpeedX` | `1.0` | gamepad aim speed, horizontal / vertical, on top of the game's own Aim Speed option: 1.0 = unchanged, 2.0 = twice as fast, 0.5 = half |
| `AimSpeedY` | `1.0` | gamepad aim speed, horizontal / vertical, on top of the game's own Aim Speed option: 1.0 = unchanged, 2.0 = twice as fast, 0.5 = half |
| `AimCameraLag` | `0.15` | gamepad (crosshair or laser): how far the camera trails the aim; 1 = the original pad aim camera (about 0.14 s behind, the character can drift into the crosshair when you turn fast), 0 = no delay; 0.15 is close to the game's own mouse ai... |
| `CrosshairKick` | `1.0` | how far the crosshair opens on each shot and springs back: 1.0 = the normal opening, 2.0 = twice as far, 0.5 = half; 0 = it never opens, also not the game's own opening for shotguns and magnums (a negative number or text counts as 0). Wi... |
| `KnifeCrosshair` | `1` | keyboard/mouse: the game's own small reticle while the knife is out (1); 0 = hidden, as with the gamepad |
| `Breathing` | `0` | aim sway (breathing): 0 = a steady aim with the gamepad and the mouse, 1 = the original game's |
| `Recoil` | `1` | automatic weapons kick the aim up a little on each shot, like with a mouse |

### [Damage]

| Key | Default | Meaning |
|---|---|---|
| `Damage` | `0` | 0 = the original game's damage (it scales with how well you play). Any other value is a fixed multiplier for all damage the players deal instead: 3 = three handgun headshots kill a common Majini, like RE3; 1.4 = about the original's star... |
| `KnifeDamage` | `2.0` | extra multiplier for the knife / machete, on top of Damage (only when Damage is not 0) |
| `FireRate` | `1.0` | rate of fire of all guns: 1.0 = the original game, 2.0 = twice as fast (0.5 to 3); the knife and grenades are not affected; in co-op the host's value is used |
| `ShotgunSpread` | `1.0` | shotgun pellet spread: 1.0 = the original game, 0.5 = half as wide, 0 = all pellets in one line (very strong at range); all shotguns of all players; in co-op the host's value is used |

### [Movement]

| Key | Default | Meaning |
|---|---|---|
| `AimPointFromCamera` | `0` | walking and running speed outside the aim: 1.0 = the original game (0.5 to 2); the walking speed while aiming is WalkSpeed / FastWalkSpeed in [Aim] |
| `AimPointDistance` | `2000` | walking and running speed outside the aim: 1.0 = the original game |
| `AimPitchFromMouse` | `0` | walking and running speed outside the aim: 1.0 = the original game |
| `MoveWalkSpeed` | `1.0` | walking and running speed outside the aim: 1.0 = the original game |
| `MoveRunSpeed` | `1.0` | walking and running speed outside the aim: 1.0 = the original game |

### [Visual]

| Key | Default | Meaning |
|---|---|---|
| `NoColorFilter` | `0` | 1 = remove the yellow colour tint of the levels (the game's own Special Settings filters still work) |

### [Advanced]

| Key | Default | Meaning |
|---|---|---|
| `HookDelayMs` | `3000` | milliseconds the mod waits after the game window appears before it hooks the game (raise it if the game says "Program integrity check failed" at start) |

### [Keyboard]

| Key | Default | Meaning |
|---|---|---|
| `KeyboardMouse` | `1` | 1 = the mod's controls also on keyboard and mouse: the mouse turns the camera, WASD move relative to it, the reload key reloads on the move, the quick turn key turns around, walking while aiming works. The keys are the game's own (Option... |
| `Run` | `Hold` | running on the keyboard: Hold (hold the run key) or Toggle (press it once) |
| `MouseSensitivity` | `0.75` | mouse speed for the camera and the aim, on top of the game's own mouse sensitivity option (1.0 = the game's speed; the camera turns exactly as far per mouse movement as the aim does); vertical relative to it |
| `MouseVertical` | `1.0` | mouse speed for the camera and the aim, on top of the game's own mouse sensitivity option |
| `MouseAim` | `RE3` | aiming: RE3 = the crosshair stays in the centre and the mouse turns the aim (the game's mouse-look style); Game = the aim style set in the game's options |
| `MouseSwitchCounts` | `30` | a gamepad player who touches the mouse switches to keyboard/mouse only after this many mouse counts within about a quarter of a second (0 = the game's own rule: any movement). Keys and mouse buttons switch at once |

### [Coop]

| Key | Default | Meaning |
|---|---|---|
| `Sync` | `1` | Extra position updates so the partner sees free-camera turns and walking while aiming |

### [Zoom]

| Key | Default | Meaning |
|---|---|---|
| `ZoomKeys` | `1` | plain-gun zoom (F in, C out while aiming); degrees of fov per step, negative = closer |
| `ZoomFovStep` | `-20` | plain-gun zoom |
| `ZoomMaxStep` | `3` | plain-gun zoom |
| `ScopeLook` | `0` | plain-gun zoom |

## Logging

The build writes a few diagnostic lines to `RE3Controls.log` next to the game: `keyq:` (aim/fire key queries), `fire:`
(the fire-action timer when the hold-off triggers), `trace:` (a short sample after an aim release) and the probe lines
`dmg:` / `blendfail:` / `layerfail:` used while tracking an action.  They are rate limited and can be ignored; the
sources can be built without them.

## Credits

- Original **RE3Controls** by *bananzaza* (MIT) and the 1.9.x work by *notencio*.
- **MinHook** by Tsuda Kageyu (BSD 2-Clause), bundled in `third_party/minhook`.
- This fork by *MXYLR*: free camera, shoulder swap, zoom, movement during actions, and the fixes listed above.
