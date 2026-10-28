# 测试与验证

日期：2026-10-21。验证平台：macOS 26.6.2（Darwin 25.6.0，Apple M3 /
arm64，Apple Clang 21，CMake 4.4.4）。跨平台构建与无窗口测试另在
GitHub Actions 的 `ubuntu-latest`、`windows-latest`、`macos-latest`
托管虚拟机（VM）上执行；Linux GUI 冒烟在 Xvfb + Mesa 软件渲染下执行。

## 1. 测试命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`REVERSI_ENABLE_GUI_TESTS=ON` 时 CTest 注册 30 个测试（Debug）：26 个
无窗口测试、`app.smoke`、`app.lab`、`app.net` 与 `app.autoplay`。不开启时注册 26 个。
CTest 名称、可执行文件和源码一一对应。

Phase 7 增加了发行包验收：`scripts/package.sh` 执行 Release 构建、CTest
与 CPack；`scripts/verify-package.sh` 校验归档 `.sha256`，从任意工作目录
连续启动两次，检查字体资源、许可证、用户数据持久化，并在 Linux 上检查
`ldd` 缺失依赖。CI 在 Linux Xvfb 下对 Linux 发行包执行同一验收；Windows
发行包执行并断言 `reversi.exe --version`。完整结果与剩余限制见
[PHASE_7_REPORT.md](PHASE_7_REPORT.md)。

Phase 8 在文档与 CI 结构调整后重新执行了本机回归：Debug 17/17、
Release 15/15、ASan+UBSan 无窗口 15/15（无报告），并验证 macOS 应用包
图标、签名与 CPack 归档。CI 工作流提取为
`.github/workflows/package.yml` 后只做了 `actionlint` 静态检查，尚未在
GitHub Actions 实跑，标记 **NOT TESTED**，需推送后确认。

## 2. 无窗口测试清单

| CTest 名称 | 可执行文件 | 断言数 | 覆盖重点 | 结果 |
|---|---|---:|---|---|
| core.board | reversi_tests | 623 | 棋盘初始化、边界、记谱、安全访问 | PASS |
| core.bitboard | reversi_bitboard_tests | 621,111 | 位映射/往返、边界位移、与标量规则的合法着法/翻转/棋盘差分、对称、非法状态 | PASS |
| core.rules | reversi_rules_tests | 227 | 开局、八方向、多方向/多子翻转、非法落子 | PASS |
| core.game | reversi_game_tests | 115 | 合法/非法落子、自动 Pass、主动 Pass 限制、终局 | PASS |
| core.random | reversi_random_tests | 557,924 | 500 局随机对局 + 独立合法性 oracle、不变量 | PASS |
| core.history | reversi_history_tests | 124 | 快照、撤销/重做、分支截断、Pass/终局 | PASS |
| core.match | reversi_match_tests | 1011 | 状态机、暂停、重开、AI 回合、撤销/重做、恢复 | PASS |
| ai.core | reversi_ai_tests | 2,988 | 评估、搜索合法性、TT 一致性、取消、确定性、自动对局 | PASS |
| ai.endgame | reversi_endgame_tests | 70 | 12/10/8/6/4/2 空与全深度搜索分数一致、INCOMPLETE 分类、边界 | PASS |
| ai.analyze | reversi_analyze_tests | 104 | MultiPV 排序、同深度可比、PV 逐步重放、精确残局、取消、黑方视角 | PASS |
| analysis.session | reversi_analysis_tests | 108 | 局面树/分支/Pass、编辑、History 导入、棋谱往返与拒绝、访问器、ply 上限、VALID 根 | PASS |
| analysis.lab | reversi_lab_tests | 9 | generation 守卫、进入/退出取消分析 | PASS |
| net.protocol | reversi_net_protocol_tests | 33 | 协议字段/版本/方向/棋盘/错误/终局快照 | PASS |
| net.ws | reversi_net_ws_tests | 20,026 | 握手、掩码、长度、分片、ping、20k 随机帧 | PASS |
| net.server | reversi_net_server_tests | 713 | 完整对局、非法、重复、重连、离开、多房间、重启、双向落子超时、限速与关闭、重连到期与旧令牌、满房、终局重连、模糊 | PASS |
| net.client | reversi_net_client_tests | 26 | 连接、创建/加入/ready/落子、断线重连 | PASS |
| net.session | reversi_net_session_tests | 32 | 会话相位、颜色、seq、回合、落子、投降终局、终局后房间关闭 | PASS |
| experiment.regression | reversi_experiment_tests | 87 | 配置拒绝、配对/换色、确定性 oracle、搜索 oracle、报告文件 | PASS |
| ai.worker | reversi_ai_worker_tests | 22 | 后台线程、快照隔离、取消、生命周期 | PASS |
| storage.save | reversi_save_tests | 91 | 往返、原子覆盖、损坏/版本/校验和/非法状态 | PASS |
| storage.settings | reversi_settings_tests | 58 | 默认值、速度档位、解析、持久化、损坏回退 | PASS |
| storage.stats | reversi_stats_tests | 44 | 记录/撤回、视角、汇总、原子持久化 | PASS |
| audio.noop | reversi_audio_tests | 2 | 无音频设备时安全空操作 | PASS |
| ui.button | reversi_ui_button_tests | 30 | 悬停/按下/取消/禁用/reduced-motion | PASS |
| ui.helpers | reversi_ui_helper_tests | 191 | 布局命中、提示门控、标题、焦点、动画 | PASS |
| ui.views | reversi_ui_views_tests | 35 | 主题、菜单、设置、存档、帮助、统计动作 | PASS |

实际运行结果：

- Debug：26/26 无窗口测试通过（v2.0.0）。
- Debug + GUI：v2.0.0 本机 30/30，包含 `app.lab` 与 `app.net`（大厅 20 帧渲染），
  `app.smoke` 与 `app.autoplay` 也通过；Windows/macOS 的交互式 GUI 流程
  仍 **NOT TESTED**。
- Release：26/26 通过（v2.0.0）。
- ASan+UBSan：v2.0.0 无窗口 26/26 通过，无 Sanitizer 报告；此前
  GUI 冒烟 17/17 通过，Sanitizer 二进制的 `--frames 2` 退出码 0。
- 全新 Release 构建树（`build-p7-clean`，联网获取 raylib 6.2）15/15 通过，
  并通过 `scripts/verify-package.sh` 独立启动验收。
- GitHub Actions 运行 `39633821096`（2026-10-22）9 个作业全部 `success`：
  Linux/Windows（MSVC）/macOS 的 Debug 各 15/15；三平台 Release 打包
  作业（含 Release 15/15 与 Windows `--version` 断言）；Linux
  ASan+UBSan；Linux Xvfb GUI；Linux 发行包在 Xvfb 下执行
  `scripts/verify-package.sh`。该运行同时验证了 Phase 8 提取的
  `.github/workflows/package.yml` 可复用工作流。运行器是托管 VM，
  不是物理机；Windows/macOS GUI 未在 CI 中运行。
- GitHub Actions 运行 `40073769484`（2026-10-24，v1.2.0）9 个作业全部
  `success`：Linux/Windows（MSVC）/macOS Debug 各 17/17；三平台 Release
  各 17/17（Windows 含 `--version` 断言）；Linux ASan+UBSan 无窗口 17/17、
  Xvfb GUI 2/2；Linux 发行包在 Xvfb 下验收。首跑 `40073029646` 暴露
  Linux 严格 C11 下 `CLOCK_MONOTONIC` 未声明，已在提交 `438a510` 修正。
- GitHub Actions 运行 `40288248734`（2026-10-26，Phase 13）9 个作业全部
  `success`：三平台 Debug/Release 各 21/21；Linux ASan+UBSan 无窗口 21/21；
  Linux Xvfb GUI 3/3；三平台打包与 Linux 包验收。
- GitHub Actions 运行 `40671454886`（2026-10-28，提交 `2d243c9`）9 个作业
  全部 `success`：Linux/Windows（MSVC）/macOS Debug 各 26/26；Linux
  ASan+UBSan 无窗口 26/26、Xvfb GUI 4/4；Linux Xvfb GUI 4/4；三平台打包
  与 Linux 包验收。该运行覆盖浏览器轮询修复（`net.server` 713、
  `net.protocol` 33）。首跑 `40298668533` 暴露测试缺少 `windows.h` 与
  Linux 端 `net.session` 等待竞态；`40299103857` 进一步显示 Windows 上
  `shutdown` 不唤醒阻塞的 `recv`，读取循环已改为先 `select` 再 `recv`
  （`95ccd03`、`699efbb`）。独立审计修复的运行是 `40452178342`
  （`abf182d`，`net.server` 709）。
- GitHub Actions 运行 `40111155790`（2026-10-24，Phase 12）9 个作业全部
  `success`：三平台 Debug/Release 各 20/20；Linux ASan+UBSan 无窗口
  20/20；Linux Xvfb GUI 3/3；Linux 发行包验收。Phase 13 的 CI 结果在
  推送后补记。首跑
  `40106930342` 仅 ASan 作业把 `app.lab` 排进无窗口步骤，已在 `1cc9147`
  修正工作流。

## 3. 需求到测试的映射

| 验证项 | 覆盖方式 | 结果 |
|---|---|---|
| 棋盘初始化 | `core.board` | PASS |
| 所有合法落子 | `core.rules`、`core.random` oracle | PASS |
| 多方向翻转 | `core.rules` 定向用例 | PASS |
| Pass 与终局 | `core.game`、`core.match`、`core.random` | PASS |
| 撤销/重做 | `core.history`、`core.match`、`ui.views` | PASS |
| 游戏状态机 | `core.match` 阶段/一致性/转换 | PASS |
| 存档与读档 | `storage.save`、`core.match`、GUI 自动对局、打包进程用户数据持久化 | PASS（逻辑）；GUI 手动存/读档 NOT TESTED |
| 损坏文件 | `storage.save` 截断/校验和/版本/非法值 | PASS |
| AI 合法性 | `ai.core` 每步复核 + 基准 `illegal=0` | PASS |
| AI 自动对局 | `ai.core` 固定对局、`reversi_ai_bench` 27 个局面 | PASS |
| GUI 鼠标点击 | 用户人工点击验证；真实窗口/冒烟/自动对局通过 | PASS |
| GUI 键盘导航 | 未单独走查；无自动化点击/键盘回归测试 | NOT TESTED |
| Debug/Release | 两套构建与测试 | PASS |
| Sanitizer | ASan+UBSan 测试与 GUI 冒烟 | PASS |
| 跨平台构建/测试 | GitHub Actions 托管 VM：v1.2.0 三平台 Debug/Release 各 17/17 | PASS |
| Linux GUI（Xvfb） | `app.smoke`、`app.autoplay`（软件渲染） | PASS |
| Windows/macOS GUI | 未运行 | NOT TESTED |
| 跨平台物理机 | 仅 macOS arm64 为本机 | Windows/Linux 物理机 NOT TESTED |
| 发行包独立验收 | 校验 `.sha256`、双启动、字体、许可证、用户数据、`ldd`（Linux） | PASS |

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
- GUI 鼠标点击流程：用户已在可交互图形会话中实际点击验证。
- Linux Xvfb（GitHub Actions）：`app.smoke` 与 `app.autoplay` 在软件
  OpenGL 下通过。

未完成：

- 键盘导航流程单独走查；没有自动化 GUI 点击/键盘回归测试。
- 760×600 最小窗口逐页截图。
- 真实听感验证。
- 锁屏/休眠/多显示器场景。
- 反复调整窗口大小的实机稳定性。
- Windows/macOS 桌面 GUI、Windows/Linux 物理机。

这些项目在报告中标记为 NOT TESTED，不视为通过。

## 5. 性能数据

以下数据为实际命令输出，Release 构建，Apple M3。

### AI

```bash
./build-release/bin/reversi_ai_bench --games 3
```

- 27 个基准局面：`illegal=0 failures=0 unfinished=0`，总耗时 9.51 s。
- Hard 中局：depth 8，约 123,904 nodes，502 ms（预算 500 ms）。
- Hard 开局：depth 10，约 63,487 nodes，461 ms。
- Medium 中局：depth 4，约 832 nodes，4 ms。
- Easy：depth 1，0–1 ms。
- 同一次运行峰值 RSS 约 9.17 MiB（`/usr/bin/time -l`）；搜索 TT 与
  残局求解器 TT 各约 2 MiB，是相对 v1.1.0 的主要增量。该值随分配器
  复用波动（轻载复跑约 4.9 MiB），只作数量级参考。
- 本轮机器有外部负载，绝对毫秒数不代表空载性能。

### 存档

20 步对局，1000 次读取 + 100 次原子写入：

- 读取：85.55 ms / 1000 ≈ **0.086 ms/次**。
- 写入（含 fsync/原子 rename）：16.16 ms / 100 ≈ **0.162 ms/次**。

### 渲染与长时间运行

- 菜单 300 帧：5.57 s，约 **53.9 FPS**；user 0.26 s + sys 0.12 s，
  约 **1.27 ms CPU/帧**。
- 自动对局 600 帧：11.23 s，约 **53.4 FPS**；user 0.54 s + sys 0.33 s，
  约 **1.45 ms CPU/帧**；完成两局且正常清理。
- 菜单 3600 帧（62.32 s）：约 **57.8 FPS**，约 **1.24 ms CPU/帧**；
  峰值内存约 232 MB，与 300 帧时基本一致。
- 100,000 次 `match_restart`：5.45 s，macOS `leaks` 为 0 leaks。
- 目标 FPS 为 60；实际受 vsync 与当前显示会话影响。小时级稳定性与
  GUI 反复重启/缩放未单独测试。

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

## 7. Web / WebAssembly 测试（Phase 10）

构建：`scripts/build-web.sh`（Emscripten 6.0.13，raylib 6.2 Web 后端）。
浏览器：系统 Chrome，Playwright `channel: chrome`，headless +
SwiftShader 软件渲染。

| 用例 | 覆盖 | 结果 |
|---|---|---|
| `smoke` | 加载、画布与逻辑尺寸一致、无控制台错误 | PASS |
| `gameplay` | 真实鼠标落子、Hard AI 在 Worker 中搜索并完成 | PASS |
| `persistence` | 自动存档、刷新、继续游戏恢复 | PASS |
| `responsive` | 1920×1080、1366×768、768×1024、390×844、360×640 | PASS（无页面滚动） |
| `difficulties` | Easy / Medium / Hard 各自完成一次 AI 回应 | PASS |
| `mobile-touch` | 390×844 触摸落子并写入自动存档 | PASS |
| `pagehide` | `pagehide` 触发持久化（重写自动存档） | PASS |
| `cancel` | 搜索中取消、Worker 终止、主循环不阻塞 | PASS |
| `autoplay` | Debug 构建完整对局到终局 | PASS |

默认情况下，任何 SKIP 都会让套件以非零退出；只有显式设置
`REVERSI_WEB_ALLOW_SKIP=1` 才接受不完整的运行。

运行方式见 [WEB_BUILD.md](WEB_BUILD.md) §4。Phase 10 时桌面回归为
Debug/Release/ASan+UBSan 各 15/15（均无窗口）；v1.2.0 的 17/17 见 §2。
Phase 10 的 GUI 测试在本机 **NOT TESTED**：当前会话没有可用显示器，
`InitWindow` 报 `GLFW: Failed to get primary monitor`。

未验证：Safari、Firefox、真实 iOS/Android 设备、真实 GPU 下的帧率、
浏览器存储被禁用的实机路径（代码有错误横幅与只读降级）。

## 8. 已知测试缺口

- 键盘导航、窗口缩放、真实音频听感、Windows/Linux 物理机、Windows/macOS
  桌面 GUI、多显示器/锁屏路径未验证。
- 鼠标点击已由用户人工验证，但没有自动化 GUI 点击回归测试；CI 仅在
  Linux Xvfb 下运行冒烟与自动对局。
- 终局统计只计一次由 `app.autoplay` 集成测试覆盖；结算后撤销与统计撤回
  仍有 `storage.stats` 单元测试和应用层代码审查，但没有自动化 GUI 点击测试。
- 存档的“终局动画中断”路径通过提前记录统计与自动存档保留逻辑处理，
  未做崩溃注入/断电测试。

## 9. AI 引擎基准（Phase 11）

```bash
./build-release/bin/reversi_ai_bench --games 3
./build-release/bin/reversi_ai_corpus
```

`reversi_ai_corpus` 生成 14 个固定局面（开局、40/30 空中局、角点争夺、
≤3 机动、必须 Pass、终局、20/12/10/8/6/4/2 空），测量标量/位板着法生成
与落子，以及固定深度 6 和 Hard 500 ms 搜索（含剪枝率、TT 命中与峰值
RSS）。A/B 方法：把同一 `tests/bench_corpus.c` 编译到基线提交的源码，
同一时段交替运行；固定深度
节点数一致说明搜索树未变。原始输出在 `docs/benchmarks/`，结论见
[PHASE_11_REPORT.md](PHASE_11_REPORT.md) §5。测量期间机器有外部负载，
绝对毫秒数不代表空载性能。


## 10. Reversi Lab 测试（Phase 12）

```bash
./build-release/bin/reversi_analysis_bench
```

- `analysis.session`：标准开局、导航、主线/分支、Pass、自定义编辑、
  History 导入、棋谱 v1 往返与损坏输入拒绝。
- `ai.analyze`：三个候选的排序与合法性、PV 重放、4 空精确解与
  `ai_endgame_solve` 分数一致、取消后无结果、黑方视角转换。
- `analysis.lab`：generation 守卫；进入 Lab 后启动分析，退出时取消并 join。
- `app.lab`（Debug + GUI）：在 Lab 页面渲染 20 帧并正常退出。
- 基准输出见 `docs/benchmarks/phase12-analysis.txt`；绝对时间受机器负载
  影响。
- 未覆盖：浏览器内 Lab 交互、自动化点击/键盘、分钟级连续分析。


## 11. 实验平台测试（Phase 13）

```bash
ctest --test-dir build -R '^experiment\.'
scripts/run-experiments.sh      # 手动，完整实验
```

- `experiment.regression`：拒绝错误配置；4 局 Random vs Greedy 的结果/
  ply/换色与固定 oracle 一致；4 局 depth 3 的 TT/no-TT 结果与子数一致；
  两次运行逻辑结果相同；CSV/JSON/Markdown/HTML 均非空且 JSON 含种子。
- 完整实验不进入 CI；由 `scripts/run-experiments.sh` 生成
  `docs/experiments/`，索引由 `scripts/make-experiment-index.py` 从
  `summary.json` 生成。
- 性能阈值不在 CI 判定：不同机器/负载的耗时不可比；回归只判断非法、
  超时、中止与确定性结果。


## 12. 在线对战测试（Phase 14）

```bash
ctest --test-dir build -R '^net\.'
scripts/run-online.sh      # 本地服务器 + Web 静态页，手动联机
```

- `net.protocol` / `net.ws`：字段与帧的严格解析、掩码方向、分片、长度上限、
  20k 随机帧模糊。
- `net.server`：两名独立 TCP 客户端完成整局；非法/重复/越权；断线重连；
  房主离开；多房间并发；服务端重启；双向落子超时；限速（错误先送达后
  关闭）；重连到期判负与旧令牌拒绝；满房；重连进已结束房间（finished +
  真实结束原因）；随机帧不崩溃。
- `net.client` / `net.session`：桌面线程客户端与浏览器共用会话逻辑；创建/
  加入/ready/落子/seq。
- `app.net`：Online 大厅渲染 20 帧。
- 浏览器双客户端（本地）：两个隔离 Chromium 上下文经 nginx HTTPS/WSS
  完成建房/加入/落子/投降；步骤与证据见
  `docs/WEB_ONLINE_TESTING.md`。
- 本地 TLS/WSS：nginx 自签证书 TLSv1.3 终止，`openssl s_client` 手工升级
  与浏览器均收到 `101`。
- `scripts/web-online-e2e.mjs`：双浏览器联机脚本（需 Playwright 与本地
  nginx），断言连接/建房/加入/落子 `seq`/投降终局；未进 CI。
- 桌面↔Web：桌面 `ws://127.0.0.1:8080` 建房、浏览器 WSS 加入，完成
  落子与投降终局；步骤见 `docs/WEB_ONLINE_TESTING.md`（手工/主机脚本）。
- **未覆盖**：真实网络延迟/丢包、Safari/Firefox、可信证书链与生产代理、
  公网部署、在线终局导入 Lab 复盘。
