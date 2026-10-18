# Phase 1 完成报告

- 日期：2026-10-19
- 阶段：完整翻转棋规则引擎与对局状态
- 结论：**完成**。规则、自动跳过、终局、结果判定与随机对局测试全部通过；
  Debug、Release、ASan+UBSan 构建均通过。

## 1. 实际完成的功能

### 棋盘与坐标

- 8×8 `Board`，`Stone` 为 `EMPTY` / `BLACK` / `WHITE`。
- 统一坐标：行 0 为顶部、列 0 为左侧；记谱 `a1` 为左下角、`h8` 为右上角。
- `move_from_notation()` / `move_to_notation()` 双向映射，支持大小写文件名。
- 标准开局：D4/E5 白、D5/E4 黑，黑方先行。
- 安全访问：非法坐标、NULL 棋盘返回安全值，不越界。
- 棋盘清空、填满判断、合法性检查、棋子计数、对手棋子转换。

### 规则引擎（无状态）

- 八方向合法着法检测。
- 单方向、多方向、多子连续翻转。
- 非法落子拒绝，且不修改棋盘。
- 当前玩家合法着法生成与计数。
- `rules_must_pass()` 查询必须跳过。
- 落子返回实际翻转数；失败返回 `-1`。

### 对局状态（有状态）

- `GameState`：棋盘 + 当前玩家 + 结果。
- 合法落子后切换回合。
- 对手无合法着法、落子方仍有：自动跳过，当前玩家保持为落子方。
- 对手与落子方都无合法着法：终局并按棋子数结算。
- 棋盘填满：终局并按棋子数结算。
- 显式 `game_pass()` 仅在必须跳过时成功；有合法着法时拒绝。
- `game_is_over()`、`game_result()`、`game_must_pass()`。
- `game_is_consistent()` 检查棋盘、玩家、进行中/终局状态与结果一致性。

## 2. 新增及修改的文件

新增：

```text
src/core/rules.h
src/core/rules.c
src/core/game.h
src/core/game.c
tests/test_helpers.h
tests/test_oracle.h
tests/test_rules.c
tests/test_game.c
tests/test_random_games.c
docs/PHASE_1_REPORT.md
```

修改：

```text
src/core/board.h / board.c   安全访问、记谱映射、开局修正、棋盘查询
tests/test_board.c           扩展棋盘/记谱/安全访问测试
CMakeLists.txt               core 源文件、测试目标与测试辅助函数
README.md                    状态、结构、规则引擎与测试说明
docs/ARCHITECTURE.md         模块状态、错误处理、测试策略、Phase 1 决策
docs/ROADMAP.md              Phase 1 完成，调整 Phase 2 范围
docs/PHASE_0_REPORT.md       增加开局坐标修正说明
```

## 3. 架构上的重要决定

1. **Rules 与 Game 分离**：`rules_*` 只操作 Board 或做只读查询；
   `GameState` 负责回合、自动跳过与终局，便于 AI 复用无状态规则。
2. **自动跳过内置于 `game_apply_move()`**：外部调用者不会看到“当前玩家无着法”
   的非法状态；同时保留 `game_pass()` 处理必须跳过的中间态。
3. **安全访问优先**：`board_get()` / `board_set()` 不再依赖 `assert` 处理
   非法输入；规则层仍显式校验，双重保护。
4. **一致性检查显式化**：`game_is_consistent()` 允许“当前方必须跳过、对手可走”
   的中间态，但拒绝进行中双方均无着法、以及结果与棋子数不符的终局。
5. **修正 Phase 0 开局**：在“行 0 顶部”的约定下，原开局颜色放反；
   Phase 1 已按规格修正，并新增基于记谱的回归测试。

## 4. 实际运行的命令及结果

环境：macOS Darwin 25.6.0，Apple M3 arm64，Apple Clang 21.0.0，CMake 4.4.4。

### 4.1 Debug 构建与 CTest

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREVERSI_ENABLE_GUI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：构建成功；CTest **5/5 passed**（4 个 core 测试 + `app.smoke`）。

直接运行各测试程序：

| 测试 | 断言数 | 结果 |
|---|---:|---|
| `core.board` | 623 | PASS |
| `core.rules` | 227 | PASS |
| `core.game` | 115 | PASS |
| `core.random` | 557,924 | PASS |

随机对局统计：

```text
random games: 500 finished, total moves 29992, max moves 60
```

总断言数：**558,889**，失败 **0**。

覆盖内容包括：开局双方各 2 子、黑方四个开局合法点（C4/D3/E6/F5）、C4 翻转 D4、
八方向翻转、多方向同时翻转、多子连续翻转、不同方向不同翻转数、四角/四边、
间隙与己方棋子阻挡、己方棋子终止翻转、零翻子非法、无对手棋子、自动跳过、
主动跳过拒绝、未填满终局、棋盘填满终局、平局/黑白胜、终局后禁止落子、
非法坐标/非法玩家、状态一致性、500 局随机对局不变量与独立合法性 oracle。

### 4.2 Release 构建与测试

```text
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

结果：构建成功；CTest **4/4 passed**。

### 4.3 ASan + UBSan

```text
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_ENABLE_ASAN=ON -DREVERSI_ENABLE_UBSAN=ON
cmake --build build-sanitize -j
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

结果：构建成功；CTest **4/4 passed**，无 AddressSanitizer / UBSan 诊断。

说明：macOS 上的 AddressSanitizer 不支持 LeakSanitizer，因此显式关闭
`detect_leaks`；内存越界与未定义行为检查正常生效。

### 4.4 警告检查

第一方源码重新编译后未产生 warning/error。仅存在 raylib 自身的第三方警告
（`stb_vorbis.c`）与配置期 developer warning。

### 4.5 Code review 修复

Standards / Spec 两轴审查结论：

- **Standards**：无硬性规范违反；发现 5 项判断项。
- **Spec**：无缺失或部分实现的需求，未发现错误实现；指出两项测试覆盖弱点。

已修复：

- 抽取 `stone_is_player()` 到 board 模块，消除 rules/game 中重复的
  `player_is_valid()`。
- `game.c` 改用 `rules_must_pass()`，使该公开查询进入生产路径，
  不再只是测试专用包装。
- `board_is_valid()` 头文件注释补充 NULL 语义。
- `test_random_games.c` 显式包含 `<stdio.h>`，不依赖测试 harness 的间接包含。
- 新增 `tests/test_oracle.h`：随机对局的每一步候选落子同时由独立合法性
  oracle 校验，避免“生成与校验使用同一实现”的自洽性盲区。
- 新增“己方棋子终止翻转”用例：验证己方棋子之后的对手棋子不会被误翻。
- 测试数量更新为 board 623、rules 227、game 115、random 557,924。

保留的判断项：

- `GameState.current_player` 仍使用 `Stone`，非法值由 `stone_is_player()`
  集中校验；引入独立 player 类型属于更大的 API 变更，暂不做。
- 测试中的方向表与引擎方向表重复，作为独立 oracle 保留。
- Spec 审查列出的 `game_pass()`、`board_clear()`、`stone_opponent()`、
  `board_set()` 返回 bool 等均为规格要求或安全输入所需，保留。

## 5. 已知问题与风险

1. **`rules_generate_legal_moves()` 返回写入数量**，不是合法着法总数；
   调用者需保证 capacity 足够（本项目统一使用 64）。总数请使用
   `rules_count_legal_moves()`。
2. **macOS 无 LeakSanitizer**：ASan 的泄漏检测不可用，已用 `detect_leaks=0`
   运行；未做堆泄漏检测。
3. **规则尚未接入 UI**：当前窗口仍只绘制基础画面；Phase 3 才进行交互集成。
4. **Windows/Linux 未验证**：仅 macOS arm64 实机验证。
5. **Phase 0 开局问题已修正**：原实现在行 0 顶部约定下颜色放反；
   现按 D4/E5 白、D5/E4 黑修正，并加入记谱回归测试。
6. **NULL 查询语义**：`game_is_over(NULL)` 返回 false、
   `game_result(NULL)` 返回 ONGOING；调用者不应依赖 NULL 状态进行游戏流程。
7. **尚未实现**：History、AI、Storage、Settings、完整 GUI。

## 6. 下一阶段的依赖与建议

Phase 2 可以直接复用现有接口：

- `GameState` 是纯值类型，可直接复制用于撤销/重做快照或历史记录。
- `rules_apply_move()` 返回翻转数，可用于历史条目或动画数据。
- `game_apply_move()` 已处理自动跳过与终局，History 只需记录成功落子及状态。
- `rules_generate_legal_moves()` 可用于 UI 合法落点提示和 AI 候选生成。
- 记谱函数可用于历史列表、存档和调试输出。
- 继续保持 `reversi_core` 不链接 raylib；History 测试应加入 CTest。
