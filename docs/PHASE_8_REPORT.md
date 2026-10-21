# Phase 8 报告：工程债与文档一致性

日期：2026-10-21  
版本：**1.0.0**（`VERSION` 未变更）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
Homebrew Clang 23.1.3，CMake 4.4.4  
起始提交：`3db8034`  
阶段提交：`33444eb`（本报告另起一个文档提交）  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 阶段目标与完成矩阵

目标：在不改变游戏行为的前提下，消除文档与工程结构中已知的不一致。

| 项目 | 结果 | 状态 |
|---|---|---|
| 合并 `ROADMAP.md` 重复的两个 Phase 6 章节 | 删除「原计划」章节，保留实际完成记录 | PASS |
| 「非目标」位置 | 从章节中间移到全文末尾 | PASS |
| ASan+UBSan 数字口径 | README 改为「无窗口 15/15」，与 `TESTING.md` 和本轮实测一致 | PASS |
| `AGENTS.md` 的 `graphify-out/` 过期说明 | 改为条件判断（文件在仓库外，不进入提交） | PASS |
| CI 三平台打包步骤重复 | 提取 `.github/workflows/package.yml` | PASS（静态检查）；实跑 NOT TESTED |
| 应用图标（macOS） | `.icns` 进包、`Info.plist` 声明、签名有效 | PASS |
| 应用图标（Windows） | `.ico` 并入版本资源；`windres` 单资源编译成功 | PASS（资源）；完整构建 NOT TESTED |
| 应用图标（Linux） | 便携包无桌面集成 | NOT APPLICABLE |
| 回归测试 | Debug 17/17、Release 15/15、ASan+UBSan 15/15 | PASS |
| 阶段报告与文档同步 | 本报告 + README/ARCHITECTURE/ROADMAP/TESTING/CHANGELOG/RELEASE | PASS |

未在本阶段处理、仍待决策的项：`CHANGELOG.md` 的 `[Unreleased]` 仍需要
一个补丁版本标签才能进入发行附件。

## 2. 关键架构决策

1. **图标是打包输入，不是运行时资源。**
   运行时资源契约保持不变：只有 `assets/fonts/` 会被加载。图标放在
   `cmake/icons/`，macOS 由 `MACOSX_PACKAGE_LOCATION "Resources"` 放进
   应用包，Windows 由 `version.rc.in` 嵌入可执行文件。`ARCHITECTURE.md`
   的「程序化视觉资源」条目已同步说明这一例外。

2. **图标由脚本生成并提交产物。**
   `scripts/make-icon.sh` 用 ImageMagick 图元绘制源图，再生成 `.icns`
   与 `.ico`。产物入库，构建时不需要 ImageMagick 或 `iconutil`；只有修改
   设计时才重新运行脚本。

3. **CI 用可复用工作流消除重复，而不是复制粘贴。**
   `package.yml` 是唯一的 Release 构建、测试、校验和规范化、Windows
   版本冒烟与产物上传实现。`ci.yml` 只保留 Debug 矩阵，Release 由
   `package` 作业承担，避免同一提交构建两次 Release。

4. **历史阶段报告不改写。**
   `PHASE_7_REPORT.md` 中「17/17」是更早一次含 GUI 的 Sanitizer 运行，
   与之后的「无窗口 15/15」并不矛盾。本阶段只修正面向当前状态的文档
   （README），历史记录保留原文并在本报告说明口径。

5. **图标设计是默认方案。**
   在未收到视觉指定的前提下，采用与默认深色主题一致的深绿棋盘加四枚
   黑白棋子的构图（Othello 起始布局）。它可以通过修改脚本中的图元整体
   替换，不影响任何代码路径。

## 3. 修改的文件

代码与构建（4）：

- `CMakeLists.txt`：macOS 图标属性与 Resources 打包；Windows 图标路径替换。
- `cmake/Info.plist.in`：`CFBundleIconFile`。
- `src/version.rc.in`：`IDI_ICON1 ICON`。
- `scripts/make-icon.sh`：图标生成脚本。

CI（3）：

- `.github/workflows/package.yml`（新增）：可复用 Release 打包工作流。
- `.github/workflows/ci.yml`：Debug 矩阵 + 调用可复用工作流。
- `.github/workflows/release.yml`：`package` 作业改为调用可复用工作流。

图标产物（4）：

- `cmake/icons/reversi-1024.png`
- `cmake/icons/Reversi.icns`
- `cmake/icons/reversi.ico`
- `cmake/icons/README.md`

文档（7）：

- `README.md`、`CHANGELOG.md`、`assets/README.md`、
  `docs/ARCHITECTURE.md`、`docs/RELEASE.md`、`docs/ROADMAP.md`、
  `docs/TESTING.md`。

仓库外（1）：

- `/Users/leo/AGENTS.md`：`graphify-out/` 由断言改为条件判断。

没有任何游戏逻辑、AI、UI、存档或测试源码被修改。

图标校验值：

| 文件 | SHA-256 |
|---|---|
| `reversi-1024.png` | `73b2ce447114e4d281e8dbdf217c6340e2f2344db777b65899eb3a1be02d20c1` |
| `Reversi.icns` | `b3dfef913c59feed228b2f90cd5e25d619ae356867e6a6e42d403cad3a4a915c` |
| `reversi.ico` | `cf191d763a824eb864c93e5820142285a4f546b89a2117138128e69093830989` |

## 4. 实际执行的测试与结果

全部在本机 macOS arm64、改动后的代码树上执行。

| 项目 | 命令要点 | 结果 | 状态 |
|---|---|---|---|
| Debug（含 GUI） | `ctest --test-dir build -C Debug` | 17/17，27.67 s | PASS |
| Release | `ctest --test-dir build-release -C Release` | 15/15，11.99 s | PASS |
| ASan+UBSan（无窗口） | 重建 `build-sanitize`；`-E 'app\.'` | 15/15，34.44 s，无报告 | PASS |
| Sanitizer 标志 | 检查 `flags.make` 与 `link.txt` | `-fsanitize=address,undefined` | PASS |
| 工作流静态检查 | `actionlint` 三个文件 | 退出码 0 | PASS |
| macOS 图标进包 | 检查 `Contents/Resources/` | `Reversi.icns` 存在 | PASS |
| `Info.plist` 声明 | `plutil -p` | `CFBundleIconFile = Reversi.icns` | PASS |
| macOS 签名 | `codesign --verify --deep --strict` | valid on disk / satisfies DR | PASS |
| 启动冒烟 | `Reversi --frames 2` | 退出码 0 | PASS |
| CPack 归档内容 | `unzip -l` | 归档内含 `Reversi.icns` | PASS |
| 发行包验收 | `scripts/verify-package.sh <zip>` | `PASS` | PASS |
| Windows 图标资源 | `windres --target=pe-x86-64` | 生成 `.rsrc` COFF 对象 | PASS（单资源） |
| GitHub Actions 实跑 | 未推送 | — | NOT TESTED |
| Windows 完整构建 | 本机无 MSVC | — | NOT TESTED |
| Sanitizer + GUI | 本轮未重跑 | — | NOT TESTED |
| Linux 发行包 | 本机为 macOS | — | NOT TESTED |

说明：ASan 首次冷启动运行耗时 189.50 s，重建后为 34.44 s；其中
`ai.core` 单项 23.45 s。这只是同一套 Sanitizer 构建的两次通过运行，
不构成性能结论。

## 5. 兼容性与性能数据

- **存档/设置兼容性**：本阶段未修改 `storage/`、`settings/` 或任何
  序列化格式，版本仍为 1.0.0；旧用户数据不受影响。
- **运行时资源**：仍只加载 `assets/fonts/`；图标不在运行时读取。
- **发行包**：macOS 归档新增 `Reversi.icns`（约 1.0 MB）；Windows
  可执行文件新增图标资源（约 0.36 MB）。包体积变化来自图标本身。
- **构建时间**：未做基准测量，不宣称变化。
- **CI 结构**：Debug 三平台保持并行；Release 构建由 `package` 作业在
  Debug 矩阵之后执行一次，取代原先每个 Release 矩阵项各自打包。

## 6. 遗留风险与技术债

1. **CI 改动未实跑。** `actionlint` 只证明语法与引用合法，不能证明
   artifact 名称、`needs` 顺序与跨作业下载在 GitHub 上成立。推送后必须
   观察一次完整运行。这是本阶段最大的未验证项。
2. **Windows 完整构建未验证。** `windres` 只验证了图标资源本身；MSVC
   的 `rc.exe`、静态 CRT 与版本资源组合仍是 NOT TESTED。
3. **图标是默认设计。** 未经过视觉评审；替换时需重跑
   `scripts/make-icon.sh` 并重新提交三个产物。
4. **Linux 无桌面图标。** 便携 ZIP 不安装 `.desktop` 文件，任务栏图标
   缺失属于形式限制，不是缺陷。
5. **`[Unreleased]` 未发布。** 本阶段的图标与 CI 改动，连同 Phase 7
   发布后的 6 项修复，都需要新的版本标签才能进入发行附件。
6. **历史文档仍有旧口径。** `PHASE_7_REPORT.md` 的「无应用图标」与
   「ASan 17/17」保留为当时记录，当前状态以 README/TESTING/本报告为准。
7. **既存缺口未变。** 键盘导航、窗口缩放、真实听感、Windows/macOS
   物理桌面 GUI、打包 GUI 手动存/读档仍为 NOT TESTED。

## 7. 回滚方法

阶段提交为 `33444eb`，可整体回滚：

```bash
git revert 33444eb
```

按改动面局部回滚：

- **图标**：从 `CMakeLists.txt` 删除 `MACOSX_BUNDLE_ICON_FILE`、icns 的
  `target_sources` 与 `set_source_files_properties`，删除
  `Info.plist` 的 `CFBundleIconFile`，删除 `version.rc.in` 的
  `IDI_ICON1`，再删除 `cmake/icons/` 与 `scripts/make-icon.sh`。
- **CI**：删除 `package.yml`，恢复 `ci.yml`/`release.yml` 中内联的
  打包步骤。`git show 3db8034:.github/workflows/ci.yml` 可取回原文。
- **文档**：`git show 3db8034:<path>` 取回任一文件的原版本。

回滚不会影响用户数据或已发布归档。

## 8. 下一阶段依赖

- 推送 `main` 并确认 GitHub Actions 完整通过（本阶段的 CI 改动是唯一
  未实跑的交付物）。
- 决定是否以 `v1.0.1` 发布 `[Unreleased]` 内容。
- 若继续做可访问性/键盘导航，需先补自动化 GUI 输入回归，否则无法区分
  「未测试」与「已通过」。
