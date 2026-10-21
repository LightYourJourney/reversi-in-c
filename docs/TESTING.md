# 测试与验证

日期：2026-10-21。验证平台：macOS 26.6.2（Darwin 25.6.0，Apple M3 /
arm64，Apple Clang 21，CMake 4.4.4）。

## 1. 测试命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`REVERSI_ENABLE_GUI_TESTS=ON` 时 CTest 注册 16 个测试：15 个无窗口测试与
1 个 `app.smoke`。不开启时注册 15 个。CTest 名称、可执行文件和源码一一对应。

## 2. 无窗口测试清单

| CTest 名称 | 可执行文件 | 断言数 | 覆盖重点 | 结果 |
|---|---|---:|---|---|
| core.board | reversi_tests | 623 | 棋盘初始化、边界、记谱、安全访问 | PASS |
| core.rules | reversi_rules_tests | 227 | 开局、八方向、多方向/多子翻转、非法落子 | PASS |
| core.game | reversi_game_tests | 115 | 合法/非法落子、自动 Pass、主动 Pass 限制、终局 | PASS |
| core.random | reversi_random_tests | 557,924 | 500 局随机对局 + 独立合法性 oracle、不变量 | PASS |
| core.history | reversi_history_tests | 124 | 快照、撤销/重做、分支截断、Pass/终局 | PASS |
| core.match | reversi_match_tests | 980 | 状态机、暂停、重开、AI 回合、撤销/重做、恢复 | PASS |
| ai.core | reversi_ai_tests | 2,977 | 评估、搜索合法性、TT 一致性、取消、确定性、自动对局 | PASS |
| ai.worker | reversi_ai_worker_tests | 22 | 后台线程、快照隔离、取消、生命周期 | PASS |
| storage.save | reversi_save_tests | 91 | 往返、原子覆盖、损坏/版本/校验和/非法状态 | PASS |
| storage.settings | reversi_settings_tests | 58 | 默认值、速度档位、解析、持久化、损坏回退 | PASS |
| storage.stats | reversi_stats_tests | 44 | 记录/撤回、视角、汇总、原子持久化 | PASS |
| audio.noop | reversi_audio_tests | 2 | 无音频设备时安全空操作 | PASS |
| ui.button | reversi_ui_button_tests | 30 | 悬停/按下/取消/禁用/reduced-motion | PASS |
| ui.helpers | reversi_ui_helper_tests | 189 | 布局命中、提示门控、标题、焦点、动画 | PASS |
| ui.views | reversi_ui_views_tests | 35 | 主题、菜单、设置、存档、帮助、统计动作 | PASS |

实际运行结果：

- Debug：15/15 无窗口测试通过。
- Debug + GUI：16/16 通过；`app.smoke` 实际打开窗口、加载字体与音频、
  渲染两帧并正常关闭。
- Release：15/15 通过；Release 二进制的 `--frames 2` 实际退出码 0。
- ASan+UBSan：15/15 通过，无 Sanitizer 报告；Sanitizer 二进制的
  `--frames 2` 退出码 0。
- 全新 Debug 构建树（`build-phase6-fresh`）15/15 通过，确认不是旧产物；
  raylib 使用固定 6.2 源码树的离线路径（本会话联网 FetchContent 超时，
  联网下载路径 NOT TESTED）。

## 3. 需求到测试的映射

| 验证项 | 覆盖方式 | 结果 |
|---|---|---|
| 棋盘初始化 | `core.board` | PASS |
| 所有合法落子 | `core.rules`、`core.random` oracle | PASS |
| 多方向翻转 | `core.rules` 定向用例 | PASS |
| Pass 与终局 | `core.game`、`core.match`、`core.random` | PASS |
| 撤销/重做 | `core.history`、`core.match`、`ui.views` | PASS |
| 游戏状态机 | `core.match` 阶段/一致性/转换 | PASS |
| 存档与读档 | `storage.save`、`core.match`、GUI 自动对局 | PASS |
| 损坏文件 | `storage.save` 截断/校验和/版本/非法值 | PASS |
| AI 合法性 | `ai.core` 每步复核 + 基准 `illegal=0` | PASS |
| AI 自动对局 | `ai.core` 固定对局、`reversi_ai_bench` 27 局 | PASS |
| GUI 关键操作 | 真实窗口/冒烟/自动对局已通过；人工点击流程未完成 | NOT TESTED |
| Debug/Release | 两套构建与测试 | PASS |
| Sanitizer | ASan+UBSan 测试与 GUI 冒烟 | PASS |
| 跨平台 | 仅 macOS arm64 实机 | Windows/Linux NOT TESTED |

## 4. GUI 与手工验证

实际完成：

- `reversi --frames 2`：窗口创建、Inter 字体加载、音频设备初始化、
  两帧渲染、字体/纹理/音频/窗口清理均成功。
- 主菜单窗口截图核对：7 个菜单项、禁用 Continue、主操作 New game、
  最小可见层级与字体渲染正常。
- 无 `assets/` 目录启动：日志出现 `bundled fonts unavailable`，程序使用
  默认字体并正常退出。
- `REVERSI_AUTOPLAY=7 --frames 600`（Debug）：自动完成两局 PVP，
  600 帧约 11.23 秒，退出码 0；统计 `pvp_games` 分别从 0→1、1→2，
  终局自动存档被删除。用于验证实际游戏循环、结果页与持久化路径。

未完成：

- 逐按钮人工点击与键盘操作（当前图形会话的窗口不在可交互 Space，
  合成点击未送达应用）。
- 760×600 最小窗口逐页截图。
- 真实听感验证。
- 锁屏/休眠/多显示器场景。
- 反复调整窗口大小的实机稳定性。

这些项目在报告中标记为 NOT TESTED，不视为通过。

## 5. 性能数据

以下数据为实际命令输出，Release 构建，Apple M3。

### AI

```bash
./build-release/bin/reversi_ai_bench --games 3
```

- 27 个基准对局/局面：`illegal=0 failures=0 unfinished=0`，总耗时 8.92 s。
- Hard 中局：depth 8，约 158,720 nodes，501 ms（预算 500 ms）。
- Hard 开局：depth 10，约 63,487 nodes，258 ms。
- Medium 中局：depth 4，约 832 nodes，3 ms。
- Easy：depth 1，0–1 ms。
- 10 组（90 个局面）压力运行峰值内存约 5.19 MB。

### 存档

20 步对局，1000 次读取 + 100 次原子写入：

- 读取：85.55 ms / 1000 ≈ **0.086 ms/次**。
- 写入（含 fsync/原子 rename）：16.16 ms / 100 ≈ **0.162 ms/次**。

### 渲染与长时间运行

- 菜单 300 帧：5.57 s，约 **53.9 FPS**；user 0.26 s + sys 0.12 s，
  约 **1.27 ms CPU/帧**。
- 自动对局 600 帧：11.23 s，约 **53.4 FPS**；user 0.54 s + sys 0.33 s，
  约 **1.45 ms CPU/帧**；完成两局且正常清理。
- 目标 FPS 为 60；实际受 vsync 与当前显示会话影响。多分钟/多小时
  稳定性未单独测试。

## 6. 静态分析与泄漏检查

- Clang Static Analyzer（`clang-analyzer-*`）：仅报告 `rand()`（Debug
  自动对局钩子）、`fprintf` 和 `storage_read` 的 `buffer[size]` 越界疑似；
  后者在 `length <= max_size <= 65536` 且 `malloc(size + 1)` 下为误报。
- `bugprone-*` / `cert-*`：仅参数易混淆、无符号常量移位等风格提示，
  未发现真实缺陷。
- macOS `leaks --atExit`：
  - `ai.worker`、`ai.core`、`storage.save`、`ui.views`：**0 leaks**。
  - 完整 GUI 自动对局：报告约 284 个系统框架分配（约 18 KB），
    调用栈位于 macOS XPC/AppIntents 初始化，未出现第一方函数栈；
    未据此宣称 GUI “零泄漏”。
- ASan 的 LeakSanitizer 在 macOS 不受支持，LeakSanitizer 结果为
  NOT TESTED；不得用 `detect_leaks=1` 运行。

## 7. 已知测试缺口

- 人工点击/键盘流程、窗口缩放、真实音频听感、Windows/Linux 实机、
  多显示器/锁屏路径未验证。
- 结算后撤销与统计撤回有 `storage.stats` 单元测试和应用层代码审查，
  但没有自动化 GUI 点击测试。
- 存档的“终局动画中断”路径通过提前记录统计与自动存档保留逻辑处理，
  未做崩溃注入/断电测试。
