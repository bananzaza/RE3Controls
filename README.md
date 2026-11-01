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

## Additions in this fork (MXYLR)

Beyond upstream v1.9 and notencio's v1.9.4 fixes, this build adds:

### Shoulder camera

| Input | Key |
|---|---|
| Keyboard | **B** (`[Controls] ShoulderKey=B`, any letter A-Z) |
| Gamepad | **RB** (**L3** when `RunButton=RB`) |

- The swap works **at any time**, aiming or not (`[Camera] ShoulderSwapAlways=1`).
  Set it to `0` for the old behaviour (swap only while aiming, RB acts as the
  knife outside the aim).
- Outside the aim the knife stays on the physical **LB**, so giving RB to the
  shoulder does not remove the knife.
- In a scope the key is the `ScopeWalk` zoom button instead.
- One wanted side ("the character stands on the left of the screen", default
  left) drives both views:
  - the walking camera uses it directly;
  - the game's own aim poses are mirrored only when needed.
  The game aims with a side of its own per character (Chris and Sheva differ), so
  that side is measured at the start of each aim 闂?three consecutive frames with
  the same sign, never while mirroring 闂?and remembered. The log prints it once:
  `shoulder: game aims with the character on the left (pose x -40.0)`.
- The key is read raw, so it is independent of the game's own button bindings,
  and a press is consumed for that frame (a key that is also bound to another
  action cannot toggle the side twice).
- `ShoulderAuto`, `ShoulderLeftKinds` and `ShoulderGameLeftKinds` from earlier
  test builds are ignored now.
### Plain-gun zoom

While aiming a gun (not a scope) the keyboard zooms:

| Key | Action |
|---|---|
| **F** | zoom in (one step) |
| **C** | zoom out |

`[Aim] ZoomKeys=1`, `ZoomFovStep=-14` (fov degrees per step), `ZoomMaxStep=2`.
The level resets when the aim ends, the sniper scope keeps its own zoom, and
gamepad players are unaffected.

### Earlier fixes kept in this fork

- The sniper scope no longer has its mouse input zeroed by the mod (it is drawn
  by the weapon camera, which the mod does not control).
- Scoped walking is on by default (`ScopeWalk=1`).
- The quick knife slash can be turned with the mouse / right stick.
- Aim pitch is inherited from the free camera, and the view no longer snaps back
  to the pre-aim pitch after aiming.

---

## Known issues in this build

- **Sniper scope aiming.** The scope view is drawn by the weapon camera (`uCameraWep`)
  and uses that camera's own state.  The shot point (`ch+0x2700`), the aim point
  (`ch+0x2870`) and the pad aim input all fail to reach it, and writing the
  character's aim state (`ch+0x2908/+0x290C`) every frame freezes its pitch because
  those fields are the game's pitch integrator.  The scope therefore starts on the
  game's own direction instead of the free camera's.  `[Zoom] ScopeLook` stays 0.
- **Knife swing direction.** The aim point and the facing are written while the
  knife is out (the point every frame, the facing on every action id change), which
  fixes most swings, but the very first swing after readiness can still go to the
  character's old front, and the game's own melee assist may pull a swing toward a
  nearby enemy.
- Five hooks cannot be installed where RE5Fix patched the entries (camera ctrl,
  bind-slot, camera aim ray, pad aim, reticle request).  The mod works without them;
  the leg-layer mask is applied at setMotion instead of in the bind callback.
## Defaults in this build

| Setting | Value |
|---|---|
| Shoulder | keyboard **B** (`[Controls] ShoulderKey`), pad **RB** (L3 with `RunButton=RB`), any time |
| Plain-gun zoom | **F** in, **C** out, `[Zoom] ZoomFovStep=-20`, `ZoomMaxStep=3` |
| Scoped walking | `ScopeWalk=1` (zoom on RB + stick) |
| Reload / switch on the move | `ReloadWalk=1`, `SwitchWalk=1` |
| Aim walking | `GunWalk=1`, `KnifeWalk=1` |
| Free camera | `FreeCamera=1`, `Shoulder=45`-`60`, `AimDistance=100` |
---

## Fixes added in this fork (MXYLR), on top of 1.9.4

- **Shoulder swap works everywhere**: keyboard B ([Controls] ShoulderKey) / pad RB at any
  time, including low cover and wall cover (the game's cover camera is shifted sideways).
- **Plain-gun zoom**: F in, C out while aiming ([Zoom] ZoomFovStep=-20, ZoomMaxStep=3).
- **Aim**: the mouse is not zeroed while aiming; shots follow the camera ray; the aim
  pitch is handed over as an absolute value on the aim press.
- **No frozen gun after a shot**: the game's fire action waits on a per-object timer
  (Fire::update, phase 2, `[object+0x20]`).  The mod holds that timer above the
  threshold while the player is not aiming, so the stance and movement come back at
  once instead of staying frozen (single-shot weapons, and right after a magazine runs
  out).
- **Reload on the move covers the game's own reload**: when a magazine is empty the
  game reloads by itself; walking and steering are enabled for that action too.
- **Steering while the gun goes down**: ReadyEnd and Fire are steerable as well, so the
  character no longer keeps a fixed direction right after leaving the aim.
- **Melee uses the game's own fixed camera** ([Camera] StockMeleeCamera=1); the knife
  stance stays on the mod's camera so it can still be turned.

## Known issues in this build

- **Mercenaries countdown**: for a moment after the countdown ends the character can
  move on its own (the scripted state keeps the pad's direction bits).  Being tracked.
- **Sniper scope**: the scope view is drawn by the weapon camera and keeps the game's
  own aim; the fields the mod can write (shot point, aim point, pad input) do not reach
  it.
- **Laser sight is pad-only**: the engine only draws it on the pad path; a keyboard
  player gets the crosshair.

## Logging

The build writes a few diagnostic lines to RE3Controls.log: `keyq:` (aim/fire key
queries), `fire: timer=闂備胶鍋ㄩ崕鍝勨枖?(the fire-action timer when the hold-off triggers) and
`trace:` (a two-second sample after an aim release).  They are rate limited and can be
ignored, or removed from the sources if you build it yourself.
## Fixes in mod64h (added after mod63x)

- **Grenade throw no longer locks the character.**  A probe showed the thrown-item
  action (`0x01044F38`, action id `0x57`) stalls exactly like the shot: phase 2,
  waiting on the timer at `[object+0x20]`.  The same hold-off now covers it, so after
  a throw the aim can be cancelled again and the character does not twitch when you
  move.
- **Leg layer hysteresis**: it stays enabled for 8 frames once allowed, so a
  borderline action no longer toggles it at frame rate.
- **Mercenaries countdown**: the movement seen after the countdown was a movement key
  held before control was handed back, not a stuck state - releasing the key stops the
  character.  No code change.
- The diagnostic `keyq:` probe was removed.

Still open: using a first-aid spray keeps the character in place (that action is not in
the walkable set yet).
## Known limitation: throwing an item and releasing the aim immediately

Throwing a grenade (aim + fire) ends in the game's own `Fire` action and then plays a
recovery that carries the character forward by **animation root motion**.  Measurements
made while investigating it:

- the action chain itself is fine (`0x0104456C` phase 1 -> 2 -> 3, then `Wait`);
- while the character slides, the pad is empty (`padOn = 0`), so the movement is not
  input-driven;
- none of the sixteen floats around the character position holds a velocity: the
  position changes but every neighbouring field stays constant, i.e. the displacement is
  applied by the model/animation layer, not by a field we can clear.

Reverting the aim during that recovery makes the two poses fight (visible twitch).
Forcing the action to end, or freezing the position, either breaks the action state
machine or stops the player's own movement, so all of those were reverted.

**Workaround**: wait for the recovery to finish (about 0.4 s) before letting go of the
aim button.  This is the only known-safe behaviour; the sources are back at the released
`v1.9.4-mod63x` state.
## Stable build: 1.9.4-mod67

Only fixes that were verified in play are in this build:

- **Picking items up no longer freezes the game.** The player-pad hook created its critical section lazily and
  then spun on `while (init != 2) Sleep(0)`, which could hang for ever on the path taken while picking an item up.
  The section is now created at start-up and there is no wait at all.
- **The aim button does nothing while a melee, a vault or an interaction plays.** It used to turn the character
  toward the camera mid-animation; `aiming` is now gated at the source for those actions and the pad bits are
  stripped as a second net.
- **The knife attack no longer fights the leg layer**, so the character can turn again while slashing.
- Everything else is the released `v1.9.4-mod63x` behaviour: fire-cancel and movement after a shot, walking and
  steering during an empty-magazine reload, steering while the gun goes down, shoulder swap, plain-gun zoom,
  melee on the game's own camera.

### Known issues (not fixed, with the workaround)

- **Quick slash on the knife key (V) does not come out.** The game raises the knife (KnifeBegin) but never
  reaches the attack action from that entry point in this remap. Holding the aim button and then using the attack
  button works and is the workaround. Measurements: the action chain stays at `0x22` when only the knife key is
  used, and re-aiming/writing the aim point on the attack action is what lets it reach `0x27` - but that write also
  interrupts the swing.
- **Throwing an item and releasing the aim immediately**: the throw's recovery carries the character forward by
  animation root motion. Waiting for the recovery to finish before letting go of the aim avoids the twitch.
---

## Changes in 1.9.4-mod103

- **Knife sprint**: holding the run input during a slash moves at the game's own run speed in the
  forward direction, and at the aim-walk speed in every other direction.  No invented multiplier -
  the previous attempt (walk speed x 2.6) made the ordinary forward run twice as fast.
- **Knife swing no longer stalls**: it feeds the leg layer (it used to fight it), so the swing can
  be moved and steered instead of freezing the character.
- **Weapon switching does not stutter**: aid 0x20 is the weapon change, not the throw, and is no
  longer treated as a thrown item.
- **Aim is ignored during interactions, melee, vaults and climbs** (busy action range plus the
  0x80000000 sentinel).
- **Thrown item holds the character still**: the stick is withheld and the gun stays raised while
  the thrown-item action plays, so the character does not walk off during it.

### Known issue

The legs twitch while the thrown item plays out.  Five different approaches were tried (clearing
the direction bits only, switching the leg layer off, zeroing the stick at the source, a 30 frame
dead window, and restricting the window to the throw action): each one either kept the twitch or
made the character unable to move at all.  The motion comes from the engine's own animation blend,
which the mod cannot address from the pad side.
### 1.9.4-mod104

- Weapon switching no longer aims (and then reloads) by itself: aid 0x20 is the weapon change, and it is
  no longer treated as the thrown item, so the mod no longer forces the gun-ready bit through a switch.