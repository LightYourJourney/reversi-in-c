# Third-Party Notices

本项目自身以 GNU General Public License v3.0 发布，详见 `LICENSE`。

## raylib 6.2

- 项目：raylib
- 作者/版权：Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)
- 许可证：zlib
- 来源：<https://github.com/raysan5/raylib>
- 构建：CMake 优先使用系统 raylib；否则通过 FetchContent 下载固定版本
  6.2，并校验 SHA256
  `df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee`。

raylib 的许可证全文如下：

```text
Copyright (c) 2013-2026 Ramon Santamaria (@raysan5)

This software is provided "as-is", without any express or implied warranty. In no event
will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not claim that you
  wrote the original software. If you use this software in a product, an acknowledgment
  in the product documentation would be appreciated but is not required.

  2. Altered source versions must be plainly marked as such, and must not be misrepresented
  as being the original software.

  3. This notice may not be removed or altered from any source distribution.
```

分发包含 raylib 静态链接的二进制时，应同时提供本声明。raylib 源码树中的
`LICENSE` 文件随下载内容保留。

## Inter 4.1

- 项目：Inter
- 作者：Rasmus Andersson / The Inter Project Authors
- 许可证：SIL Open Font License 1.1
- 文件：`assets/fonts/Inter-Regular.ttf`、`assets/fonts/Inter-SemiBold.ttf`
- 许可证全文：`assets/fonts/OFL.txt`
- 来源：<https://github.com/rsms/inter>

字体许可证只覆盖字体文件，不影响项目代码的 GPL-3.0 授权。

## 音效与图像

落子、翻转、按钮、胜负与平局音效均由程序在运行时合成；棋盘、棋子、
阴影、按钮和提示均由代码绘制。项目没有第三方贴图、图标或音频文件。
