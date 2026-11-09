# RE5 Enhanced Movement（EM）源码分析

分析对象：`F:\SteamLibrary\steamapps\common\Resident Evil 5\source\re5_enhanced_movement_v03376135.c`
（`#define RE5EM_FIX_VERSION 214u`，文件头标注 `v0.3.37.7.229 - AIM GAIT COHERENCE + LAYERED FIX PASS`）

对比对象：本工作区的 RE3Controls fork（`analysis\RE3Controls_ref`，1.9.5mod）。

---

## 1. 规模与构建

| 项 | 值 |
|---|---|
| 源码 | **单个 .c 文件，9,392 行，617 KB**（高度压缩写法，多数函数为长单行） |
| 函数 | 410 个（其中 static 384 个） |
| 依赖 | **无 CRT、无 MinHook**：freestanding，仅静态链接 kernel32 |
| 构建 | `build.py`：Zig `cc -target x86-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin`，`-nostdlib -Wl,--entry=DllMain@12` |
| 产物 | `plugins\RE5EnhancedMovement.dll`（902 KB），加载器为 **dinput8 代理** |
| 导出 | **14 个 `__declspec(dllexport)` 诊断接口**（如 `RE5EM_CameraReloadDeltaSamples`） |
| 配置 | `RE5EnhancedMovement.ini`，9 个区段：`General / KeyboardMouse / Controller / ControllerBinds1 / ControllerBinds2 / Weapons / FirstPerson / Camera / Characters` |
| 备份 | `_re5em_backups\`（.dll / .bin 快照）、`source\changes_*.diff`（逐版 diff） |

代码风格：函数名带版本尾号（`tps_move_late_core123`、`patch_camera_render_job123`），
并用 `.219`、`.221` 这类修订标记解释"为什么这样改"，等于把调试史写进代码。

---

## 2. 加载与初始化

```
DllMain(DLL_PROCESS_ATTACH)
  ├─ resolve_apis()          // 动态解析 kernel32（GetProcAddress）
  ├─ 创建线程
  │    ├─ 等待游戏窗口/模块就绪
  │    ├─ 写启动横幅：GetModuleFileNameA(...) → 记录自身 DLL 路径与基址（.219 加的"身份验证"）
  │    ├─ 安装钩子（逐条，失败即写日志并继续）
  │    └─ 主循环：读配置、喂菜单、计时、性能统计
  └─ 导出诊断计数器
```

- **不依赖 MinHook**：所有钩子都是自己写 trampoline + 改跳转。
- 每条安装都有日志：`TPSHOOK ok / tramp_fail / patch_fail`（16 处 `TPSHOOK` 日志点）。

---

## 3. 挂钩体系（核心工程部分）

### 3.1 内联 trampoline + 签名校验

```c
static void* make_inline_trampoline(uintptr_t fn, unsigned n, const uint8_t* sig){
    if(!memeq(fn, sig, n)) return 0;                 // ← 先校验函数前 n 字节
    uint8_t* t = inline_tramp_slot(fn, n);
    int64_t d = (int64_t)(fn + n) - (int64_t)(t + n + 5);
    for(i=0;i<n;i++) t[i] = sig[i];                  // 复制被覆盖的序言
    t[n] = 0xE9; *(int32_t*)(t+n+1) = (int32_t)d;    // jmp 回原函数剩余部分
    FlushInstructionCache(...);
    return t;
}
```

签名表举例：

```c
g_moveSigWalk  = {55 8B EC 83 E4 F0 81 EC C8 00 00 00}
g_moveSigWalkL = {51 56 8B F1 8B 4E 18}
```

要点：

1. **版本防呆**：函数体变了就不打补丁（返回 0），不会把游戏改坏；
2. **rel32 范围**：trampoline 页必须落在 ±2 GB 内（`inline_tramp_slot` 负责分配）；
3. **共享页重装保护**：`.219` 注释说明共用页会被重复使用，故有 `g_inlineTrampSealed219` 防止覆盖正在执行的 trampoline。

### 3.2 补丁原语

```c
patch_ptr(at, fn)     // 写 4 字节指针
patch_call(at, fn)    // E8 rel32
patch_jmp(at, fn)     // E9 rel32
patch_jmp6/7          // 带 NOP 填充的 6/7 字节跳转
patch_jmp_n(at, fn, n)// 覆盖 n 字节的跳转（配合 trampoline）
write_mem / mem_readable / memeq
```

共 **23 个 `PATCH_*` 站点常量**，例如相机渲染作业三处：

```c
static const uintptr_t sites[3] = {PATCH_CAMERA_JOB_A, PATCH_CAMERA_JOB_B, PATCH_CAMERA_JOB_C};
for(i=0;i<3;i++){
    if(rd8(sites[i]-1) != 0x68 || rd32(sites[i]) != FN_CAMERA_RENDER_JOB) continue;  // 校验 push imm32
    write_mem(sites[i], &v, 4);   // 只改立即数，不动指令流
}
```

### 3.3 全部可回滚

```c
install_inline_move_hooks()   / restore_inline_move_hooks()
patch_camera_render_job123()  / restore_camera_render_job123()
```

恢复时同样先校验目标位置仍是自己写的值，再写回原始值 —— 避免误恢复别人的补丁。

---

## 4. 移动实现（与 RE3Controls 最大的架构差异）

### 4.1 思路：接管原生移动函数，而不是叠加动画层

七个原生移动更新函数被**入口替换**：

| 函数 | 地址 |
|---|---|
| Walk | `0x00C7A220` |
| WalkL | `0x00C89790` |
| WalkR | `0x00C898C0` |
| Run | `0x00C7A6E0` |
| Back | `0x00C7A960` |
| TurnL | `0x00C7AD30` |
| TurnR | `0x00C7ADE0` |

```c
void* stubs[7] = {WalkInlineStub, WalkLInlineStub, WalkRInlineStub, RunInlineStub,
                  BackInlineStub, TurnLInlineStub, TurnRInlineStub};
patch_jmp_n(g_moveInlineFns[i], stubs[i], g_moveInlineLens[i]);
```

每个 stub → `normal_move_action(action, orig, hookTag)`：

- 从 action 取 actor（`*(void**)(action+0x18)`）；
- 判定是否本地玩家（`tps_player_pawn_ok`）；
- **跳过**原生用于侧移的 TurnL/TurnR（它们本来不产生位移），改为执行一次"相机相对"的位移；
- 调用 trampoline 继续原生逻辑。

因为位移是**替换原生函数**得到的，动画层不需要与它抢骨骼 —— 这正是 RE3Controls 里
"挥刀抽搐 / 速度跳变"的根因所在（我们的 `aimwalk.cpp` 里正是层与动作互相覆盖）。

### 4.2 相机组件内的"迟到修正"

```c
tps_move_late_core123(void* actor, uintptr_t cameraComponent, int from_pad)
```

- 由相机组件更新钩子（`CameraComponentUpdateHook`，`0x00849EF0`）在**帧末**调用；
- 用 `tps_camera_is_local123()` 过滤，只让真正取景本地角色的那个组件移动它；
- 带 `TPS_AIMREJ()` 拒绝计数，配合导出接口做统计；
- 注释里明确说明：**"stock Walk/Run/WalkL/WalkR 会自己平移角色并贴自己的片段，除非七个入口钩子全部替换成功；
  部分安装时相机侧移动会叠加成第二次位移"** —— 即它自己也知道混装的危险，故有 `tps_self_heal()` 与互锁判断。

### 4.3 输入层

| 钩子 | 作用 |
|---|---|
| `PadUpdateHook`（`0x00B8A3E0`） | 每帧输入注入 / 时机判断，带 `actor_pad_pair_ok` 配对校验 |
| `PadAxisHook`（`0x00B86CD0`） | 摇杆轴数据，用于相机相对移动与瞄准 |
| `LegacyPadAxisBodySyncHook` | 旧版轴同步兼容 |
| `InputHeldHook`（`0x0079A4D0`） | 原生按键查询（与 RE3Controls 的 `KbRead` 同一函数） |
| `CpadProfileGateHook` / `CpadMouseReadyHook` | 手柄/鼠标就绪门控 |

---

## 5. 相机体系

| 组件 | 地址/做法 |
|---|---|
| `CameraComponentUpdateHook` | `0x00849EF0`（RE3Controls 也钩这个） |
| `BodyCameraController123` | 身体/相机耦合控制 |
| `LegacyBodyCameraBasisHook` | 旧版相机基向量兼容 |
| `ActorCameraStateHook` / `CameraSyncHook` | 相机状态同步 |
| `CameraRenderJob123` | **三处 `push imm32` 站点替换**，直接换渲染作业 |
| `patch_camera_yaw_decay123` | 相机偏航衰减修正 |

相机侧大量使用"只改立即数、不改指令"的补丁（`0x68 imm32` → 新地址），风险低于改写指令流。

---

## 6. 其它功能模块

| 模块 | 说明 | 相关符号 |
|---|---|---|
| 闪避（Dodge） | 键盘/手柄方向闪避，**60 秒充能**，暂停冻结冷却，HUD 弧形充能条 | `DodgeEnabled`、`DodgeCooldownSeconds`、`DodgeKey` |
| 锁定跑动（Run） | 未锁定的 EM 动作可跑，速度百分比 | `RunEnabled`、`RunSpeedPercent` |
| 第一人称 | `[FirstPerson]` 区段 | — |
| FPS 上限 | 30–480，原生可变帧时序 + 高精度等待 | `FPSCapEnabled`、`FPSCap` |
| 阴影质量 | 分辨率可调，关卡/画质重载时重建资源 | `ShadowQualityEnabled`、`ShadowMapResolution` |
| 去除色滤镜 | 去掉绿黄色调 | `RemoveColourFilter` |
| 激光颜色 | 键盘/手柄独立色相与 RGB 循环 | `LaserHue`、`LaserRGB`、`LaserSight` |
| 血条 | 自定义色相/RGB 动画 | `HealthBarColorEnabled`、`HealthBarHue`、`HealthBarRGB` |
| NO HUD | 隐藏游戏 HUD，菜单/背包保留 | `NoHUD` |
| 同伴移动 | `CompanionMovement` | — |
| 背包行走 | `InventoryWalk` | — |
| 菜单 | 集成到游戏原生 Options 列表（7 行可见、可滚动、鼠标/手柄操作），Back 保存返回 | `tps_menu_block123` |
| 自定义按键 | 两玩家 × A/B/C/D 四套，Schema 1→6 迁移，占用交换，和弦后需松手 | `[ControllerBinds1/2]` |
| 性能诊断 | 5 秒 CPU 回调统计、相机 CPU/墙钟采样（每 128 次调用） | `PerformanceLogging`、`PerformanceSummary` |

---

## 7. 值得借鉴的工程实践

1. **签名校验后再打补丁**，补丁失败只记日志、不中断其它功能；
2. **只改立即数的补丁**（如 `push imm32` 换函数）优先于改写指令流；
3. **全套可回滚**：安装/恢复成对存在，恢复前再校验；
4. **自愈**：`tps_self_heal()` 周期性检查钩子是否被别人覆盖（RE5Fix 等）并重装；
5. **身份横幅**：启动日志先打印"当前加载的是哪个 DLL 文件、基址多少"，用于排除"装错/没生效"；
6. **导出诊断计数器**（14 个），便于外部工具读取运行状态；
7. **版本 diff 归档**（`changes_*.diff`）与 DLL 备份，回归时可直接比对。

---

## 8. 与 RE3Controls（本项目）的对比

| 维度 | Enhanced Movement | RE3Controls 1.9.5mod |
|---|---|---|
| 移动实现 | **替换七个原生移动函数**，自己算相机相对位移 | 写 `uModel` 腿部槽位（动画层）+ pad 接管 |
| 与原版动画的冲突 | 低（位移不再依赖动画层） | **高**（挥刀/投掷时层与动画抢骨骼，即"抽搐/速度跳变"的根源） |
| 钩子框架 | 自研 trampoline + 签名校验，无 MinHook | MinHook |
| 补丁方式 | 入口跳转 + 立即数替换（23 个站点） | 入口跳转（MinHook create/detour） |
| 配置界面 | 集成到游戏原生 Options 菜单 | INI 文件 |
| 体量 | 9392 行单文件，410 函数 | 约 1.6 万行多文件 |

**结论**：EM 之所以能做到"TPS 全向移动 + 不抽搐"，是因为它**换掉了移动的执行者**；
RE3Controls 是在原生移动之上**叠了一层腿部动画**，两者天然冲突。
若本项目要彻底解决挥刀/投掷的手感问题，正确的路线是参考 EM：直接接管
`Walk/Run/Back/Turn*` 七个入口，而不是继续调 `aimwalk` 的层参数。

---

## 9. 风险与注意

- **无 MinHook**：自研 trampoline 需要保证 ±2 GB 相对跳转与共享页安全（代码里已有 `.219` 注释说明踩过的坑）；
- **多站点补丁**：`patch_camera_render_job123` 要求 3 个站点全部命中才算成功（`n==7`），否则视为部分安装；
- **签名绑定版本**：游戏版本一变，签名不匹配即全部跳过（安全但不生效）；
- **状态机复杂**：`tps_*` 系列有大量互锁与"部分安装"特判，阅读成本高；
- 与 **RE5Fix / 其它插件** 的钩子冲突需要用"校验后重装"的方式处理。
