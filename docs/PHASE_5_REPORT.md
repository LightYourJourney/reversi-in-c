# Phase 5 完成报告 — 产品化功能与游戏体验

日期：2026-10-21
平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，16 GiB
工具链：Apple LLVM 21.0.0（`/usr/bin/clang`），CMake 4.4.4

## 1. 交付范围

Phase 5 把前四个阶段的功能完整游戏推进为可日常使用的桌面应用：

- 版本化存档、自动保存、3 个手动槽位、继续上次对局、读档与删除
- 设置持久化：音量、音效、动画、动画速度、合法提示、最后一步、默认难度、主题
- 运行时明/暗主题与调色板重构
- 程序化落子、翻转、按钮、胜利、失败、平局音效；无设备时静默降级
- 规则/操作帮助（3 页）、统计页、存档管理页
- 菜单、暂停与结算页的功能扩展，以及一轮 UI 交互审查与修复

## 2. 持久化

### 2.1 用户数据位置

所有用户数据写入操作系统规定的用户数据目录，不写入程序安装目录：

| 平台 | 目录 |
|---|---|
| macOS | `~/Library/Application Support/Reversi` |
| Linux | `$XDG_DATA_HOME/reversi`，未设置时 `~/.local/share/reversi` |
| Windows | `%APPDATA%\Reversi`，回退 `%USERPROFILE%\AppData\Roaming\Reversi` |

本机实际使用 `~/Library/Application Support/Reversi`，已确认创建了
`settings.dat` 与 `stats.dat`，自动/手动存档也位于同一目录。

### 2.2 存档格式（版本 1）

存档是可读 UTF-8 文本：

```text
REVERSI_SAVE 1
checksum 29412222
mode 0
human 1
difficulty 1
moves 50
move 5 3
move 5 2
...
board .BWWWWWW.BWWWBWB.WWBWBBBWWWWBBBBWWBBBBWW.BWWWWWWB.BWWWW....BWWW.
player 1
result 0
```

- `mode`：0 = 本地双人，1 = 人机
- `human`：1 = 黑，2 = 白
- `difficulty`：0 = Easy，1 = Medium，2 = Hard
- `moves`：当前历史分支的落子序列（行、列均为 0-based）
- `board`：64 个字符，`B`/`W`/`.`，用于读取后的最终状态比对
- `player`/`result`：期望的当前行棋方与结果
- `checksum`：对文件头之后全部字节计算的 32 位 FNV-1a

读取流程：

1. 限制文件大小（64 KiB），拒绝 NUL 污染与缺失文件；
2. 检查魔数与版本；不支持的版本返回 `SAVE_STATUS_VERSION`；
3. 校验校验和；不符返回 `SAVE_STATUS_CHECKSUM`；
4. 严格解析每一行与每一个整数，拒绝尾随垃圾、越界坐标、非法着法数；
5. 用 `GameState` 回放着法，要求每步合法；
6. `app` 用 `match_restore_checked()` 以 `Match` API 回放并重建历史/阶段，
   同时比较回放后的棋盘、当前玩家、结果与文件中的期望值。

`match_restore()` / `match_restore_checked()` 是 Phase 5 新增的 core API：
它们从标准开局回放着法，逐步经过 `match_apply_move()` /
`match_finish_animation()` / `match_finish_pass()`；checked 变体在回放完成后
直接比对期望局面。两者失败时都保持目标对局不变，因此应用层不需要先构造临时
`Match` 再整体赋值，也没有引入第二条修改对局的路径。

### 2.3 自动保存与手动管理

- 每次落子、撤销、重做、重开后，`app` 写 `autosave.rvs`；内容为当前撤销分支。
- 终局动画期间仍保留自动存档；进入 `GAME_OVER` 后才删除，避免动画中退出或
  崩溃丢失对局。读入已结束存档时同样删除。
- 主菜单“继续”仅在自动存档存在且校验通过时启用。
- 暂停菜单与结算页提供“保存游戏”，可写入 3 个手动槽位；槽位显示模式、
  难度、步数与是否已结束。
- 读档页列出自动存档与 3 个槽位，支持加载与删除；删除前弹出确认对话框。
- 文件写入使用同目录临时文件、`fsync`/`_commit` 与原子重命名。残留 `.tmp`
  不会被当作正式存档读取。

## 3. 设置与主题

设置文件为 `settings.dat`，版本 1，包含：

| 键 | 默认值 | 范围 |
|---|---:|---|
| volume | 70 | 0–100 |
| sfx | 1 | 0/1 |
| animations | 1 | 0/1 |
| animation_speed | 100 | 50–200（界面档位 50%/75%/100%/150%/200%） |
| legal_hints | 1 | 0/1 |
| last_move | 1 | 0/1 |
| default_difficulty | 1 | 0–2 |
| theme | 0 | 0 暗 / 1 亮 |

- 设置页有 8 行；开关点击/Enter 切换，数值行点击/Enter/左右键调整。
- 修改后立即应用并原子保存；退出时再保存一次。
- 文件缺失、损坏、越界值或非法布尔值会回退到完整默认值，程序继续运行；
  未知键被忽略，以便后续版本增加设置。

主题重构没有改动 130 余处绘制调用点：`theme.h` 把原有颜色宏映射到
`ui_theme` 指针的字段，`theme.c` 保存两张 `static const UiTheme`。切换主题只改
指针，设置页立即重绘。亮色主题重新选择了背景、表面、文字、边框、棋盘、棋子、
强调色与危险色，并加深白子描边以提高亮色表面上的对比。

## 4. 音效

`audio/audio.c` 在运行时生成 16-bit 单声道波形，不使用外部音频文件：

- 落子：210→150 Hz 短促衰减音
- 翻转：430→300 Hz 轻音
- 按钮：640→600 Hz 极短提示
- 胜利：C5–E5–G5–C6 上行
- 失败：G4–E4–C4 下行
- 平局：A4 双音

`audio_init()` 调用 `InitAudioDevice()`；设备不可用或任一音效加载失败时卸载
已加载音效、关闭设备并让所有 `audio_play()` 成为空操作。音效开关与音量来自
设置，按钮/落子/翻转/胜负/平局均在应用层触发。

本机初始化日志显示 Core Audio 设备成功、48 kHz 双声道输出；未进行人工听感
验证。

## 5. 统计

`GameStats` 记录：

- 总对局数（PVP + PVE）
- PVE 人机视角的总 W/D/L
- Easy/Medium/Hard 分难度的局数、胜、平、负
- 本地双人局数与平局数（双人局没有单一人称胜负）

`app` 在首次进入 `GAME_OVER` 时记录一次并写盘；读入已结束存档时直接标记为
已记录。结算后撤销会撤回该局记录并写盘，重新结束或重做回结算只记录新结果
一次，不会重复计数。统计页显示汇总和分难度表（局数/胜/平/负）；数据文件
损坏时归零并继续运行。

## 6. 验证结果

所有测试均由实际运行得到。

### 6.1 自动化测试

| 测试 | 断言数 | 结果 |
|---|---:|---|
| core.board | 623 | 通过 |
| core.rules | 227 | 通过 |
| core.game | 115 | 通过 |
| core.random | 557,924 | 通过 |
| core.history | 124 | 通过 |
| core.match | 980 | 通过 |
| ai.core | 约 2,970 | 通过 |
| ai.worker | 22 | 通过 |
| storage.save | 91 | 通过 |
| storage.settings | 58 | 通过 |
| storage.stats | 44 | 通过 |
| audio.noop | 2 | 通过 |
| ui.button | 30 | 通过 |
| ui.helpers | 189 | 通过 |
| ui.views | 35 | 通过 |

- Debug：15/15 无 GUI 测试通过。`app.smoke` 在本阶段显示可用时曾随 16/16
  通过；最终复核时图形会话再次无法获取主显示器，`app.smoke` 以 GLFW
  “Failed to get primary monitor” 失败，未作为代码失败处理。
- Release：15/15 无 GUI 测试通过。
- ASan+UBSan：15/15 无 GUI 测试通过，无 Sanitizer 报告。

`storage.save` 覆盖：往返、覆盖写入、缺失、截断、错误版本、错误校验和、
非法着法、棋盘/行棋方/结果不匹配、着法数越界、坐标越界、状态消息、用户路径
校验、残留临时文件；`core.match` 覆盖从着法恢复、checked 期望局面校验、
终局恢复、非法序列与越界计数。

### 6.2 存档/读档与重启恢复

使用 Debug 自动落子模式（`REVERSI_AUTOPLAY=1`）实际生成对局：

1. 让程序自动走到中后盘，退出；用户数据目录中的 `autosave.rvs` 为 50 步，
   最终棋盘 20 黑 / 34 白，最后一步 D2（`move 6 3`）。
2. 重新启动程序，通过应用的真实读档函数 `app_restore_saved()` 加载该文件。
   验证期间使用过临时调试启动钩子，提交前已移除，产品代码不包含该入口。
3. 对局页显示相同的 20:34、最后一步 D2 与黑方行棋，说明着法回放、历史重建、
   棋盘/回合/结果校验与重启恢复一致。
4. 自动存档存在时主菜单“继续”启用并作为主按钮显示；删除后恢复禁用。

注意：当前图形会话能更新鼠标悬停，但按键与点击事件未送达应用，因此没有完成
逐按钮点击的“暂停→保存→继续”人工流程；自动写入与读档路径均由应用实际执行，
手动槽位的保存/读取/删除动作由 `ui.views` 与 `storage.save` 覆盖。

### 6.3 损坏存档

将 `autosave.rvs` 替换为错误的校验和与垃圾内容后重启：

- 程序未崩溃，保持主菜单；
- 显示 “Save checksum is invalid” 提示；
- 修正加载有效性判断后，损坏自动存档会使“继续”置灰，用户仍可从读档页
  查看并删除它。

自动化测试还覆盖错误版本、截断文件、非法着法、棋盘/行棋方/结果不匹配等路径。

### 6.4 设置持久化

- 写入 `settings.dat`（`theme 1`）后重启，主菜单、设置、帮助、读档、统计与
  对局页均以亮色主题渲染；`theme 0` 时恢复暗色。
- 应用退出时实际创建/重写了 `settings.dat` 与 `stats.dat`，路径为
  `~/Library/Application Support/Reversi`。
- 设置文件的默认值、往返、损坏头、越界音量/速度、非法布尔、未知键均有
  `storage.settings` 测试。

### 6.5 统计记录

用自动落子模式完成一局 PVP（15:49，白胜）：

- `stats.dat` 的 `pvp_games` 从 3 增至 4，`pvp_draws` 保持 1；
- 结算页正常显示 15/49 与“再来一局/撤销最后一步”；
- 对局结束后 `autosave.rvs` 被删除。

测试生成的统计与存档随后已清理，用户数据恢复为默认设置与零统计。

### 6.6 UI 截图核对

本阶段实际截图检查了：

- 暗色/亮色主菜单（含继续启用/禁用、损坏存档时禁用）
- 暗色/亮色设置页（8 行、开关、值、主题）
- 亮色帮助页（页标题、正文、Previous 禁用、Next/Back）
- 亮色统计页（总对局、人机战绩、双人局、分难度表；评审后新增 Draws 列，
  未重新截图）
- 暗色/亮色读档页（自动存档、空槽位、禁用按钮）
- 亮色对局页（棋盘、坐标、信息栏、操作按钮）
- 亮色结算页（比分、按钮；随后又增加了“保存游戏”按钮）
- 损坏存档提示条

截图暴露并修复的问题：

1. 亮色主题下白子在白色结算面板上对比不足——增加结果背板、加深白子描边。
2. 损坏自动存档仍让“继续”可点——改为仅在校验通过时启用。
3. “返回主菜单会丢失当前游戏”的旧弹窗文案与自动保存语义冲突——改为
   “Your game is saved; continue from the menu.”，退出弹窗同理。
4. 帮助页声称 `U`/`Y` 可直接撤销/重做，但代码只支持 Ctrl/Cmd 组合——补齐
   直接按键，使说明与实际一致。
5. 统计页 “1 draws” 单复数错误。
6. 存档删除原先单击立即生效——改为确认模态，避免误删。
7. 结算页只能“再来一局/撤销/菜单”，无法保存已完成对局——增加“保存游戏”，
   复用同一存档格式与槽位页。
8. 结算页 PvE 仍显示 “Black wins/White wins”——改为“You win/Computer wins”。

### 6.7 代码审查后的修正

- 结算后撤销现在会撤回已记录的统计；重新结束/重做只记录一次。
- PVE 仅在人方回合显示合法着法提示，AI 回合不再暴露电脑可选位置。
- Setup 中选择的一次性难度不再覆盖“默认难度”设置。
- 帮助页 Previous/Next 翻页会播放按钮反馈音。
- 自动存档在终局翻转动画期间保留，进入结算后才删除。
- 分难度统计表增加 Draws 列；移除未使用的错误音效与调试启动钩子。
- 抽出共享文本拆分/整数解析辅助；存档恢复改用 `match_restore_checked()`；
  统计聚合就近放在 `stats.h`，避免 UI 与 app 各写一份。

## 7. 未验证项

- **逐按钮人工点击**：当前会话中鼠标移动可以更新悬停，但合成的按键与鼠标
  按下/释放未送达 GLFW 窗口。因此 Continue/设置调节/手动保存/删除确认等
  点击流程未人工完成；它们由单元测试、应用真实读档启动路径与截图覆盖。
- **音效听感**：设备初始化与音效加载成功，但未人工试听音量、音色与同时播放
  落子+翻转的效果。
- **760×600 最小窗口**：新页面的几何常量已按 600 高度核对，但没有逐页截图。
- **完整人机对局到结算页**：PvE 结果标题逻辑有单元测试，Phase 5 未再次人工
  走完一局人机；Phase 4 也未完成该项。
- **Windows / Linux**：用户数据路径、原子替换与线程后端均未实机运行。
- **泄漏检测**：本阶段只做了 ASan/UBSan；未对 raylib/UI 进程做 LeakSanitizer。
- **存档重做分支**：只保存当前撤销分支，撤销后尚未重做的未来分支不写入文件。

## 8. 复验步骤

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
ctest --test-dir build-sanitize --output-on-failure
```

人工复验（显示可用时）：

1. 开始一局，落子后关闭程序；重启确认“继续”恢复同一局面。
2. 暂停 → 保存游戏 → 选择槽位；返回菜单 → 读取游戏 → 加载该槽位。
3. 删除槽位，确认对话框出现，取消后槽位仍在，确认后消失。
4. 修改主题、音量、动画速度与默认难度，重启确认全部保留。
5. 用垃圾内容覆盖 `autosave.rvs`，重启确认“继续”禁用且无崩溃。
6. 完成一局 PvE，确认结算页显示 “You win/Computer wins”，统计与难度记录一致。
7. 在 760×600 与更大窗口下检查菜单/设置/帮助/统计/存档/结果页无重叠。
