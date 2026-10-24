# Phase 13 报告：Reversi AI Benchmark 与实验平台

日期：2026-10-25  
版本：**1.4.0**（源码；提交后确认）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
AppleClang 21，CMake 4.4.4  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 完成矩阵

| 任务 | 交付 | 状态 |
|---|---|---|
| 1. 实验基础设施 | `src/experiment/`：配置、固定种子、引擎参数、开局、批量对局、原始数据、汇总与可复现清单 | PASS |
| 2. 配置与对手 | Random / Greedy / AI（正式难度）/ Search（TT、残局、深度、时间、节点可独立配置） | PASS（跨代码版本需两次构建） |
| 3. 公平对战 | 成对开局、交换黑白、固定种子、相同预算、异常分类记录 | PASS |
| 4. 性能基准 | 固定深度 / 节点 / 时间；节点、NPS、P50/P95、最大深度、剪枝、TT、RSS、取消 | PASS |
| 5. 强度评估 | 配对胜率、颜色分组、Wilson 95% CI、双引擎 Elo 与区间 | PASS |
| 6. 自动回归 | `experiment.regression` 确定性 oracle、异常为 0、重复运行一致；性能不进 CI 判定 | PASS |
| 7. 报告系统 | `games.csv`、`moves.csv`、`summary.json`、`report.md`、`report.html` | PASS |
| 8. 可视化 | 静态 `index.html` + 每实验 HTML；图表由内嵌的 `summary.json` 生成 | PASS |
| 9. 自动化集成 | 快速回归进入 CTest；完整实验由 `scripts/run-experiments.sh` 手动执行 | PASS |
| 10. 验收实验 | 7 组对战 + 规则吞吐 + 精确残局验证 | PASS |

## 2. 架构与复用

- 对局仍由 `core/rules` + `core/game` 驱动；AI 仍由同一份搜索与评估执行。
- `ai_search_choose_flags()` 暴露 TT/残局两个开关；`AiConfig.max_nodes` 与
  `AiStats.node_limit` 提供节点预算。正式对局默认 `max_nodes=0`，行为不变。
- `ai_search_choose_ex()` 的“纯 alpha-beta”语义现在也包含“不因残局而自动
  加深”：只有 `use_endgame` 才把 Hard 的深度提升到剩余空位。
- Random / Greedy 提取到 `ai_baselines`，`bench_ai` 与新平台共用。
- 实验模块不依赖 UI/raylib；工具 `reversi_experiment` 只链接 AI/core 与
  C 标准库。

## 3. 配置与输出

配置格式、公平对战、指标定义、Elo 方法与回归策略见
[BENCHMARK_METHODOLOGY.md](BENCHMARK_METHODOLOGY.md)。每个实验目录包含
`config.exp`、两份 CSV、`summary.json`、`report.md` 与 `report.html`；
`docs/experiments/index.html` 从各 `summary.json` 生成。

## 4. 验收实验结果

全部为 Release 构建、`seed=20261025`、成对开局与颜色交换。样本很小，
区间很宽；结果只描述这些对局，不宣称通用强度。

| 实验 | 局数 | A 战绩 (W/D/L) | A 分数 (95% CI) | Elo 差 (95% CI) | 异常 | 备注 |
|---|---:|---|---|---|---:|---|
| TT vs no-TT（d6） | 8 | 4/0/4 | 0.500 (0.215–0.785) | 0 (−225..+225) | 0 | 节点 1.38M vs 3.11M；P50 13 vs 30 ms |
| Depth 4 vs 6 | 8 | 2/0/6 | 0.250 (0.071–0.591) | −191 (−445..+64) | 0 | 深度 6 领先，但区间跨 0.5 |
| Full vs basic eval（d5） | 8 | 4/0/4 | 0.500 (0.215–0.785) | 0 (−225..+225) | 0 | 节点 523k vs 849k |
| Easy vs Hard | 8 | 1/0/7 | 0.125 (0.022–0.471) | −338 (−656..−20) | 0 | 唯一区间不跨 0.5 的强度对照 |
| 固定 150 ms | 8 | 5/0/3 | 0.625 (0.306–0.863) | +89 (−142..+320) | 0 | 双方 P50 150/151 ms，最大深度 13；区间跨 0.5 |
| 节点预算 20k | 4 | 2/0/2 | 0.500 (0.150–0.850) | 0 (−301..+301) | 0 | 双方各 2,012,302 节点；97/97 次命中节点上限；深度 10 |
| Random ×100 | 100 | 45/6/49 | 0.480 (0.385–0.577) | −14 (−82..+54) | 0 | 无非法/超时/中止 |

固定时间对局的深度与耗时只描述该次运行；不同机器不可直接比较。两次
固定时间运行中 5:3 与 4:4 的差异说明该类结果受时序影响。节点预算实验
在两次运行中给出相同的节点总数与深度。

### 4.1 规则吞吐（同二进制标量 vs Bitboard）

来自 `docs/experiments/rules-throughput/corpus.txt`，单位为 ns/op
（10,000 次/局面，测量时机器有负载）：

| 局面 | 标量着法 | Bitboard 着法 | 标量落子 | Bitboard 落子 |
|---|---:|---:|---:|---:|
| opening | 49,560 | 7.6 | 2,196 | 8.8 |
| midgame-40 | 53,977 | 12.2 | 53.5 | 9.7 |
| midgame-30 | 27,423 | 13.8 | 48.4 | 8.5 |
| endgame-20 | 15,452 | 19.2 | 54.3 | 9.9 |

绝对纳秒数受负载影响，只用来说明数量级；固定深度节点数在 Phase 11 已证明
两套规则搜索树一致。

### 4.2 精确残局验证

`docs/experiments/endgame-verify/verify.txt`：Release `ai.endgame` 67 次
断言 PASS，12/10/8/6/4/2 空位置与独立全深度 alpha-beta 分数一致，
取消分类为 INCOMPLETE。

## 5. 测试

| 配置 | 结果 |
|---|---|
| Debug 无窗口 | 21/21 PASS |
| Release 无窗口 | 21/21 PASS |
| ASan+UBSan 无窗口 | 21/21 PASS |
| Debug + GUI | 24/24 PASS |
| `experiment.regression` | 63 次断言：配置拒绝、颜色配对、确定性 oracle、搜索 oracle、输出文件 |
| `ai.core` | 2,991 次断言（含节点上限与 flags 入口） |
| Emscripten Release | 构建通过（实验模块不进入 Web 构建） |

## 6. 已知限制

1. **跨代码版本对照需要两次构建**：没有运行时可切换的旧搜索/旧评估；
   清单用 `commit` 区分基线构建与当前构建。
2. **样本小**：8 局对局的 Wilson 区间很宽；只有 Easy vs Hard 的区间不跨
   0.5，仍不能外推为稳定强度。
3. **时间预算非确定性**：固定时间实验的深度与耗时受机器负载影响。
4. **崩溃**：进程崩溃会以非零退出，但不会写出部分原始数据；正常异常
   （非法/超时/中止）均记录。
5. **Elo**：只实现双引擎 logistic 估计，不构建多引擎对手池。
6. **可视化**：静态 HTML，无历史数据库；索引由脚本扫描 `summary.json`。

## 7. 回滚

```bash
git revert <phase-13-commit>
```

局部回滚：删除 `src/experiment/`、`tests/run_experiment.c`、
`tests/test_experiment.c`、`experiments/`、`docs/experiments/` 与
`scripts/run-experiments.sh`；`ai.h` 删除 `max_nodes`/`node_limit`，
移除 `ai_search_choose_flags()`；`bench_ai.c` 可恢复本地 Random/Greedy。
`VERSION` 回退到 `1.3.0`。

## 8. 下一阶段依赖

- 如需真正的新旧引擎对战，引入可切换的评估/搜索后端，而不是复制实现。
- 多引擎对手池与贝叶斯 Elo，需要更大样本与更长的手动运行。
- 将完整实验接入独立 CI 工作流（手动触发），并把性能阈值限定在同机对照。
