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
| 1. 真实性能基线 | `docs/benchmarks/phase11-baseline.txt`；14 局面语料（含 20/10/8/6/4/2 空、两个中局）与剪枝率/TT/内存输出 | PASS（测量环境见 §5） |
| 2. Bitboard 数据结构 | `src/core/bitboard.[ch]`，row-major 位映射、转换、计数、空位 | PASS |
| 3. 位运算规则引擎 | 合法着法/翻转/落子/Pass/终局；标量 Oracle 差分 | PASS（约 62 万次断言） |
| 4. 接入搜索 | 搜索内部使用 Bitboard；`Board`/`Match`/存档/UI 接口不变 | PASS |
| 5. 搜索优化 | 位板着法生成、叶子零着法数组、增量 Zobrist | PASS；其余候选项未合并（见 §2.4） |
| 6. 精确残局求解器 | `ai_endgame.[ch]`，EXACT/INCOMPLETE，Hard ≤12 空接入；对称/奇偶排序与跨调用 TT 复用未做 | PASS（部分） |
| 7. 跨平台计时 | Windows / Emscripten / POSIX 单调时钟 + C11 回退 | PASS |
| 8. 性能对照 | 同语料、同时段 A/B；节点/NPS/深度/剪枝率/TT 命中/峰值内存/对局结果 | PASS（绝对时间受外部负载影响） |
| 9. 正确性与兼容 | 桌面三套 17/17；Web 目标构建；存档/UI 未改 | PASS（桌面 + Web 构建）；Web 浏览器、Windows/Linux 未实测 |

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
  `phase11-ab-bitboard.txt`（修正前，文件头已标注）、
  `phase11-ab-baseline-final.txt`、`phase11-ab-bitboard-final.txt`
  （最终权威数据）、`phase11-matchups.txt`。
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
| ASan+UBSan 无窗口 | 17/17 PASS，无报告（29.7 s） |
| `core.bitboard` | 约 620,000 次差分断言 PASS |
| `ai.endgame` | 12/10/8/6/4/2 空分数与**独立**全深度 alpha-beta 一致（`ai_search_choose_ex` 已禁用残局求解器）；INCOMPLETE 分类 PASS |
| `ai.core` / `ai.worker` | PASS（TT/无 TT、取消、确定性、完整对局） |
| GitHub Actions（1.2.0，`40073769484`） | 9/9 PASS：三平台 Debug 各 17/17、三平台 Release 各 17/17、Linux ASan+UBSan 17/17、Xvfb GUI 2/2、Linux 发行包验收；首跑 `40073029646` 的 Linux 构建失败已修正 | PASS |
| Emscripten Release 构建 | PASS（主 wasm 316 KB，AI Worker wasm 54 KB） |
| Web 浏览器套件 | **NOT TESTED**：本地预览服务被系统因内存压力停止，未在本阶段重启 |
| 桌面 GUI | **NOT TESTED**：当前会话无显示器 |

`app.autoplay` 与 Web 自动对局未在本阶段重跑；规则与搜索的随机对局测试
覆盖了非法着法、终局与 Pass。

Linux 失败原因不是测试断言，而是严格 C11 下 `<time.h>` 不暴露
`CLOCK_MONOTONIC`：`ai_search` / `ai_endgame` 的 `defined(CLOCK_MONOTONIC)`
分支被编译掉、静默走非单调回退，`bench_corpus.c` 则因直接调用而编译失败。
已在三个编译单元包含头文件之前定义 `_POSIX_C_SOURCE=200809L`（仅
`__linux__`，避免 macOS 上隐藏 `ru_maxrss`）。本机三套 17/17 复验通过；
修正后 CI 重跑 `40073769484` 9/9 通过。

## 5. 性能数据

测量方法：同一份 `tests/bench_corpus.c` 分别编译到基线提交
（`22486c1`，标量规则）与当前树；同一时段交替运行，固定深度 6、Medium、
不使用 TT。固定深度下两套引擎的节点数完全相同，因此时间差来自实现而非
搜索树变化。

| 局面 | 空位 | 节点 | 基线 ms | 新 ms | 倍数 |
|---|---:|---:|---:|---:|---:|
| opening | 60 | 1,820 | 73.8 | 9.7 | 7.6× |
| midgame-40 | 40 | 39,686 | 516.2 | 198.2 | 2.6× |
| midgame-30 | 30 | 89,997 | 1564.7 | 575.9 | 2.7× |
| corner-contest | 52 | 15,200 | 149.0 | 160.7 | 0.93× |
| mobility-≤3 | 59 | 1,752 | 46.8 | 10.1 | 4.6× |
| endgame-20 | 20 | 33,844 | 276.6 | 113.1 | 2.4× |
| endgame-12 | 12 | 4,076 | 72.4 | 8.6 | 8.4× |
| endgame-10 | 10 | 1,086 | 1.74 | 1.93 | 0.90× |
| endgame-8 | 8 | 1,899 | 3.58 | 3.33 | 1.08× |
| endgame-6 | 6 | 543 | 0.42 | 0.49 | 0.85× |
| endgame-4 | 4 | 36 | 0.030 | 0.031 | — |
| endgame-2 | 2 | 10 | 0.008 | 0.009 | — |

corner-contest 与 10/6 空的小幅回退在噪声范围内（同一时段、节点相同）；
没有证据表明是系统性问题，未据此撤销位板实现。

Hard 默认 500 ms 预算下的实际深度：

| 局面 | 基线深度 | 新深度 | 备注 |
|---|---:|---:|---|
| opening | 7 | 10 | 397 ms |
| midgame-40 | 6 | 7 | |
| midgame-30 | 6 | 7 | |
| corner-contest | 7 | 8 | |
| mobility-≤3 | 9 | 10 | |
| endgame-20 | 7 | 8 | |
| endgame-12 | 12 | 12 | 281 → 22.6 ms，精确求解器 |
| endgame-10 | 10 | 10 | 12.5 → 1.7 ms |
| endgame-8 | 8 | 8 | 4.1 → 0.6 ms |

峰值 RSS：基线 5,072 KB，新引擎 9,136 KB（Hard TT 2 MiB 与残局 TT 在其
中）；同机轻载复跑为 5,056 KB，说明该指标受分配器复用影响，只作数量级
参考。对局基准 27 局 `illegal=0 failures=0 unfinished=0`；但基线与当前
运行使用不同编译器（Clang 23.1.3 / 21.0.0），基线记录缺少 Random 对局，
Easy vs Hard 的胜负也不同（1/2 与 0/3）。因此该数据只作为合法性与终止性
回归，不作为强度对照。原始输出见 `docs/benchmarks/phase11-matchups.txt`。

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
2. **两套 Zobrist/TT**：主搜索与残局求解器各有一份；`ai_endgame.c` 还重复
   实现了时钟、着法排序与 `moves_from_bits`，后续应上移到
   `ai_internal.h` 共享。
3. **TT 生命周期**：Hard 每次搜索 `calloc/free` 2 MiB，替换为直接覆盖；
   长对局中的分配开销与跨着法复用未处理。
4. **残局求解器只在 ≤12 空启用**：更深的精确求解需要更强的排序/内存
   策略与更长的预算。
5. **未实现未测量的优化**：PVS、Aspiration、Killer/History、评估缓存。
6. **Web 浏览器套件未复验**：AI Worker 的 wasm 已成功构建，但浏览器
   行为未在本阶段运行；桌面与 Web 共用同一份 AI 源码。
7. **性能数字受外部负载影响**：报告给出同时段 A/B，但不宣称绝对 NPS。
8. **测试辅助函数重复**：新增测试各自复制 `next_random`；可迁入
   `tests/test_helpers.h`。

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

## 8. 审查修正（`/code-review max --fix`）

| 审查发现 | 修正 | 复验 |
|---|---|---|
| 残局测试的“全深度参照”也调用求解器，等于自我比较 | `ai_search_choose_ex` 现在同时禁用 TT 与残局求解器；参照是独立的全深度 alpha-beta | `ai.endgame` PASS |
| 取消分类测试在 `if (solved)` 内断言，false 会静默通过 | 要求根节点 ≥2 个合法着法并断言 `solved && INCOMPLETE`；求解器在首个子树被取消时返回首个有序合法着法 | `ai.endgame` PASS |
| 求解器节点/耗时未计入返回统计 | 合并 nodes/cutoffs/tt_hits，并用 `session_start_ms` 计算总耗时 | 代码审查 + 基准 |
| Task 1 语料缺少 20/10/6/2 空、第二个中局、剪枝率/TT/内存 | 语料扩到 14 个局面，输出 cut%/tt/峰值 RSS | 最终 A/B 已重跑 |
| `apply_search_move` 文档声称会比较两套结果 | 文档改为“Bitboard 落子失败则分支失败；等价性由差分测试保证” | 文档同步 |
| ARCHITECTURE 的 AI/Endgame 依赖表过期 | 补充 `GameState`、`AiStats`、平台时钟头与 Emscripten | 文档同步 |
| Task 9 在矩阵中标记全 PASS，但浏览器/Windows/Linux 未测 | 矩阵改为“桌面 + Web 构建；其余未实测” | 文档同步 |

保留的偏差（记录而非掩盖）：

- **没有运行时可切换后端**：位板接入通过独立提交与基线提交的同语料
  A/B 验证；标量规则仍是 Oracle。运行时应答开关会增加一份需要长期维护
  的搜索路径。
- **提交粒度**：`5ab2d0d` 同时包含位板着法、增量哈希与叶子优化；它们
  在同一次回归中验证，但没有拆成三个提交。
- **残局状态**：任务书列出 `EXACT / HEURISTIC / INCOMPLETE`，实现只保留
  `AI_ENDGAME_EXACT` / `AI_ENDGAME_INCOMPLETE`；HEURISTIC 由常规迭代加深
  回退承担，API 未单列第三态，避免声明一个求解器永远不会产生的状态。
- **残局求解器**：未实现奇偶排序、跨调用 TT 复用；集成上限为 12 空。
- **平台**：只在本机 macOS 与 Emscripten 构建上执行；Windows/Linux
  物理机与 Web 浏览器套件未在 1.2.0 上复验。

### 8.1 第二次审查（Phase 10 + 11 联合，2026-10-24）

| 审查发现 | 处理 | 复验 |
|---|---|---|
| EMSCRIPTEN 下全局开启 GNU 扩展，与 ARCHITECTURE §10 冲突 | 仅 raylib 使用 GNU 模式，第一方源码严格 C11 | Web 构建 PASS |
| 内部 TT 标志与公共 `AI_ENDGAME_*` 同名不同义 | 重命名为 `ENDGAME_TT_*` | 三套 17/17 |
| 对局基准被描述为“与基线一致” | 记录编译器与对局集合差异，改为合法性/终止性回归 | 文档同步 |
| ROADMAP 仍引用修正前 A/B | 改用最终 14 局面数据 | 文档同步 |
| 任务书的三态分类缺少 HEURISTIC | 由回退搜索承担，记录为偏差 | 文档同步 |

## 9. 下一阶段依赖

- 优先实现位板评估（机动性、潜在机动性、前沿、稳定子），再复测中局。
- 评估位板化后，再考虑 PVS / Aspiration / Killer / History，并用同一
  语料做 A/B。
- 合并两份 Zobrist/TT，处理 Hard 的 TT 生命周期。
- 恢复 Web 预览服务后重跑 `tests/web/run-tests.js`，确认 AI Worker 行为。
- 发布 1.2.0 需要单独授权（推送、标签、三平台包）。
