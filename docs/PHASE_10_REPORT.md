# Phase 10 报告：WebAssembly 跨平台移植

日期：2026-10-22  
版本：**1.1.0**（源码；未发布、未推送、未部署）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
Homebrew Clang 23.1.3，CMake 4.4.4，Emscripten **6.0.13**，
raylib **6.2**，Node 26.12，Playwright 1.64.1（系统 Chrome）  
起始提交：`e6c0d6f`（v1.0.1 文档收尾）  
阶段提交：`6b55d99`  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 阶段目标与完成矩阵

目标：把同一份 ISO C11 规则、AI 与存档校验编译为 WebAssembly，在浏览器
中完整运行；桌面构建与行为不变。

| 任务 | 交付 | 状态 |
|---|---|---|
| 1. Web 可移植性审计 | `docs/WEB_PORTING_PLAN.md`（10 项不兼容点、复用边界、适配接口） | PASS |
| 2. Emscripten 构建 | `scripts/build-web.sh`、`web/`、`docs/WEB_BUILD.md`；raylib Web 后端 | PASS |
| 3. 主循环改造 | `app_boot` / `app_frame` / `app_release`；`emscripten_set_main_loop_arg` | PASS |
| 4. Web AI 方案 | 独立 Web Worker 承载同一份 C 搜索；取消 = terminate | PASS |
| 5. 浏览器持久化 | `localStorage` 适配 `storage.h`；导出/导入；错误横幅 | PASS |
| 6. 网页界面 | 加载/错误/全屏/GitHub/版本；五种视口；触摸落子 | PASS |
| 7. 资源与性能 | 预加载字体；体积、加载、堆、帧率实测 | PASS |
| 8. Web 自动化测试 | `tests/web/` 六项；真实 Chrome | PASS（6/6） |
| 9. 部署工程 | `web/dist/` + `_headers`；本地预览脚本 | PASS（未公开部署） |
| 桌面回归 | Debug/Release/ASan 无窗口各 15/15 | PASS |
| 桌面 GUI 回归 | 本轮会话无显示器 | NOT TESTED |

## 2. 关键架构决策

1. **AI 用独立 Web Worker，而不是 pthreads。**
   现有搜索没有步进接口，必须整段运行；Emscripten pthreads 要求
   SharedArrayBuffer + COOP/COEP，且浏览器主线程不能 `pthread_join`。
   Worker 方案复用同一份 `core`/`ai` 源码，主线程只做消息收发，取消直接
   `terminate()`，部署不需要跨源隔离头。

2. **持久化用 `localStorage`，而不是 IDBFS/IndexedDB。**
   现有存储 API 是同步的（启动读取设置/统计/槽位，落子在写入路径内），
   IDBFS 会引入异步初始化竞态。存档上限 8 KiB、总量远低于 5 MB 限额，
   `localStorage` 的同步语义与调用顺序完全匹配。页面提供 JSON 导出/导入；
   存储不可用时显示错误横幅。

3. **Web 端关闭 `FLAG_WINDOW_HIGHDPI`。**
   raylib Web 的 `SetWindowSize` 在 HIGHDPI 下把尺寸再除以
   `GetWindowScaleDPI()`，而 GL viewport 保持除法后的值，导致画布只绘制
   左上角 1/DPR。关闭后 `canvas == screen == viewport`，布局正确；代价是
   以 CSS 分辨率渲染，HiDPI 由浏览器放大。

4. **生命周期拆分但不重写应用。**
   `app_run` 只提取 `app_boot`、`app_frame`、`app_release`；桌面保留原
   `while`，Web 用 `emscripten_set_main_loop_arg`。`AppRun` 在 Web 上为
   静态存储，桌面仍在栈上；游戏逻辑、页面路由、状态机未改。

5. **窄屏对局按钮改为两行。**
   390 px 宽时五个 88 px 按钮加间距超出底栏，`game_view` 在底栏不足时
   切换到两行（撤销/重做/暂停 + 重开/主菜单），并缩小窄屏边距；桌面宽
   布局的断点与尺寸未变。

6. **测试用一个显式的诊断边界。**
   Web 构建导出 `reversi_web_*`（帧数、屏幕/鼠标、游戏状态、取消、堆），
   供 Playwright 断言；完整对局用 Debug 构建的自动对局入口。真实鼠标与
   触摸落子另有独立用例，不是用诊断函数代替 UI 输入。

## 3. 修改的文件

构建与代码（13）：

- `CMakeLists.txt`：`EMSCRIPTEN` 分支、Web/AI Worker 目标、预加载、链接参数、
  禁用 Web 不适用分支（Threads、sanitizer、`m`、GUI 测试、资源复制）。
- `VERSION` → `1.1.0`。
- `src/app/app.c`：生命周期拆分、Web 主循环、`reversi_web_*` 诊断接口、
  Web 关闭 HIGHDPI。
- `src/ui/game_view.c`：窄屏底栏两行。
- `src/ui/setup_view.c`：缩短说明文字。
- `src/platform/web/storage_web.c`（新增）。
- `src/platform/web/ai_worker_web.c`（新增）。
- `web/ai_worker_main.c`、`web/shell.html`、`web/app.js`、
  `web/style.css`、`web/ai_worker_boot.js`、`web/_headers`（新增）。
- `scripts/build-web.sh`、`scripts/serve-web.sh`（新增）。

测试（4）：

- `tests/web/lib.js`、`tests/web/run-tests.js`、`tests/web/package.json`、
  `tests/web/package-lock.json`。

文档与忽略（7）：

- `docs/WEB_PORTING_PLAN.md`、`docs/WEB_BUILD.md`、`docs/PHASE_10_REPORT.md`
  （本报告另起提交）。
- `README.md`、`CHANGELOG.md`、`docs/ARCHITECTURE.md`、`docs/ROADMAP.md`、
  `docs/TESTING.md`、`.gitignore`。

没有修改 `core/`、`ai/ai_search.c`、`ai/ai_eval.c`、`storage/save.c`、
`settings` 或 `stats` 的序列化与校验逻辑。

## 4. 实际执行的测试与结果

### 4.1 桌面（最终树）

| 配置 | 命令 | 结果 | 状态 |
|---|---|---|---|
| Debug 无窗口 | `ctest -C Debug` | 15/15，16.39 s | PASS |
| Release 无窗口 | `ctest -C Release` | 15/15，11.95 s | PASS |
| ASan+UBSan 无窗口 | `build-sanitize` + `-E 'app\.'` | 15/15，24.60 s，无报告 | PASS |
| Debug GUI | `app.smoke` / `app.autoplay` | `GLFW: Failed to get primary monitor` | NOT TESTED |

GUI 失败发生在 `InitWindow`，早于任何本轮改动的代码；同一会话中直接运行
Release 二进制也报同样错误。因此记为环境不可用，不记为代码失败。

### 4.2 浏览器（`tests/web/run-tests.js`）

| 用例 | 结果 |
|---|---|
| `smoke` | PASS（无控制台错误；画布与逻辑尺寸一致） |
| `gameplay` | PASS（真实鼠标落子；Hard AI 在 Worker 中完成） |
| `persistence` | PASS（自动存档写入、刷新、继续恢复） |
| `responsive` | PASS（五种视口，无页面滚动） |
| `difficulties` | PASS（Easy / Medium / Hard 各完成一次 AI 回应） |
| `mobile-touch` | PASS（390×844 触摸落子并写入自动存档） |
| `pagehide` | PASS（`pagehide` 重写自动存档） |
| `cancel` | PASS（Worker 被终止；取消后帧数继续增长） |
| `autoplay` | PASS（Debug 构建完整对局到终局） |

测试环境：headless Chrome + SwiftShader；不是 GPU 或真实手机。

计划 §9 的“桌面/Web 规则一致性”比对（同一着法序列导出棋盘并与本地 C
程序比较）未实现：Web 诊断接口没有导出棋盘状态，也没有对应的 C 比对
程序。该验收项标记 **NOT TESTED**，不计入上表的 PASS。

### 4.3 手动/截图验证

- 桌面 1280×800：菜单、新游戏、对局、AI 落子、结算截图核对。
- 390×844（DPR 3，触摸模拟）：菜单铺满、设置页按钮可点、对局棋盘约
  42 px 单元格、底部两行按钮完整可见、触摸落子 d3 正确翻转。
- 刷新后 `Continue` 恢复到相同局面（Black 4 / White 1，White to move）。
- `favicon.ico` 缺失导致的控制台 404 已消除。

## 5. 性能与兼容性数据

### 5.1 体积

| 文件 | 大小 |
|---|---:|
| `reversi.wasm`（Release） | 309 KB |
| `ai_worker.wasm`（Release） | 49 KB |
| `reversi.js` | 149 KB |
| `reversi.data`（字体预加载） | 818 KB |
| `index.html` + CSS + 页面 JS + 图标 | 约 360 KB |

### 5.2 加载与内存（Chrome headless，本地 HTTP）

- 冷启（新浏览器进程）：3.31 s；同进程后续页面：0.38 / 0.54 / 0.55 / 0.91 s。
- 复核时机器被另一个项目的 Playwright 进程占满（load average ~30），同一
  构建的冷启为 4.3–6.2 s；该区间不代表空载性能。
- 堆内存：16,973,824 B（约 16.2 MiB），Hard 搜索前后无增长。
- 首帧前的资源请求全部 200；无 4xx/5xx。

### 5.3 帧率与 AI 响应

- AI Hard 搜索期间（1 s 窗口）：空载约 28.8 FPS；复核时机器负载 ~30，
  同一窗口约 19 FPS。两次都帧计数连续增长，无卡顿。
- 菜单/对局在 SwiftShader 软件渲染下约 30–48 FPS；真实 GPU 未测。
- 取消后 500 ms 内帧数继续增长（22 帧），Worker 已终止。

### 5.4 兼容性

- 存档格式仍为版本 1，设置/统计格式未变，桌面与 Web 可互相理解内容。
- 桌面 CMake 默认值、GUI 测试与发行流程未改变。

## 6. 遗留风险与技术债

1. **Web 以 CSS 分辨率渲染**：HiDPI 屏幕上文字与棋子的锐度低于桌面；
   根因是 raylib Web 的 DPR/viewport 处理，需要上游修复或自定义渲染
   路径，未在本阶段处理。
2. **Safari / Firefox / 真实移动设备未验证**：只测了 Chrome（桌面 +
   移动模拟）。触摸、音频解锁、`localStorage` 限额在真实设备上可能不同。
3. **`localStorage` 可被清理**：页面已提示并提供导出/导入，但不做同步
   或备份承诺。
4. **取消语义是丢弃**：终止 Worker 会放弃未完成的搜索结果；对玩法无
   影响，但与桌面的协作式取消不完全相同。
5. **CI 尚未构建 Web 目标**：`ci.yml` 仍只覆盖桌面；Web 构建依赖本机
   Emscripten 6.0.13。接入 CI 需要额外的 SDK 安装与缓存策略。
6. **Worker 文件名固定**：`_headers` 已从 `immutable` 改为
   `must-revalidate`；若未来需要长期缓存，应改为文件名哈希。
7. **诊断接口常驻导出**：`reversi_web_*` 仅在 Web 构建中存在，暴露的是
   本地游戏状态；没有安全影响。审查后决定保留（浏览器测试依赖），并在
   `WEB_PORTING_PLAN.md` §12 记录为接受的偏差。
8. **未推送、未打标签、未部署**：`phase-10-web` 分支只在本地；GitHub
   Actions 未验证 v1.1.0。

## 7. 回滚方法

本阶段提交为 `6b55d99`：

```bash
git revert 6b55d99
```

局部回滚：

- **Web 构建**：删除 `web/`、`src/platform/web/`、`scripts/build-web.sh`、
  `scripts/serve-web.sh`、`tests/web/`，移除 `CMakeLists.txt` 的
  `if(EMSCRIPTEN)` 分支。
- **主循环**：`git show e6c0d6f:src/app/app.c` 取回原文件；桌面行为在
  本阶段已回归通过，回滚后应重跑 15 项测试。
- **UI**：`game_view.c` 的窄屏分支只在宽度 <520 时生效；回滚删除该分支
  即可，桌面布局不受影响。
- **版本**：`VERSION` 回退到 `1.0.1`。

Web 产物在 `build-web*/` 与 `web/dist/`，均为忽略目录，可直接删除。

## 8. 审查修正（`/code-review max --fix`）

两轴审查（Standards / Spec）后的修正与复验：

| 审查发现 | 修正 | 复验 |
|---|---|---|
| 页面关闭时没有最终保存 | `pagehide`/`visibilitychange` → `reversi_web_persist` | `pagehide` 用例 PASS |
| Web 端 Worker 启动失败会退回主线程同步搜索 | Web 端改为落合法着法 + 提示，不阻塞 | 代码审查 + 三档 AI 用例 |
| Worker 返回错误时静默降级 | 显示“搜索失败”提示 | 代码审查 |
| 构建脚本不校验 Emscripten 版本 | 校验 6.0.13，可显式覆盖 | `build-web.sh` 运行 PASS |
| 生命周期没有独立 update/render | 拆出 `app_run_update` / `app_run_render`，清理改名 `app_shutdown` | 桌面 + Web 回归 |
| 仅测试 Hard | 新增 Easy/Medium/Hard 用例 | `difficulties` PASS |
| 小屏只检查布局，不检查可操作性 | 新增 390×844 触摸落子用例 | `mobile-touch` PASS |
| SKIP 可让套件静默通过 | 默认 SKIP 即失败 | 套件行为已改 |
| 未使用的存储探测、重复常量、无用的 generation 参数 | 删除/改用核心常量 | 构建无警告 |
| `_headers` 的 `immutable` 缓存 | 改为 `must-revalidate` | 文件审查 |
| 文档声称 `app_web.c`、着法协议 | 在 `WEB_PORTING_PLAN.md` §12 与 `ARCHITECTURE.md` §19 记录实际边界与棋盘快照协议 | 文档同步 |

审查后的最终验证：桌面 Debug / Release / ASan+UBSan 无窗口各 15/15；
浏览器套件 9/9；桌面 GUI 仍为 NOT TESTED（无显示器）。审查期间机器被另
一个项目的测试占用，用例改成基于 `screenIndex`、帧计数与自动存档的等待，
不再依赖固定延时。

### 8.1 第二次审查（Phase 10 + 11 联合，2026-10-24）

| 审查发现 | 处理 | 复验 |
|---|---|---|
| 计划 §2.6 要求“点击后启用声音”的提示，页面只有静默 `resume()` | `shell.html` 增加提示，首次指针/键盘输入后隐藏；无 `AudioContext` 时直接隐藏 | 代码审查；浏览器 **NOT TESTED** |
| 计划 §7 要求触控目标最小 44 px，窄屏按钮为 42 px | `game_view.c` 窄屏按钮高度改为 44 px | 代码审查；浏览器 **NOT TESTED** |
| 计划 §9 的桌面/Web 规则一致性比对 | 未实现 | **NOT TESTED**（见 §4.2） |
| 计划 §7 的“<760 px 两行”与实现阈值不一致 | 实际：紧凑布局在宽 <1000 或高 <680 时启用，底栏在宽度 <520 时改两行；覆盖五档目标视口 | 文档记录 |

设置页说明文字缩短属于桌面可见文案变化，已在 `WEB_PORTING_PLAN.md` §12
记录；不改变布局或交互。

## 9. 下一阶段依赖

- 推送 `phase-10-web` 前决定是否合并到 `main`；合并后应观察 CI（当前
  CI 不构建 Web，建议新增一个独立的 Emscripten 作业）。
- 若要发布 `v1.1.0`，需要新增标签并确认三平台桌面包与 Web 产物；本阶段
  未执行任何远程操作。
- 跨浏览器与真实设备验证应优先于“Web 正式支持”的对外声明。
