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

## Defaults in this build

| Setting | Value |
|---|---|
| Shoulder | keyboard **B** (`[Controls] ShoulderKey`), pad **RB** (L3 with `RunButton=RB`), any time |
| Plain-gun zoom | **F** in, **C** out, `[Zoom] ZoomFovStep=-14`, `ZoomMaxStep=2` |
| Scoped walking | `ScopeWalk=1` |
| Reload / switch on the move | `ReloadWalk=1`, `SwitchWalk=1` |
| Aim walking | `GunWalk=1`, `KnifeWalk=1` |
| Free camera | `FreeCamera=1`, `Shoulder=45`-`60`, `AimDistance=100` |

## Logging

The build writes a few diagnostic lines to `RE3Controls.log` next to the game: `keyq:` (aim/fire key queries), `fire:`
(the fire-action timer when the hold-off triggers), `trace:` (a short sample after an aim release) and the probe lines
`dmg:` / `blendfail:` / `layerfail:` used while tracking an action.  They are rate limited and can be ignored; the
sources can be built without them.

## Credits

- Original **RE3Controls** by *bananzaza* (MIT) and the 1.9.x work by *notencio*.
- **MinHook** by Tsuda Kageyu (BSD 2-Clause), bundled in `third_party/minhook`.
- This fork by *MXYLR*: free camera, shoulder swap, zoom, movement during actions, and the fixes listed above.
