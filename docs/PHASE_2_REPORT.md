# Phase 2 完成报告：图形界面与交互

日期：2026-10-19
平台：macOS 26.6.2（Darwin 25.6.0，Apple M3 / arm64）

## 1. 交付范围

Phase 2 交付了可实际操作的完整界面，而不是静态效果图：

- **主菜单**：品牌标识、Play / Settings / Quit；鼠标悬停/点击与键盘上下/Enter 均可操作。
- **对局页**：8×8 棋盘、坐标、当前回合、双方实时棋子数、合法着法提示、
  最后一步脉冲标记、状态文本、Restart / Main menu；宽窗口侧栏布局，
  窄窗口上下信息栏布局。
- **设置页**：Animations、Legal move hints、Last move highlight 三个开关，
  切换后立即影响对局渲染。
- **结算页**：胜者/平局、最终比分、Play again / Main menu，带出现动画。
- **模态确认**：退出、重开、返回主菜单；背景变暗且输入被阻断。
- **动画**：落子缩放（ease-out-back）、翻转交错缩放、最后一步脉冲、
  页面淡入淡出、按钮悬停/按压位移；动画与 `GameState` 完全分离，
  动画期间棋盘输入被禁用。
- **布局与字体**：最小 760×600，支持任意缩放；HiDPI 按 DPI 加载字体；
  内置 Inter 4.1（OFL 1.1），资源缺失时回退 raylib 默认字体。
- **程序化视觉**：棋盘、棋子、阴影、提示点、按钮均绘制生成，无贴图依赖。

## 2. 文件变更

### 新增

```text
src/ui/theme.h              设计令牌（颜色、字号、间距、圆角、时长、最小窗口）
src/ui/draw.[ch]            绘制原语、按钮状态机、文字裁剪、程序化棋子、缓动
src/ui/fonts.[ch]           Inter 加载、DPI 图集、默认字体回退
src/ui/animation.[ch]       落子/翻转动画（最多 32 个翻转格）
src/ui/board_view.[ch]      棋盘、坐标、提示点、最后一步、光标/幽灵子
src/ui/menu_view.[ch]       主菜单
src/ui/game_view.[ch]       对局页与宽/窄响应式布局
src/ui/settings_view.[ch]   设置页
src/ui/result_view.[ch]     结算页
src/ui/modal.[ch]           通用确认模态
tests/test_ui_button.c      按钮输入语义测试（26 项断言）
assets/fonts/Inter-Regular.ttf
assets/fonts/Inter-SemiBold.ttf
assets/fonts/OFL.txt
docs/PHASE_2_REPORT.md
```

### 修改

```text
CMakeLists.txt              构建全部 UI 模块；新增 ui.button 测试目标
src/app/app.c               页面状态机、转场、模态、动画驱动、开发钩子
README.md                   状态、界面、操作、测试结果、字体与已知限制
docs/ARCHITECTURE.md        UI 模块职责、数据流、动画边界、Phase 2 决策
docs/ROADMAP.md             Phase 2 完成；History 调整为 Phase 3
assets/README.md            字体来源、许可证、SHA256、回退说明
```

### 删除

```text
src/ui/render.[ch]          Phase 0 的单文件渲染入口，已由多视图结构取代
```

## 3. 架构决策

1. **按页面拆分视图**：每个页面拥有自己的布局与局部状态，返回动作枚举；
   `app` 是唯一组装根，负责调用规则层、切换页面、打开模态。
2. **动画由棋盘差异驱动**：落子前保存 `Board` 快照，落子后比较前后状态生成
   `UiMoveAnimation`；动画只影响绘制，绝不修改 `GameState`。
3. **输入门控**：动画期间 `board_accept=false`；模态打开时只更新模态；
   页面转场期间只推进被动布局。三种机制共同避免背景误触。
4. **响应式布局**：宽度 ≥1000 且高度 ≥680 使用棋盘+侧栏；否则使用顶部回合/
   计分栏、居中棋盘、底部状态/操作栏。最小窗口 760×600。
5. **HiDPI 字体一次加载**：启动时按 `GetWindowScaleDPI()` 以 32/64 像素
   加载 Regular/SemiBold；绘制循环不加载资源、不做堆分配。
6. **设置暂存内存**：本阶段只实现显示/动画开关，持久化留给 Phase 5，
   避免提前固化存储格式。
7. **鼠标与键盘同权**：按钮有 hover/pressed/disabled 状态；
   棋盘支持鼠标命中与键盘光标；Enter/Space 等价于点击。

## 4. 实际执行的命令与结果

### 4.1 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
```

三套构建均成功；第一方源码重新编译后未出现 `warning:` 或 `error:`。
第三方 raylib 仍会打印其既有的 macOS OpenGL 废弃提示。

### 4.2 测试

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-release --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

| 构建 | 结果 |
|---|---|
| Debug（GUI 测试开启） | **6/6 通过** |
| Release | **5/5 通过** |
| ASan+UBSan | **5/5 通过** |

直接运行测试二进制的断言数：

```text
core.board   623 checks
core.rules   227 checks
core.game    115 checks
core.random  557,924 checks（500 局，29,992 步，最长 60 步）
ui.button     26 checks
```

GUI 冒烟：

```bash
./build-release/bin/reversi --frames 2     # exit 0
ASAN_OPTIONS=detect_leaks=0 \
  ./build-sanitize/bin/reversi --frames 5  # exit 0，无 sanitizer 报告
```

说明：调试过程中 `app.smoke` 曾因 macOS 屏幕锁定、GLFW 无法获取主显示器而
失败（`Failed to get primary monitor`）。唤醒显示后重跑即通过；这是验证环境
状态，不是代码缺陷。

## 5. 界面验证

验证方式：在真实窗口中运行程序，使用窗口截图与窗口录屏逐项目视检查。
游戏操作优先使用真实键盘事件；桌面会话在验证中途进入锁屏后，GLFW 不再接收
普通键盘事件，因此后续页面与开关通过 `read_input()` 入口注入合成按键推进，
所有画面仍是真实渲染结果。鼠标移动/悬停由系统事件注入验证；鼠标“按下/释放”
的真实点击在锁屏会话中无法送达，其语义改由 `ui.button` 单元测试覆盖。

已确认：

- **主菜单**：品牌、标题、三个按钮、页脚；焦点与悬停样式正确。
- **对局开局**：D5/E4 黑、D4/E5 白；C4/D3/E6/F5 四处合法提示；
  回合、比分 2:2、状态文本、按钮均正确。
- **落子与翻转**：D3、C5、D6 依次落子；比分依次 4:1、3:3、5:2；
  回合切换、最后一步记谱、提示点更新正确。
- **非法落子**：在 C4（白方回合的非法位置）确认，棋盘与比分保持不变，
  出现拒绝反馈。
- **动画**：窗口录屏逐帧确认 D6 落子时的缩放出现、D5 翻转与随后稳定状态；
  设置关闭动画后落子立即完成。
- **设置**：Animations / Legal move hints / Last move highlight 均可关闭与恢复；
  关闭提示后棋盘无提示点，关闭最后一步后无高亮，关闭动画后无过渡。
- **模态**：M 打开返回菜单确认框，背景变暗；模态打开期间方向键不改变棋盘；
  Esc 取消后状态不变；R 打开重开确认框，Enter 确认后回到开局。
- **结算**：自动走完一局后显示 “Black wins”、40:24、Play again / Main menu；
  Play again 启动新局。
- **窗口尺寸**：760×600、800×620、1100×800、1400×1000 均无重叠或裁切；
  宽/窄布局按阈值正确切换。
- **字体回退**：从无可执行文件同级 `assets/` 的临时目录启动，日志输出
  `bundled fonts unavailable, using raylib default font`，菜单正常渲染。
- **退出/缩放**：`--frames` 冒烟路径与进程结束均返回 0；运行时缩放不丢失
  对局状态。窗口标题栏关闭按钮的真实点击未验证。

未验证：

- 窗口失焦/重新聚焦时的行为（锁屏环境无法模拟普通失焦）。
- Windows 与 Linux 实机运行。
- 真实鼠标按下/释放完成一次按钮激活（改由单元测试覆盖逻辑）。

## 6. 已知问题与风险

- 设置尚未持久化，重启后恢复默认（Phase 5）。
- 尚无撤销/重做、AI、存档/读档（Phase 3–5）。
- 动画期间刻意阻断落子输入；快速连续操作需要等待约 0.5 秒的落子+翻转动画。
- 在锁屏或无活动图形会话时，`app.smoke` 无法初始化窗口；需解锁后运行。
- 第三方 raylib 的既有警告仍会出现在构建/运行日志中，不影响第一方代码。
- 真实鼠标点击路径缺少端到端验证；`ui.button` 已覆盖按下/释放/移出取消/
  禁用/输入阻断语义，但未覆盖窗口坐标换算。

## 7. 下一阶段依赖

Phase 3（History / 撤销重做）可以复用：

- `app` 的动作路由与 `GAME_VIEW_RESTART/MENU` 模式；
- 对局页侧栏/底栏的状态展示位置；
- `GameState` 的只读绘制接口与动画差异机制。

接入时应在 `game_apply_move` 成功后追加历史记录，并在撤销/重做后重建
最后一步标记；动画仍只能读取状态，不得写回。Phase 4 的 AI 需要新增模式/
难度入口，并确保思考不阻塞事件循环；Phase 5 的持久化应复用 `AppSettings`
字段，不把存储格式引入 UI 层。
