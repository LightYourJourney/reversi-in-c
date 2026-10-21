# 应用图标

| 文件 | 用途 |
|---|---|
| `reversi-1024.png` | 1024×1024 源图 |
| `Reversi.icns` | macOS 应用包图标（`Contents/Resources/`） |
| `reversi.ico` | Windows 可执行文件图标（版本资源嵌入） |

## 重新生成

```bash
scripts/make-icon.sh
```

依赖 ImageMagick 7（`magick`）与 macOS `iconutil`。图标由脚本中的
ImageMagick 图元绘制，不依赖外部素材；配色取自默认深色主题
（棋盘 `#1E4938`、边缘 `#0F281F`）。

## 许可证

图标是本项目的原创作品，随项目以 GPL-3.0 分发，不含第三方素材。

## 接入位置

- macOS：`CMakeLists.txt` 的 `MACOSX_BUNDLE_ICON_FILE` 与
  `MACOSX_PACKAGE_LOCATION "Resources"`；`cmake/Info.plist.in` 的
  `CFBundleIconFile`。
- Windows：`src/version.rc.in` 的 `IDI_ICON1`，路径由 CMake 以原生
  路径替换 `@REVERSI_ICON_ICO@`。
- Linux：便携包没有桌面集成，不引用图标。
