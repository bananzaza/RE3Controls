#include "common.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>

static Config g_cfg;

const Config& Cfg() { return g_cfg; }

static const char kDefaultIni[] =
    "; RE3Controls for Resident Evil 5 - settings (1 = on, 0 = off)\r\n"
    "\r\n"
    "[Controls]\r\n"
    "\r\n"
    "; Master switch for the whole mod\r\n"
    "          Enabled=1\r\n"
    "\r\n"
    "; Mode: Full = everything below\r\n"
    "; MeleeCamera = original game, except free camera during melee attacks\r\n"
    "; (uses Speed, PitchSpeed, InvertY, Distance, Height, Shoulder, FollowPlatform and GrabCamera from [Camera];\r\n"
    "; AimDistance, FieldOfView and AimFieldOfView stay the game's)\r\n"
    "          Mode=Full\r\n"
    "\r\n"
    "; 1 = RE3 layout on gamepad:\r\n"
    ";     A = interact, X = reload, Back = map, L3 = run, R3 = camera reset,\r\n"
    ";     RB = locate partner (swaps shoulder while aiming), LT = aim, LB = knife, RT = fire\r\n"
    "; 0 = original control type D buttons\r\n"
    "          RemapButtons=1\r\n"
    "\r\n"
    "; Run behaviour: Toggle / Hold / Stock\r\n"
    "          Run=Toggle\r\n"
    "\r\n"
    "; Which button runs when RemapButtons=1: L3 or RB\r\n"
    "          RunButton=RB\r\n"
    "\r\n"
    "; Interact button when RemapButtons=1: A (RE3 style) or X (original)\r\n"
    "          InteractButton=A\r\n"
    "\r\n"
    "; 1 = X reloads from almost any state (standing, walking, running, cover, aim)\r\n"
    "; 0 = X does not reload (LT+A still works while aiming)\r\n"
    "          OneButtonReload=1\r\n"
    "\r\n"
    "; 1 = B + stick back = quick turn\r\n"
    "; 0 = no quick turn\r\n"
    "          QuickTurn=1\r\n"
    "\r\n"
    "; Wesker's dash behaviour (The Mercenaries)\r\n"
    "; 1 = original game behaviour\r\n"
    "; 0 = R3 only recenters camera (no dash on gamepad)\r\n"
    "          WeskerDash=1\r\n"
    "\r\n"
    "; Original stick-click moves (taunt with R3+L3, etc.)\r\n"
    "          StickClicks=1\r\n"
    "\r\n"
    "; LB (knife) while LT (aim) is held\r\n"
    "; 1 = knife comes out from gun aim (original)\r\n"
    "; 0 = LB does nothing while aiming\r\n"
    "          KnifeFromAim=1\r\n"
    "\r\n"
    "; 1 = walk/run while inventory is open (cursor moves with d-pad)\r\n"
    "; 0 = character stops, stick moves the cursor\r\n"
    "          InventoryMove=1\r\n"
    "\r\n"
    "\r\n"
    "[Camera]\r\n"
    "\r\n"
    "; 1 = free over-the-shoulder camera (right stick orbits, left stick is camera-relative)\r\n"
    "; 0 = original game camera\r\n"
    "          FreeCamera=1\r\n"
    "\r\n"
    "; Camera turn speeds\r\n"
    "          Speed=180\r\n"
    "          PitchSpeed=90\r\n"
    "          InvertY=0\r\n"
    "\r\n"
    "; Camera placement (RE3 style)\r\n"
    "          Distance=230\r\n"
    "          Height=150\r\n"
    "          Shoulder=60\r\n"
    "          Tilt=8\r\n"
    "\r\n"
    "; Extra distance while aiming (cm). 0 = game's own aim camera\r\n"
    "          AimDistance=100\r\n"
    "\r\n"
    "; Field of view offset (degrees) added to the game's values\r\n"
    "          FieldOfView=0\r\n"
    "          AimFieldOfView=0\r\n"
    "\r\n"
    "; How fast the character turns to face the camera direction\r\n"
    "          TurnSpeed=720\r\n"
    "\r\n"
    "; Moving toward the camera without running:\r\n"
    "; 0 = turn around and walk toward camera (RE3/RE2/RE4 Remake style)\r\n"
    "; 1 = step backward while facing away (original RE5 style)\r\n"
    "          BackStep=0\r\n"
    "\r\n"
    "; Camera follows rotating boats / moving platforms\r\n"
    "          FollowPlatform=1\r\n"
    "\r\n"
    "; 1 = keep free camera during melee attacks\r\n"
    "; 0 = use the game's cinematic attack camera\r\n"
    "          MeleeCamera=1\r\n"
    "\r\n"
    "; 1 = keep free camera during enemy grabs\r\n"
    "; 0 = use the game's cinematic grab camera\r\n"
    "          GrabCamera=1\r\n"
    "\r\n"
    "\r\n"
    "[Aim]\r\n"
    "\r\n"
    "; Walk while aiming / knife / reloading / switching weapons\r\n"
    "          GunWalk=1\r\n"
    "          KnifeWalk=1\r\n"
    "          ReloadWalk=1\r\n"
    "          SwitchWalk=1\r\n"
    "\r\n"
    "; Walk while looking through a scope (sniper / rocket launcher)\r\n"
    "          ScopeWalk=1\r\n"
"          ; Shoulder from the character: the bit means the CHARACTER stands on the LEFT of the screen\r\n"
"          ShoulderAuto=1\r\n"
"          ; 1 = the shoulder key (RB / Q) works outside the aim too; the knife stays on LB\r\n"
"          ShoulderSwapAlways=1\r\n"
"          ; keyboard key that swaps the shoulder (letters A-Z)\r\n"
"          ShoulderKey=B\r\n"
"          ; bit 0 = Chris (character on the left), bit 1 = Sheva (character on the right by default)\r\n"
"          ShoulderLeftKinds=0\r\n"
"          ; sides the GAME itself uses (bit per kind): default bit 0 = Chris aims with the character left, Sheva right\r\n"
"          ShoulderGameLeftKinds=\r\n"

    "\r\n"
    "; Walking speeds while aiming (1.0 = normal walk)\r\n"
    "          WalkSpeed=1.125\r\n"
    "          FastWalkSpeed=1.875\r\n"
    "\r\n"
    "; Knife attack speed multiplier\r\n"
    "          KnifeSpeed=1.5\r\n"
    "\r\n"
    "; Centred crosshair instead of laser sight\r\n"
    "          Crosshair=1\r\n"
    "\r\n"
    "; Size of the mod's crosshair (0.25 \342\200\223 4.0)\r\n"
    "          CrosshairSize=0.5\r\n"
    "\r\n"
    "; 1 = horizontal aim turns view + body at once\r\n"
    "; 0 = original arm-only aim zone\r\n"
    "          DirectHorizontal=1\r\n"
    "\r\n"
    "; Gamepad aim speed multipliers (on top of the in-game Aim Speed option)\r\n"
    "          AimSpeedX=1.0\r\n"
    "          AimSpeedY=1.0\r\n"
    "\r\n"
    "; How much the camera lags behind the aim (0 = none, 1 = original pad lag)\r\n"
    "          AimCameraLag=0.15\r\n"
    "\r\n"
    "; How far the crosshair opens on each shot\r\n"
    "          CrosshairKick=1.0\r\n"
    "\r\n"
    "; Show the game's small knife reticle on keyboard/mouse\r\n"
    "          KnifeCrosshair=1\r\n"
    "\r\n"
    "; Aim sway (breathing)\r\n"
    "          Breathing=0\r\n"
    "\r\n"
    "; Automatic weapons kick the aim upward slightly\r\n"
    "          Recoil=1\r\n"
    "\r\n"
    "\r\n"
    "[Damage]\r\n"
    "\r\n"
    "; Damage multiplier (0 = original scaling, any other value = fixed multiplier)\r\n"
    "          Damage=0\r\n"
    "\r\n"
    "; Extra knife/machete multiplier (only used when Damage is not 0)\r\n"
    "          KnifeDamage=2.0\r\n"
    "\r\n"
    "; Rate of fire multiplier for all guns\r\n"
    "          FireRate=1.0\r\n"
    "\r\n"
    "; Shotgun pellet spread (1.0 = original, 0 = all pellets in a line)\r\n"
    "          ShotgunSpread=1.0\r\n"
    "\r\n"
    "\r\n"
    "[Movement]\r\n"
    "; EXPERIMENTAL, off: the mod replaces the native walk/run/back updates so it owns the step\r\n"
    "EngineMove=0\r\n"
    "\r\n"
    "; Walking / running speed outside of aiming\r\n"
    "          MoveWalkSpeed=1.0\r\n"
    "          MoveRunSpeed=1.0\r\n"
    "\r\n"
    "\r\n"
    "[Visual]\r\n"
    "\r\n"
    "; Remove the yellow colour tint of the levels\r\n"
    "          NoColorFilter=1\r\n"
    "\r\n"
    "\r\n"
    "[Advanced]\r\n"
    "\r\n"
    "; Milliseconds to wait after the game window appears before installing hooks\r\n"
    "          HookDelayMs=3000\r\n"
    "\r\n"
    "\r\n"
    "[Zoom]\r\n"
"; Plain-gun zoom while aiming (not the scope): F zooms in, C zooms out.\r\n"
"; ZoomFovStep: field of view degrees per step (negative = closer). ZoomMaxStep: highest step.\r\n"
"ZoomKeys=1\r\n"
"ZoomFovStep=-20\r\n"
"ZoomMaxStep=3\r\n"
"; EXPERIMENTAL (off): 1 = the mod drives the scope pitch.  It freezes the pitch on this build, keep 0.\r\n"
"ScopeLook=0\r\n"
"\r\n"
"[Keyboard]\r\n"
    "\r\n"
    "; Enable the mod's controls on keyboard + mouse\r\n"
    "          KeyboardMouse=1\r\n"
    "\r\n"
    "; Running on keyboard: Hold or Toggle\r\n"
    "          Run=Toggle\r\n"
    "\r\n"
    "; Mouse sensitivity (on top of the in-game setting)\r\n"
    "          MouseSensitivity=0.75\r\n"
    "          MouseVertical=1.0\r\n"
    "\r\n"
    "; Aim style: RE3 (centred crosshair) or Game (in-game setting)\r\n"
    "          MouseAim=RE3\r\n"
    "\r\n"
    "; How many mouse counts are needed to switch from gamepad to mouse\r\n"
    "          MouseSwitchCounts=30\r\n"
    "\r\n"
    "\r\n"
    "[Coop]\r\n"
    "\r\n"
    "; Extra position updates so the partner sees free-camera turns and aim-walking\r\n"
    "          Sync=1\r\n";

static bool GetBool(const char* path, const char* sec, const char* key, bool def) {
    return GetPrivateProfileIntA(sec, key, def ? 1 : 0, path) != 0;
}

static float GetFloat(const char* path, const char* sec, const char* key, float def) {
    char buf[64], dflt[64];
    sprintf(dflt, "%g", def);
    GetPrivateProfileStringA(sec, key, dflt, buf, sizeof(buf), path);
    return (float)atof(buf);
}

// A whole number like "0.5" or "0,5" (spaces around allowed); false for empty or other text.
static bool ParseNumber(char* buf, float* out) {
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    char* end = nullptr;
    const double v = strtod(buf, &end);
    if (end == buf) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    *out = (float)v;
    return v == v;
}

// A speed multiplier: "1,5" works like "1.5"; anything that is not a positive number means the default.
static float GetScale(const char* path, const char* sec, const char* key, float lo, float hi, float def = 1.0f) {
    char buf[64], dflt[32];
    sprintf(dflt, "%g", def);
    GetPrivateProfileStringA(sec, key, dflt, buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    const float v = (float)atof(buf);
    if (!(v > 0.0f)) return def;  // also NaN
    return v < lo ? lo : v > hi ? hi : v;
}

static FILETIME g_iniTime = {};

// Re-read the ini when it changes on disk (cheap: one attribute query per second).
void PollConfig() {
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last < 1000) return;
    last = now;
    char path[MAX_PATH];
    wsprintfA(path, "%sRE3Controls.ini", ModDir());
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &a)) return;
    if (CompareFileTime(&a.ftLastWriteTime, &g_iniTime) != 0) LoadConfig();
}

void LoadConfig() {
    char path[MAX_PATH];
    wsprintfA(path, "%sRE3Controls.ini", ModDir());
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, kDefaultIni, sizeof(kDefaultIni) - 1, &w, nullptr);
            CloseHandle(h);
        }
    }
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) g_iniTime = fa.ftLastWriteTime;
    // The profile API keeps the last ini file it read cached; drop it so edits made while the game runs apply.
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, path);
    // Only the first load is logged (the startup log); later reloads of the ini are silent.
    static bool firstLoad = true;
    const bool logIt = firstLoad;
    firstLoad = false;
    if (logIt) Log("config file: %s", path);
    Config c;
    c.enabled = GetBool(path, "Controls", "Enabled", c.enabled);
    c.remapButtons = GetBool(path, "Controls", "RemapButtons", c.remapButtons);
    char run[32];
    GetPrivateProfileStringA("Controls", "Run", "Toggle", run, sizeof(run), path);
    c.runMode = !_stricmp(run, "Hold") ? RunMode::Hold : !_stricmp(run, "Stock") ? RunMode::Stock : RunMode::Toggle;
    char runButton[32];
    GetPrivateProfileStringA("Controls", "RunButton", "RB", runButton, sizeof(runButton), path);
    c.runButton = !_stricmp(runButton, "RB") ? RunButton::RB : RunButton::L3;
    c.oneButtonReload = GetBool(path, "Controls", "OneButtonReload", c.oneButtonReload);
    c.quickTurn = GetBool(path, "Controls", "QuickTurn", c.quickTurn);
    c.weskerDash = GetBool(path, "Controls", "WeskerDash", c.weskerDash);
    c.stickClicks = GetBool(path, "Controls", "StickClicks", c.stickClicks);
    c.knifeFromAim = GetBool(path, "Controls", "KnifeFromAim", c.knifeFromAim);
    c.inventoryMove = GetBool(path, "Controls", "InventoryMove", c.inventoryMove);
    c.freeCamera = GetBool(path, "Camera", "FreeCamera", c.freeCamera);
    c.cameraSpeed = GetFloat(path, "Camera", "Speed", c.cameraSpeed);
    c.cameraPitchSpeed = GetFloat(path, "Camera", "PitchSpeed", c.cameraPitchSpeed);
    c.invertCameraY = GetBool(path, "Camera", "InvertY", c.invertCameraY);
    c.cameraDistance = GetFloat(path, "Camera", "Distance", c.cameraDistance);
    c.cameraHeight = GetFloat(path, "Camera", "Height", c.cameraHeight);
    c.cameraShoulder = GetFloat(path, "Camera", "Shoulder", c.cameraShoulder);
    c.cameraDefaultPitch = GetFloat(path, "Camera", "Tilt", c.cameraDefaultPitch);
    {
        char buf[64];
        GetPrivateProfileStringA("Camera", "AimDistance", "100", buf, sizeof(buf), path);
        float v = 0.0f;
        c.aimDistance = ParseNumber(buf, &v) && v > 0.0f ? (v > 200.0f ? 200.0f : v) : 0.0f;
    }
    {
        auto degrees = [&](const char* key) {
            char buf[64];
            GetPrivateProfileStringA("Camera", key, "0", buf, sizeof(buf), path);
            float v = 0.0f;
            if (!ParseNumber(buf, &v) || !(v == v)) return 0.0f;
            return v < -15.0f ? -15.0f : v > 40.0f ? 40.0f : v;
        };
        c.fieldOfView = degrees("FieldOfView");
        c.aimFieldOfView = degrees("AimFieldOfView");
    }
    c.turnSpeed = GetFloat(path, "Camera", "TurnSpeed", c.turnSpeed);
    c.backStep = GetBool(path, "Camera", "BackStep", c.backStep);
    c.followPlatform = GetBool(path, "Camera", "FollowPlatform", c.followPlatform);
    c.meleeCamera = GetBool(path, "Camera", "MeleeCamera", c.meleeCamera);
    c.grabCamera = GetBool(path, "Camera", "GrabCamera", c.grabCamera);
    // AimWalk (up to v1.4) is the default for the three separate switches.
    const bool aimWalk = GetBool(path, "Aim", "AimWalk", true);
    c.gunWalk = GetBool(path, "Aim", "GunWalk", aimWalk);
    c.knifeWalk = GetBool(path, "Aim", "KnifeWalk", aimWalk);
    c.reloadWalk = GetBool(path, "Aim", "ReloadWalk", aimWalk);
    c.switchWalk = GetBool(path, "Aim", "SwitchWalk", aimWalk);
    c.scopeWalk = GetBool(path, "Aim", "ScopeWalk", c.scopeWalk);
    c.shoulderAuto = GetBool(path, "Camera", "ShoulderAuto", c.shoulderAuto);
    c.shoulderSwapAlways = GetBool(path, "Camera", "ShoulderSwapAlways", c.shoulderSwapAlways);
    GetPrivateProfileStringA("Controls", "ShoulderKey", "B", c.shoulderKey, sizeof(c.shoulderKey), path);
    {
        char km[64];
        GetPrivateProfileStringA("Camera", "ShoulderLeftKinds", "0", km, sizeof(km), path);
        unsigned mask = 0;
        for (const char* p = km; *p;) {
            while (*p == ' ' || *p == ',' || *p == ';') ++p;
            if (*p < '0' || *p > '9') { ++p; continue; }
            int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            if (v >= 0 && v < 32) mask |= 1u << v;
        }
        if (mask) c.shoulderLeftMask = mask;
    }
    {
        char km[64];
        GetPrivateProfileStringA("Camera", "ShoulderGameLeftKinds", "", km, sizeof(km), path);
        unsigned mask = 0;
        for (const char* p = km; *p;) {
            while (*p == ' ' || *p == ',' || *p == ';') ++p;
            if (*p < '0' || *p > '9') { ++p; continue; }
            int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            if (v >= 0 && v < 32) mask |= 1u << v;
        }
        c.shoulderGameCharLeftMask = mask;  // empty / none = every character is game-right, which is RE5's default
    }
    c.zoomKeys = GetBool(path, "Zoom", "ZoomKeys", c.zoomKeys);
    c.zoomFovStep = GetFloat(path, "Zoom", "ZoomFovStep", c.zoomFovStep);
    c.zoomMaxStep = (int)GetFloat(path, "Zoom", "ZoomMaxStep", (float)c.zoomMaxStep);
    c.scopeLook = GetBool(path, "Zoom", "ScopeLook", c.scopeLook);
    c.laserFill = GetBool(path, "Zoom", "LaserFill", c.laserFill);
    c.aimCamera = GetBool(path, "Camera", "AimCamera", c.aimCamera);
    c.aimPitchFromMouse = GetBool(path, "Aim", "AimPitchFromMouse", c.aimPitchFromMouse);
    c.aimPitchSnap = GetBool(path, "Aim", "AimPitchSnap", c.aimPitchSnap);
    c.stockMeleeCamera = GetBool(path, "Camera", "StockMeleeCamera", c.stockMeleeCamera);
    c.coverShoulder = GetBool(path, "Controls", "CoverShoulder", c.coverShoulder);
    {
        char ib[16];
        GetPrivateProfileStringA("Controls", "InteractButton", "A", ib, sizeof(ib), path);
        c.interactX = !_stricmp(ib, "X");
    }
    c.aimWalkAnimSpeed = GetFloat(path, "Aim", "WalkSpeed", c.aimWalkAnimSpeed);
    c.aimWalkFastSpeed = GetFloat(path, "Aim", "FastWalkSpeed", c.aimWalkFastSpeed);
    c.crosshair = GetBool(path, "Aim", "Crosshair", c.crosshair);
    c.directHorizontal = GetBool(path, "Aim", "DirectHorizontal", c.directHorizontal);
    c.aimSpeedX = GetScale(path, "Aim", "AimSpeedX", 0.25f, 4.0f);
    c.aimSpeedY = GetScale(path, "Aim", "AimSpeedY", 0.25f, 4.0f);
    {
        char buf[64];
        GetPrivateProfileStringA("Aim", "AimCameraLag", "0.15", buf, sizeof(buf), path);
        float v = 0.15f;
        c.aimCameraLag = ParseNumber(buf, &v) ? (v >= 0.0f && v <= 1.0f ? v : (v > 1.0f ? 1.0f : 0.15f)) : 0.15f;
    }
    {
        const int ms = (int)GetPrivateProfileIntA("Advanced", "HookDelayMs", 3000, path);
        c.hookDelayMs = (unsigned)(ms < 0 ? 0 : ms > 60000 ? 60000 : ms);
    }
    c.crosshairSize = GetScale(path, "Aim", "CrosshairSize", 0.25f, 4.0f, 0.5f);
    c.crosshairKick = GetFloat(path, "Aim", "CrosshairKick", c.crosshairKick);
    c.knifeCrosshair = GetBool(path, "Aim", "KnifeCrosshair", c.knifeCrosshair);
    c.knifeSpeed = GetFloat(path, "Aim", "KnifeSpeed", c.knifeSpeed);
    c.breathing = GetBool(path, "Aim", "Breathing", c.breathing);
    c.padRecoil = GetBool(path, "Aim", "Recoil", c.padRecoil);
    c.damage = GetFloat(path, "Damage", "Damage", c.damage);
    c.knifeDamage = GetFloat(path, "Damage", "KnifeDamage", c.knifeDamage);
    c.fireRate = GetScale(path, "Damage", "FireRate", 0.5f, 3.0f);
    {
        char buf[64];
        GetPrivateProfileStringA("Damage", "ShotgunSpread", "1", buf, sizeof(buf), path);
        float v = 1.0f;
        c.shotgunSpread = ParseNumber(buf, &v) && v >= 0.0f && v <= 1.0f ? v : 1.0f;  // anything else: the original
    }
    c.engineMove = GetBool(path, "Movement", "EngineMove", c.engineMove);
    c.moveWalk = GetScale(path, "Movement", "MoveWalkSpeed", 0.5f, 2.0f);
    c.moveRun = GetScale(path, "Movement", "MoveRunSpeed", 0.5f, 2.0f);
    c.noColorFilter = GetBool(path, "Visual", "NoColorFilter", c.noColorFilter);
    c.coopSync = GetBool(path, "Coop", "Sync", c.coopSync);
    c.keyboardMouse = GetBool(path, "Keyboard", "KeyboardMouse", c.keyboardMouse);
    char kbRun[32];
    GetPrivateProfileStringA("Keyboard", "Run", "Toggle", kbRun, sizeof(kbRun), path);
    c.kbRunMode = !_stricmp(kbRun, "Toggle") ? RunMode::Toggle : RunMode::Hold;
    c.mouseSensitivity = GetScale(path, "Keyboard", "MouseSensitivity", 0.1f, 10.0f, 0.75f);
    c.mouseVertical = GetScale(path, "Keyboard", "MouseVertical", 0.1f, 10.0f);
    char mouseAim[32];
    GetPrivateProfileStringA("Keyboard", "MouseAim", "RE3", mouseAim, sizeof(mouseAim), path);
    c.mouseAimRE3 = _stricmp(mouseAim, "Game") != 0;
    {
        const int n = GetPrivateProfileIntA("Keyboard", "MouseSwitchCounts", c.mouseSwitchCounts, path);
        c.mouseSwitchCounts = n < 0 ? 0 : n > 100000 ? 100000 : n;
    }
    char mode[32];
    GetPrivateProfileStringA("Controls", "Mode", "Full", mode, sizeof(mode), path);
    if (!_stricmp(mode, "MeleeCamera")) {
        // Melee camera only: the original game everywhere else (the camera placement settings still apply).
        c.meleeOnly = true;
        c.meleeCamera = true;
        c.remapButtons = false;
        c.runMode = RunMode::Stock;
        c.runButton = RunButton::L3;
        c.oneButtonReload = false;
        c.quickTurn = false;
        c.stickClicks = false;
        c.knifeFromAim = false;
        c.inventoryMove = false;
        c.freeCamera = false;
        c.gunWalk = c.knifeWalk = c.reloadWalk = c.switchWalk = c.scopeWalk = false;
        c.knifeSpeed = 1.0f;
        c.crosshair = false;
        c.directHorizontal = false;
        c.aimSpeedX = c.aimSpeedY = 1.0f;
        c.aimCameraLag = 1.0f;
        c.breathing = true;
        c.padRecoil = false;
        c.damage = 0.0f;
        c.knifeDamage = 1.0f;
        c.fireRate = c.moveWalk = c.moveRun = 1.0f;
        c.shotgunSpread = 1.0f;
        c.aimDistance = c.fieldOfView = c.aimFieldOfView = 0.0f;
        if (logIt) Log("config: Mode=MeleeCamera (everything else as in the original game)");
    }
    g_cfg = c;
    if (logIt) Log("config: remap=%d run=%s/%s reload=%d quickturn=%d invmove=%d freecam=%d (%.0f/%.0f) aimwalk=%d (%.2f/%.2f) crosshair=%d (%.2f) "
        "aimspeed=%.2f/%.2f aimlag=%.2f fire=%.2f spread=%.2f move=%.2f/%.2f nofilter=%d sync=%d kbm=%d (%s, %.2f/%.2f, %s, %d) "
        "dash=%d clicks=%d knifeaim=%d backstep=%d aimdist=%.0f fov=%+.0f/%+.0f grabcam=%d scopewalk=%d interact=%s",
        c.remapButtons, run, c.runButton == RunButton::RB ? "RB" : "L3", c.oneButtonReload, c.quickTurn, c.inventoryMove, c.freeCamera, c.cameraSpeed, c.cameraPitchSpeed,
        c.gunWalk + c.knifeWalk * 2 + c.reloadWalk * 4 + c.switchWalk * 8, c.aimWalkAnimSpeed, c.aimWalkFastSpeed,
        c.crosshair, c.crosshairSize, c.aimSpeedX, c.aimSpeedY, c.aimCameraLag, c.fireRate, c.shotgunSpread, c.moveWalk, c.moveRun, c.noColorFilter,
        c.coopSync, c.keyboardMouse, c.kbRunMode == RunMode::Hold ? "hold" : "toggle", c.mouseSensitivity,
        c.mouseVertical, c.mouseAimRE3 ? "RE3" : "game", c.mouseSwitchCounts, c.weskerDash, c.stickClicks, c.knifeFromAim,
        c.backStep, c.aimDistance, c.fieldOfView, c.aimFieldOfView, c.grabCamera, c.scopeWalk,
        c.interactX ? "X" : "A");
}
