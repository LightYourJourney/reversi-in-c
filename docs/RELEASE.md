# 安装、卸载与发布

版本号来自仓库根目录的 `VERSION` 文件，当前为 **1.0.0**。发行包由
CMake/CPack 生成，包含可执行程序、运行时资源、文档、许可证与 SHA-256
校验值。

## 1. 发行包形式

| 平台 | 文件 | 形式 |
|---|---|---|
| macOS 11+ arm64 | `Reversi-1.0.0-Darwin-arm64.zip` | `Reversi.app` 应用包 |
| Windows x64 | `Reversi-1.0.0-Windows-AMD64.zip` | 便携目录，含 `reversi.exe` |
| Linux x86_64 | `Reversi-1.0.0-Linux-x86_64.zip` / `.tar.gz` | 便携目录，含 `reversi` |

每个归档旁有 `.sha256` 文件；GitHub Release 另提供汇总的 `SHA256SUMS`。
当前 macOS 构建只做 **ad-hoc 签名**（用于保证包结构完整），**没有
Developer ID、未公证**；Windows 构建**未做 Authenticode 签名**。

## 2. macOS 安装与卸载

安装：

1. 解压 zip。
2. 把 `Reversi.app` 拖入 `/Applications`（或任意用户目录）。
3. 首次启动可右键选择“打开”。构建只有 ad-hoc 签名，可能被 Gatekeeper
   拦截；这是当前分发限制，不是程序损坏。
4. `Reversi.app/Contents/Resources/` 内含 `LICENSE`、
   `THIRD_PARTY_NOTICES.md` 与字体许可证，单独拖动应用也不会丢失声明。

卸载：

```bash
rm -rf /Applications/Reversi.app
rm -rf "$HOME/Library/Application Support/Reversi"
```

第二条命令会同时删除设置、统计和存档；只卸载程序时不要执行它。

Developer ID 签名与公证是可选流程，需要发布者自己的证书，仓库不包含
任何私钥。可在配置时传入签名身份：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DREVERSI_MACOS_CODESIGN_IDENTITY="Developer ID Application: ..."
```

签名并打包后，可用发布者自己的 notarytool 凭据提交公证：

```bash
xcrun notarytool submit Reversi-1.0.0-Darwin-arm64.zip \
  --keychain-profile <profile> --wait
xcrun stapler staple Reversi.app
```

签名后的包应在发布前用 `codesign --verify --deep --strict` 和
`spctl --assess` 复核。未提供 Developer ID 时保留 ad-hoc 签名构建，
并如实标注分发限制。

## 3. Windows 安装与卸载

安装：

1. 解压 zip 到任意目录（例如 `%LOCALAPPDATA%\Programs\Reversi`）。
2. 运行 `reversi.exe`。运行时资源在同级 `assets\` 目录中，必须一起保留。
3. 发行包使用静态 MSVC 运行库，不要求单独安装 Visual C++ Redistributable。

卸载：关闭程序后删除解压目录，并可选删除
`%APPDATA%\Reversi`（设置、统计与存档）。

当前产物未做 Authenticode 签名，SmartScreen 可能显示未知发布者警告。

## 4. Linux 安装与卸载

运行时依赖（Debian/Ubuntu 示例）：

```bash
sudo apt install libasound2 libx11-6 libxrandr2 libxi6 libgl1 \
  libglu1-mesa libxcursor1 libxinerama1 libwayland-client0 \
  libxkbcommon0
```

安装：

```bash
unzip Reversi-1.0.0-Linux-x86_64.zip
cd Reversi-1.0.0-Linux-x86_64
chmod +x reversi
./reversi
```

`reversi` 与同级 `assets/` 必须一起保留。可以从任意工作目录启动。

卸载：删除解压目录，并可选删除
`$XDG_DATA_HOME/reversi`（未设置时为 `~/.local/share/reversi`）。

## 5. 用户数据目录

程序只向操作系统用户数据目录写入设置、统计和存档：

| 平台 | 目录 |
|---|---|
| macOS | `~/Library/Application Support/Reversi` |
| Windows | `%APPDATA%\Reversi` |
| Linux | `$XDG_DATA_HOME/reversi` 或 `~/.local/share/reversi` |

程序安装目录只读使用；不把用户数据写入安装目录。

## 6. 校验下载

macOS：

```bash
shasum -a 256 -c Reversi-1.0.0-Darwin-arm64.zip.sha256
```

Linux：

```bash
sha256sum -c Reversi-1.0.0-Linux-x86_64.zip.sha256
```

Windows PowerShell：

```powershell
Get-FileHash .\Reversi-1.0.0-Windows-AMD64.zip -Algorithm SHA256
```

## 7. 从源码生成发行包

macOS / Linux：

```bash
./scripts/package.sh
./scripts/verify-package.sh build-release/package/Reversi-*.zip
```

`scripts/package.sh` 强制使用固定 raylib 6.2，执行 Release 配置、构建、
CTest、CPack，并生成 `SHA256SUMS`。`scripts/verify-package.sh` 先校验
归档 `.sha256`，在临时目录解压，从 `/` 连续启动两次，检查运行时资源、
字体加载、用户数据持久化、许可证文件，并在 Linux 上执行 `ldd` 缺失
依赖检查，不依赖源码目录。

Windows 使用同一套 CMake/CPack 规则：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cd build
cpack -C Release -B package
```

## 8. 自动发布流程

- `.github/workflows/ci.yml`：Linux/Windows/macOS 的 Debug 与 Release
  构建、CTest、Release 打包上传；Linux ASan+UBSan；Linux Xvfb GUI。
- `.github/workflows/release.yml`：推送 `v1.0.0` 标签后，三个平台分别
  构建并打包，汇总 `SHA256SUMS`，创建 GitHub Release。
- 标签必须与 `VERSION` 完全一致（`v` + 版本号），否则发布作业失败。
- `workflow_dispatch` 只构建并上传工作流产物，不会发布 Release。

发布前检查：

```bash
git tag -a v1.0.0 -m "Reversi 1.0.0"
git push origin v1.0.0
```

推送标签会触发正式发布；在确认所有平台产物与许可证无误前不要推送。
