# WebAssembly 构建与部署

日期：2026-10-22  
版本：1.1.0  
工具链：Emscripten **6.0.13**、raylib **6.2**、CMake 3.24+

## 1. 依赖

| 组件 | 版本 / 位置 |
|---|---|
| Emscripten SDK | 6.0.13，默认 `~/emsdk`（可用 `EMSDK_DIR` 覆盖） |
| CMake | 3.24+ |
| Python 3 | 本地静态服务器 |
| Node.js | 20+，仅浏览器测试需要 |
| Chrome | 浏览器测试使用系统安装的 Chrome |

安装 Emscripten（一次性）：

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install 6.0.13
./emsdk activate 6.0.13
```

## 2. 构建

```bash
scripts/build-web.sh
```

脚本会加载 `emsdk_env.sh`、用 `emcmake` 配置 `build-web/`、构建两个目标，
并把结果组装到 `web/dist/`：

| 文件 | 说明 |
|---|---|
| `index.html` | 页面容器（Emscripten shell + 版本号注入） |
| `reversi.js` / `reversi.wasm` | 主模块（raylib + 游戏） |
| `reversi.data` | 预加载的 `assets/`（字体） |
| `ai_worker.js` / `ai_worker.wasm` | AI 搜索 Worker 模块（无 raylib） |
| `app.js` / `ai_worker_boot.js` / `style.css` | 页面逻辑与样式 |
| `icon.png` / `favicon.ico` | 图标 |
| `_headers` | 静态托管的响应头 |

可调环境变量：`EMSDK_DIR`、`BUILD_TYPE`（默认 `Release`）、`JOBS`。

### 离线 / 下载失败

构建时 raylib 通过 FetchContent 获取。若网络不可用，可复用已下载的源码：

```bash
emcmake cmake -S . -B build-web \
  -DCMAKE_BUILD_TYPE=Release \
  -DREVERSI_BUILD_TESTS=OFF \
  -DREVERSI_FORCE_FETCH_RAYLIB=ON \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build-web/_deps/raylib-src"
cmake --build build-web -j
```

### Debug 构建（浏览器测试用）

自动对局钩子只在非 `NDEBUG` 构建中存在：

```bash
emcmake cmake -S . -B build-web-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DREVERSI_BUILD_TESTS=OFF \
  -DREVERSI_FORCE_FETCH_RAYLIB=ON \
  -DFETCHCONTENT_SOURCE_DIR_RAYLIB="$PWD/build-web/_deps/raylib-src"
cmake --build build-web-debug -j
```

把 `build-web-debug/bin/` 的产物与 `web/` 的静态文件组装成另一个目录，
用不同端口提供服务即可（测试脚本通过 `REVERSI_WEB_DEBUG_URL` 指定）。

## 3. 本地预览

```bash
scripts/serve-web.sh 8080
# http://127.0.0.1:8080/
```

必须通过 HTTP 打开；直接双击 `index.html`（`file://`）无法加载 WASM。

## 4. 浏览器自动化测试

```bash
cd tests/web
npm install
cd ../..
REVERSI_WEB_URL=http://127.0.0.1:8080 \
REVERSI_WEB_DEBUG_URL=http://127.0.0.1:8081 \
node tests/web/run-tests.js
```

用例：

| 名称 | 覆盖 |
|---|---|
| `smoke` | 页面加载、画布/逻辑尺寸一致、无控制台错误 |
| `gameplay` | 真实鼠标落子、Hard AI 在 Worker 中搜索并完成 |
| `persistence` | 自动存档写入、刷新、继续游戏恢复 |
| `responsive` | 1920×1080、1366×768、768×1024、390×844、360×640 |
| `cancel` | 搜索中取消、Worker 被终止、主循环不阻塞 |
| `autoplay` | 完整对局到终局（需要 Debug 构建） |

`--case <name>` 可只跑一项。截图输出到 `tests/web/screenshots/`（已忽略）。

## 5. 部署

`web/dist/` 是完整的静态站点，可直接用于 Cloudflare Pages 或任意静态托管：

- 构建命令：`scripts/build-web.sh`
- 输出目录：`web/dist`
- 不需要 COOP/COEP（未使用 SharedArrayBuffer/pthreads）
- `_headers` 设置了 `application/wasm`、JS/CSS MIME、长期缓存（带内容哈希
  的文件名不含哈希，因此对 `.wasm`/`.js` 使用 immutable 前请确认发布
  流程会替换文件名；当前构建文件名固定，若频繁更新建议改为
  `max-age=0, must-revalidate`）

公开部署需要单独授权；本阶段只生成本地产物。

## 6. 运行时结构

- 主模块：raylib 6.2 Web 后端 + 游戏；`app_run` 使用
  `emscripten_set_main_loop_arg`，不依赖阻塞式循环。
- AI：独立 Web Worker 加载 `ai_worker_boot.js`，后者再加载
  `ai_worker.js`（同一份 `core`/`ai` 源码，无 raylib）。请求/回复是文本
  协议；取消通过终止 Worker 实现。
- 存储：`src/platform/web/storage_web.c` 用 `localStorage` 实现
  `storage.h`，键前缀 `reversi:`；存档格式与校验逻辑不变。
- 资源：`--preload-file assets@/assets`，运行时路径 `/assets/fonts/...`。
- 尺寸：Web 端关闭 `FLAG_WINDOW_HIGHDPI`；JS 按 CSS 尺寸调用
  `reversi_web_resize`，画布与 GL viewport 保持一致。

## 7. 已知限制

- Web 端以 CSS 分辨率渲染，HiDPI 屏幕上会被浏览器放大（不是原生
  backing store 渲染）。这是 raylib Web `SetWindowSize` 的 DPR 行为所致。
- `localStorage` 可能被浏览器、隐私模式或用户清理；页面已提供导出/导入。
- AI 取消是终止 Worker，不是协作式停止；未完成的搜索结果被丢弃。
- Safari、Firefox、真实 iOS/Android 设备未验证。
- 页面在极慢设备上首次加载可能超过 3 秒（本机冷启约 3.3 s，热启
  0.4–0.9 s）。
