# 资源目录

## 字体

| 文件 | 用途 | 来源 | SHA256 |
|---|---|---|---|
| `fonts/Inter-Regular.ttf` | 正文、说明文字 | Inter 4.1（rsms/inter） | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `fonts/Inter-SemiBold.ttf` | 标题、分数、按钮 | Inter 4.1（rsms/inter） | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `fonts/OFL.txt` | 字体许可证 | SIL Open Font License 1.1 | — |

Inter 由 Rasmus Andersson 开发，以 **SIL Open Font License 1.1** 发布，
允许随本项目一起分发；许可证全文见 `fonts/OFL.txt`。
本项目自身仍使用 GPL-3.0，字体许可证仅覆盖字体文件。

## 运行时位置

CMake 构建会把本目录复制到可执行文件旁；macOS 应用包复制到
`Reversi.app/Contents/Resources/assets`。运行时代码以可执行文件目录为
基准（raylib 的 `GetApplicationDirectory()`）解析 `assets/fonts/...`，
并在 macOS 上优先检查应用包的 Resources 目录，不依赖源码目录或调用者的
当前工作目录。

若字体文件缺失或无法加载，`ui_fonts_load()` 会记录警告并回退到 raylib
默认字体；界面保持可操作，只是字形与排版不同。该路径已通过无资源目录
启动验证。

## 授权

除字体外，当前没有贴图、图标或音效资源；棋盘、棋子、阴影与提示均由
代码程序化绘制。新增资源必须是原创作品或与 GPL-3.0 兼容的授权，并在
本文件记录来源与许可证。
