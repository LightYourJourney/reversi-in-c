# 发行记录

记录已发布版本、对应标签与发行文件 SHA-256。发行文件可从 GitHub Release
下载；校验值同时随附件提供。

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
