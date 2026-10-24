# Phase 12 报告：Reversi Lab 棋局分析与复盘

日期：2026-10-24  
版本：**1.3.0**（源码；未推送、未发布）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
Homebrew Clang 21.0.0，CMake 4.4.4，Emscripten 6.0.13  
起始提交：`ad6ff01`（Phase 10/11 联合审查后）  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 完成矩阵

| 任务 | 交付 | 状态 |
|---|---|---|
| 1. 分析会话架构 | `analysis/analysis.[ch]`；独立 `AnalysisSession` 局面树，不修改 `Match` | PASS |
| 2. 自由摆盘 | 放置黑白、清除、清空、标准开局、行动方、换色、载入对局；`VALID/CUSTOM` 分类 | PASS |
| 3. AI 分析 API | `ai/ai_analyze.h` + `ai_analyze_position()`；推荐、深度、分数、节点、耗时、完成/精确、PV | PASS |
| 4. MultiPV | 最多 3 个候选；同深度全窗口、整层丢弃；候选分数可比 | PASS |
| 5. 评估可视化 | 总分、角点、机动、潜在机动、稳定子、前沿、位置、子差、阶段；固定黑方为正 | PASS |
| 6. 复盘 | 首/上/下/末、自动播放、速度、图线跳转、标记、分支前后切换与分数对比（深度不同会标注）、主线返回、History 导入 | PASS |
| 7. 棋谱格式 | 版本化文本 v1，严格校验、Pass、一层分支；保存/读取用户数据目录的 `lab-record.rvr` | PASS（固定文件名，无文件选择器） |
| 8. 分析 UI | 独立 Lab 页面；桌面最小 760×600 即显示完整评估与曲线；更窄用紧凑布局 | PASS（渲染冒烟） |
| 9. 生命周期 | 开始/停止/取消/换局面重搜；generation 绑定；后台线程/Worker | PASS |
| 10. 自动分析与复盘 | Analyze all；逐 ply 最佳分数、实际着法对比、深度/完成度；不做浅层“严重错误”判定 | PASS（保守版） |
| 11. 测试与性能 | 20 个无窗口测试、3 个 GUI 测试、Emscripten 构建、分析基准 | PASS（浏览器 NOT TESTED） |

## 2. 关键决策

1. **会话与对局彻底分离。** `AnalysisSession` 拥有自己的局面池；正式
   `Match` 只作为只读来源（导入 History 或当前局面）。分析页面没有任何
   写回 `Match` 的路径。
2. **编辑重置树。** 手工摆盘会产生无法从标准开局推导的局面；保留旧分支
   会暗示一段不存在的历史，因此第一次编辑把当前局面变成新的 `CUSTOM`
   根。UI 在编辑模式下明确提示。
3. **MultiPV 用“同深度整层”而不是每候选独立迭代。** 每层对所有根着法做
   全窗口搜索；某一层被取消就整层丢弃，保留上一层。这样同一结果内的候选
   深度相同、完成状态相同，避免把不同深度的分数当精确排名。
4. **分析不共享正式对局的选着路径。** `ai_analyze_position()` 独立实现，
   关闭 TT 分数截断（只用 TT 排序），不改变 `ai_choose_move()` 的行为。
5. **PV 在子树新鲜时收集。** 每个根着法搜索完成后立即从 TT 重建 PV，
   避免后续根着法覆盖条目；PV 在报告前逐着法重放验证。TT 未命中、Pass
   或长度上限会截短 PV。
6. **结果绑定 generation。** 任何导航、编辑、导入都递增
   `position_generation` 并取消在跑任务；旧结果无法覆盖新局面。
7. **Lab 视图编入 `reversi_app`。** `reversi_ui` 保持不依赖 AI，
   `ui.*` 单元测试的链接边界不变。

## 3. 修改的文件

新增：

- `src/analysis/analysis.[ch]`、`src/analysis/record.[ch]`
- `src/ai/ai_analyze.h`（实现位于 `src/ai/ai_search.c`）
- `src/ui/lab_view.[ch]`
- `tests/test_analysis.c`、`tests/test_analyze.c`、`tests/test_lab.c`、
  `tests/bench_analysis.c`
- `docs/ANALYSIS_ARCHITECTURE.md`、`docs/RECORD_FORMAT.md`
- `docs/benchmarks/phase12-analysis.txt`

修改：

- `src/ai/ai_search.c`：MultiPV、PV 重建、`ai_evaluation_to_black()`
- `src/ai/ai_worker.[ch]`、`src/platform/web/ai_worker_web.c`、
  `web/ai_worker_main.c`、`web/ai_worker_boot.js`：分析任务与文本协议
- `src/ui/menu_view.[ch]`：Analysis lab 入口（8 项）
- `src/app/app.c`：`APP_SCREEN_LAB`、生命周期、调试渲染冒烟
- `CMakeLists.txt`：`reversi_analysis`、Lab 源、测试与基准
- `tests/test_ai_worker.c`、`tests/test_endgame.c`（sanitizer 下的参照超时）
- `VERSION`、`CHANGELOG.md`、README、ARCHITECTURE、ROADMAP、TESTING

未修改：`core/rules.c`、`core/game.c`、`core/match.c`、`storage/`、
`settings/`、正式对局的 AI 选着路径。

## 4. 测试

| 配置 | 结果 |
|---|---|
| Debug 无窗口 `ctest` | 20/20 PASS |
| Release 无窗口 | 20/20 PASS |
| ASan+UBSan 无窗口 | 20/20 PASS，无报告 |
| Debug + GUI | 23/23 PASS（含 `app.lab` 20 帧渲染） |
| `analysis.session` | 108 次断言：树、分支、Pass、编辑、History 导入、棋谱往返/拒绝、访问器、ply 上限、VALID 根限制 |
| `analysis.lab` | 9 次断言：generation 守卫、进入/退出取消分析 |
| `ai.analyze` | 104 次断言：MultiPV 排序、PV 合法性、精确残局、取消、黑方视角 |
| `ai.worker` | 44 次断言：分析任务、取消、结果隔离 |
| `ai.endgame` | PASS（67 次断言）；sanitizer 下参照搜索超时则跳过比较 |
| Emscripten Release 构建 | PASS（主 wasm 346 KB，Worker wasm 58 KB） |
| GitHub Actions `40107607886` | 9/9 PASS：三平台 Debug 各 19/19、三平台 Release 各 19/19、Linux ASan+UBSan 无窗口 19/19、Xvfb GUI 3/3、Linux 包验收 |
| Web 浏览器套件 | **NOT TESTED**：本轮未启动预览服务 |

`app.lab` 是 Debug 渲染冒烟：在 Lab 页面渲染 20 帧并正常退出，验证布局
与绘制路径不崩溃；它不模拟点击，也不启动分析。

## 5. 性能

Release，Apple M3，命令：

```bash
./build-release/bin/reversi_analysis_bench
```

MultiPV=3、Hard 评估、上限 10 s：

| 局面 | 深度 | 耗时 | 节点 | 报告深度 | 备注 |
|---|---:|---:|---:|---:|---|
| opening | 4 | 1 ms | 190 | 4 | |
| opening | 6 | 8 ms | 1,704 | 6 | |
| opening | 8 | 61 ms | 13,931 | 8 | |
| midgame-30 | 4 | 14 ms | 3,884 | 4 | |
| midgame-30 | 6 | 149 ms | 44,588 | 6 | |
| midgame-30 | 8 | 2,314 ms | 683,318 | 8 | |
| endgame-8 | 8 | 11 ms | 12,754 | 8 | exact |

同一结果内三个候选分数可比；PV 最佳线达到完整深度，其他候选可能因 TT
未命中而较短。原始输出见 `docs/benchmarks/phase12-analysis.txt`。
测量时有后台任务，绝对毫秒数只作数量级参考。

## 6. 已知限制与缺口

1. **浏览器未复验**：Web 构建通过，但分析 Worker 的文本协议、棋谱文件
   导入/导出、窄屏 Lab 交互未在浏览器中运行。
2. **Windows/Linux 物理机未运行**：CI 托管 VM 已通过（三平台 19/19），
   但物理桌面 GUI 仍未实测。
3. **UI 交互未自动化**：`app.lab` 只验证渲染；按钮、棋盘点击、分支切换、
   拖拽/键盘流程由代码审查覆盖，没有自动化点击回归。
4. **长时分析**：分钟级连续分析与内存增长未测。
5. **PV 截短**：依赖 TT，非最佳候选的 PV 可能短于报告深度。
6. **复盘判定保守**：批次分析展示“最佳着法评估变化”，不把浅层搜索的
   单次波动称为“严重错误”；精确残局才给出确定分数。
7. **棋谱文件**：固定文件名 `lab-record.rvr`，没有文件选择器或多份棋谱
   管理；Web 端通过 `localStorage` 读写，未在浏览器复验。
8. **编辑语义**：自定义局面不校验能否从标准开局到达；第一次编辑会重置
   当前分支树。

## 7. 回滚

```bash
git revert <phase-12-commit>
```

局部回滚：

- 删除 `src/analysis/`、`src/ui/lab_view.*`、`src/ai/ai_analyze.h` 与
  对应测试/基准。
- `ai_search.c` 中删除分析段与 `no_tt_cutoffs` 字段；`ai_worker` 恢复
  单任务接口。
- 菜单恢复 7 项，`app.c` 删除 `APP_SCREEN_LAB`。
- `VERSION` 回退到 `1.2.0`。

回滚不影响正式对局、存档、设置与既有 AI 选着路径。

## 8. 下一阶段依赖

- 恢复 Web 预览服务，跑浏览器套件（含 Lab 分析 Worker 与棋谱文件）。
- 为 Lab 增加自动化点击/键盘回归（可复用 `__reversiTest` 诊断导出）。
- 评估更长的批次分析、TT 复用与 PV 三角表，减少 PV 截短。
- 若需要跨工具棋谱兼容，另立格式与验证，不修改 v1 语义。
