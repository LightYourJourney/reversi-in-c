# Phase 6 最终审计报告

日期：2026-10-21  
平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64  
工具链：Apple Clang 21，CMake 4.4.4  
固定点：Phase 5 完成提交及 Phase 5 审查修正  
状态标记：PASS / FAIL / NOT TESTED

## 1. 项目功能完成矩阵

| 功能 | 实现 | 验证 | 状态 |
|---|---|---|---|
| 标准 8×8 规则、八方向翻转、合法着法 | `core/board`、`core/rules` | `core.board`、`core.rules`、500 局随机 oracle | PASS |
| Pass、终局、胜负/平局 | `core/game`、`core/match` | `core.game`、`core.match`、`core.random` | PASS |
| 对局状态机、暂停、重开、撤销/重做 | `core/history`、`core/match` | `core.history`、`core.match` | PASS |
| Easy / Medium / Hard AI | `ai/ai`、`ai/ai_eval`、`ai/ai_search` | `ai.core`、AI 基准 27 局 | PASS |
| 后台 AI 线程、取消、快照隔离 | `ai/ai_worker` | `ai.worker`、`ai.core` | PASS |
| 本地双人、人机执黑/执白 | `app`、`ui/setup_view` | UI 逻辑测试、自动对局 | PASS |
| 菜单/设置/对局/暂停/历史/结算/帮助/统计/读档页面逻辑与渲染 | `ui/*` | UI 逻辑测试、真实窗口截图、GUI 冒烟 | PASS |
| 逐按钮点击/键盘 GUI 流程 | 全应用 | 合成输入未送达窗口 | NOT TESTED |
| 存档/读档、自动存档、3 个槽位、删除确认 | `storage/*`、`app` | `storage.save`、自动对局、UI 测试 | PASS |
| 损坏/截断/版本/校验和/非法存档拒绝 | `storage/save` | `storage.save` | PASS |
| 设置持久化、明暗主题 | `settings/*`、`ui/theme` | `storage.settings`、`ui.views` | PASS |
| 统计（总/人机/分难度/平局）、撤销撤回 | `storage/stats`、`app` | `storage.stats`、自动对局计数 | PASS |
| 程序化音效、无设备降级 | `audio/audio` | `audio.noop`、GUI 日志 | PASS |
| 真实听感与音量混合 | `audio/audio` | 未试听 | NOT TESTED |
| 字体加载与缺失回退 | `ui/fonts` | 真实窗口、无 assets 启动 | PASS |
| 布局常量与主菜单渲染 | `ui/*` | 代码审查、主菜单实际窗口 | PASS |
| 760×600 逐页与窗口缩放压力 | `ui/*` | 未执行 | NOT TESTED |
| 构建、运行、测试、打包文档 | README、docs、CHANGELOG | 实际命令复跑 | PASS |

## 2. 架构审计结论

**总体：PASS（存在一个规模观察项，不阻塞交付）。**

- **分层**：`core` 不依赖 UI/AI；`ai` 只依赖 core；`persistence` 依赖
  core；`ui` 只读取 core/settings/stats 数据结构；`app` 负责编排。
  CMake 目标与头文件方向一致。
- **唯一写入者**：`Match` 只由 `core/match` API 修改；UI 接收
  `const Match *`；AI 搜索在 `GameState` 副本上工作。
- **恢复路径**：`match_restore_checked()` 内部回放并校验，失败时目标
  `Match` 保持不变；应用层不再直接整体赋值。
- **重复实现**：统计记录/撤回共享 `adjust_record`；三个结算入口共享
  `app_complete_game_over`；存档/设置/统计共享 `storage/text`；
  统计聚合位于 `stats.h`，UI 与 app 共用。
- **全局状态**：仅音频模块有静态设备状态（单线程主线程使用）；
  没有新增可变全局状态。
- **内存与所有权**：存档读取失败不修改输出；设置/统计加载失败回退
  默认值；AI worker job 在 join/take 后释放；字体/音频/窗口在退出路径
  显式清理。
- **数组/UB**：边界访问集中封装；ASan+UBSan 全部测试通过；Clang
  Static Analyzer 未发现真实缺陷（`storage_read` 的疑似越界为误报，
  `length <= max_size <= 65536` 时 `malloc(size + 1)` 安全）。
- **错误处理**：文件缺失/损坏/版本/校验和/非法状态均有状态返回和
  用户可读提示；无设备音频/字体缺失可降级。
- **AI 安全**：每次返回前重新校验合法着法；搜索有深度与时间上限；
  取消通过原子标志；内部入口现在也校验 `AiConfig`。
- **状态机**：`match_is_consistent()` 覆盖阶段、历史当前项、最后一步、
  Pass、AI 回合与终局条件。
- **死代码/抽象**：本阶段移除未使用错误音效、调试启动钩子与重复解析；
  没有发现无法到达的公开入口。
- **观察项**：`src/app/app.c` 约 1.5k 行，仍是应用编排的集中点。拆分
  会扩大改动面，当前未做无收益重构；模块边界与测试已足够定位功能。

## 3. 实际测试结果

### 3.1 构建矩阵

| 构建 | 命令 | 结果 | 状态 |
|---|---|---|---|
| Debug + GUI | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREVERSI_ENABLE_GUI_TESTS=ON` + CTest | 16/16 | PASS |
| Debug 无 GUI | 15 个测试目标 | 15/15 | PASS |
| Release | `build-release` + CTest | 15/15；`--frames 2` 退出码 0 | PASS |
| ASan+UBSan | `build-sanitize` + CTest | 15/15，无报告；GUI 冒烟退出码 0 | PASS |
| 全新 Debug 树 | `build-phase6-fresh`（复用本地 raylib 源码） | 15/15 | PASS |
| LeakSanitizer | macOS ASan | 平台不支持，测试中止 | NOT TESTED |

CTest 注册项与实际测试程序一致：15 个无窗口测试 + `app.smoke`；
`reversi_ai_bench` 不注册（独立基准工具）。

### 3.2 断言数

`core.board` 623、`core.rules` 227、`core.game` 115、`core.random`
557,924、`core.history` 124、`core.match` 980、`ai.core` 2,977、
`ai.worker` 22、`storage.save` 91、`storage.settings` 58、
`storage.stats` 44、`audio.noop` 2、`ui.button` 30、`ui.helpers` 189、
`ui.views` 35。

### 3.3 GUI/实际程序

- `reversi --frames 2`：窗口、Inter 字体、音频初始化、两帧渲染、
  字体/纹理/音频/窗口清理成功，退出码 0。
- 主菜单实际窗口截图：7 项菜单、Continue 禁用、New game 主操作、
  字体/层级正常。
- 无 `assets/` 启动：回退默认字体并正常退出。
- Debug 自动对局 600 帧：两局完成，退出码 0；统计 `pvp_games`
  0→1→2；终局自动存档删除。
- **未完成**：逐按钮点击/键盘流程、760×600 逐页、窗口缩放、
  真实听感、多显示器/锁屏。标记 NOT TESTED。

## 4. 性能测试结果

| 项目 | 实测 | 状态 |
|---|---|---|
| 菜单渲染 300 帧 | 5.57 s ≈ 53.9 FPS；CPU user+sys 0.38 s ≈ 1.27 ms/帧 | PASS（当前会话） |
| 自动对局 600 帧 | 11.23 s ≈ 53.4 FPS；CPU 0.87 s ≈ 1.45 ms/帧 | PASS（短时） |
| AI Easy | depth 1，0–1 ms | PASS |
| AI Medium 中局 | depth 4，832 nodes，3 ms | PASS |
| AI Hard 中局 | depth 8，约 158,720 nodes，501 ms（预算 500 ms） | PASS |
| AI 合法性 | 27 局 `illegal=0 failures=0 unfinished=0` | PASS |
| AI 10 组压力 | 峰值内存约 5.19 MB | PASS（短时） |
| 存档读取 | 0.086 ms/次（1000 次，20 步存档） | PASS |
| 存档写入 | 0.162 ms/次（100 次，含 fsync/rename） | PASS |
| 多分钟/多小时稳定性 | 未做长时运行 | NOT TESTED |
| 频繁窗口重开/重启资源泄漏 | 未做 GUI 循环 | NOT TESTED |
| 窗口反复缩放 | 未做实际缩放压力 | NOT TESTED |

未做针对性性能改动：测量未发现需要优化的真实瓶颈；Hard 受 500 ms
时间预算约束，Medium/Easy 远低于交互可感知阈值。

## 5. 仍然存在的问题

1. **GUI 人工交互未验证**：当前会话窗口不在可交互 Space，合成点击未
   送达；逐按钮流程仍为 NOT TESTED。
2. **跨平台未实机验证**：Windows（Win32 线程、`%APPDATA%`、`_commit`）
   与 Linux（XDG、pthread、X11/Wayland）仅代码审查，NOT TESTED。
3. **LeakSanitizer 不可用**：macOS ASan 不支持；macOS `leaks` 对非 GUI
   测试为 0 leaks，GUI 进程泄漏报告来自系统 XPC/AppIntents 框架，
   未出现第一方栈，但不能宣称第三方零泄漏。
4. **长时稳定性/缩放压力未测**：短时 600 帧通过，长时间运行与反复
   缩放仍为 NOT TESTED。
5. **真实听感未验证**：音频初始化/加载/清理成功，音色与混合听感
   NOT TESTED。
6. **`app.c` 规模**：约 1.5k 行，长期维护时可按屏幕/持久化编排拆分，
   本阶段为避免无收益大改未拆分。
7. **存档格式只有当前撤销分支**：未来分支不写入；这是明确设计而非缺陷。
8. **raylib 联网获取未完成**：本会话首次 FetchContent 下载在超时内未结束；
   全新构建改用同一固定 raylib 6.2 源码树的离线路径验证。标准联网下载
   路径标记为 NOT TESTED。

## 6. 已修复的问题（Phase 6 与 Phase 5 审查）

- 结算后撤销导致统计重复计数；新增撤回路径与共享调整函数。
- 终局动画期间自动存档被提前删除。
- 终局状态产生时立即记录统计，避免动画中退出/保存漏记。
- PvE AI 回合泄露合法着法提示。
- 一次性难度选择覆盖默认难度。
- 帮助翻页无按钮反馈音。
- 分难度统计缺少 Draws 列。
- 存档恢复失败时可能修改目标 `Match`；改为 `match_restore_checked()`。
- 未使用错误音效、调试启动钩子、重复文本解析。
- AI 内部搜索入口缺少配置校验。
- 文档测试数量、命令、平台状态、许可证与性能数据不一致。

## 7. 跨平台验证情况

| 平台 | 构建 | 运行 | 测试 | 状态 |
|---|---|---|---|---|
| macOS arm64（Darwin 25.6.0） | PASS | PASS（窗口/自动对局） | 16/16（含 GUI） | PASS |
| Windows x64 | 未执行 | 未执行 | 未执行 | NOT TESTED |
| Linux x86_64 | 未执行 | 未执行 | 未执行 | NOT TESTED |

未生成 Windows/Linux 安装包，也不声称其可用。

## 8. 最终交付物清单

- `src/`：core、ai、persistence、settings、audio、ui、app、main。
- `tests/`：15 个自动化测试 + `bench_ai`。
- `assets/`：Inter Regular/SemiBold、OFL 许可证、资源说明。
- `CMakeLists.txt`：Debug/Release、ASan/UBSan、GUI 测试、raylib 固定版本。
- `README.md`、`docs/ARCHITECTURE.md`、`docs/BUILD.md`、
  `docs/TESTING.md`、`docs/ROADMAP.md`、Phase 0–6 报告。
- `CHANGELOG.md`、`LICENSE`、`THIRD_PARTY_NOTICES.md`。
- 已验证可执行程序：
  `build-release/package/reversi-0.1.0-macos-arm64/reversi`
  及同目录 `assets/`、`docs/`、许可证与说明；由
  `cmake --install build-release --prefix <dir> --component Reversi` 生成。
- 压缩包：
  `build-release/package/reversi-0.1.0-macos-arm64.zip`
  （约 1.2 MB，macOS arm64，实际 `--frames 2` 退出码 0）。

交付物不包含构建缓存、测试生成的用户数据或未验证平台的安装包。
