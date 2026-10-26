# Phase 13 报告：Reversi AI Benchmark 与实验平台

日期：2026-10-25  
版本：**1.4.0**（提交 `102226c`）  
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
| TT vs no-TT（d6） | 8 | 4/0/4 | 0.500 (0.150–0.850) | 0 (−301..+301) | 0 | 节点 1.38M vs 3.11M；P50 14 vs 33 ms |
| Depth 4 vs 6 | 8 | 2/0/6 | 0.250 (0.046–0.699) | −191 (−528..+147) | 0 | 点估计偏向深度 6，区间跨 0.5 |
| Full vs basic eval（d5） | 8 | 4/0/4 | 0.500 (0.150–0.850) | 0 (−301..+301) | 0 | 两者均启用 TT；节点 523k vs 460k |
| Easy vs Hard | 8 | 2/0/6 | 0.250 (0.046–0.699) | −191 (−528..+147) | 0 | 点估计偏向 Hard，区间跨 0.5 |
| 固定 150 ms | 8 | 2/0/6 | 0.250 (0.046–0.699) | −191 (−528..+147) | 0 | 双方 P50 151 ms，最大深度 13；区间跨 0.5 |
| 节点预算 20k | 4 | 2/0/2 | 0.500 (0.095–0.905) | 0 (−393..+393) | 0 | 双方各 2,012,302 节点；97/97 次命中节点上限；深度 10 |
| Random ×100 | 100 | 47/8/45 | 0.510 (0.376–0.643) | +7 (−88..+102) | 0 | 无非法/超时/节点上限/中止 |

置信区间以**配对开局**为独立单位（8 局 = 4 对），不是把换色两局当作
独立样本。因此所有实验的区间都跨过 0.5：在这个样本量下没有任何一组
可以宣称存在统计显著的强度差异；Easy vs Hard 的 1:7 也只是点估计。

固定时间对局的深度与耗时只描述该次运行；同一配置的重跑出现 4:4、5:3、
3:5、2:6 的差异，说明该类结果受时序影响。预算每 1024 节点检查一次，负载下
单次检查间隔可超时：150 ms 实验的 P50 为 151 ms，但 `moves.csv` 中最大
`elapsed_ms` 为 417 ms（该着法仍来自已完成的深度，标记 `timed_out=1`）。
节点预算实验在多次运行中给出相同的节点总数与深度。

“评估函数版本”用完整组件（Hard）与基础组件（Medium）在同一深度下近似；
实验入口现在对非 Hard 配置也按 `tt=on` 分配置换表，BasicEval 的
`tt_hits=3,348` 证明没有把“启用 TT”与“完整评估”混在一起。“TT 策略”
只比较开/关，不比较替换策略。

### 4.1 旧/新搜索与规则对照

旧/新搜索代码不在同一进程内可切换，使用 Phase 11 的两次构建 A/B：
同一 `bench_corpus`、固定深度 6，两套引擎节点数完全相同（搜索树未变），
开局 73.8→9.7 ms、中局 516→198 ms、残局 12 空 72→8.6 ms；Hard 500 ms
下开局 7→10 层。详见 [PHASE_11_REPORT.md](PHASE_11_REPORT.md) §5。

### 4.1 规则吞吐（同二进制标量 vs Bitboard）

来自 `docs/experiments/rules-throughput/corpus.txt`，单位为 ns/op
（10,000 次/局面，测量时机器有负载）：

| 局面 | 标量着法 | Bitboard 着法 | 标量落子 | Bitboard 落子 |
|---|---:|---:|---:|---:|
| opening | 1,598.1 | 4.1 | 34.9 | 9.2 |
| midgame-40 | 1,023.3 | 9.2 | 41.2 | 6.9 |
| midgame-30 | 1,421.3 | 10.3 | 35.0 | 9.3 |
| endgame-20 | 714.3 | 15.7 | 41.9 | 7.7 |

表中数值是当次 `corpus.txt` 的快照；重跑会随机器负载变化，复核时以数据
文件为准。绝对纳秒数只用来说明数量级；固定深度节点数在 Phase 11 已证明
两套规则搜索树一致。

### 4.2 精确残局验证

`docs/experiments/endgame-verify/verify.txt`：Release `ai.endgame` 70 次
断言 PASS，12/10/8/6/4/2 空位置与独立全深度 alpha-beta 分数一致，
取消分类为 INCOMPLETE。

## 5. 测试

| 配置 | 结果 |
|---|---|
| Debug 无窗口 | 21/21 PASS |
| Release 无窗口 | 21/21 PASS |
| ASan+UBSan 无窗口 | 21/21 PASS |
| Debug + GUI | 24/24 PASS |
| `experiment.regression` | 87 次断言：配置拒绝、颜色配对、种子敏感性、未配对/中止分类、确定性/节点 oracle、节点上限分类、输出文件与异常列表 |
| `ai.core` | 2,988 次断言（含节点上限、flags 入口与共享基线） |
| Emscripten Release | 构建通过（实验模块不进入 Web 构建） |
| GitHub Actions `40281111365` | 9/9 PASS：三平台 Debug/Release 各 21/21、Linux ASan+UBSan 21/21、Xvfb GUI 3/3、三平台打包与 Linux 包验收 |

## 6. 已知限制

1. **跨代码版本对照需要两次构建**：没有运行时可切换的旧搜索/旧评估；
   清单用 `commit` 区分基线构建与当前构建。
2. **样本小**：8 局只有 4 个配对单位，所有区间都跨 0.5；不能从点估计
   外推强度差异。
3. **时间预算非确定性**：固定时间实验的深度与耗时受机器负载影响。
4. **崩溃**：进程崩溃会以非零退出，但不会写出部分原始数据；正常异常
   （非法/超时/中止）均记录。
5. **Elo**：只实现双引擎 logistic 估计，不构建多引擎对手池。
6. **可视化**：静态 HTML，无历史数据库；索引由脚本扫描 `summary.json`。
7. **性能退化**：回归测试固定节点数与异常；墙钟退化需在同机运行
   `scripts/compare-experiments.py`，按 P50/P95/节点阈值提示。
8. **旧代码/评估与 TT 替换策略**：旧搜索/旧评估需要两次构建；本轮用
   完整/基础评估与 TT 开关做近似，并在 §4.1 引用 Phase 11 的跨构建 A/B。

## 7. 回滚

```bash
git revert <phase-13-commit>
```

局部回滚：删除 `src/experiment/`、`tests/run_experiment.c`、
`tests/test_experiment.c`、`experiments/`、`docs/experiments/` 与
`scripts/run-experiments.sh`；`ai.h` 删除 `max_nodes`/`node_limit`，
移除 `ai_search_choose_flags()`；`bench_ai.c` 可恢复本地 Random/Greedy。
`VERSION` 回退到 `1.3.0`。

## 8. 审查修正（`/code-review max --fix`）

| 审查发现 | 处理 | 复验 |
|---|---|---|
| 评估对照被 TT 混淆（Medium 不分配 TT） | `ai_search_choose_flags` 按 `use_tt` 为任意难度分配 TT；正式对局仍只给 Hard | 重跑 eval 实验，BasicEval `tt_hits=3,348` |
| 置信区间忽略配对设计 | Wilson 以配对开局为独立单位（8 局 = 4 对） | 重跑全部实验；报告更新 |
| 节点上限失败被记成超时 | 新增 `node_limit` 游戏标记、CSV 列与摘要计数 | `experiment.regression` 节点上限用例 |
| 报告缺少平均耗时/深度与总耗时 | 引擎统计与 JSON/MD/HTML 补齐 | 重跑报告 |
| 报告吞吐表与原始数据不符 | 按当前 `corpus.txt` 重写表格 | 数据文件对照 |
| 异常对局无法在页面查看 | JSON/HTML/Markdown 增加异常列表；索引增加原始数据链接 | 报告生成 |
| 无同机性能退化判定 | 新增 `scripts/compare-experiments.py`（P50/P95/节点阈值） | 脚本自检 + 文档 |
| 公开 `experiment_run` 不校验配置 | 入口完整校验，防止越界 | `experiment.regression` |
| 宏前缀、死分支、const cast、缺头文件、CMake 缩进 | 全部修正 | 构建无警告 |
| 基线引擎无直接测试 | `ai.core` 增加确定性/合法性断言 | `ai.core` |

保留：旧搜索/旧评估需两次构建；Elo 仅双引擎；崩溃不保留部分数据；`experiment.c` 约 850 行（配置/清单/对局/统计集中），清单字段仍分散在 `ExperimentResult` 中；墙钟性能退化只提供同机对照脚本，不进入 CI。

## 9. 下一阶段依赖

- 如需真正的新旧引擎对战，引入可切换的评估/搜索后端，而不是复制实现。
- 多引擎对手池与贝叶斯 Elo，需要更大样本与更长的手动运行。
- 将完整实验接入独立 CI 工作流（手动触发），并把性能阈值限定在同机对照。
