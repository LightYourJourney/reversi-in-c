# WebAssembly 移植方案（Phase 10）

日期：2026-10-22  
目标版本：**1.1.0**（本阶段只实现，不发布）  
基线：`v1.0.1` / 分支 `phase-10-web`  
工具链：Emscripten **6.0.13**（`~/emsdk`），raylib **6.2**（仓库固定版本）

## 1. 目标与约束

目标：把同一份 ISO C11 规则、AI 与对局逻辑编译为 WebAssembly，在浏览器中
完整运行；桌面构建与行为不变。

约束：

- 核心规则、AI、存档格式不得用 JavaScript 重写。
- macOS/Windows/Linux 现有构建、测试与发行流程保持可用。
- Web 专属代码集中在 `src/platform/web/` 与 `web/`，不得扩散到规则层。
- 不引入第二套规则判定；AI 不直接操作 UI 或对局状态。
- 未实际验证的浏览器能力必须标记 NOT TESTED。

非目标（本阶段）：在线对战、账号系统、移动端原生包。

## 2. Web 不兼容点审计

逐项对应任务书列出的 10 点，结论均来自源码检查（行号为 `v1.0.1`）。

### 2.1 原生窗口主循环

`app_run` 在 `src/app/app.c:1492` 内联完成窗口初始化，`while
(!WindowShouldClose())` 位于 `:1512`。raylib 6.2 的 Web 后端让
`WindowShouldClose()` 恒返回 `false` 并调用 `emscripten_sleep(12)`
（`rcore_web.c:169-183`），因此原循环在浏览器中既不会自然退出，也依赖
Asyncify。

处理：提取 `app_frame()`，用 `emscripten_set_main_loop_arg` 驱动；
Web 端退出只看 `app.running` 与帧上限。

### 2.2 线程创建与 join

唯一线程代码在 `src/ai/ai_worker.c`：POSIX `pthread_create/join`
（`:57-78`）、Win32 `CreateThread/WaitForSingleObject`（`:57-78`），
同步仅用 C11 `atomic_bool cancel/done`（`:21-22`）。主线程在
`app_cancel_ai`（`app.c:355-363`）与 `ai_worker_take`（`:152`）阻塞
join。

处理：Web 端不使用 pthread，改用独立 Web Worker（见 §5）。

### 2.3 时间测量与预算

`ai_now_ms` 使用 `timespec_get(TIME_UTC)`（`ai_search.c:43-53`），是
墙钟而非单调时钟；失败时返回 0 会静默关闭时间预算。搜索每 1024 个节点
检查一次取消/超时（`ai_search.c:13,105`）。

处理：Web 端提供单调时间函数（`emscripten_get_now()`），保持
30/200/500 ms 预算与 1024 节点检查节奏不变。

### 2.4 用户数据目录与文件系统

平台路径全部集中在 `src/storage/storage.c:89-164`：`HOME`、
`XDG_DATA_HOME`、`APPDATA`、`mkdir`、`fopen/fread/fwrite`、
`fsync`、`rename`。`save.c`、`settings.c`、`stats.c` 只做内存字符串
解析与校验。

处理：仅替换 `storage.h` 的 7 个函数（§6），存档格式与校验不变。

### 2.5 字体与资源路径

`fonts.c:7-18` 用 `GetApplicationDirectory()` 拼路径；Web 下 raylib
返回 `"/"`（`rcore.c:2745-2747`），因此需要 `/assets/fonts/...`。
macOS 的 `../Resources` 分支不会被 `emcc` 编译。

处理：用 `--preload-file assets@/assets` 把资源挂到 `/assets`。

### 2.6 音频与自动播放限制

`audio_init` 在 `app_init` 内调用 `InitAudioDevice`（`audio.c:116`），
早于任何用户手势。浏览器 AudioContext 可能处于 suspended 状态。

处理：保留合成逻辑；Web 端在首次指针/键盘输入时恢复音频上下文，并在
界面提示“点击后启用声音”。无设备/被拒时沿用静默降级。

### 2.7 窗口与屏幕缩放

`app.c:1494-1506` 设置 1100×800、最小 760×600、HighDPI。Web 下
`GetWindowScaleDPI()` 返回 `devicePixelRatio`；VSync 提示无效。

处理：画布按容器尺寸与 DPR 渲染；布局需要新的窄屏模式（§7）。

### 2.8 键盘输入与页面焦点

`read_input`（`app.c:152-195`）直接读 raylib 键盘/鼠标。浏览器中键盘
事件只在画布聚焦时到达，方向键与空格可能滚动页面。

处理：画布可聚焦；CSS 禁止页面滚动与触摸手势冲突；`preventDefault`
仅作用于游戏按键。保留 `Escape` 由页面而非 `SetExitKey` 处理。

### 2.9 页面关闭时的状态保存

桌面在 `app_run:1546-1559` 退出时自动保存。浏览器标签关闭不保证执行
`beforeunload` 中的异步逻辑。

处理：改为**每次关键状态变化后立即写入**（现有 `app_autosave` 已在
落子/撤销/暂停等路径调用），并在 `visibilitychange`/`pagehide` 时再写
一次；不依赖退出时保存。

### 2.10 CMake 平台条件与链接

当前没有 Emscripten 分支；`find_package(Threads REQUIRED)`
（`CMakeLists.txt:124`）、`UNIX AND NOT APPLE` 链接 `m`
（`:215,:240`）、Sanitizer 分支（`:139-150`）、GUI 测试
（`:452-474`）都需要 Web 条件。raylib 的 `CMakeOptions.txt` 在
`EMSCRIPTEN` 下默认 `PLATFORM=Web`。

处理：新增 `if(EMSCRIPTEN)` 分支；不改桌面分支的默认值。

## 3. 可复用的代码

| 模块 | 复用方式 |
|---|---|
| `core/*` | 100% 复用，无平台依赖 |
| `ai/ai.c`、`ai_eval.c`、`ai_search.c` | 100% 复用；仅时间函数在 Web 端替换实现 |
| `storage/save.c`、`settings.c`、`stats.c`、`text.c` | 100% 复用，含版本/校验/回放校验 |
| `ui/*` | 复用；新增窄屏布局分支，不改桌面断点行为 |
| `app/app.c` | 复用；仅拆分生命周期与 AI/存储适配 |
| `audio/audio.c` | 复用合成与降级；新增首次交互恢复 |

## 4. 必须新增的平台适配接口

1. `src/app/app.c`（`__EMSCRIPTEN__` 条件块）
   - `app_web_main_loop` 与 `app_boot` / `app_frame` / `app_shutdown`
   - `reversi_web_*` 尺寸、持久化与诊断导出
   - 说明：这些函数直接读写 `App`，因此不拆到独立文件；平台存储与 AI
     适配仍在 `src/platform/web/`。这是对 §4 初稿的修正，见 §12。
2. `src/platform/web/storage_web.c`
   - 实现 `storage.h` 的 7 个函数（§6）
3. `src/platform/web/ai_worker_web.c`
   - 实现 `ai_worker.h` 的公开接口，语义与桌面版一致
4. `web/ai_worker_main.c`
   - 无 raylib 的 Worker 入口：解析请求、调用 `ai_choose_move`、回传结果
5. `web/index.html`、`web/style.css`、`web/app.js`
   - 页面容器、加载/错误状态、全屏、导入导出、音频解锁
6. 构建脚本与 CMake 分支

桌面端不编译 `src/platform/web/`。

## 5. AI 执行方案

### 5.1 候选对比

| 方案 | 优点 | 问题 |
|---|---|---|
| A. Emscripten pthreads | 复用 `ai_worker.c` | 主线程不能 `pthread_join`；需 SharedArrayBuffer + COOP/COEP；部署与本地预览复杂 |
| B. 独立 Web Worker | 不阻塞渲染；无需 COOP/COEP；复用同一份 C 搜索 | 需要消息协议与二次构建；取消要能终止 Worker |
| C. 分帧增量搜索 | 无额外线程 | 搜索没有步进接口，需重写 `negamax`/迭代加深/置换表生命周期，风险最高 |

### 5.2 选择：B（独立 Web Worker）

- 新目标 `reversi_ai_worker`：只链接 `reversi_core` 与 `reversi_ai`，
  不链接 raylib。
- 协议：主线程发送 `{difficulty, budget_ms, seed, player, result,
  board[64]}`；Worker 校验棋盘后构造 `GameState`，调用 `ai_choose_move`，
  回传 `{ok, row, col, nodes, depth, cancelled}`。棋盘快照避免重放整局；
  合法性仍由 `ai_choose_move` 复核。
- 取消：`worker.terminate()` 后按需重建。语义从“协作式停止”变为
  “立即丢弃”，但**不会阻塞页面**，且结果本来就不需要保留。
- 结果回收：Worker 的 `message` 回调通过导出的 C 函数写入 `ai_worker`
  状态；`ai_worker_status()` 每帧轮询，不改变 `app.c` 的调用方式。
- `ai_worker_join()` 在 Web 端变为“终止并复位”，不阻塞。

### 5.3 必须验证

- Easy/Medium/Hard 三档在 Worker 中均返回合法着法。
- 取消后立即发起新搜索不会串结果。
- 搜索期间页面持续渲染（帧率与主线程心跳）。
- 与桌面使用相同的 `ai_choose_move`，不降级为随机落子。

## 6. 浏览器持久化

### 6.1 选择：localStorage 适配层

现有存储 API 是同步的：`app_init` 在启动时同步读取设置/统计/四个存档槽，
`app_autosave` 在落子路径内同步写入。IndexedDB/IDBFS 会把这条路径改成
异步并引入初始化竞态。

存档上限：每个 8 KiB，四个槽 + 自动存档 + 设置 + 统计，总量远低于
localStorage 的 5 MB 限额。

因此 Web 端实现 `storage.h` 的 7 个函数，以键名代替路径：

| 函数 | Web 语义 |
|---|---|
| `storage_user_dir` | 恒返回成功 |
| `storage_make_user_dir` | 空操作 |
| `storage_path` | 返回裸文件名作为键 |
| `storage_read` | `localStorage.getItem`，返回堆缓冲区 |
| `storage_write_atomic` | `setItem`（单键写入天然原子） |
| `storage_remove` | `removeItem`，不存在视为成功 |
| `storage_exists` | 键是否存在 |

键前缀 `reversi:`，实际键为 `settings.dat`、`stats.dat`、
`autosave.rvs`、`save1.rvs`–`save3.rvs`。

### 6.2 失败与限制

- `localStorage` 被禁用/配额满/隐私模式：捕获异常，读取返回失败，
  写入返回失败；页面顶部显示可关闭的错误横幅。
- 存储可能被浏览器清理；界面明确提示“存档保存在本机浏览器中”。
- 导出/导入：页面按钮把上述键导出为 JSON 文件并下载；导入时逐键写入，
  随后由现有严格校验拒绝损坏数据。
- 不修改 `save.c` 的版本号、校验和或回放校验。

## 7. 网页容器与响应式

- 画布占满可用区域，按 `devicePixelRatio` 设置 backing store。
- 目标视口：1920×1080、1366×768、768×1024、390×844、360×640。
- 桌面宽度沿用现有宽/窄两套布局；小于 760 px 时启用新的紧凑布局：
  棋盘优先、操作按钮改为底部两行大触控目标，最小 44 px。
- 触摸：`touchstart/touchend` 映射为落子；拖动页面被阻止，但保留
  页面内滚动区域以外的手势。
- 键盘：画布可聚焦，方向键/Enter/Esc 不触发页面滚动；
  浏览器快捷键不被拦截。
- 页面元素：标题、简短说明、加载进度、全屏按钮、错误提示、
  GitHub 链接、版本号（来自构建注入）。

## 8. 构建与部署

- `scripts/build-web.sh`：加载 `~/emsdk/emsdk_env.sh`（可用
  `EMSDK_DIR` 覆盖），`emcmake cmake` 到 `build-web/`，构建
  `reversi` 与 `reversi_ai_worker`，输出到 `web/dist/`。
- raylib 使用仓库固定的 6.2；`REVERSI_FORCE_FETCH_RAYLIB=ON`。
- 资源：`--preload-file assets@/assets`。
- 输出：`index.html`、`reversi.js`、`reversi.wasm`、
  `reversi.data`、`ai_worker.js`、`ai_worker.wasm`。
- 部署目录 `web/dist/` 可直接用于 Cloudflare Pages；附带 `_headers`
  （MIME、Cache-Control、安全头）。**本阶段不部署。**
- 本地预览：`scripts/serve-web.sh`（Python 静态服务器，固定端口）。

## 9. 测试与验收

- 桌面：Debug 17/17、Release 15/15、ASan+UBSan 15/15 保持通过。
- Web：`node` 编译/链接检查 + Playwright（使用系统 Chrome）自动化：
  页面加载、开始对局、落子、AI 三档、取消、保存/刷新恢复、
  小屏布局、控制台无致命错误。
- 规则一致性：同一着法序列在桌面与 Web 产生相同棋盘与结果
  （通过浏览器内导出状态与本地 C 程序比对）。
- 性能：首次加载、wasm 体积、AI 期间帧率与主线程心跳。
- 无法执行的真实设备浏览器标记 NOT TESTED。

## 10. 实施顺序

1. CMake/Emscripten 骨架与空白页面构建。
2. 主循环拆分与 `app_frame`。
3. 资源预加载与字体/音频。
4. 存储适配层。
5. AI Worker 与取消。
6. 网页容器与响应式/触摸。
7. 自动化测试与性能测量。
8. 文档与阶段报告。

## 11. 风险

| 风险 | 缓解 |
|---|---|
| 窄屏布局需要改动 UI 模块 | 只在宽度 <760 时进入新分支，桌面断言保持不变 |
| Worker 二次构建与模块加载顺序 | 先做最小 echo Worker 验证，再接入搜索 |
| 音频自动播放策略因浏览器而异 | 仅在用户手势后恢复；无声音时保持可玩 |
| `localStorage` 被禁用 | 错误横幅 + 只读降级，不崩溃 |
| 磁盘与构建时间 | 已安装固定 emsdk；构建产物在 `build-web/`，可清理 |

## 12. 实施结果（2026-10-22）

| 计划项 | 实际结果 |
|---|---|
| Emscripten 构建 | `scripts/build-web.sh`；Release WASM 309 KB，Worker WASM 49 KB |
| 主循环 | `app_boot` / `app_frame` / `app_release` + `emscripten_set_main_loop_arg` |
| AI | 独立 Web Worker，复用同一份 C 搜索；取消 = 终止 Worker |
| 持久化 | `localStorage` 适配 `storage.h`；存档格式不变；导出/导入已实现 |
| 页面 | 加载/错误/全屏/GitHub/版本；五种视口无滚动；触摸落子 |
| 测试 | `tests/web/` 六项用例全部 PASS |
| 性能 | 冷启 3.3 s / 热启 0.4–0.9 s；堆 16.2 MiB；AI 期间约 29 FPS（软件渲染） |

与计划的偏差：

- **HiDPI**：raylib Web 在 `FLAG_WINDOW_HIGHDPI` 下会把 `SetWindowSize`
  再除以 DPR，而 GL viewport 保持除法后的尺寸，导致画布只绘制左上角。
  实际实现改为 Web 端关闭该标志、以 CSS 分辨率渲染。
- **窄屏按钮**：原计划只要求“可操作”；实测 390 宽下单行五按钮溢出，
  因此把 `game_view` 的窄屏底栏改为两行，并缩短设置页说明文字。
- **取消验证**：UI 点击跨越 AI 完成时刻会被动画门控吞掉，自动化改用
  `app_cancel_ai` 边界做确定性验证；真实鼠标/触摸落子另有独立用例。
- **平台边界**：主循环与诊断导出留在 `app.c`（需要 `App` 内部状态），
  没有按 §4.1 初稿新建 `app_web.c`；存储与 AI 适配仍在
  `src/platform/web/`。
- **AI 协议**：实际发送棋盘快照而不是着法序列；见 §5.2 的修正。
- **页面关闭保存**：已按 §2.9 增加 `pagehide` / `visibilitychange`
  触发器，调用导出的 `reversi_web_persist`（重写自动存档、设置、统计）。
- **测试接口**：`reversi_web_*` 与 `window.__reversiTest` 保留在 Web
  构建中，用于浏览器自动化；它们只暴露本地游戏状态，未在部署构建中裁剪。
- **SDK 校验**：`build-web.sh` 现在校验 Emscripten 6.0.13；可用
  `REVERSI_WEB_ALLOW_ANY_EMSDK=1` 覆盖。
