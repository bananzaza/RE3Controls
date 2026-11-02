# 全盘分析：控制链路、闸门模型与遗留问题

范围：本 fork 的全部改动（mod63x → mod116）、全部已知问题、以及"为什么某些修法必然失败"的机制解释。
证据来源：CE 内存采样、IG5/Ghidra 反编译、capstone 静态分析、游戏日志（state/itemuse/knifespeed 探针）。

## 一、控制链路（实测）

```
物理输入(KbRead)
   ↓
phys / aimHeld / knife / busyAction        ← 动作状态判定
   ↓
三个独立闸门：
   ① walkAllowed   → KbSetDriving(keyAimWalk)：摇杆是否交给游戏
   ② layerAllowed  → AimWalkUpdate(walkAllowed)：腿部层是否写 uModel 槽位
   ③ moveSteer     → 方向是否由摇杆接管
   ↓
out(按键位) + lAng/lMag(摇杆) → 写回 pad
   ↓
游戏动作状态机（cPlAction id）+ uModel 动画
```

**关键教训**：① 与 ② 曾被同一个变量控制，因此"压抽搐"必然导致"不能走"；mod110 起才拆分。

## 二、动作 id 表（IG5 从静态注册序列反解，运行时验证一致）

| id | 名称 | 用途 |
|---|---|---|
| 0x00–0x09 | wait/relax/walk/run/back/turn | 移动 |
| 0x1B/0x1C/0x1D | ready_begin/loop/end | 举枪三态 |
| 0x1E | weapon_fire | 开火 |
| 0x1F | weapon_reload | 换弹 |
| 0x20/0x21 | weapon_change(_ready) | 换枪 |
| 0x22–0x24 | knife_ready_begin/loop/end | 持刀 |
| 0x25/0x26 | knife↔gun | 刀枪互切 |
| 0x27 | knife_attack | 挥砍 |
| 0x28–0x2F | jump/bump/fence/ladder/provoke | 交互 |
| 0x30–0x58 | attack_* / avoid_* | 体术/闪避 |
| 0x59–0x60 | item_get/put/dump | 拾取放置（**运行时探针在此看到 0x5F/0x60**） |
| 0x61/0x62 | item_use_my_first/second | 急救助喷雾（**探针确认**） |
| 0x63–0x68 | item_use_partner / item_load | 同伴/装填 |
| 0x85/0x86 | plaga_stick_in/out | 寄生虫附着（**曾被误当投掷**） |
| 0x88+ | damage_* | 受击 |

投掷（`cEmAction_pl_act_attack_throw`）在**另一个注册表**（仅 2 项），运行时 id 尚未确认。

## 三、已修复（机制清楚、可复现）

| # | 问题 | 机制 | 证据等级 |
|---|---|---|---|
| 1 | 开火后枪不放/不能动 | `Fire::update` 相位 2 等 `[obj+0x20]` 计时器，每帧顶 999 | IG5 反编译 + CE 51 帧转储 |
| 2 | 空仓换弹不能走/转向 | 自动换弹不走 mod 的 reloadMove | 版本验证 |
| 3 | 收枪后方向固定 | ReadyEnd 未接管方向 | 版本验证 |
| 4 | 换枪后自动瞄准+换弹 | 0x20 是 weapon_change，曾被当投掷强按 P_RT | IG5 id 表 |
| 5 | 挥刀卡住 | 挥砍与开火共用判定，需同时校验 aid=0x27 | 版本验证 |
| 6 | 交互/体术/攀爬瞄准转身 | busyAction 未覆盖 0x80000000 | 版本验证 |
| 7 | 虚表判定静默失效 | 保护壳重定位，必须 VaOf() | 运行时虚表对比 |
| 8 | 倒计时结束后自己走 | 短路返回不写 pad，脚本态继承旧方向 | mod107 |
| 9 | 寄生虫附着误触投掷 | 0x85 判定错误 | mod108 |
| 10 | 刀枪互切被判为持刀 | 范围 0x22–0x27 应排除 0x25/0x26 | mod108 |

## 四、遗留问题：机制判定

### A. 急救助喷雾不能移动（已定位到机制）
- 探针：`aid=61/62, walk=1, layer=1, got=0` → 三个闸门都开，**层未启动**；
- `aimwalk.cpp:974`：`LegLayerState(ch) || preStart` 为假即返回 false；
- 修法：道具动作首帧 `itemStart` 传给 preStart（mod115 起）；
- 状态：待验证。

### B. 手雷收势抽搐（机制未定）
- 日志显示抽搐时段 `layer=0` → **不是腿部层**；
- 已排除：方向位、摇杆源、层开关、30 帧静默；
- 新假设：**相机双主**（mod 与游戏逐帧交替接管）；mod115 起在"游戏自主动作"期间让出相机，待验证；
- 若仍抽，则只剩"引擎动画混合"，属不可修，需在 README 标注。

### C. 挥刀跑速（已回退）
- `MoveSpeedFor(ch,5)` 在跑键未按时返回走路速度（1.12），其它方向用的是 aimWalkFastSpeed（1.88）；
- 统一到 run5 后"其它方向跑不起来" → 已回退，并加 `knifespeed:` 探针记录三个候选值；
- 下一步：读探针数值后固定一个常量，做到"各方向一致且可跑"。

## 五、方法论结论（避免再走弯路）

1. **不要用虚表地址做判定**（会随重定位失效），优先用动作 id；
2. **不要用单个 id 判定一整类动作**（运行时存在偏移：喷雾 0x61 与 0x5F/0x60 并存）；
3. **闸门必须分开**：驱动(①) 与 层写入(②) 混用必然互相牵连；
4. **诊断先行**：每次定位都应有 `state:`/探针日志，避免"改了没反应"的循环；
5. **一次只改一处**，并保留可回退提交点（`f8330ae` 是已验证稳定基线）。

## 六、剩余工作清单

| 优先 | 任务 | 风险 | 验证方式 |
|---|---|---|---|
| 1 | 喷雾 itemStart 验证 | 低 | 边走边喷雾 |
| 2 | 手雷相机让出验证 | 低 | 丢雷看抽搐 |
| 3 | 挥刀速度按探针数值定死 | 低 | 各方向跑动 |
| 4 | 提交/打包/发布 mod116+ | — | — |