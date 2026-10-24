# AI 引擎 2.0（Phase 11）

日期：2026-10-23  
版本：1.2.0  
入口：`ai_choose_move()`（同步）、`ai_worker`（桌面线程 / Web Worker）

## 1. 组成

| 模块 | 职责 |
|---|---|
| `core/bitboard.[ch]` | 64 位棋盘、合法着法、翻转、计数、Pass/终局 |
| `ai/ai_search.c` | Negamax + Alpha-Beta、迭代加深、TT、增量 Zobrist |
| `ai/ai_eval.c` | 局面评估（未改公式） |
| `ai/ai_endgame.[ch]` | 全宽度残局求解器，EXACT/INCOMPLETE |
| `ai/ai_worker.c` / `platform/web/ai_worker_web.c` | 搜索隔离 |

标量 `core/rules.c` 仍是外部对局、存档校验与差分测试的 Oracle。

## 2. Bitboard 表示

```c
typedef struct Bitboard { uint64_t black; uint64_t white; } Bitboard;
```

位映射与 `Board` 的行主序一致：

```
bit_index = row * 8 + col      // row 0 在顶部
bit 0  = a8（左上）            bit 63 = h1（右下）
```

文件掩码：

```
FILE_A = 0x0101010101010101
FILE_H = 0x8080808080808080
```

八方向位移在移位前屏蔽会越界到相邻行的源列：

| 方向 | 操作 |
|---|---|
| 东 | `(x & ~FILE_H) << 1` |
| 西 | `(x & ~FILE_A) >> 1` |
| 南 | `x << 8` |
| 北 | `x >> 8` |
| 东南 | `(x & ~FILE_H) << 9` |
| 西南 | `(x & ~FILE_A) << 7` |
| 东北 | `(x & ~FILE_H) >> 7` |
| 西北 | `(x & ~FILE_A) >> 9` |

所有移位距离都在 0–63 内；`1ULL << index` 不会触发 64 位移。popcount 与
ctz 优先使用编译器内建（`__has_builtin`），否则使用 SWAR / 逐位回退，
不依赖编译器扩展。

## 3. 合法着法与翻转

对每个方向，从己方位集合出发逐格前进：

```
walk = shift(own) & opponent
while walk:
    moves |= shift(walk) & empty
    walk = shift(walk) & opponent
```

翻转同理，但只在终止于己方棋子时提交该方向捕获的位集合。
`bitboard_apply_move()` 在无翻转时失败，因此非法落子不会改变棋盘。

## 4. 搜索集成

搜索内部同时携带：

- `GameState`（评估、终局计分、对外结果）
- `Bitboard`（合法着法、落子、翻转位）
- `uint64_t hash`（增量 Zobrist）

`apply_search_move()` 是唯一的状态转换点：先按标量规则落子（保持评估
语义），再用 Bitboard 落子并取得翻转位；Bitboard 落子失败时该分支失败。
两套规则对同一着法给出相同棋盘由差分测试保证；运行时不逐节点比对，
以避免热路径开销。

节省的热点：

- 合法着法与计数：位运算，不再扫 64 格。
- 叶子节点不再构造着法数组（只保留合法位集合）。
- Zobrist 不再每节点重算 64 格：落子时异或落点、翻转位与回合键。

TT 语义未改：键含棋子与回合，条目含深度、EXACT/LOWER/UPPER 与最佳着法；
边界标记仍以收紧后的窗口为准。

## 5. 残局求解器

`ai_endgame_solve()` 对剩余空位做全宽度 Negamax：

- 终止：双方均无着法 → `±TERMINAL_WIN + 盘面差`（与主搜索一致）。
- Pass：不消耗空位，交换回合键后继续。
- TT：按剩余空位数存储，命中需 `entry->empties >= empties`。
- 着法排序：角点优先，其次位置权重。
- 取消：根节点每步检查；内部每 1024 节点检查一次。
- 预算：主搜索为求解器保留 25% 余量；若求解被截断，回退搜索至少获得
  50 ms，避免“求解器耗尽预算导致无着法”。

状态：

| 状态 | 含义 |
|---|---|
| `AI_ENDGAME_EXACT` | 完整搜索，分数是精确终局值 |
| `AI_ENDGAME_INCOMPLETE` | 超时或取消，分数不是精确值 |

Hard 在剩余空格 ≤ 12 时先尝试求解；EXACT 直接返回，否则回退到常规
迭代加深。求解被截断时，求解器的 nodes/cutoffs/tt_hits 会计入最终统计，
耗时从本次搜索开始计算（不受预算重置影响）；若在首个子树完成前被取消，
返回首个有序合法着法并标记 INCOMPLETE。

`ai_search_choose_ex()` 同时禁用 TT 与残局求解器，作为测试用的独立
plain alpha-beta 参照。实验平台使用 `ai_search_choose_flags()` 独立控制
TT 与残局；关闭残局时 Hard 不再把深度提升到剩余空位。

## 6. 计时

| 平台 | 时钟 |
|---|---|
| Windows | `GetTickCount64()` |
| Emscripten | `emscripten_get_now()` |
| POSIX（macOS/Linux） | `clock_gettime(CLOCK_MONOTONIC)` |
| 其他 | `timespec_get(TIME_UTC)` 回退 |

预算按 1024 节点检查；根节点每步检查。单调时钟避免了系统时间回拨导致的
超时误判。`AiConfig.max_nodes > 0` 时同样在节点检查点停止，结果通过
`AiStats.node_limit` 区分；正式对局默认不设节点上限。Linux 的严格 C11 构建默认不暴露 `clock_gettime`，相关编译单元
在包含 `<time.h>` 之前定义 `_POSIX_C_SOURCE=200809L`。

## 7. 验证

- `core.bitboard`：约 62 万次差分断言（200 局随机对局逐步比较合法着法、
  翻转位、结果棋盘、Pass/终局），外加边界/非法状态/对称用例。
- `ai.endgame`：12/10/8/6/4/2 空位置与**独立**的全深度 alpha-beta
  （`ai_search_choose_ex` 已禁用求解器）分数一致；取消后分类为
  INCOMPLETE。
- `ai.core` / `ai.worker` / 全量桌面测试保持通过。
- 固定深度 6 的 A/B 中两套引擎节点数完全相同，说明搜索树未因位板而改变。

## 8. 已知限制

- 中局评估仍是标量实现（每叶 2 次标量机动性枚举 + 4 次全盘扫描），
  因此中局固定深度耗时改善有限；这是下一步最明确的优化点。
- Hard 的 TT 仍是每次搜索 `calloc/free` 2 MiB，替换策略为直接覆盖。
- 残局求解器与主搜索各自维护一份 Zobrist 表与 TT；后续可合并。
- 未实现 PVS、Aspiration Window、Killer/History；这些需要单独测量后再决定。
- 没有运行时可切换的标量后端；标量规则只作为测试 Oracle。
- 残局求解器未实现奇偶排序与跨调用 TT 复用。
