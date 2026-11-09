# 玩家动作 id 完整映射（IG5 / 静态初始化表反解）

来源：`game_image_layout.bin` 中动作类的静态注册序列（`push <类名>; mov ecx,<注册表>; call 0x6D04F0`），
按 `cPlAction_pl_act_wait` 校准为 id 0x00。已与运行时观测对齐（0x1E=weapon_fire、0x1C=ready_loop 等全部吻合）。

## 关键区间

| id | 名称 | 备注 |
|---|---|---|
| 0x00 | wait | 空闲 |
| 0x02 | walk | 走 |
| 0x05 | run | 跑 |
| 0x1B | weapon_ready_begin | 举枪起手 |
| 0x1C | weapon_ready_loop | 瞄准循环 |
| 0x1D | weapon_ready_end | 收枪 |
| **0x1E** | **weapon_fire** | 开火（已用于冻结修复） |
| **0x1F** | **weapon_reload** | 换弹 |
| **0x20** | **weapon_change** | 换枪（曾误判为投掷） |
| 0x21 | weapon_change_ready | 瞄准中换枪 |
| 0x22–0x24 | knife_ready_begin/loop/end | 持刀 |
| 0x25/0x26 | knife 与枪互相切换 | |
| **0x27** | **knife_attack** | 挥砍（`aid::KnifeAttack` 常量正确） |
| 0x28–0x2F | jump / bump / fence / ladder / provoke | 交互族 |
| 0x30–0x58 | attack_* / avoid_* | 体术与闪避 |
| 0x59–0x60 | item_get / put / dump | 拾取/放置 |
| **0x61** | **item_use_my_first** | **自用道具（急救喷雾）** |
| **0x62** | item_use_my_second | 自用道具（第二形态/连用） |
| 0x63/0x64 | item_use_partner_* | 给同伴用 |
| 0x67/0x68 | item_load_* | 装填 |
| 0x7A–0x7D | coop_item_load_* | 协作装填 |
| 0x84 | emotion | 情绪动作 |
| 0x85 | plaga_stick_in | 寄生虫附着（**不是投掷**） |
| 0x88+ | damage_* | 受击 |

另外 `cEmAction_pl_act_attack_throw` 属于 **cEmAction** 注册表（仅 2 项：attack_neck、attack_throw），
与 `cPlAction` 的 id 空间不同，因此投掷在运行时看到的 id 不一定落在上表内。

## 由此得到的修正

1. **急救喷雾 = 0x61**：此前完全不知道，导致使用道具时不能移动。把 0x61/0x62 纳入可行走集合即可。
2. **0x2A 不是挥砍**，是 `jump_down`；挥砍是 0x27 —— 之前代码里凡是按 0x2A 判断挥砍的地方都是错的。
3. **0x85 不是投掷**，是 `plaga_stick_in`；把它当投掷会在被寄生虫抓住时误触发投掷逻辑。
4. 投掷动作属于 cEmAction 表，需要用它的类（RTTI 名 `cEmAction_pl_act_attack_throw`）定位，
   而不能用 cPlAction 的 id 推断。