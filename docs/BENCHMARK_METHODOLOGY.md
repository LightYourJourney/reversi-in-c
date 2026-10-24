# Reversi 实验方法与基准规范（Phase 13）

日期：2026-10-25  
版本：1（配置格式 v1，原始数据 v1）  
工具：`reversi_experiment`

## 1. 目标与继承关系

本平台把已有的 `tests/bench_ai.c` 与 `tests/bench_corpus.c` 扩展为可重复、
可比较、可自动化的实验系统，而不是另起一套 AI：

- 对局引擎与正式游戏共用 `core/rules`、`core/game`、`ai_choose_move`、
  `ai_search_choose_flags` 与同一份评估。
- Random / Greedy 提取到 `src/ai/ai_baselines`，旧基准与新平台共用。
- `bench_ai` 仍作为低层冒烟基准；实验平台负责配对、统计与报告。

## 2. 可复现清单

每次实验写入 `summary.json` 的 `manifest`：

| 字段 | 来源 |
|---|---|
| `commit` | 运行时 `git rev-parse --short HEAD`，工作区脏时追加 `-dirty` |
| `os` | `uname`（Windows 为 `Windows`） |
| `cpu` | macOS `machdep.cpu.brand_string`，Linux `/proc/cpuinfo`，Windows `PROCESSOR_IDENTIFIER` |
| `compiler` | 构建时注入的编译器标识与版本 |
| `build_type` | 构建时注入的 `$<CONFIG>` |
| `data_version` | 配置/原始数据/开局生成器的格式版本 |
| `seed`、引擎参数 | 实验配置（同时写入每个输出目录的 `config.exp`） |

固定深度/节点的逻辑结果在同一构建上可复现；固定时间预算只保证配置与
统计口径可复现，不保证跨机器得到相同深度或相同着法。

## 3. 配置格式

```text
version 1
name tt-vs-nott
seed 20261025
games 8
openings 2
opening-ply 4
max-plies 100
engine-a kind=search difficulty=hard depth=6 time=2000 nodes=0 tt=on endgame=off label=TT
engine-b kind=search difficulty=hard depth=6 time=2000 nodes=0 tt=off endgame=off label=NoTT
```

规则：

- `games` 必须为偶数：每两局共享同一开局并交换黑白。
- `openings` 个开局由 `seed` 派生，使用合法随机着法生成到 `opening-ply`。
- `max-plies` 是安全上限；达到时记为 `aborted`，不当作平局。
- 未知键、奇数局数、越界参数一律拒绝，不静默修正。
- 运行器会把规范化配置写回输出目录的 `config.exp`。

引擎类型：

| kind | 行为 |
|---|---|
| `random` | 均匀合法着法，RNG 由实验种子/局号/引擎派生 |
| `greedy` | 一步贪心，使用完整评估 |
| `ai` | 正式游戏的 `ai_choose_move`（保留 Easy 的随机性） |
| `search` | `ai_search_choose_flags`，可独立控制 TT、残局求解、深度、时间与节点 |

`search` 的 `depth` 是硬上限；`time` 与 `nodes` 是软上限，命中时与时间预算
一样丢弃未完成的一层，返回上一层结果。`endgame=on` 只对 Hard 生效。

## 4. 公平对战

- 每局记录 `opening`、A 的颜色、双方子数、ply 数、Pass 次数与结果。
- 同一开局的相邻两局交换颜色；A 的黑/白胜场分别统计。
- 两个引擎使用相同的开局、相同的 `time`/`nodes` 配置（由配置决定）与
  独立的确定性 RNG 流。
- 非法着法、无法在预算内给出着法（`timed_out`/`node_limit`/`cancelled`）
  与达到 `max-plies` 的对局分别记为 `illegal`、`timeout`、`aborted`，
  不计入胜率，并在报告中单独列出。
- 进程崩溃不在进程内捕获；运行器以非零退出，不生成摘要。此时没有完整
  原始数据，属于显式失败而非静默忽略。

## 5. 指标

逐着法记录：深度、分数、节点、剪枝、TT 命中、耗时、取消/超时/节点上限。
引擎汇总：

- 总节点、NPS（节点 × 1000 / 总耗时）。
- P50 / P95 / 最大搜索耗时（最近秩）。
- 最大深度、超时数、节点上限命中数、取消数。
- 进程峰值 RSS（`getrusage`，平台口径不同）。

这些指标是**单机观测**；CI 虚拟机上的绝对耗时不能与开发机直接比较。

## 6. 强度统计

- A 分数：`(A 胜 + 0.5 × 平) / 完成局数`。
- 95% 置信区间：Wilson 区间（二项比例）。
- Elo 差：双引擎 logistic 模型 `score = 1 / (1 + 10^(-Δ/400))`，
  平局按 0.5；区间由 Wilson 上下界变换得到，分数夹在 0.001..0.999。
- 样本很小时区间很宽；全胜/全负也不能推出真实胜率 100%/0%。
- 报告不输出没有区间的“精确 Elo”；是否显著由区间是否跨过 0.5 判断。

## 7. 两类评测

| 类型 | 配置 | 用途 |
|---|---|---|
| A. 算法/正确性 | 固定 `depth` 或固定 `nodes` | 同一构建上的行为对照、回归 |
| B. 实际性能 | 固定 `time` | 记录真实完成深度与耗时分布 |

固定时间实验的结果不是跨机器确定性的输出；固定深度/节点实验在同一构建
与相同输入下可重复（搜索本身无随机性，Easy 的随机性由种子固定）。

## 8. 回归机制

`experiment.regression`（CTest）使用两个小配置：

1. Random vs Greedy：校验每局结果、ply 数与颜色配对，作为规则/贪心评估
   的确定性 oracle。
2. TT vs no-TT 固定深度 3：校验每局结果、子数与总节点数，作为搜索路径
   与算法工作量的 oracle。节点数是跨机器的确定性指标；墙钟时间不进入
   CI 判定。

同时要求 `illegal=timeouts=aborted=0`，并在同一次测试中重复运行以比对
逻辑结果。性能回归不进入 CI 判定：不同机器与负载下的原始耗时不可比；
需要时在同一台机器上重跑并对照 P50/P95，使用报告中的实测波动设置阈值。

## 9. 输出与可视化

每个实验目录包含：

```text
config.exp     规范化配置
games.csv      每局结果（含异常标记）
moves.csv      逐着法指标
summary.json   清单、配置、汇总与引擎统计
report.md      Markdown 摘要
report.html    静态页面；图表由内嵌的 summary.json 生成
```

`docs/experiments/index.html` 由 `scripts/make-experiment-index.py` 扫描
各 `summary.json` 生成。所有图表读取实际数据文件，不存在手工趋势线。

## 10. CI 策略

- `experiment.regression` 每次 CI 运行，耗时约 1 秒，只覆盖确定性与异常。
- 完整实验通过 `scripts/run-experiments.sh` 手动执行，结果提交到
  `docs/experiments/`，不占用每次提交的 CI 预算。
- 旧版本引擎对照：在同一配置下用基线提交的构建运行，清单中的 `commit`
  区分两次运行；不把不同机器的绝对耗时直接当作算法优劣。

## 11. 已知限制

- 没有运行时可切换的旧搜索引擎/旧评估实现；跨版本对照依赖两次构建。
- 固定时间结果受机器负载影响；报告的深度与耗时只描述该次运行。
- 进程崩溃不会留下部分原始数据。
- Elo 只适用于双引擎对局；不构建多引擎对手池。
