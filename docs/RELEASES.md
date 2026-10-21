# 发行记录

记录已发布版本、对应标签与发行文件 SHA-256。发行文件可从 GitHub Release
下载；校验值同时随附件提供。

## v1.0.1 — 2026-10-22

- 标签：`v1.0.1`
- 提交：`ea7aa12`
- Release：<https://github.com/LightYourJourney/reversi-in-c/releases/tag/v1.0.1>
- 发布工作流：`39634542587`（三平台打包与发布全部成功）

| 文件 | SHA-256 |
|---|---|
| `Reversi-1.0.1-Darwin-arm64.zip` | `d39eef3d0992307fce67e94bc6e06e46b6e5b52d0a4ee2eb09bbeb7658ea1595` |
| `Reversi-1.0.1-Windows-AMD64.zip` | `5f6e834c03fa4fe5332e46e47bb815bcf34ff3b60f599bd3942bfde9f5d375fd` |
| `Reversi-1.0.1-Linux-x86_64.zip` | `ab8d8d76d16c9e2ff4ea497a3c57bee33832e44eaf1f81b94c1d988bdcf157d4` |
| `Reversi-1.0.1-Linux-x86_64.tar.gz` | `23ecb08ac81fab40ca88dc52e662b98b3726aff7dea4631107df364c444e7ae5` |

内容：Phase 7 发布后的审查修正、应用图标（macOS `.icns` / Windows
`.ico`）与 CI 打包工作流收敛。

发布后通过 GitHub API 重新下载 macOS 归档，SHA-256 与 `SHA256SUMS`
一致，`scripts/verify-package.sh` PASS；包内 `Reversi.icns` 存在，
`codesign --verify --deep --strict` 通过（ad-hoc，无 Developer ID/公证）。

## v1.0.0 — 2026-10-21

- 标签：`v1.0.0`
- 提交：`6522c43`
- Release：<https://github.com/LightYourJourney/reversi-in-c/releases/tag/v1.0.0>
- 发布工作流：`39609234964`（打包与发布全部成功）

| 文件 | SHA-256 |
|---|---|
| `Reversi-1.0.0-Darwin-arm64.zip` | `9070f3afba04147bb4a4598d24ef3b3991f8b9e54fbd5999d9c8852cf825748c` |
| `Reversi-1.0.0-Windows-AMD64.zip` | `63cc0140fde5a2af033fd11f83afa630e7451f81d75fe56f9980a62c04f52c01` |
| `Reversi-1.0.0-Linux-x86_64.zip` | `53ab7d2a069550b52e4be2959f61fff3278594a6acaee4ed40957baf3d35840b` |
| `Reversi-1.0.0-Linux-x86_64.tar.gz` | `1bf39a61e735d9e348cc5b892017f92a856d2ab9be3d5b16e18f1e35f14a6e5e` |

发布后已重新下载全部附件，`sha256sum -c SHA256SUMS` 通过；macOS 应用包
`codesign --verify --deep --strict` 通过（ad-hoc，无 Developer ID/公证）。

发布后代码审查修正（归档校验、双启动验收、包内许可证、固定 raylib 6.2、
Windows 版本断言等）已在 `main`，但**不包含在此版本附件中**。要让修正
进入发行包，需要新的补丁版本标签。
