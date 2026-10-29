# Reversi 2.0.0 发布就绪评估（Release Candidate）

日期：2026-10-29  
源码版本：`VERSION` = 2.0.0  
最新已发布版本：v1.0.1（2026-10-22）  
状态：**Release Candidate（RC）— 未发布、未打标签、未创建 GitHub Release**

本文件回答三个问题：2.0.0 包含什么、还缺什么证据、现在能不能发布。
结论只使用 PASS / PARTIAL / FAIL / NOT TESTED。

## 1. 结论摘要

| 问题 | 结论 | 依据 |
|---|---|---|
| 是否具备 Public Beta 条件 | **PARTIAL ✅ 可以，但限“自托管、邀请式”** | 核心在线对战、三引擎浏览器、TLS/代理、优雅关闭、CI 9/9 与安装包均有证据；公网证书与真实网络未测 |
| 是否具备正式 Release 条件 | **FAIL（接近，暂不具备）** | 见 §6：桌面↔Web 未复验、物理桌面 GUI 与 Safari/iOS/Android 未验证、可信证书与公网未测 |

“可以进入 Public Beta”的含义是：源可用、可由用户自行 `ws://127.0.0.1`
或本地 TLS 代理运行；**不得**宣称“公网/生产就绪”。正式发布前必须补齐
§6 的阻碍项。

## 2. v2.0.0 功能清单

1. **Reversi Online（自托管）**：权威服务端、临时房间 + 6 位代码、
   颜色选择、双方 ready、seq 防重复、Pass、终局、120 s 每步超时、
   60 s 重连窗口、投降/离开、断线重连。
2. **桌面客户端**：命令行 `reversi_server` + 桌面应用内 Online 大厅与
   对局 HUD（对手状态、延迟、Resign/Leave）；协议 v1。
3. **Web 客户端**：Emscripten 构建，浏览器原生 WebSocket，TLS 由反向
   代理终止；与桌面共用同一 C 协议/会话层。
4. **安全控制**：消息/帧上限、令牌桶限速、连接/房间上限、握手/空闲/
   房间 TTL、128 位令牌轮换、加入失败上限、日志不记录令牌。
5. **本阶段加固**（Phase 15）：
   - 浏览器接收队列溢出不再静默丢弃：报 `FAILED(queue_overflow)` 并关闭
     套接字；房间内自动重连重同步，大厅内需手动重连；
   - 桌面客户端套接字改为非阻塞 + 有界写入（1 s），服务器卡死不会再冻结
     UI 线程；写失败即断开并重连；
   - 等待房间断线保留座位（重连窗口），不再瞬间销毁房间；
   - 令牌持有者可在旧连接仍未回收时接管座位（NAT 变化/Half-open）；
   - 服务端强制协议首条消息为 `hello`（版本协商前置）；
   - 房间 TTL 在加入/重连时刷新，修复“房间刚加入就过期”；
   - 每连接 8 次加入/重连失败后断开（房间代码枚举防护）；
   - `SIGTERM` 优雅关闭：广播 `roomclosed status=shutdown` 后退出；
   - 可配置的握手/空闲/房间 TTL（用于测试与私有部署）；
   - 浏览器 E2E 支持 Chrome/Firefox/WebKit。
6. **完整保留**：本地双人、AI 三档、Reversi Lab、实验平台、存档/设置/
   统计；存档格式 v1 与设置格式 v1 **未变**。

## 3. Release Notes 草稿

> **Reversi 2.0.0 — Online play**
>
> - Play a remote game from the desktop app or the browser: room codes,
>   colour choice, server-authoritative rules, timeouts, resign, and
>   reconnection.
> - Self-hosted: the game server binds loopback and speaks WebSocket; TLS
>   is terminated by your reverse proxy (nginx template included).
> - Hardening: bounded client writes, loud queue-overflow failure in the
>   browser, waiting-room reconnect, seat takeover after NAT changes,
>   protocol-order enforcement, join-failure limiting, graceful shutdown.
> - Saves, settings, statistics, Lab and the experiment platform are
>   unchanged and compatible with 1.x data.

中文摘要可复用 §2 与 CHANGELOG 的 `[2.0.0]` 段。

## 4. 升级说明（1.0.1 → 2.0.0）

- 存档 `REVERSI_SAVE 1`、设置 `REVERSI_SETTINGS 1`、棋谱
  `reversi-record 1` 格式均未变；`src/storage/` 自 v1.0.1 起无 diff。
  本阶段用 v1.0.1 二进制实际写出的存档/设置作为 fixture 加载验证（PASS）。
- 新增 `reversi_server` 可执行文件与 Online 菜单项；本地玩法与快捷键不变。
- 在线对战默认只绑定 `127.0.0.1`，需要自建服务器；没有账号/匹配服务。
- 协议 v1 尚未公开部署，因此 `hello` 顺序强制不构成对外兼容破坏。

## 5. 测试矩阵

环境：macOS 26.6.2 arm64，AppleClang 21 / Homebrew clang 23.1.4 (TSan)，
CMake 4.4.4，Emscripten 6.0.13，nginx 1.31.8，Chrome 155、
Playwright Firefox 157、Playwright WebKit 27.2。

| 项 | 结果 | 证据 |
|---|---|---|
| Debug 无窗口 | PASS 26/26 | 本机 `ctest`；CI `40818759364` Linux/Windows/macOS 各 26/26 |
| Release 无窗口 | PASS 26/26 | `ctest --test-dir build-release` |
| ASan+UBSan 无窗口 | PASS 26/26（无报告） | `ctest --test-dir build-sanitize -E "app.*"` |
| Debug GUI | PARTIAL | 本机无显示器（NOT TESTED）；CI Linux Xvfb 4/4 PASS（`40818759364`）；macOS/Windows 物理桌面未验证 |
| TSan `net.session` / `net.client` | PASS | Homebrew clang 23，无竞争报告 |
| 网络边界 | PASS | `net.server` 5,757 断言：握手/空闲/房间 TTL、连接（HTTP 503）/房间上限、失败限速、重连接管、优雅关闭、延迟代理整局、日志脱敏、soak 8 局 |
| 客户端队列溢出 | PASS | `net.client` 32 断言（`queue_overflow`） |
| 会话重启 | PASS | `net.session` 71 断言（重启后重连回大厅、预算耗尽 FAILED） |
| 旧版本数据 | PASS | `storage.save` 102 / `storage.settings` 68（v1.0.1 fixture）、`analysis.session` 112（v1.4.0 棋谱 fixture） |
| Emscripten 构建 | PASS | `scripts/build-web.sh`，wasm 395,038 B |
| 浏览器整局（Chrome） | PASS | `scripts/web-online-e2e.mjs`，房间 `38RSUR` / `9J25DT`（生产式代理）/ `YDU8UG`（复审后） |
| 浏览器整局（Firefox） | PASS | Playwright Firefox 157，房间 `45H8XY`、`SYTL45`（复审后） |
| 浏览器整局（WebKit） | PASS | Playwright WebKit 27.2，房间 `V3U9MB`、`JAZTR3`（复审后；**引擎代理，不等于 Safari**） |
| Web 队列溢出（三引擎） | PASS | `scripts/web-queue-overflow-check.mjs`：FAILED(7) → 手动 Connect → LOBBY(2) |
| Web 自动化套件 | PASS 9/9 | `tests/web/run-tests.js`：smoke/gameplay/persistence/responsive/移动触摸/pagehide/cancel/autoplay |
| 桌面↔Web 联机 | NOT TESTED（本阶段） | Phase 14 曾 PASS（`HVVFKP`）；需要 macOS 窗口自动化 |
| 字体渲染 / 音效 | NOT TESTED | 浏览器 smoke 只证明无控制台错误；桌面字体/音效为 Phase 14 GUI 证据，本阶段未复验 |
| Safari / iOS Safari / Android Chrome | NOT TESTED | 无 Safari WebDriver、无真机/模拟器 |
| 真实跨网络（延迟/丢包） | NOT TESTED | 只有回环 + 进程内延迟代理（单向 20 ms） |
| 可信证书链 / 公网 HSTS | NOT TESTED | 自签证书本地验证，未申请证书/未改 DNS |
| 代理层限速 | PASS（本地） | 20 次连续升级：前 11 次转发（上游 502），之后 503 |
| 代理层连接上限 | NOT TESTED | `limit_conn` 已配置、语法通过，未压测 |
| 10 分钟 ASan soak | PASS | `PASS net.server (865909 checks)`，退出码 0，无报告 |
| 安装包验收 | PASS（CI） | 三平台归档 + SHA256；Linux 包 Xvfb 验收、Windows `--version`；macOS 包未启动 |
| CI（本阶段提交） | PASS 9/9 | `40818759364`（`21c8d20`）：三平台 Debug、ASan+UBSan、Xvfb GUI、三平台打包与 Linux 包验收 |
| Windows / Linux 桌面 | NOT TESTED（本阶段） | 依赖 CI；Phase 14 时 CI PASS |

断言数（本机 Debug，全部无窗口测试合计 **1,210,317**）：
`net.protocol` 33、`net.ws` 20,026、`net.server` 5,757、`net.client` 32、
`net.session` 71、`analysis.session` 112、`storage.save` 102、
`storage.settings` 68；其余见 `TESTING.md` §2。

## 6. 发布阻碍（正式 Release 前必须关闭或明确披露）

1. **桌面↔Web 联机本阶段未复验**：Phase 14 的证据仍在（`HVVFKP`），但
   2.0.0 的客户端写入路径与重连顺序已改，需要一次桌面 + 浏览器的完整
   复验。
2. **物理桌面 GUI 未验证**：CI 的 Linux Xvfb 4/4 通过，但 macOS/Windows
   的真实窗口、HiDPI、字体与音频听感未复验（Phase 14 有 macOS 证据）。
3. **Safari、iOS Safari、Android Chrome** 未验证；Firefox/WebKit 通过，
   但 WebKit 构建不等于 Safari。
4. **真实公网**：可信证书链、HSTS 实际生效、DNS、NAT/防火墙、跨地域
   延迟与丢包均未测（本地自签与隔离代理已验证，不构成生产 PASS）。
5. **桌面端发送超时路径**只有代码审查与有界写实现，没有可复现的
   “服务器不读”自动化用例（Web 端有自动化）。
6. **服务端发送队列饱和**（16 KB）仍只有代码审查；集成测试无法在
   合理时间内填满内核发送缓冲。

## 7. 建议

- 若以 **Public Beta** 名义发布：只发布源码 + 强调自托管与“未做公网
  安全评估”；在 Release Notes 中列出 §6 的第 3–4 项。
- 正式 v2.0.0：补 §6 第 1–2 项（桌面↔Web 与物理桌面）复验；第 3–4 项
  可作为已知限制在发布说明中明确披露，由发布者决定是否接受。CI、打包、
  ASan/UBSan 与 Linux GUI 已是绿灯。
