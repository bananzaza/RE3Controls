#pragma once

enum class RunMode { Stock, Toggle, Hold };
enum class RunButton { L3, RB };

struct Config {
    bool enabled = true;
    bool remapButtons = true;     // A = interact, X = reload, Back = map, L3 = run, R3 = camera reset
    bool interactX = false;       // InteractButton=X: the prompts stay on X (as in the game), not A
    RunMode runMode = RunMode::Toggle;
    RunButton runButton = RunButton::RB;  // RB runs, L3 locates the partner (L3: the other way round)
    bool oneButtonReload = true;
    bool quickTurn = true;          // B + stick back
    bool weskerDash = true;         // Wesker's dash: R3 (not aiming) to the game, right stick/A/RT during the dash
    bool stickClicks = true;        // R3 held + L3 click = taunt; R3 while aiming goes to the game (RPG-7 NVS)
    bool knifeFromAim = true;       // LB pressed while LT is held readies the knife from the gun aim
    bool inventoryMove = true;      // walk/run with the inventory open (its cursor on the d-pad only)
    bool freeCamera = true;         // right stick orbits the camera, movement is camera-relative
    float cameraSpeed = 180.0f;     // degrees per second at full stick
    float cameraPitchSpeed = 90.0f;   // vertical look speed (90 = about one second over the whole range)
    float cameraDistance = 230.0f;    // orbit camera: distance behind the shoulder pivot (cm)
    float cameraHeight = 150.0f;      // pivot height above the feet
    float cameraShoulder = 60.0f;     // sideways shift over the shoulder
    float cameraDefaultPitch = 8.0f;  // degrees, + = looking down; R3 returns here
    float aimDistance = 100.0f;       // aiming: the camera this many cm farther back along its view (0..200)
    float fieldOfView = 0.0f;         // degrees added to the game's camera fov outside the aim (-15..40)
    float aimFieldOfView = 0.0f;      // the same while aiming (-15..40)
    bool invertCameraY = false;
    float turnSpeed = 720.0f;       // character turn rate toward the stick direction, degrees per second
    bool backStep = false;          // toward the camera without running: step back facing away from it
    bool followPlatform = true;     // on a turning boat or platform the orbit turns with it (like the game camera)
    bool meleeCamera = true;        // melee attacks keep the normal camera (no attack cinematic), right stick turns it
    bool grabCamera = true;         // enemy grabs keep the normal camera (no grab cinematic), right stick turns it
    bool meleeOnly = false;         // Mode=MeleeCamera: only the melee camera, everything else as in the original game
    bool gunWalk = true;            // walk while aiming a gun
    bool scopeWalk = true;          // walk in the sniper / rocket launcher scope (zoom on RB + left stick)
    bool shoulderAuto = true;       // pick the shoulder from the character (Chris left, Sheva right by default)
    bool shoulderSwapAlways = true; // the shoulder key works outside the aim too (knife stays on LB)
    char shoulderKey[8] = "B";      // keyboard key that swaps the shoulder (letters A-Z, read raw)
    unsigned shoulderLeftMask = 1u; // bit per character kind (0x715B90) whose CHARACTER is on the LEFT of the screen
    unsigned shoulderGameCharLeftMask = 0u; // bit per kind: the GAME's own aim side puts the character on the LEFT (RE5: none)
    bool zoomKeys = true;           // F zooms in and C zooms out while aiming a plain gun
    float zoomFovStep = -20.0f;     // degrees of fov per zoom step (negative = closer)
    int zoomMaxStep = 3;            // highest zoom step (3 = three presses, about -60 degrees)
    bool scopeLook = false;         // EXPERIMENTAL, off: writing the aim state per frame freezes the scope pitch
    bool stockMeleeCamera = true;
    bool aimPitchSnap = false;      // EXPERIMENTAL, off: nudge the aim pitch on press (it biased the aim downwards per press)
    bool coverShoulder = true;      // keep the shoulder choice (and its aim mirror) while in cover
    bool laserFill = false;         // EXPERIMENTAL, off: fill the uLaserSight object for the keyboard path
    bool aimPitchFromMouse = false;  // the mod drives the aim pitch and writes it into cam::LookV every frame
    bool aimCamera = false;          // keep the mod's camera while aiming (no view jump, the crosshair stays centred)
    bool knifeWalk = true;          // walk with the knife readied
    bool reloadWalk = true;         // reload on the move (otherwise standing, camera stays free)
    bool switchWalk = true;         // keep walking while switching weapons
    float aimWalkAnimSpeed = 1.125f;  // playback rate of the walk/strafe clip (its root motion moves the character)
    float aimWalkFastSpeed = 1.875f;  // fast walk (L3 while aiming)
    float knifeSpeed = 1.5f;        // knife attack speed
    bool crosshair = true;
    float crosshairSize = 0.5f;     // crosshair scale (0.25..4): 1 = the game's own mouse crosshair
    bool directHorizontal = true;   // horizontal stick turns the aim at once, not the arms first
    float aimSpeedX = 1.0f;         // pad aim speed multipliers on top of the game's option (0.25..4)
    float aimSpeedY = 1.0f;
    float aimCameraLag = 0.15f;     // aim camera smoothing: 1 = the game's pad aim camera (~0.14 s behind), 0 = none
    bool knifeCrosshair = true;     // keyboard/mouse: the game's own reticle while the knife is out (pad: none)
    float crosshairKick = 1.0f;     // how far the crosshair opens on a shot (0 = not at all)
    bool breathing = false;         // aim sway like a gamepad in the original; off = steady like a mouse
    bool padRecoil = true;          // automatic weapons kick the aim like they do with a mouse
    float damage = 0.0f;            // fixed multiplier for all damage the players deal (0 = the game's own)
    float knifeDamage = 2.0f;       // extra multiplier for the knife / machete
    float fireRate = 1.0f;          // rate of fire of all guns (0.5..3)
    float shotgunSpread = 1.0f;     // shotgun pellet spread: 1 = original, 0 = all pellets in one line
    float moveWalk = 1.0f;          // walking / running speed outside the aim (0.5..2)
    float moveRun = 1.0f;
    bool noColorFilter = true;      // remove the yellow area colour grade
    bool coopSync = true;           // keep the co-op partner's view in step (extra heartbeats, leg clip)
    unsigned hookDelayMs = 3000;      // wait this long after the game window appears before hooking the game
    // keyboard/mouse
    bool keyboardMouse = true;      // the mod's controls also on keyboard/mouse (0 = the game's own)
    RunMode kbRunMode = RunMode::Toggle;
    float mouseSensitivity = 0.75f;  // camera and aim, on top of the game's mouse sensitivity option
    float mouseVertical = 1.0f;     // vertical mouse speed relative to horizontal
    bool mouseAimRE3 = true;        // aim with the crosshair in the centre (the game's mouse-look aim style)
    int mouseSwitchCounts = 30;     // mouse counts that switch a pad player to keyboard/mouse (0 = the game's)
};

const Config& Cfg();
void LoadConfig();
void PollConfig();  // reload when the ini changed
