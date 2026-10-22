# Phase 11 报告：AI 引擎 2.0（Bitboard 与精确残局）

日期：2026-10-23  
版本：**1.2.0**（源码；未发布、未推送）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
Homebrew Clang 23.1.3，CMake 4.4.4，Emscripten 6.0.13  
起始提交：`22486c1`（Phase 10 合并后）  
阶段提交：`7102239`（Bitboard）、`5ab2d0d`（搜索接入），其余见本阶段最后一次提交  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 阶段目标与完成矩阵

目标：在不破坏规则、存档、UI 与 Web 行为的前提下，引入 Bitboard、
搜索热点优化与可分类的精确残局求解。

| 任务 | 交付 | 状态 |
|---|---|---|
| 1. 真实性能基线 | `docs/benchmarks/phase11-baseline.txt`；固定语料工具 `tests/bench_corpus.c` | PASS（测量环境有外部负载，见 §5） |
| 2. Bitboard 数据结构 | `src/core/bitboard.[ch]`，row-major 位映射、转换、计数、空位 | PASS |
| 3. 位运算规则引擎 | 合法着法/翻转/落子/Pass/终局；标量 Oracle 差分 | PASS（约 62 万次断言） |
| 4. 接入搜索 | 搜索内部使用 Bitboard；`Board`/`Match`/存档/UI 接口不变 | PASS |
| 5. 搜索优化 | 位板着法生成、叶子零着法数组、增量 Zobrist | PASS；其余候选项未合并（见 §2.4） |
| 6. 精确残局求解器 | `ai_endgame.[ch]`，EXACT/INCOMPLETE，Hard ≤12 空接入 | PASS |
| 7. 跨平台计时 | Windows / Emscripten / POSIX 单调时钟 + C11 回退 | PASS |
| 8. 性能对照 | 同语料、同时段 A/B；固定深度节点数一致 | PASS（绝对时间受外部负载影响） |
| 9. 正确性与兼容 | 桌面三套 17/17；Web 目标构建；存档/UI 未改 | PASS；Web 浏览器套件 NOT TESTED |

## 2. 关键架构决策

1. **搜索同时携带 GameState 与 Bitboard。**
   评估、终局计分与对外结果继续使用 `GameState`；Bitboard 只负责合法
   着法、落子与哈希。这样避免了先重写评估带来的行为漂移，也让两套规则
   在差分测试中互为 Oracle。`apply_search_move()` 是唯一的状态转换点。

2. **增量 Zobrist，不改变 TT 语义。**
   落子时异或落点、翻转位与回合键；Pass 只交换回合键。TT 的键、深度、
   边界标签与替换策略未改，因此原有 TT/无 TT 一致性测试继续有效。

3. **残局求解器保留预算余量。**
   求解器最多使用 75% 的预算；若被截断，主搜索重置起点并至少获得
   50 ms。这个修正来自一次真实回归：求解器耗尽 500 ms 后，回退搜索
   被同一时钟判为超时并返回无着法。

4. **只合并可证明有效的优化。**
   位板着法生成与增量哈希有同语料 A/B 和相同节点数支撑；评估仍是标量，
   中局固定深度没有明显改善。PVS、Aspiration、Killer/History 未实现，
   因为没有测量证据表明它们优于当前评估热点。

5. **计时按平台分支，保持 C11 回退。**
   Windows `GetTickCount64`、Emscripten `emscripten_get_now`、POSIX
   `clock_gettime(CLOCK_MONOTONIC)`；其他环境回退 `timespec_get`。

## 3. 修改的文件

核心与 AI（8）：

- `src/core/bitboard.h`、`src/core/bitboard.c`（新增）。
- `src/ai/ai_search.c`：位板着法/落子、增量 Zobrist、残局接入、单调时钟、
  预算余量。
- `src/ai/ai_endgame.h`、`src/ai/ai_endgame.c`（新增）。
- `tests/test_bitboard.c`、`tests/test_endgame.c`、`tests/bench_corpus.c`
  （新增）。
- `CMakeLists.txt`：核心源、AI 源、两个测试目标、语料基准目标。

数据与文档（6）：

- `docs/benchmarks/phase11-baseline.txt`、`phase11-ab-baseline.txt`、
  `phase11-ab-bitboard.txt`、`phase11-ab-bitboard-rerun.txt`。
- `docs/AI_ENGINE.md`、`docs/PHASE_11_REPORT.md`（本报告）。
- `VERSION`、`CHANGELOG.md`、`README.md`、`docs/ARCHITECTURE.md`、
  `docs/ROADMAP.md`、`docs/TESTING.md`。

未修改：`core/rules.c`、`core/game.c`、`core/match.c`、`storage/`、
`settings/`、`ui/`、`app/`、存档格式与 Web 适配层。

## 4. 实际执行的测试

| 配置 | 结果 |
|---|---|
| Debug 无窗口 `ctest` | 17/17 PASS（含 `core.bitboard`、`ai.endgame`） |
| Release 无窗口 | 17/17 PASS |
| ASan+UBSan 无窗口 | 17/17 PASS，无报告（205.6 s） |
| `core.bitboard` | 约 620,000 次差分断言 PASS |
| `ai.endgame` | 12/10/8/6/4/2 空分数与全深度搜索一致；INCOMPLETE 分类 PASS |
| `ai.core` / `ai.worker` | PASS（TT/无 TT、取消、确定性、完整对局） |
| Emscripten Release 构建 | PASS（主 wasm 316 KB，AI Worker wasm 54 KB） |
| Web 浏览器套件 | **NOT TESTED**：本地预览服务被系统因内存压力停止，未在本阶段重启 |
| 桌面 GUI | **NOT TESTED**：当前会话无显示器 |

`app.autoplay` 与 Web 自动对局未在本阶段重跑；规则与搜索的随机对局测试
覆盖了非法着法、终局与 Pass。

## 5. 性能数据

测量方法：同一份 `tests/bench_corpus.c` 分别编译到基线提交
（`22486c1`，标量规则）与当前树；同一时段交替运行，固定深度 6、Medium、
不使用 TT。固定深度下两套引擎的节点数完全相同，因此时间差来自实现而非
搜索树变化。

| 局面 | 空位 | 节点 | 基线 ms | 新 ms | 倍数 |
|---|---:|---:|---:|---:|---:|
| opening | 60 | 1,820 | 284.5 | 90.2 | 3.2× |
| midgame-40 | 40 | 39,686 | 3066.3 | 3220.9 | 1.0× |
| corner-contest | 52 | 15,200 | 1978.7 | 1268.5 | 1.6× |
| mobility-≤3 | 59 | 1,752 | 271.7 | 229.1 | 1.2× |
| endgame-12 | 12 | 4,076 | 184.4 | 37.8 | 4.9× |
| endgame-8 | 8 | 1,899 | 45.2 | 8.9 | 5.1× |
| endgame-4 | 4 | 36 | 0.04 | 0.04 | — |

Hard 默认 500 ms 预算下的实际深度：

| 局面 | 基线深度 | 新深度 | 备注 |
|---|---:|---:|---|
| opening | 6 | 7 | |
| midgame-40 | 5 | 6 | |
| corner-contest | 5 | 6 | |
| mobility-≤3 | 7 | 7 | |
| endgame-12 | 7 | 12 | 新引擎使用精确求解器，99,753 节点 / 95.5 ms |
| endgame-8 | 8 | 8 | 节点 2,451 → 868 |

微基准（10,000 次/局面，Release）：

- 合法着法：标量约 4–25 µs/次，Bitboard 约 5–850 ns/次；绝对值随机器
  负载波动很大，只用于说明数量级。
- 落子：标量约 0.3–2 µs，Bitboard 约 10–460 ns。
- 中局固定深度没有明显改善，因为评估仍是标量实现（每叶 2 次标量机动性
  枚举 + 4 次全盘扫描），这是下一步的主要热点。

**测量环境警告**：测量期间另一个项目的 Playwright 测试使 load average
达到 30–76。固定深度 A/B 是同时段对照，倍数可信；绝对毫秒数不代表空载
性能。基线文件中的早期数字（load 较低时）仅作为参考。

## 6. 遗留风险与技术债

1. **评估仍是标量**：中局 NPS 的主要限制；位板化的机动性/前沿/潜在
   机动性/稳定子尚未实现。
2. **两套 Zobrist/TT**：主搜索与残局求解器各有一份；后续可共享。
3. **TT 生命周期**：Hard 每次搜索 `calloc/free` 2 MiB，替换为直接覆盖；
   长对局中的分配开销与跨着法复用未处理。
4. **残局求解器只在 ≤12 空启用**：更深的精确求解需要更强的排序/内存
   策略与更长的预算。
5. **未实现未测量的优化**：PVS、Aspiration、Killer/History、评估缓存。
6. **Web 浏览器套件未复验**：AI Worker 的 wasm 已成功构建，但浏览器
   行为未在本阶段运行；桌面与 Web 共用同一份 AI 源码。
7. **性能数字受外部负载影响**：报告给出同时段 A/B，但不宣称绝对 NPS。

## 7. 回滚方法

本阶段提交（`7102239`、`5ab2d0d` 与最终提交）可整体回滚：

```bash
git revert <final-commit> 5ab2d0d 7102239
```

局部回滚：

- **Bitboard**：删除 `src/core/bitboard.[ch]` 与 CMake 中的源/测试项。
- **搜索**：`git show 22486c1:src/ai/ai_search.c` 取回标量搜索；恢复
  `ai_worker` 与 Web 适配不受影响。
- **残局求解器**：删除 `src/ai/ai_endgame.[ch]` 与 `ai_search.c` 中的接入块。
- **版本**：`VERSION` 回退到 `1.1.0`。

回滚不影响存档、UI 或 Web 容器；`Board`/`Match` 接口从未改变。

## 8. 下一阶段依赖

- 优先实现位板评估（机动性、潜在机动性、前沿、稳定子），再复测中局。
- 评估位板化后，再考虑 PVS / Aspiration / Killer / History，并用同一
  语料做 A/B。
- 合并两份 Zobrist/TT，处理 Hard 的 TT 生命周期。
- 恢复 Web 预览服务后重跑 `tests/web/run-tests.js`，确认 AI Worker 行为。
- 发布 1.2.0 需要单独授权（推送、标签、三平台包）。
