# Changelog

本项目遵循 Keep a Changelog 的章节结构，版本号使用语义化版本。

## [Unreleased]

## [1.2.0] - 2026-10-23

### Added

- `core/bitboard`：64 位棋盘表示、8 方向合法着法与翻转、位运算落子、
  计数、Pass 与终局判定；位移带边界掩码，popcount/ctz 有内建与可移植
  两条路径。
- `ai/ai_endgame`：全宽度残局求解器，区分 `AI_ENDGAME_EXACT` 与
  `AI_ENDGAME_INCOMPLETE`；Hard 在剩余 ≤12 空时接入。
- `tests/test_bitboard.c`（621,111 次断言）与 `tests/test_endgame.c`：
  位板与标量规则的差分测试、残局分数对照、取消分类。
- `tests/bench_corpus.c`：固定局面语料、标量/位板微基准与固定深度搜索；
  可在基线提交上编译做同时段 A/B。
- `docs/AI_ENGINE.md`、`docs/benchmarks/`。
- Web 页面在首次输入前提示“点击后启用声音”；窄屏操作按钮提高到 44 px。

### Changed

- 搜索内部改用 Bitboard 生成着法、落子并取得翻转位；`GameState` 仍用于
  评估与对外结果，`Board`/`Match`/存档/UI 接口未变。
- Zobrist 改为增量更新：落子时异或落点、翻转位与回合键，不再每节点重算
  64 格。
- 叶子节点不再构造着法数组。
- 搜索计时改为平台单调时钟（Windows / Emscripten / POSIX），C11
  `timespec_get` 仅作回退。
- EMSCRIPTEN 构建仅对 raylib 开启 GNU C 扩展；第一方源码保持严格 C11。

### Fixed

- 残局求解器耗尽预算后，回退搜索会被同一时钟立即判为超时并返回无着法；
  现在求解器只用 75% 预算，回退至少保留 50 ms。
- Linux 严格 C11 构建下 `<time.h>` 不声明 `CLOCK_MONOTONIC`：搜索与求解器
  会静默回退到非单调的 `timespec_get(TIME_UTC)`，语料基准则直接编译失败。
  现在在三个使用该时钟的编译单元中、包含头文件之前定义
  `_POSIX_C_SOURCE=200809L`（仅 `__linux__`）。
- 文档不再宣称 27 局对局基准“与基线一致”（编译器与对局集合不同），
  ROADMAP 的 Phase 11 数字改用最终 A/B。

### Verified

- 桌面 Debug / Release / ASan+UBSan 无窗口各 17/17。
- GitHub Actions `40073769484` 9/9：Linux/Windows/macOS Debug 各 17/17、
  三平台 Release 各 17/17（含 Windows `--version` 断言）、Linux ASan+UBSan
  17/17、Xvfb GUI 2/2、Linux 发行包验收。
- 固定深度 6 的 A/B 中两套引擎节点数完全相同；开局 73.8→9.7 ms、
  中局 516→198 ms、残局 12 空 72→8.6 ms；10/6 空的小幅回退在噪声内。
- Hard 500 ms 预算下，开局 7→10 层、中局 6→7 层、残局 12 空精确
  （281→22.6 ms）；27 局对局基准 `illegal=0 failures=0 unfinished=0`。
- Emscripten Release 构建通过；Web 浏览器套件在 1.2.0 上 **NOT TESTED**
  （预览服务因系统内存压力停止）。

## [1.1.0] - 2026-10-22

### Added

- WebAssembly 构建：`emcmake` + raylib 6.2 Web 后端，输出可直接部署的
  `web/dist/`（`index.html`、JS、WASM、预加载资源）。
- `scripts/build-web.sh`、`scripts/serve-web.sh`、`docs/WEB_BUILD.md`。
- 浏览器主循环：`app_boot` / `app_frame` / `app_release` 拆分，
  Web 端使用 `emscripten_set_main_loop_arg`。
- `src/platform/web/storage_web.c`：以 `localStorage` 实现 `storage.h`
  的 7 个接口，存档格式与严格校验不变；页面支持导出/导入存档。
- `src/platform/web/ai_worker_web.c` 与独立 Web Worker：Easy/Medium/Hard
  继续使用同一份 C 搜索代码，取消时终止 Worker，不阻塞页面。
- 网页容器：加载/错误状态、全屏、GitHub 入口、版本号、响应式布局与
  触摸落子；窄屏下对局操作按钮改为两行。
- `tests/web/`：Playwright + 系统 Chrome 的浏览器回归测试（冒烟、对局、
  AI、取消、持久化、五种视口、Debug 自动对局）。

### Changed

- Web 端关闭 `FLAG_WINDOW_HIGHDPI`：raylib Web 的 `SetWindowSize` 会按
  DPR 再除一次尺寸，而 GL viewport 保持除法后的值，导致 HiDPI 画布只
  绘制左上角。Web 以 CSS 分辨率渲染，浏览器负责放大。
- `setup_view` 的说明文字缩短，避免窄屏裁切。
- `app_run` 的帧体拆为 update / render；退出清理命名为 `app_shutdown`。

### Fixed

- 页面隐藏/关闭时没有最终保存：新增 `pagehide` 与 `visibilitychange`
  触发器，调用 `reversi_web_persist` 重写自动存档、设置与统计。
- Web 端 Worker 启动失败时会退回主线程同步搜索：现在直接落一个合法
  着法并显示提示，不再阻塞页面。
- Worker 返回错误时静默降级：现在显示“搜索失败”提示。
- `build-web.sh` 未校验 Emscripten 版本：现在要求 6.0.13，可用
  `REVERSI_WEB_ALLOW_ANY_EMSDK=1` 覆盖。
- 删除未使用的 `reversi_web_storage_available` 探测。
- `_headers` 对固定文件名的 JS/WASM/DATA 使用 `immutable` 缓存，导致
  更新可能不生效；改为 `must-revalidate`。

### Verified

- 桌面 Debug 无窗口 15/15、Release 15/15、ASan+UBSan 15/15。
- 浏览器套件 9/9 通过（含三档 AI、触摸、`pagehide` 保存）；五种视口
  无页面滚动；完整对局数秒内完成；AI 搜索期间页面持续渲染（SwiftShader
  软件渲染，帧率受机器负载影响）。
- Release WASM 309 KB，AI Worker WASM 49 KB，预加载资源 818 KB；
  堆内存约 16.2 MiB，搜索前后无增长。

## [1.0.1] - 2026-10-22

### Added

- 应用图标：`cmake/icons/` 下的源 PNG、macOS `.icns` 与 Windows `.ico`，
  由 `scripts/make-icon.sh` 可重复生成；macOS 应用包与 Windows 可执行
  文件现在携带图标。
- `.github/workflows/package.yml`：CI 与 Release 共用的 Release 打包
  工作流。

### Changed

- CI 的 Debug 构建矩阵与 Release 打包解耦：Release 只构建、测试并打包
  一次，由可复用工作流统一执行。
- 合并 `docs/ROADMAP.md` 中重复的 Phase 6 章节；「非目标」移至文末。
- ASan+UBSan 的通过数统一按实测口径标注（无窗口 15/15）。

### Fixed

- 打包验收脚本在 `pipefail` 下使用 `find | head` 可能因 SIGPIPE 退出；
  改为精确匹配唯一包根目录。
- 打包验收脚本现在校验归档 `.sha256`、连续启动两次、检查用户数据
  持久化与许可证文件，并在 Linux 上检查 `ldd` 缺失依赖。
- macOS 应用包未携带 `LICENSE` 与 `THIRD_PARTY_NOTICES.md`；现已复制到
  `Contents/Resources/`。
- Windows 包 `--version` 冒烟未检查退出码与版本文本；现已断言两者。
- 发布脚本与 CI 打包改为强制获取固定 raylib 6.2，避免使用未审计的
  系统版本。
- 字体加载的重复卸载判断集中到 `unload_if_not_default()`。

### Verified

- 本机 macOS arm64：Debug 17/17、Release 15/15、ASan+UBSan 无窗口 15/15。
- GitHub Actions `39633821096` 9/9 通过，包含 Windows Debug 与三平台
  Release 打包；发布工作流 `39634542587` 完成三平台打包并发布。
- 发布后通过 API 重新下载 macOS 归档：SHA-256 与 `SHA256SUMS` 一致，
  `scripts/verify-package.sh` PASS，包内图标与 ad-hoc 签名有效。

## [1.0.0] - 2026-10-21

首个正式发行版本。发布工程、跨平台打包与安装验收完成；游戏玩法与
Phase 6 一致。

### Added

- `VERSION` 单一版本源与生成式版本头；`--version` 输出版本、提交与构建类型。
- macOS `Reversi.app` 应用包、自定义 `Info.plist`、Resources 资源布局与
  ad-hoc 重签名（可选 Developer ID）。
- CMake/CPack 发行包：macOS ZIP、Windows ZIP、Linux ZIP/TGZ，附 SHA-256。
- Windows 静态 MSVC 运行库与版本资源。
- `scripts/package.sh` 与 `scripts/verify-package.sh`。
- GitHub Actions Debug/Release 矩阵、ASan+UBSan、Linux Xvfb GUI 与产物上传。
- 标签触发的 GitHub Release 工作流与 `SHA256SUMS`。
- `docs/RELEASE.md`、`docs/PHASE_7_REPORT.md` 与 README 实际截图。

### Changed

- 构建产物在 macOS 上改为应用包；字体从 `Contents/Resources/assets` 加载。
- 版本号从 0.1.0 提升到 1.0.0。
- README 重写发行状态、平台支持、安装与发布说明。

### Verified

- macOS 本机 Debug 17/17、Release 15/15、ASan+UBSan 17/17。
- 全新 Release 构建树联网获取 raylib 6.2，15/15，并通过独立包启动验收。
- macOS 发行包从任意工作目录、隔离用户目录启动，资源完整。
- 独立 Debug 包自动完成一局并持久化统计；重启后读取并递增。
- GitHub Actions 运行 `39597445741`：Linux/Windows/macOS 的 Debug 与
  Release、Linux ASan+UBSan、Linux Xvfb、Linux 发行包验收全部通过；
  Windows 发行包 `--version` 实际执行成功。
- 标签发布运行 `39609234964` 完成三平台打包并发布 GitHub Release；
  发行文件与 SHA-256 见 `docs/RELEASES.md`。
- 具体运行号与平台限制见 `docs/PHASE_7_REPORT.md`。

## [0.1.0] - 2026-10-21

首个可交付版本。ISO C11 + raylib 6.2，包含完整规则引擎、三档 AI、
桌面 UI、持久化、音效与测试/审计文档。

### Added

- 完整 Reversi 规则引擎：八方向翻转、合法着法、Pass、终局与结果。
- 对局状态机与历史：`PLAYER_TURN` / `ANIMATING` / `PASSING_TURN` /
  `PAUSED` / `GAME_OVER` / `AI_THINKING`，撤销/重做与重开。
- Easy / Medium / Hard AI：Alpha-Beta、迭代加深、Zobrist 置换表、
  难度化评估；后台线程搜索与取消。
- 本地双人、人机对战（执黑/执白）、三档难度。
- 菜单、开局设置、对局、暂停、历史、结算、设置、帮助、统计、读档页面。
- 存档：版本化文本格式、FNV-1a 校验、着法回放、原子写入、自动存档与
  3 个手动槽位；严格拒绝损坏/不兼容/非法存档。
- 设置持久化：音量、音效、动画、动画速度、合法提示、最后一步高亮、
  默认难度、明暗主题。
- 统计：总对局、人机 W/D/L、分难度局数/胜/平/负、PVP 局数/平局；
  结算后撤销会撤回记录。
- 程序化音效：落子、翻转、按钮、胜利、失败、平局；无设备时静默降级。
- Inter 4.1 字体（OFL 1.1）与字体缺失回退。
- 自动化测试、AI 基准、ASan/UBSan 构建选项。
- GitHub Actions：Linux/Windows/macOS Debug 构建与测试矩阵，以及 Linux
  Xvfb GUI 冒烟。
- `docs/BUILD.md`、`docs/TESTING.md`、`docs/PHASE_6_REPORT.md`、
  `THIRD_PARTY_NOTICES.md`。

### Changed

- 统计记录在终局状态产生时立即写入，避免终局动画中退出/存档导致漏记。
- 存档恢复直接使用 `match_restore_checked()`，失败时不修改目标对局。
- 统计记录/撤回共享同一条调整路径；三个结算入口共用最终化 helper。
- 文本行拆分与整数解析集中到 `storage/text`。
- 动画速度档位集中到 settings 模块；AI 内部入口增加配置校验。
- 文档中的测试数量、性能数据与平台状态按实际运行结果更新。

### Fixed

- 结算后撤销导致统计重复计数。
- PvE AI 回合暴露合法着法提示。
- 一次性难度选择覆盖默认难度设置。
- 帮助页翻页无按钮反馈音。
- 终局动画期间自动存档被提前删除。
- 统计页分难度平局不可见。
- 读取最终动画中的存档时可能漏记已完成对局。
- 未使用的错误音效、调试启动钩子与重复解析实现。

### Verified

- Debug 17/17（含 GUI 冒烟与自动对局）、Release 15/15、ASan+UBSan 15/15。
- 全新 Debug 构建树 15/15。
- AI 基准 27 个局面 `illegal=0 failures=0 unfinished=0`。
- 主菜单实际窗口截图、字体/音频/清理日志、无资源字体回退、自动对局
  600 帧与统计递增；鼠标点击流程经用户人工验证。
- GitHub Actions `39588002802`：Linux、Windows（MSVC）、macOS 的 Debug
  构建与 15/15 无窗口测试通过；Linux Xvfb 软件渲染下 GUI 冒烟与自动对局
  通过。托管运行器为 VM，不等同物理机验证。
- 键盘导航、窗口缩放、真实听感、Windows/Linux 物理机与 Windows/macOS
  桌面 GUI 仍为 NOT TESTED。
