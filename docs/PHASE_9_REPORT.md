# Phase 9 报告：v1.0.1 补丁发布

日期：2026-10-22  
版本：**1.0.1**（根目录 `VERSION`）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
Homebrew Clang 23.1.3，CMake 4.4.4  
起始提交：`ae50eab`（Phase 8 末尾）  
阶段提交：`0c341ea`、`323856f`、`ea7aa12`，标签 `v1.0.1`  
状态标记：PASS / FAIL / NOT TESTED / NOT APPLICABLE

## 1. 阶段目标与完成矩阵

目标：推送 Phase 8 改动以验证 CI 重构，并将 `[Unreleased]` 内容以
`v1.0.1` 发布。

| 项目 | 结果 | 状态 |
|---|---|---|
| 推送 `main` | 经 SSH 推送成功（HTTPS 443 在本机网络被阻断） | PASS |
| CI 重构首次验证（`ae50eab`） | 5 成功、1 失败、2 跳过 | FAIL |
| 定位并修复 Windows 资源编译失败 | `.rc` 反斜杠转义问题，改用正斜杠 | PASS |
| CI 复验（`323856f`） | 9/9 `success`，运行 `39633821096` | PASS |
| 版本号提升到 1.0.1 | `VERSION`、二进制 `--version`、`Info.plist` 一致 | PASS |
| 标签与发布 | `v1.0.1`，发布工作流 `39634542587` 成功 | PASS |
| 发布后独立验收 | 下载 macOS 归档，校验值与 `SHA256SUMS` 一致，包验收 PASS | PASS |
| 文档同步 | README/CHANGELOG/ROADMAP/TESTING/RELEASE/RELEASES | PASS |

## 2. 关键架构决策与事件

1. **CI 重构通过真实运行验证，而不是只靠静态检查。**
   首次推送（`ae50eab`）暴露了 Windows 构建失败，证明 `actionlint`
   只能覆盖语法与引用，不能替代跨平台执行。修复后 `323856f` 9/9 通过。

2. **Windows 图标路径使用正斜杠。**
   `file(TO_NATIVE_PATH)` 生成 `D:\a\...`，`.rc` 把它按 C 字符串解析，
   `\a`、`\r` 变成控制字符，`rc.exe` 报
   `RC2135: file not found`。CMake 的原始路径在 Windows 上本来就是
   正斜杠，资源编译器可以直接使用，因此删除了 `TO_NATIVE_PATH`。

3. **发布基于已通过 CI 的提交。**
   标签 `v1.0.1` 指向 `ea7aa12`，其前置 `323856f` 已通过完整 CI；
   发布工作流又在标签上重建、测试并打包三平台产物。

4. **发布后重新下载并校验实际附件。**
   不只依赖工作流日志：通过 GitHub API 取回 macOS 归档，核对
   SHA-256、运行 `verify-package.sh`、检查包内图标与签名。

5. **网络限制按环境事实记录。**
   本机无法连接 `github.com:443`，但 `api.github.com` 与
   `github.com:22` 可用；因此 git 推送走 SSH，`gh` 走 API。该限制与
   项目代码无关，未修改仓库 remote。

## 3. 修改的文件

- `CMakeLists.txt`：删除 `file(TO_NATIVE_PATH ...)`，改用正斜杠图标路径。
- `VERSION`：`1.0.0` → `1.0.1`。
- `CHANGELOG.md`：新增 `[1.0.1] - 2026-10-22` 与 `### Verified`。
- `README.md`：当前版本、发行链接、验证结果与已知问题。
- `docs/RELEASE.md`：当前版本与示例包名。
- `docs/TESTING.md`：CI 运行号与可复用工作流验证。
- `docs/RELEASES.md`：v1.0.1 标签、提交、运行号与四个归档的 SHA-256。
- `docs/ROADMAP.md`：Phase 9 记录。
- `docs/PHASE_9_REPORT.md`：本报告。

游戏逻辑、AI、UI、存档格式与测试源码未修改。

## 4. 实际执行的测试与结果

### 4.1 本机

| 项目 | 提交 | 结果 | 状态 |
|---|---|---|---|
| Debug + GUI `ctest` | `ae50eab` | 17/17，27.67 s | PASS |
| Release `ctest` | `ae50eab` | 15/15，11.99 s | PASS |
| ASan+UBSan 无窗口 | `ae50eab` | 15/15，34.44 s | PASS |
| 重新构建 Debug | `323856f` | 构建成功 | PASS |
| `--version` | `323856f` | `Reversi 1.0.1 (commit 323856f, Debug build)` | PASS |
| `Info.plist` 版本 | `323856f` | `CFBundleShortVersionString`/`CFBundleVersion` = `1.0.1` | PASS |

版本号提升后未在本机重跑 CTest；该提交的三平台测试由 CI 执行（见 4.2）。

### 4.2 GitHub Actions

首次运行 `39633276663`（`ae50eab`）：

| 作业 | 结果 |
|---|---|
| Linux Debug / macOS Debug / Linux ASan+UBSan / Linux Xvfb GUI | success |
| Windows Debug | **failure**（`RC2135`） |
| Release package / Linux packaged smoke | skipped（依赖失败） |

修复后运行 `39633821096`（`323856f`）：

| 作业 | 结果 |
|---|---|
| Linux Debug / Windows Debug / macOS Debug | success |
| Release package / Package Linux / Windows / macOS | success |
| Linux ASan+UBSan / Linux Xvfb GUI / Linux packaged smoke | success |

9/9 `success`，同时验证了 Phase 8 提取的
`.github/workflows/package.yml`。

### 4.3 发布

发布工作流 `39634542587`（标签 `v1.0.1`）：`Package Linux`、
`Package Windows`、`Package macOS`、`Publish GitHub Release` 全部
`success`。

发行附件：

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `Reversi-1.0.1-Darwin-arm64.zip` | 2,659,900 B | `d39eef3d...8ea1595` |
| `Reversi-1.0.1-Windows-AMD64.zip` | 1,383,058 B | `5f6e834c...5d375fd` |
| `Reversi-1.0.1-Linux-x86_64.zip` | 1,762,419 B | `ab8d8d76...dcf157d4` |
| `Reversi-1.0.1-Linux-x86_64.tar.gz` | 1,745,590 B | `23ecb08a...444e7ae5` |

完整值见 [RELEASES.md](RELEASES.md)。

### 4.4 发布后独立验收（macOS）

| 检查 | 结果 | 状态 |
|---|---|---|
| 从 Release 下载归档 | 成功 | PASS |
| SHA-256 与 `SHA256SUMS` 比对 | 一致 | PASS |
| `scripts/verify-package.sh` | `PASS` | PASS |
| 包内 `Reversi.icns` | 存在（1,080,680 B） | PASS |
| `CFBundleIconFile` | `Reversi.icns` | PASS |
| `codesign --verify --deep --strict` | valid on disk / satisfies DR | PASS |
| 包内二进制 `--version` | `Reversi 1.0.1 (commit ea7aa12, Release build)` | PASS |

## 5. 兼容性与性能数据

- **存档与设置**：格式未变，1.0.0 的用户数据可直接使用。
- **版本号**：`VERSION` 是唯一来源，`--version`、窗口标题与应用包信息
  均来自该文件。
- **包体**：macOS 归档 2.66 MB（含 1.0 MB `.icns`），Windows 1.38 MB，
  Linux 1.76 MB。
- **性能**：本阶段未做性能测量，不宣称变化。

## 6. 遗留风险与技术债

1. **macOS 无 Developer ID、未公证**：Gatekeeper 仍可能拦截首次启动。
2. **Windows 未做 Authenticode 签名**：SmartScreen 可能提示未知发布者。
3. **Windows/macOS 物理桌面 GUI 未验证**：CI 覆盖构建、测试与打包，
   Linux 覆盖 Xvfb GUI；Windows GUI 仍为 NOT TESTED。
4. **Linux 物理桌面未验证**：仅 Xvfb 软件渲染。
5. **本机网络对 `github.com:443` 不通**：后续推送需继续使用 SSH 或恢复
   网络；这不是项目问题，但会影响自动化脚本。
6. **图标未经视觉评审**：默认设计可用，替换需重跑脚本并重新发布。
7. **既存测试缺口未变**：键盘导航、窗口缩放、真实听感、打包 GUI 手动
   存/读档、自动化 GUI 点击回归。

## 7. 回滚方法

本地代码回滚：

```bash
git revert ea7aa12 323856f 0c341ea
```

`v1.0.1` 是增量补丁，不回滚也不影响 1.0.0 用户数据。

远程回滚（**破坏性，需明确授权**，本次未执行）：

```bash
gh release delete v1.0.1 --yes
git push --delete origin v1.0.1
git tag -d v1.0.1
```

若只需撤回版本号，可把 `VERSION` 改回 `1.0.0` 并发布新的补丁版本；
不要复用已发布的标签。

## 8. 下一阶段依赖

- 后续开发可继续在 `main` 上进行；CI 已恢复全绿基线。
- 若要继续补测试缺口，优先补自动化 GUI 输入回归，否则键盘导航与
  窗口缩放仍只能标记 NOT TESTED。
- 若要正式对外分发，需要先解决 macOS 签名/公证与 Windows 签名。
