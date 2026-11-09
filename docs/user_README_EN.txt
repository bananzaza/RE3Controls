RE3Controls for Resident Evil 5 - Resident Evil 3 (2020) style controls
=======================================================================

What the mod does
-----------------
- Gamepad and keyboard/mouse: everything below works with both (keyboard/mouse: see that section).
- RE3-style camera: the right stick always orbits the camera around your character, even while
  running. The camera sits over the shoulder with the character slightly left of centre. Look up and
  the camera drops and moves closer, look down and it rises. It stays where you leave it; R3 puts it
  back behind you. The left stick moves the character relative to the camera. On boats, lifts,
  moving floors and conveyors the camera rides along (and turns with a turning boat).
- Walk while aiming: hold LT (or LB for the knife) and walk in any direction, diagonals included.
  The legs walk while the upper body keeps aiming, with footstep sounds; you can shoot and reload on
  the move. The forward/back and sideways walks are blended by the direction, so the legs change
  direction smoothly; the spine takes up the turn of the hips, so the waist does not bend at one point.
  Two paces: normal and fast (press RB while aiming). Aiming straight out of a run does not stop
  you, you carry on at the fast pace. Walking with a gun, with the knife and while reloading can be
  switched on and off separately. Looking through a scope (sniper rifles, rocket launchers) you stand
  still by default, as in the original game: the left stick zooms there (control type D). With
  ScopeWalk=1 you walk in the scope too and zoom with RB held + the left stick.
- Reload on the move: X reloads straight away while walking or running (the gun is not raised
  first) without stopping you, and the camera stays normal. Running, the legs keep running; walking,
  they keep walking.
- Switch weapons on the move: drawing or switching a weapon (d-pad, 1-9, the inventory) no longer
  stops you.
- Crosshair instead of the laser sight (gamepad and keyboard/mouse): a neat crosshair in the centre
  of the screen, green, red on an enemy, as in RE3. Bullets go exactly where it points at any distance, and there
  is no weapon sway ("breathing"), like with a mouse. Each shot opens the crosshair a little,
  automatic weapons also kick the aim up. Horizontal aiming turns the view at once instead of first
  moving only the arms (with the mouse too: the body turns with the aim, no trembling), and the aim
  camera keeps up with the aim instead of trailing behind it. Gamepad aim speed can be raised or
  lowered separately for both axes.
- Shoulder swap: L3 while aiming moves the camera to the other shoulder (and back).
- Faster knife: knife attacks are 1.5x faster (adjustable).
- Walk and run with the inventory open: the left stick moves you (L3 runs), the d-pad moves the
  inventory cursor.
- Damage: the original game's by default. Damage=3.0 gives RE3-like damage (three handgun headshots
  or four body shots kill a common Majini, the knife/machete takes five slashes): one multiplier for
  all player weapons (plus an extra one for the knife) instead of the game's floating difficulty
  scaling. Damage you take is unchanged.
- Optional balance and look settings: rate of fire of all guns (FireRate), walking and running speed
  (MoveWalkSpeed, MoveRunSpeed), no yellow colour filter (NoColorFilter, on by default).
- Free camera during melee attacks (MeleeCamera, on by default), and a separate "melee camera only"
  mode that leaves the rest of the game original (see below).
- The original game's own moves still work on the new layout: Wesker's dash in The Mercenaries,
  the taunt, the knife straight from the gun aim, the reload with LT + A, night vision in the
  RPG-7 NVS scope and turning while crouched behind low cover (see "Original moves" below).
- RE3-style layout (Xbox gamepad):

    LT ............ aim (the character turns to where the camera looks)
    RT ............ fire (with the knife: slash; also the game's fire button outside the aim)
    LB ............ knife (hold), RT to slash; pressed while aiming: the knife from the gun aim
    A ............. action: open, pick up, finishers (the on-screen prompt shows A); LT + A reloads
    X ............. one-button reload (no need to aim; works while walking and running)
    B ............. partner command; B with the stick toward the camera = 180 turn (the camera
                    swings round behind you)
    Y ............. inventory (the left stick keeps walking while it is open)
    RB ............ run (toggle: press to run, let go of the stick to stop);
                    while aiming: fast / normal pace
    R3 click ...... camera behind you (for Wesker in The Mercenaries also his dash)
    R3 + L3 ....... taunt (hold R3, click L3)
    L3 click ...... locate partner; while aiming: swap shoulder
    Back (View) ... map
    D-pad ......... quick weapon select (as in the original)
    Start ......... pause

  RB runs (and switches the pace while aiming), L3 locates the partner (and swaps the shoulder
  while aiming). RunButton=L3 swaps them: L3 runs, RB locates the partner. One press never does both.
  In the inventory the left stick walks (L3 runs) and the d-pad moves the cursor; A opens the item
  menu and RB shows your partner's items. InventoryMove=0 gives the original inventory (you stand
  still, the stick moves the cursor). Menus and QTEs work as in the original: in QTEs press what is
  shown on screen.

Installation
------------
1. Copy xinput1_3.dll and RE3Controls.ini from the RE5_RE3Controls folder in the archive into the
   game folder, next to re5dx9.exe: Steam -> Resident Evil 5 -> Manage -> Browse local files.
   Updating: replace the DLL. Old RE3Controls.ini files keep working; take the new one to see the
   new settings. Note: an ini from v1.6 or older keeps Damage=3.0 (the new default is 0, the
   original damage).
2. In the game: Options -> Controls -> control type D (the default).
3. Play with an Xbox gamepad. Steam Input can stay as it is.

Uninstall: delete xinput1_3.dll (and RE3Controls.ini) from the game folder.

Original moves
--------------
The RE3 layout keeps the original game's own moves where they have a meaning:
- Wesker's dash (The Mercenaries): R3 when not aiming starts it (it also puts the camera behind
  him). During the dash the right stick steers, A + the right stick left/right/back turns, RT is the
  Knee Cannon, and the game's own camera follows the dash. Keyboard/mouse: the game's keys (Left
  Shift + X starts it, Shift + A/D/S turns, the left mouse button is the Knee Cannon).
- The taunt: hold R3 and click L3 (that click then does not run or swap the shoulder). Keyboard: the
  game's own taunt key.
- R3 while aiming goes to the game: the RPG-7 NVS scope switches its night vision.
- LB pressed while aiming readies the knife straight from the gun aim; let go of LB to go back to the
  gun.
- LT + A reloads while aiming, as in the original (X still reloads anywhere).
- In cover (low or wall) the game's own camera and sticks are used, so the right stick also turns the
  character, as in the original.
Each of these has a setting (WeskerDash, StickClicks, KnifeFromAim); 0 gives the v1.8
behaviour.

Melee camera only (separate mode)
---------------------------------
Set Mode=MeleeCamera in RE3Controls.ini ([Controls]). Everything is then the original game -
original buttons, camera, laser, aiming, damage, knife - except that during melee attacks
(A on a staggered enemy, finishers, kicks, stomps, helping your partner) the attack cinematic is
skipped and the right stick turns the camera. When the attack ends, the camera turns back to where
the game's camera looks and hands over. Of [Camera] it uses Speed, PitchSpeed, InvertY, Distance,
Height, Shoulder, FollowPlatform and GrabCamera (AimDistance and the fields of view stay the game's).
Nothing extra to install: it is one setting of the same mod. Not to be confused with MeleeCamera=1 in
[Camera], which keeps the full mod and only adds the free melee camera. In co-op both players should use
the same Mode.

Keyboard and mouse
------------------
Everything works with keyboard and mouse too, the same way as with the gamepad. The game switches
between gamepad and keyboard/mouse by itself, to whatever you used last, and the mod follows it: you can
switch at any moment.
- The mouse turns the camera; WASD move relative to it. While aiming the crosshair stays in the centre
  and the mouse turns the aim directly (no dead zone); WASD walk while aiming.
- The keys are the game's own: change them in the game (Options -> Controls -> keyboard and mouse).
  With the defaults:

    W A S D ....... move (relative to the camera)
    Mouse ......... camera; while aiming: the aim
    Right mouse ... aim
    Left mouse .... fire (with the knife: slash)
    Space ......... knife (hold)
    Shift ......... run (hold; Run=Toggle in [Keyboard] to press once); while aiming: fast / normal pace;
                    pressed while standing in the inventory: confirm (as in the game)
    R ............. reload (works while walking and running)
    C ............. quick turn (180; the camera swings round behind you)
    F ............. action: open, pick up, finishers (as in the game)
    V ............. partner command
    Q ............. locate partner; while aiming: swap shoulder
    E ............. inventory (WASD keep walking, Shift runs; click items with the mouse)
    M ............. map
    Middle mouse .. camera behind you
    1-9, Esc ...... item slots, pause (as in the game)
    Left Shift + X  Wesker's dash in The Mercenaries (as in the game)

- Mouse speed: the game's own mouse sensitivity option, times MouseSensitivity in [Keyboard]. The camera
  and the aim turn by the same angle for the same mouse movement. MouseVertical sets the vertical speed;
  invert with the game's own option.
- Nudging the mouse while playing with the gamepad does not switch to keyboard/mouse: that takes a real
  mouse movement (MouseSwitchCounts). A key or a mouse button switches at once, a gamepad button back.
- Menus, the inventory screen, QTEs, cover, mounted guns and vehicles work as in the game.
- KeyboardMouse=0 in [Keyboard] gives the game's own keyboard/mouse controls (as up to v1.7).

Online co-op
------------
- Both players should install the mod, v1.7 or newer (it does not exchange data with older versions). Then your partner sees your legs walking while you aim, your faster knife, and your
  bullets hit the crosshair on their side too.
- Enemy damage is applied by the machine the enemy is chasing, so both players need the damage
  multiplier: on the guest the mod automatically uses the host's Damage, KnifeDamage and FireRate.
  Each player's own walking/running speed (MoveWalkSpeed, MoveRunSpeed) is shown on the other side.
- If only one player has the mod, the other still gets your position and facing more often than
  usual, but sees you slide instead of walk while you aim.
- Apart from Damage and FireRate the mod does not change game rules: ammo, health and money are as
  in the original game.

Settings (RE3Controls.ini, applied while the game is running)
-------------------------------------------------------------
    Mode=MeleeCamera          only the melee camera, everything else original (see above)
    Run=Toggle|Hold|Stock     run: toggle with RB / hold RB (L3 with RunButton=L3) / original (hold A)
    RunButton=L3              run with L3 instead of RB (RB then locates the partner / swaps shoulder)
    InteractButton=X          the prompts (open, pick up, finishers) stay on X as in the original game;
                              X reloads when no prompt is on screen (default A, as in RE3)
    InventoryMove=0           the original inventory: you stand still and the stick moves the cursor
    WeskerDash=0              Wesker: R3 only puts the camera behind him (no dash with the gamepad)
    StickClicks=0             no taunt (R3 + L3) and no R3 while aiming: L3 and R3 are only the mod's
    KnifeFromAim=0            LB does nothing while aiming (let go of LT first, as in v1.8)
    Speed=180                 horizontal camera speed (degrees per second)
    PitchSpeed=90             vertical camera speed
    InvertY=1                 invert the camera vertically
    Distance=230              camera distance behind the shoulder (cm)
    Height=150                height of the point the camera orbits around
    Shoulder=60               how far the camera is shifted toward the shoulder
    Tilt=8                    default camera tilt (degrees, + = looking down); R3 returns to it
    AimDistance=100           aiming: the camera this many cm farther back (0-200; 0 = the game's own aim
                              camera), e.g. 60 for a roomier
                              view; the crosshair and where shots land stay the same (not in the sniper or
                              rocket launcher scope, on gun emplacements or vehicle guns)
    FieldOfView=0             field of view, degrees added to the game's 45 (-15 to 40), e.g. 15 = 60
    AimFieldOfView=0          the same while aiming (the game's 33-40); not in the sniper or rocket
                              launcher scope, on gun emplacements or vehicle guns
    TurnSpeed=720             how fast the character turns to follow the stick
    BackStep=1                stick toward the camera (S) without running: step back facing away from the
                              camera, as in the original game, instead of turning round (RE3 Remake);
                              also while reloading or switching on the move and in the inventory
    FollowPlatform=0          on a turning boat the camera keeps its world direction instead
    MeleeCamera=0             the game's attack cinematics during melee attacks (default 1: no
                              cinematics, the camera stays free)
    GrabCamera=0              the game's grab cinematics (default 1: no cinematics, the camera stays
                              free while an enemy holds you; also with Mode=MeleeCamera); deadly grabs
                              (chainsaw, Executioner), bosses and giants keep the game's camera
    GunWalk=0                 shoot standing: no walking while aiming a gun (as in the original)
    KnifeWalk=0               no walking with the knife
    ReloadWalk=0              reload standing when you press X without aiming (the camera still stays
                              normal, no zoom); a reload while holding LT follows GunWalk
    WalkSpeed=1.125           walking pace while aiming or holding the knife (1.0 = normal walk)
    FastWalkSpeed=1.875       fast pace while aiming or holding the knife (RB)
    SwitchWalk=0              stand still while drawing or switching a weapon (as in the original)
    ScopeWalk=1               walk in the scope of sniper rifles and rocket launchers too; zoom with RB
                              held + the left stick (the legs stop meanwhile); keyboard: the mouse wheel.
                              Control types B and D (in A and C the left stick aims there)
    AimSpeedX=1.0             gamepad aim speed, horizontal: 2.0 = twice as fast, 0.5 = half (0.25-4)
    AimSpeedY=1.0             the same, vertical. On top of the game's own Aim Speed option; also in the
                              sniper scope (the game still slows the aim with the zoom)
    AimCameraLag=0.15         gamepad (crosshair or laser): how far the aim camera trails the aim:
                              1.0 = the original (about 0.14 s behind), 0 = not at all
    KnifeSpeed=1.5            knife attack speed (1.0 = original)
    Crosshair=0               back to the laser sight (also turns off DirectHorizontal)
    CrosshairSize=1.0         crosshair size (default 0.5): 1.0 = the game's own, 2.0 = twice as big
                              (0.25-4); the opening on a
                              shot grows and shrinks with it
    DirectHorizontal=0        back to the original horizontal arm-only aim zone (the view does not turn)
    CrosshairKick=1.0         how far the crosshair opens on a shot (2.0 = twice, 0 = not at all, also not
                              for shotguns and magnums)
    KnifeCrosshair=0          keyboard/mouse: hide the game's small reticle while the knife is out
                              (the gamepad has none)
    Breathing=1               bring back the aim sway (breathing) of the original game, gamepad and mouse
    Recoil=0                  automatic weapons do not kick the aim
    Damage=0                  player damage: 0 = the original game (default), 3 = like RE3,
                              1.4 = about the start of Normal in the original, 1.0 = a little weaker
    KnifeDamage=2.0           extra multiplier for the knife and machete (only when Damage is not 0)
    FireRate=1.0              rate of fire of all guns: 2.0 = twice as fast (0.5-3); not the knife or
                              grenades
    ShotgunSpread=1.0         shotgun pellet spread: 1.0 = the original (default), 0.5 = half as wide,
                              0 = all pellets in one line (very strong at range); all shotguns, in
                              co-op the host's value (both players need v1.8 or newer)
    MoveWalkSpeed=1.0         walking speed outside the aim (0.5-2; 1.0 = original)
    MoveRunSpeed=1.0          running speed (0.5-2)
    NoColorFilter=0           keep the yellow colour tint of the levels (default 1: removed; the game's own
                              Special Settings filters still work)
    HookDelayMs=3000          extra wait before the mod changes the game at start (see below)
    KeyboardMouse=0           keyboard/mouse: the game's own controls instead of the mod's
    Run=Hold                  keyboard: hold the run key (default Toggle: press once; in [Keyboard])
    MouseSensitivity=1.0      mouse speed for the camera and the aim, on top of the game's own option
                              (default 0.75; higher = faster)
    MouseVertical=1.0         vertical mouse speed relative to horizontal
    MouseAim=Game             keyboard aiming: the aim style set in the game's options, with the game's
                              own reticle and arm-only aim zone (RE3 = the mod's centred crosshair)
    MouseSwitchCounts=30      mouse movement needed to switch from the gamepad (0 = any movement)
    FreeCamera=0              back to the original camera
    RemapButtons=0            back to the original button layout (set OneButtonReload=0 too, or X
                              also reloads)
    OneButtonReload=0         X does not reload (LT + A still reloads while aiming)
    Enabled=0                 turn the whole mod off
  Every setting is also listed with a comment in RE3Controls.ini itself.

Compatibility
-------------
- Steam version of Resident Evil 5 Gold Edition 1.2.0 (latest patch), Windows 10 or newer.
- Conflict: RE5Fix also installs as xinput1_3.dll. If you use it, rename its DLL to dinput8.dll
  (as RE5Fix itself suggests) or remove it. Do not rename this mod's DLL: the game only loads it as
  xinput1_3.dll. Use only one colour filter switch: NoColorFilter here or RE5Fix's own (RE5Fix's also
  removes the Special Settings filters). RE5Fix was last updated before the current 1.2.0 build; if
  the game will not start with both, try without RE5Fix.
- The mod writes a log, RE3Controls.log, in the game folder (useful if something goes wrong).

"Program integrity check failed" at start
-----------------------------------------
This message comes from the game's own copy protection (Enigma Protector): it closes the game when
something changes the game's code while it is still starting. v1.7 waits until the game has
completely started (its code is unpacked, its own window is up and it has read the gamepad) and then
3 more seconds before it changes anything; older versions could start too early on some PCs.
If it still happens: set HookDelayMs=10000 in [Advanced], verify the game files in Steam, try
without other mods (RE5Fix, ASI loaders, d3d9/dinput8 DLLs) and send RE3Controls.log from the game
folder with your report.

Antivirus warnings
------------------
The DLL is not signed and it changes the game's code in memory (that is how every mod of this kind
works), so some antivirus programs or Windows Smart App Control may warn about it or block it.
What the DLL does: it forwards the XInput calls to Windows' own XInput, hooks game functions inside
re5dx9.exe, reads RE3Controls.ini and writes RE3Controls.log in the game folder. It opens no network
connections of its own (co-op data goes through the game's own Steam networking) and touches no other
program. Since v1.6 the release DLL carries version information (Properties -> Details) and contains
no developer tools.
SHA-256 of the v1.9.4 xinput1_3.dll: @SHA256@
If you get a warning, please report the exact text, the program that showed it, the detection name
and the SHA-256 of your file (PowerShell: Get-FileHash "<game folder>\xinput1_3.dll").

Known limitations
-----------------
- Tutorial texts ("press A to run") are still the original ones.
- Only you see the crosshair; in your partner's game your laser sight looks as before.
- With MeleeCamera=1 the game's small HUD sub-screen stays visible during finishers.
- With InventoryMove=1 the inventory cursor moves with the d-pad only.
- Mouse speed was set up on a slow test PC; if the camera or the aim feels too fast or too slow, change
  MouseSensitivity (or the game's own mouse sensitivity).
- Walking while aiming has footstep sounds but no footstep dust or water ripples.
- A device-change notice from the game itself appears for a moment whenever you switch.
