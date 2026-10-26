# Phase 13 独立验收与代码质量审计

日期：2026-10-26  
审计对象：Phase 13（原始任务书写作“Phase 11”，按仓库编号顺延为 13），
版本 **1.4.0**  
固定点：`defc4d9`（Phase 12 末）；审计时 HEAD：`22cbec6`；审计修复：
`d7c6bb8` 及本报告所在提交  
证据标记：PASS / FAIL / NOT TESTED / PARTIAL

## 1. 审查范围

重新阅读并交叉核对：

- 原始任务书（10 项任务与验收条件）。
- `docs/PHASE_13_REPORT.md`、`docs/BENCHMARK_METHODOLOGY.md`。
- `git diff defc4d9...HEAD`（源码、测试、配置、生成数据）。
- `src/experiment/`、`src/ai/ai_baselines.*`、`src/ai/ai_search.*` 的本次改动。
- `tests/test_experiment.c`、`tests/test_ai.c`、`tests/test_endgame.c`。
- `ARCHITECTURE.md` §22、`TESTING.md`、`README.md`、`CHANGELOG.md`。

不采信完成报告中的数字；下面的结论来自独立重跑、原始 CSV/JSON 交叉核对
与代码阅读。独立一致性检查输出保存在
`docs/experiments/audit.txt`。

## 2. 发现的问题（按严重程度）

| ID | 严重度 | 问题 | 证据 | 状态 |
|---|---|---|---|---|
| H1 | 高 | 实验种子没有控制 Easy 的随机性：AI 配置使用 `spec->seed`（默认 1），不是配置的 `seed`；修改实验种子不会改变 Easy 的着法，违反“固定种子可复现” | `experiment.c` 旧代码 `mix_seed(spec->seed, ...)`；修复后 `random-100` 结果改变，且两次运行 CSV 完全一致 | 已修复 |
| H2 | 高 | 设置节点上限时，精确残局求解器仍会先运行；求解器只按时间计预算，节点上限可被绕过 | `search_choose` 在节点预算检查前调用求解器；新增 `ai.endgame` 用例在 4 空局面 `max_nodes=1` 断言失败为 node_limit | 已修复 |
| M1 | 中 | `report.md` 写入失败时 `summary.json` 的缓冲区未释放 | `experiment_write_all` 失败分支缺 `buffer_free(&json)` | 已修复 |
| M2 | 中 | 报告中的规则吞吐表与 `corpus.txt` 不一致，且高负载下微基准在不同运行间相差一个数量级 | 审计脚本对比表格与原始文件；多次重跑数值不可比 | 已修复（移除不可靠表格，改用 Phase 11 受控 A/B，原始文件保留） |
| M3 | 中 | 超时与非法着法的对局级路径没有自动化测试；运行器没有注入非法/超时引擎的接口 | 现有测试覆盖 node_limit 与 aborted；timeout/illegal 仅代码审查 | PARTIAL（记录） |
| M4 | 中 | 颜色分组只给胜场，未给每色 W/D/L 明细；平局不进入颜色统计 | `ExperimentSummary` 只有 `a_black_wins` 等字段 | PARTIAL（记录） |
| L1 | 低 | `experiment.c` 约 850 行，配置/清单/对局/统计集中；manifest 字段仍散在 `ExperimentResult`；`set_error`/`report_error` 重复 | 代码阅读 | 接受（技术债） |
| L2 | 低 | 无完成对局时 CLI 仍把未定义的 Elo 打印为 0 | `experiment_print_summary` | 已修复（打印 n/a） |
| L3 | 低 | 配置重复键静默取最后一个；未在文档声明 | 代码阅读 | 接受（记录） |
| L4 | 低 | `ai.endgame` 断言数与报告/证据文件不一致（67 vs 70） | 报告/`verify.txt` 对照 | 已修复（重新生成证据） |

已重新验证上一轮审查的修正，未发现回退：

- 评估对照的 TT 混淆已消除（BasicEval `tt_hits` 非零）。
- Wilson 区间以配对开局为单位（8 局 = 4 对），所有区间跨 0.5。
- 节点上限失败与超时分开记录（`node_limit` 列/计数）。
- 平均耗时/深度、异常列表、索引原始数据链接存在。
- 吞吐表、实验数据与报告数字一致（审计脚本无错误）。

## 3. 修复内容

1. **H1**：所有引擎随机流改为由
   `实验种子 ^ 引擎 seed`、局号与引擎编号共同派生；Easy 与 Random 都受
   实验种子控制。`experiment.regression` 的确定性 oracle 相应更新。
2. **H2**：`max_nodes > 0` 时跳过精确残局求解器与残局自动加深；新增
   `test_node_cap_skips_solver`（4 空局面、`max_nodes=1`），断言
   返回失败且 `node_limit`，不是超时。
3. **M1**：补齐失败路径的缓冲区释放。
4. **M2**：移除高负载下不可比的微基准表，改用 Phase 11 的受控 A/B；`corpus.txt` 保留并标注高负载。
5. **L2**：无完成对局时 CLI 打印 `n/a`。
6. **L4**：重新生成 `endgame-verify/verify.txt`（70 次断言）。
7. 重新生成全部实验数据（`sha=d7c6bb8`）与索引；`docs/experiments/audit.txt`
   记录一致性检查。
8. 强度统计只使用**双方都完成**的配对开局；未配对的幸存者单独计入
   `excluded_unpaired`，不再当作独立样本。
9. 补齐种子敏感性测试（改 seed 必须改变 Random 着法）与 `aborted` 注入
   测试；`experiment.regression` 增至 87 次断言。
10. 固定深度/节点实验改用最大时间上限（10 s），只由深度/节点停止；
    固定时间实验保留 150 ms。四组固定深度实验重跑 `games.csv` 逐字节一致。
11. 新增发现并修复：固定深度实验原先带紧时间上限，负载下会变成时间依赖，
    破坏“固定配置可复现”的声明。

没有删除或弱化任何测试；新增了节点上限与残局求解器交互的测试。

## 4. 实际执行的测试

| 项目 | 命令/方法 | 结果 |
|---|---|---|
| Debug 无窗口 | `ctest --test-dir build` | 21/21 PASS |
| Release 无窗口 | `ctest --test-dir build-release` | 21/21 PASS |
| ASan+UBSan | `ctest --test-dir build-sanitize` | 21/21 PASS，无报告 |
| Debug + GUI | `ctest --test-dir build-gui` | 24/24 PASS |
| Web 构建 | `scripts/build-web.sh` | PASS（实验模块不进入 Web） |
| 实验套件 | `scripts/run-experiments.sh` | 7 组对战 + 吞吐 + 残局；`illegal=0 timeouts=0 node_limited=0 aborted=0` |
| 随机确定性 | `reversi_experiment experiments/random-100.exp` 两次 | `games.csv`/`moves.csv` 逐字节相同 |
| 指标重算 | 从 `moves.csv` 重算节点/剪枝/TT/耗时/P50/P95/平均 | 与 `summary.json` 完全一致，无错误 |
| 数据一致性 | 分类求和、胜负和、颜色和、分数公式、CI 包含、CSV/JSON/异常列表/索引链接/吞吐表 | 无错误 |
| 性能对照脚本 | 对自身（PASS）与人为放大 3 倍 P50/P95（检出 REGRESSION） | 行为正确 |
| 节点上限/残局 | `ai.endgame` 70 次断言 | PASS |
| 节点上限/flags | `ai.core` 2,988 次断言 | PASS |
| CI（审计前代码） | GitHub Actions `40118354934` | 9/9 success（三平台 21/21、ASan 21/21、Xvfb 3/3） |
| 审查修正 CI | GitHub Actions `40288248734` | 9/9 success（三平台 21/21、ASan 21/21、Xvfb 3/3） |

## 5. 仍未解决的风险

1. **浏览器套件**：Lab/Web AI 在 1.4.0 上未复验；实验平台不进入 Web，但
   AI 配置字段（`max_nodes` 默认 0）随 Web 构建编译。**NOT TESTED**。
2. **超时/非法着法的对局级路径**：分类逻辑存在，但没有注入式测试。
   **NOT TESTED**。
3. **崩溃**：进程崩溃以非零退出，不写部分原始数据；未做故障注入。
4. **旧/新搜索与旧评估**：需要两次构建；本轮用 Phase 11 的跨构建 A/B
   引用，没有在同一运行器中切换实现。**PARTIAL**。
5. **性能退化**：只提供同机手动对照脚本；CI 只校验确定性节点数与异常，
   不比较墙钟。固定时间实验跨重跑波动（4:4、5:3、3:5、2:6）。
6. **统计**：每实验 4 个配对单位；所有 95% 区间跨 0.5，没有显著强度结论。
   固定时间预算每 1024 节点检查一次，负载下观测到 150 ms 预算、本次
   最大 225 ms（此前 417 ms）的单着耗时；已在该实验报告中披露。
7. **颜色分组**：仅胜场，无每色 W/D/L 明细。
8. **可视化**：静态索引与单实验页；没有跨实验对比控件。
9. **代码结构**：`experiment.c` 较大、manifest 字段散落、错误 helper 重复；
   不影响当前验收，但后续扩展前应拆分。

## 6. 是否达到验收标准

| 验收条件 | 结论 | 依据 |
|---|---|---|
| 实验能通过命令重复运行 | PASS | `scripts/run-experiments.sh` |
| 固定配置/种子可复现逻辑结果 | PASS | 回归 oracle；random-100 两次一致；四组固定深度实验 `games.csv` 逐字节一致；固定时间实验不在此列 |
| 数据文件完整 | PASS | 审计脚本无错误 |
| 异常对局不会被遗漏 | PARTIAL：node_limit 与 aborted 有注入测试；timeout/illegal 对局级路径未测；崩溃 NOT TESTED | games/summary/anomalies 交叉一致 |
| 性能统计准确 | PASS | 从 moves.csv 独立重算一致 |
| 强度估计有明确方法 | PASS | 方法文档；配对 Wilson + Elo 模型 |
| 图表可追溯到原始数据 | PASS | 内嵌 summary JSON；链接原始 CSV |
| 现有游戏功能不受影响 | PASS（本机 21/21、24/24、Web 构建）；浏览器 NOT TESTED | 回归矩阵 |

总体：**达到阶段验收标准**，附带上表中的 NOT TESTED/PARTIAL 项；没有
发现阻塞性缺陷，修复后的一致性与可复现性可接受。

## 7. 是否建议进入下一阶段

**建议进入下一阶段**，前提是：

1. 审计提交 CI 已确认 9/9（`40281111365`；最终文档 `40281520579`）。
2. 下一阶段优先补浏览器回归与 Lab/实验 UI 的自动化点击。
3. 若要做真正的旧/新引擎与旧评估对战，先设计可切换后端；不要复制搜索。
4. 性能阈值继续限定同机对照，不把 CI 原始耗时当作算法优劣。
