# 路线图

按阶段增量交付。每个阶段结束时必须提供可复现的构建/测试结果；
无法验证的内容明确标注“未验证”。

## Phase 0 — 工程初始化与技术架构（已完成，2026-10-19）

交付内容：

- 模块化目录与依赖方向
- CMake 构建（C11、Debug/Release、CTest、可选 ASan/UBSan）
- raylib 6.2 固定版本与系统包优先策略
- 最小可运行窗口程序与干净退出
- 不依赖 GUI 的 core.board 测试目标
- README、架构文档、路线图、Phase 0 报告

验收结果：Debug、Release、ASan+UBSan 三套构建通过；CTest 1/1 通过；
窗口程序在三种构建下均以退出码 0 正常关闭。

## Phase 1 — 规则引擎与对局状态（已完成，2026-10-19）

交付内容：

- `core/board`：8×8 棋盘、坐标/记谱映射、安全读写、初始局面、棋子统计
- `core/rules`：八方向合法着法、单/多方向翻转、合法着法生成/计数、
  非法落子拒绝、无状态落子
- `core/game`：黑方先行、回合切换、自动跳过、主动跳过限制、终局判定、
  胜负/平局、状态一致性检查
- 修正 Phase 0 在“行 0 顶部”坐标约定下的开局颜色位置
- 四组测试：board、rules、game、随机对局
- 随机对局不变量：每步合法、占用数一致、无棋子消失、有限步结束

验收结果：

- `core.board` 623 项断言、`core.rules` 227 项、`core.game` 115 项、
  `core.random` 557,924 项，全部通过。
- 500 局确定性随机对局，共 29,992 步，最长 60 步。
- Debug、Release、ASan+UBSan 三套构建全部通过。
- 规则层零 raylib 依赖。

## Phase 2 — 图形界面与交互（已完成，2026-10-19）

交付内容：

- `ui/theme.h`：颜色、字号、间距、圆角、动画时长与最小窗口尺寸令牌
- `ui/draw`：圆角面板/阴影/文字裁剪/按钮状态/程序化棋子与缓动函数
- `ui/fonts`：Inter 4.1 Regular/SemiBold，按 DPI 加载，缺失时回退默认字体
- `ui/animation`：落子缩放、翻转交错、最后一步脉冲；仅影响渲染
- `ui/board_view`：棋盘、坐标、合法提示、最后一步、光标与幽灵子
- `ui/menu_view`、`ui/game_view`、`ui/settings_view`、`ui/result_view`、
  `ui/modal`：主菜单、对局、设置、结算与确认框
- `app`：页面状态机、淡入淡出转场、模态门控、自动跳过/终局延迟、
  动画期间输入阻断、响应式布局
- `ui.button` / `ui.helpers` 测试：按钮输入、reduced-motion、缓动、命中、
  合法格判断与动画插值

验收结果：

- Debug 7/7（含 `app.smoke`）、Release 6/6、ASan+UBSan 6/6 通过；
  三套 GUI 冒烟均以退出码 0 关闭。
- 主菜单、对局、设置、结算、退出/重开/返回模态均完成窗口截图验证。
- 实际对局：开局四子、四处合法提示、D3/C5/D6 落子、翻转、计数与回合更新正确；
  非法落点被拒绝且棋盘不变；落子动画期间棋盘与操作按钮均被门控。
- 响应式布局在 760×600、800×620、1100×800、1400×1000 下无重叠/裁切；
  HiDPI 截图清晰。
- 落子与翻转动画通过窗口录屏逐帧确认；Animations 关闭时页面直接切换、
  模态/结算不淡入、按钮不缓动；字体缺失时回退默认字体且菜单正常渲染。
- 设置页三个开关可切换，并实际影响提示点、最后一步高亮与动画范围。

已知限制：设置尚未持久化；鼠标点击语义由 `ui.button` 覆盖，真实鼠标点击在
锁屏会话中无法注入，菜单/对局/模态操作通过键盘与合成输入完成验证。

## Phase 3 — 游戏流程与对局状态管理（已完成，2026-10-19）

交付内容：

- `core/history`：局面快照栈（第 0 项为起始局面）、撤销/重做游标、
  重做分支截断、跳过事件与翻转数记录
- `core/match`：对局状态机（`PLAYER_TURN` / `ANIMATING` / `PASSING_TURN` /
  `PAUSED` / `GAME_OVER`）、模式与 AI 边界、撤销/重做/暂停/继续/重开、
  阶段一致性检查；人机模式预留“撤销到玩家上一个决策点”
- `ui/setup_view`：新游戏页（模式与先手颜色；AI 缺失时禁用 Computer 并说明）
- `ui/pause_view`：暂停浮层（继续 / 历史 / 重开 / 返回菜单）
- `ui/history_view`：着法列表、当前局面高亮、重做行置灰、滚动
- `ui/game_view`：撤销/重做/暂停/重开/返回操作区、跳过提示、终局显示
- `ui/result_view`：终局比分、再来一局、撤销最后一步
- `app`：页面路由与 `AppScreen`/`MatchPhase` 双状态、跳过与终局计时、
  待执行的重开/撤销转场
- 测试：`core.history` 124 项、`core.match` 653 项断言；`ui.helpers` 扩充到
  128 项，覆盖新游戏/暂停/历史/结算的输入映射与共享 UI 辅助

验收结果：

- Debug 9/9（含 `app.smoke`）、Release 8/8、ASan+UBSan 8/8 通过；
  Sanitizer 构建另以 `--frames 5` 运行无报告。
- 5 局自动对局（不同随机种子）均走到终局：均为 60 步、历史 61 项、
  终局状态自检通过，结果覆盖白胜、黑胜与平局。
- 撤销/重做恢复一致性由 `core.history` 与 `core.match` 双层断言覆盖：
  撤销回到完全相同局面，重做回到终局，新落子截断重做分支。
- 应用层交互经调试器注入输入验证：主菜单 → 新游戏 → 开始 → 落子 →
  撤销 → 重做 → 暂停 → 历史 → 返回 → 继续；结算页“撤销最后一步”回到对局，
  “再来一局”重置为起始局面。界面在 1100×800（HiDPI 2×）下逐屏截图核对。

已知限制：AI 尚未实现，人机模式不可选；人机撤销语义有测试覆盖但没有真实
AI 对局的人工验证；设置与对局仍不持久化。

## Phase 4 — AI 与难度（已完成，2026-10-21）

交付内容：

- `ai/ai`：三档难度配置、评估入口、合法性兜底、同步控制器
- `ai/ai_eval`：角点、机动性、潜在机动性、位置权重、边稳定子、前沿子、
  棋子数与奇偶，按阶段切换权重；Easy/Medium 使用基础分量，Hard 使用全部分量；
  分量值单独暴露
- `ai/ai_search`：Negamax + Alpha-Beta、显式 pass 节点、精确终局分、迭代加深、
  时间预算、取消回调、置换表（Hard，边界标签以收紧后的窗口为准）、着法排序、
  残局深度扩展
- `ai/ai_worker`：POSIX/Windows 后台线程、状态快照、原子取消/完成标志、
  结果回收；线程失败时同步搜索兜底
- `core/match`：`AI_THINKING` 阶段、开始/取消 AI 回合、AI 阶段撤销与重开
- `ui/setup_view`：Computer 模式、先手颜色与 Easy/Medium/Hard 难度选择
- `ui/game_view`：思考状态提示、AI 阶段操作门控（暂停/棋盘禁用，撤销/重开/
  菜单可用）
- `app`：AI 回合生命周期、后台轮询、旧搜索取消、撤销到玩家决策点
- 测试与基准：`ai.core` 约 2,970 项、`ai.worker` 22 项、`ui.helpers` 175 项
  （含 6 局固定深度对局 + 1 局实际 Hard 参数完整对局、TT 一致性测试）；
  `bench_ai` 固定种子、固定深度对局与局面探针，异常时返回非零

验收结果：

- Debug 11/11（含 GUI 冒烟）、Release 10/10、ASan+UBSan 10/10 通过。
- 7 局完整人机对局（含实际 Hard 深度 10 / 500 ms）全部终局，AI 零非法着法。
- Release 基准 180 局（9 组 × 20 局）：全部 `illegal=0 failures=0
  unfinished=0`；Hard 对 Random/Greedy 20:0、对 Medium 20:0、对 Easy 17:3；
  Medium 对 Easy 20:0、对 Random 17W2D1L；Easy 对 Random 13W1D6L、
  对 Greedy 11W9L。详见 [PHASE_4_REPORT.md](PHASE_4_REPORT.md)。
- 固定深度对局中 Hard 平均约 14–35 ms/步、Medium 约 3–6 ms、Easy 约 0.1 ms；
  实际参数探针 Hard 约 347–505 ms；后台线程使主线程不阻塞。
- 搜索取消、状态快照隔离、非法着法兜底、人机撤销均有自动化测试覆盖。

已知限制：完整人机对局到结算页、人机自动跳过提示、思考中关闭窗口三项未人工
验证（逻辑由自动化测试覆盖）；基准对局使用固定深度而非实际时间预算，样本
每组 20 局；Windows 线程后端未实机运行；AI 测试进程 leaks 为 0，但 raylib/UI
进程未做泄漏检测；设置与对局仍不持久化。

## Phase 5 — 产品化功能与游戏体验（已完成，2026-10-21）

交付内容：

- `storage/storage`：平台用户数据目录、目录创建、原子写入、受限读取
- `storage/save`：版本 1 存档、FNV-1a 校验、着法序列 + 最终局面、
  严格格式/坐标/回放校验、自动存档与 3 个手动槽位
- `core/match`：`match_restore()` / `match_restore_checked()` 从着法序列恢复完整对局与历史，并可校验期望局面
- `settings/settings`：音量、音效、动画、动画速度、合法提示、最后一步、
  默认 AI 难度、主题；缺失/损坏时安全回退
- `storage/stats`：总对局、人机 W/D/L、分难度局数/胜/平/负、PVP 局数/平局；
  只有完成局计数，结算后撤销会撤回记录，重做不重复计数
- `audio/audio`：程序化落子/翻转/按钮/胜负/平局音效，无设备时静默降级
- `ui/theme`：运行时明/暗调色板；`settings_view` 扩展为 8 项持久化设置
- 新页面：帮助（3 页）、统计、存档管理；菜单增加继续/读档/帮助/统计；
  暂停与结算增加保存；删除存档带确认
- 视觉/交互修复：结果页 PvE 人称标题、白子对比、结果页保存、
  损坏自动存档禁用“继续”、弹窗文案与真实持久化语义一致、
  `U`/`Y` 快捷键与帮助一致、统计单复数

验收结果：

- Debug/Release/ASan+UBSan 无 GUI 测试均 15/15 通过，Sanitizer 无报告。
- 新增断言：`core.match` 980、`storage.save` 91、`storage.settings` 58、
  `storage.stats` 44、`audio.noop` 2、`ui.helpers` 191、`ui.views` 35。
- 自动保存 50 步对局后退出并重启，应用通过读档路径恢复到相同的
  20:34 局面与最后一步 D2；损坏自动存档保持主菜单、显示校验错误、不崩溃。
- 明暗主题、设置页、帮助页、统计页、读档页、对局页与结算页完成截图核对；
  用户数据写入 `~/Library/Application Support/Reversi`。
- 一局自动完成的 PVP 对局使 `pvp_games` 从 3 增至 4，并删除自动存档。

已知限制：当前图形会话的按键/点击注入未送达应用，逐按钮人工点击未完成；
音效未做听感验证；760×600 新页面按布局常量核对但未逐页截图；
Windows/Linux 未实机验证。详见 [PHASE_5_REPORT.md](PHASE_5_REPORT.md)。

## Phase 6 — 全面审计、测试、优化与发布准备（已完成，2026-10-21）

交付内容：

- 完整代码审计：分层、依赖、重复、全局状态、内存所有权、边界/UB、
  错误处理、资源生命周期、持久化、AI 安全、状态机、死代码、平台与依赖。
- 测试矩阵实际执行：Debug 17/17（含 GUI 冒烟与自动对局）、Release 15/15、
  ASan+UBSan 15/15、全新 Debug 构建树 15/15；GitHub Actions 上 Linux、
  Windows、macOS 各 15/15，Linux Xvfb GUI 冒烟通过。
- 性能测量：AI Hard 500 ms 预算、Medium 3 ms、Easy 0–1 ms；
  存档读 0.086 ms/次、写 0.162 ms/次；菜单/自动对局约 54 FPS。
- GUI 实机冒烟：真实窗口、Inter 字体、音频初始化/清理、主菜单截图、
  600 帧自动对局两局、统计递增与终局自动存档删除；鼠标点击流程经用户
  人工验证。
- 修复：终局统计漏记、AI 内部配置校验；此前 Phase 5 审查修正一并保留。
- 文档：`docs/BUILD.md`、`docs/TESTING.md`、`docs/PHASE_6_REPORT.md`、
  `CHANGELOG.md`、`THIRD_PARTY_NOTICES.md`，README/ARCHITECTURE 更新。
- 交付：`build-release/package/reversi-0.1.0-macos-arm64/` 与 zip，
  实际运行 `--frames 2` 退出码 0（该 0.1.0 交付物已被 Phase 7 的
  1.0.0 发行包取代）。

未验证项：键盘导航、窗口缩放、真实听感、Windows/Linux 物理机、
Windows/macOS 桌面 GUI、LeakSanitizer（macOS 不支持）、多分钟/多小时长跑。
GitHub Actions 为托管 VM，不等同物理机。详见
[PHASE_6_REPORT.md](PHASE_6_REPORT.md)。

## Phase 7 — 正式发布工程（已完成，2026-10-21）

交付内容：

- 语义化版本 `1.0.0`、生成式版本头、`--version` 与窗口标题。
- CMake install/CPack：macOS `Reversi.app`、Windows/Linux 便携 ZIP，
  Linux 另有 TGZ；每个归档附 `.sha256`。
- macOS 应用包 Resources 布局、许可证复制、ad-hoc 重签名；可选
  Developer ID 入口。
- Windows 静态 MSVC 运行库、版本资源与包内 `--version` 冒烟。
- `scripts/package.sh`、`scripts/verify-package.sh`、`docs/RELEASE.md`、
  `docs/RELEASES.md`、`docs/PHASE_7_REPORT.md`。
- CI：三平台 Debug/Release、ASan+UBSan、Linux Xvfb、Linux 发行包验收、
  产物上传；标签触发 GitHub Release。
- README 实际截图、安装、平台、构建、测试、架构与许可证说明。

验证与发布：

- 本机 Debug 17/17、Release 15/15、ASan+UBSan 15/15。
- CI 运行 `39613503376` 9/9 通过。
- `v1.0.0` 已发布：<https://github.com/LightYourJourney/reversi-in-c/releases/tag/v1.0.0>。
- 发布后审查修正已在 `main`，需要新的补丁版本标签才能进入发行附件。

剩余限制：macOS 无 Developer ID/公证、Windows 无 Authenticode 签名、
Windows/macOS 物理桌面 GUI 与 Linux 物理桌面未验证、打包 GUI 手动
存/读档未走查。详见 [PHASE_7_REPORT.md](PHASE_7_REPORT.md)。

## Phase 8 — 工程债与文档一致性（已完成，2026-10-21）

目标：在不改变游戏行为的前提下，消除文档与工程结构中已知的不一致。

交付内容：

- 合并 `docs/ROADMAP.md` 中重复的两个 Phase 6 章节；「非目标」移到全文末尾。
- 统一 ASan+UBSan 报告口径：README 明确标注为「无窗口 15/15」，
  与 `TESTING.md` 及本轮实测一致；GUI 冒烟另计。
- 提取 `.github/workflows/package.yml` 可复用工作流，`ci.yml` 与
  `release.yml` 共用同一套 Release 构建、测试、校验和规范化、Windows
  版本冒烟与产物上传；CI 的 Debug 矩阵与 Release 打包不再重复构建。
- 新增应用图标：`cmake/icons/`（1024 PNG、macOS `.icns`、Windows `.ico`），
  由 `scripts/make-icon.sh` 可重复生成；macOS 应用包声明并携带
  `Reversi.icns`，Windows 版本资源嵌入图标。
- 更新 `docs/ARCHITECTURE.md` 的资源说明、`docs/RELEASE.md` 的包内容、
  `assets/README.md` 的图标指引，并新增 `cmake/icons/README.md`。

验证（本机 macOS arm64）：

- Debug 17/17、Release 15/15、ASan+UBSan 无窗口 15/15，均无报告。
- `Reversi.app/Contents/Resources/Reversi.icns` 存在，`Info.plist`
  `CFBundleIconFile` 正确，`codesign --verify --deep --strict` 通过。
- CPack 归档内含图标，`scripts/verify-package.sh` PASS。
- `actionlint` 对三个工作流返回 0。
- 工作流改动无法在本机执行，GitHub Actions 实跑为 **NOT TESTED**，
  需推送后确认。

已知限制：Linux 便携包没有桌面集成，图标只在 macOS/Windows 生效；
Windows 图标的资源编译未在本机执行（无 MSVC），为 **NOT TESTED**。
详见 [PHASE_8_REPORT.md](PHASE_8_REPORT.md)。

## Phase 9 — v1.0.1 补丁发布（已完成，2026-10-22）

- 版本号提升到 `1.0.1`；`CHANGELOG.md` 的 `[Unreleased]` 归入该版本。
- 发布内容：Phase 7 后的审查修正、应用图标、CI 打包工作流收敛。
- 首次推送（`ae50eab`）暴露 Windows 资源编译失败：`.rc` 把反斜杠路径
  当作 C 转义；改用正斜杠后在 `323856f` 通过。
- CI 运行 `39633821096` 9/9 通过；发布工作流 `39634542587` 成功发布
  <https://github.com/LightYourJourney/reversi-in-c/releases/tag/v1.0.1>。
- 发布后重新下载 macOS 归档：SHA-256 与 `SHA256SUMS` 一致，
  `scripts/verify-package.sh` PASS。详见 [PHASE_9_REPORT.md](PHASE_9_REPORT.md)。

## Phase 10 — WebAssembly 跨平台移植（已完成，2026-10-22）

目标：把同一份 ISO C11 规则、AI 与存档校验编译为 WebAssembly，在浏览器
中完整运行；桌面构建与行为不变。版本标记 `1.1.0`（未发布）。

交付内容：

- `.github` 之外新增 `web/`：Emscripten shell、页面逻辑、样式、
  AI Worker 启动器与部署响应头。
- `scripts/build-web.sh`、`scripts/serve-web.sh`、`docs/WEB_BUILD.md`。
- `src/platform/web/`：`localStorage` 存储适配、AI Worker 适配、尺寸接口。
- `app.c` 生命周期拆分；Web 使用 `emscripten_set_main_loop_arg`。
- AI 由独立 Web Worker 承载同一份 C 搜索；取消终止 Worker。
- 页面加载/错误/全屏/导出导入；五种视口与触摸落子；窄屏对局按钮两行。
- `tests/web/`：Playwright + 系统 Chrome 的六项回归测试。

验证（本机 macOS arm64）：

- 桌面 Debug 15/15、Release 15/15、ASan+UBSan 15/15（均无窗口）。
- 浏览器套件 9/9 通过（含 Easy/Medium/Hard、触摸、`pagehide` 保存）；
  完整对局数秒内完成；五种视口无页面滚动。
- Release WASM 309 KB + Worker WASM 49 KB + 预加载资源 818 KB；
  堆约 16.2 MiB；AI 搜索期间约 29 FPS（SwiftShader 软件渲染）。

已知限制：Web 端以 CSS 分辨率渲染（raylib Web 的 HiDPI 路径限制）；
Safari/Firefox 与真实移动设备未验证；`localStorage` 可能被清理；本阶段
未推送、未发布、未公开部署。详见 [PHASE_10_REPORT.md](PHASE_10_REPORT.md)。

## Phase 11 — AI 引擎 2.0（已完成，2026-10-23）

目标：在保持规则、存档、UI 与 Web 行为不变的前提下，引入 Bitboard、
搜索热点优化与可分类的精确残局求解。版本标记 `1.2.0`（未发布）。

交付内容：

- `core/bitboard`：row-major 位映射、八方向合法着法/翻转、位运算落子、
  计数、Pass/终局；位移带边界掩码，popcount/ctz 有内建与可移植回退。
- 搜索接入：合法着法、落子与翻转位使用 Bitboard；Zobrist 改为增量；
  叶子不再构造着法数组。
- `ai/ai_endgame`：全宽度残局求解器，区分 EXACT / INCOMPLETE；Hard 在
  ≤12 空时接入，并为回退搜索保留预算。
- 平台单调时钟（Windows / Emscripten / POSIX，C11 回退）。
- `tests/test_bitboard.c`（621,111 次差分断言）、`tests/test_endgame.c`、
  `tests/bench_corpus.c`、`docs/AI_ENGINE.md` 与基准数据。

验证（本机 macOS arm64）：

- 桌面 Debug / Release / ASan+UBSan 无窗口各 17/17。
- 固定深度 6 的 A/B：两套引擎节点数相同；开局 73.8→9.7 ms、
  中局 516→198 ms、残局 12 空 72→8.6 ms（10/6 空的小幅回退在噪声内）。
- Hard 500 ms：开局 7→10 层、中局 6→7 层、残局 12 空精确
  （281→22.6 ms）。
- Emscripten Release 构建通过（wasm 316 KB）。

已知限制：中局评估仍是标量，是下一步热点；TT 生命周期与两份
Zobrist/TT 未合并；Web 浏览器套件在 1.2.0 上 **NOT TESTED**。详见
[PHASE_11_REPORT.md](PHASE_11_REPORT.md)。

## Phase 12：Reversi Lab（v1.3.0，源码）

- `analysis/`：独立 `AnalysisSession` 局面树、主线与一层分支、Pass 节点、
  自由摆盘（`VALID/CUSTOM`）、从 `History` 导入、版本化棋谱格式。
- `ai/ai_analyze`：只读 MultiPV（最多 3 个候选），同深度全窗口分数、
  TT 重建并验证的 PV、完成/精确状态；不改变正式对局选着路径。
- `ai_worker`：桌面线程与 Web Worker 均支持分析任务；generation 绑定，
  旧结果不能覆盖新局面。
- Lab UI：棋盘与候选高亮、MultiPV、评估分量（黑方为正）、时间轴、
  评分曲线、自动播放、重要落点、批次分析；宽/窄布局。
- 测试：`analysis.session`、`ai.analyze`、`ai.worker` 分析用例、
  `app.lab` 渲染冒烟。

验证（本机 macOS arm64）：

- Debug / Release / ASan+UBSan 无窗口各 20/20；Debug + GUI 23/23。
- Emscripten Release 构建通过（主 wasm 346 KB，Worker 58 KB）。
- MultiPV 基准：开局 d8 约 70 ms，中局 30 空 d8 约 2.3 s，
  8 空精确约 11 ms。

已知限制：浏览器套件未在 1.3.0 复验；Windows/Linux 未推送；Lab 交互
没有自动化点击测试；PV 可能因 TT 未命中而短于报告深度。详见
[PHASE_12_REPORT.md](PHASE_12_REPORT.md)。

## Phase 13：AI 实验平台（v1.4.0，源码）

- `src/experiment/`：版本化配置、固定种子、成对开局与换色、Random/Greedy/
  AI/Search 引擎、固定深度/节点/时间、异常分类。
- 可复现清单（运行时 Git SHA、OS/CPU/编译器/构建类型）、CSV/JSON/MD/HTML
  报告、静态索引。
- Wilson 区间与双引擎 Elo；小样本与全胜不外推。
- `experiment.regression` 确定性 oracle 进入 CTest；完整实验手动运行。
- 验收实验：TT、深度、评估、难度、固定时间、节点预算、100 局随机稳定性，
  以及规则吞吐与精确残局验证。

验证（本机 macOS arm64）：

- Debug / Release / ASan+UBSan 无窗口各 21/21；Debug + GUI 24/24。
- 7 组配对实验全部 `illegal=0 timeouts=0 aborted=0`；样本与区间见
  `docs/experiments/` 与 [PHASE_13_REPORT.md](PHASE_13_REPORT.md)。
- 固定深度/节点结果可复现；固定时间结果只描述当次运行。

已知限制：跨代码版本对照需两次构建；崩溃不保留部分原始数据；Elo 只覆盖
双引擎。方法与 CI 策略见 [BENCHMARK_METHODOLOGY.md](BENCHMARK_METHODOLOGY.md)。

## Phase 14：Reversi Online 在线对战（v2.0.0，源码）

- 独立网络层：协议 v1、WebSocket 帧/握手、权威服务端、桌面线程客户端、
  Web WebSocket 适配与会话状态机。
- 房间生命周期、颜色/ready、服务端规则校验、seq 防重复、Pass、终局、
  超时、断线重连、投降与离开。
- 安全：消息/帧上限、速率限制、连接/房间上限、令牌轮换、日志不记录令牌；
  本地只绑定回环，TLS 由反向代理终止（未实现/未测试）。
- 大厅与网络对局 HUD；`app.net` 渲染冒烟。
- 文档：NETWORK_ARCHITECTURE、NETWORK_PROTOCOL、NETWORK_SECURITY、
  PHASE_14_REPORT。

验证（本机 macOS arm64）：

- Debug / Release / ASan+UBSan 无窗口各 26/26；Debug + GUI 30/30。
- 两名独立客户端完成整局；非法、重复、断线重连、多房间、服务端重启、
  20k 随机帧与 ASan 通过。
- Emscripten Release 构建通过（wasm 385 KB）。

未验证：浏览器双客户端联机、真实网络延迟/丢包、TLS/WSS、公网部署、
在线对局导入 Lab 复盘。详见 [PHASE_14_REPORT.md](PHASE_14_REPORT.md)。

## 非目标（当前）

- 账号系统、排位、观战（在线对战已在 Phase 14 实现基础版）
- 移动端原生包（Web 构建已在 Phase 10 完成）
- 第三方 UI 框架或资源商店素材

这些方向如需加入，应先更新架构文档并评估对模块边界的影响。
