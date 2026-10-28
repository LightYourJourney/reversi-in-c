# Phase 14 报告：Reversi Online 在线对战

日期：2026-10-26  
版本：**2.0.0**（源码）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
AppleClang 21，CMake 4.4.4，Emscripten 6.0.13  
状态标记：PASS / FAIL / NOT TESTED / PARTIAL

## 1. 完成矩阵

| 任务 | 交付 | 状态 |
|---|---|---|
| 1. 网络架构 | `net_protocol` / `net_ws` / `net_server` / `net_client` / `net_session`；服务端权威；复用 `core` | PASS |
| 2. 房间系统 | 创建/加入/代码分享/颜色/ready/开始/房主离开/退出/关闭；明确生命周期 | PASS |
| 3. 网络对局 | 落子请求、合法性、状态同步、回合、Pass、终局、120 s 超时、断线、重连、seq 防重复 | PASS（协议/客户端层） |
| 4. 安全设计 | 输入校验、大小/频率限制、权限、令牌、重放防护、资源上限、日志脱敏；TLS 由代理终止 | PARTIAL（本地 nginx 终止 TLS 已实测；生产证书/HSTS 未测） |
| 5. 联网体验 | 对手状态、延迟、掉线/重连提示、投降、终局；复盘未接入在线流程 | PARTIAL |
| 6. 跨平台 | 桌面与 Web 构建；本地双人/AI 未改；协议 v1 | PASS（桌面↔Web 与 Chrome↔Chrome 本地完成一局；验收范围为桌面 + Chrome Web，Safari/Firefox 未测） |
| 7. 测试 | 正常对局、非法、重复、断线重连、离开、多房间、服务端重启、模糊、ASan、浏览器 E2E | PARTIAL（真实网络延迟 **NOT TESTED**） |

## 2. 架构与审计

- 先完成 `docs/NETWORK_ARCHITECTURE.md`：网络层独立于 UI；服务端链接
  `core/rules`、`core/game`，是唯一规则判定方；客户端只发意图。
- 传输选择内置最小 WebSocket（RFC 6455），无新增第三方依赖；桌面客户端
  用同一帧/握手实现，Web 用浏览器 `WebSocket`。
- 无账号；临时房间 + 6 位代码 + 128 位令牌；默认只绑定 `127.0.0.1`。
- 服务端单线程 `select` 事件循环；客户端桌面为 worker 线程 + 消息队列，
  Web 为 JS 回调队列；UI 只读 `NetSession`，不持有 socket。

## 3. 文件

新增：

- `src/net/net_protocol.[ch]`、`net_ws.[ch]`、`net_server.[ch]`、
  `net_client.[ch]`、`net_session.[ch]`、`src/platform/web/net_client_web.c`
- `server/main.c`（`reversi_server`）
- `src/ui/net_view.[ch]`（大厅）；`app.c` 增加 `APP_SCREEN_NET` 与网络对局路径
- `tests/test_net_protocol.c`、`test_net_ws.c`、`test_net_server.c`、
  `test_net_client.c`、`test_net_session.c`
- `docs/NETWORK_ARCHITECTURE.md`、`NETWORK_PROTOCOL.md`、
  `NETWORK_SECURITY.md`、`WEB_ONLINE_TESTING.md`、本报告
- `scripts/web-online-e2e.mjs`（本地浏览器双客户端验证脚本）
- Web 自动化只读访问器：`reversi_web_net_code` / `reversi_web_net_phase`
  （`web/app.js` 的 `__reversiTest.netCode/netPhase`）

修改：菜单 9 项、CMake 目标、`VERSION`/`CHANGELOG`/README/ROADMAP/
ARCHITECTURE/TESTING。

## 4. 测试

| 配置 | 结果 |
|---|---|
| Debug 无窗口 | 26/26 PASS |
| Release 无窗口 | 26/26 PASS |
| ASan+UBSan 无窗口 | 26/26 PASS |
| Debug + GUI | 30/30 PASS（含 `app.net` 大厅渲染） |
| GitHub Actions `40679842862` | 9/9 作业 PASS：Linux/Windows（MSVC）/macOS Debug 各 26/26；ASan+UBSan 无窗口 26/26、Xvfb GUI 4/4；Linux Xvfb GUI 4/4；三平台打包与 Linux 包验收 |
| `net.protocol` | 33 次断言：字段/版本/方向/棋盘/错误/终局快照 |
| `net.ws` | 20,026 次断言：握手、掩码、长度、分片、ping、20k 随机帧 |
| `net.server` | 713 次断言：完整对局、非法、重复、重连、离开、多房间、重启、双向落子超时、限速与关闭、重连到期与旧令牌、满房、终局重连、模糊 |
| `net.client` | 26 次断言：连接、创建/加入/ready/落子、断线重连 |
| `net.session` | 37 次断言：会话相位、颜色、seq、回合、落子、投降终局、终局后房间关闭 |
| Emscripten Release | 构建通过（主 wasm 394,922 B） |
| 浏览器在线双客户端 | PASS：两个独立 Chromium 上下文经 HTTPS/WSS 建房/加入/落子/投降完成一局（脚本 `scripts/web-online-e2e.mjs`；房间 `MJXHDX`，双方 seq 1、GAME、PLAYING、终局 BLACK_WIN） |
| 真实网络延迟/丢包 | **NOT TESTED**；只在回环与本地代理上测试 |
| 本地 TLS/WSS（自签） | PASS：nginx TLSv1.3 终止，静态 HTTPS + WSS 代理；`s_client` 与浏览器均收到 `101` |
| 公网/TLS 证书链/HSTS | **NOT TESTED**；未部署、未购买资源 |

首次跨平台 CI（`40298668533`、`40299103857`）暴露并修正了三处问题：
测试在 Windows 缺少 `windows.h`；`net.session` 只等本方 `seq` 造成 Linux
竞态；Windows 上 `shutdown` 返回 0 但不唤醒阻塞的 `recv`，导致客户端线程
`join` 挂起（读取循环改为先 `select` 再 `recv`，握手等待同样加超时）。

## 5. 关键证据

- `net.server` 中两名独立 TCP 客户端完成整局：每步校验服务端棋盘与本地
  重放一致，终局结果一致。
- 重复/乱序：旧 `seq` 被拒绝；非法落子与越权回合返回错误且状态不变。
- 落子超时：黑方（座位 0）与白方分别不走棋时均判超时方的对手获胜，
  `status=timeout`；超时/投降终局由客户端接受服务端结果并进入 GAME_OVER。
- 断线重连：座位保留、令牌轮换、对手收到 disconnected/reconnected。
- 服务端重启：旧房间不存在，新客户端收到 `room_not_found`。
- 模糊：20k 随机帧不崩溃；ASan 无报告。
- 限速：突发 45 条消息触发 `rate_limited`，错误送达后连接关闭。
- 重连超时：座位保留窗口到期后对手获胜（`status=disconnect`），旧令牌重连
  被拒；重连到已结束房间收到 `finished` 快照与真实结束原因。
- 满房：第三名玩家加入双人等待房间收到 `room_full`。
- 浏览器 E2E：两个隔离 Chromium 上下文经 `https://localhost:8443` 加载页面、
  `wss://localhost:8444` 连接；建房/加入/双方 ready/黑方 d3/白方投降，
  双方相位 LOBBY→WAITING→PLAYING→FINISHED，落子后双方 `seq=1`，终局双方
  `BLACK_WIN`。可复跑脚本见 `docs/WEB_ONLINE_TESTING.md`（未进 CI）。
- 桌面↔Web：桌面应用以 `ws://127.0.0.1:8080` 建房（黑），浏览器经 WSS
  加入（白）；桌面 d3 后浏览器 `seq=1`，浏览器投降后桌面显示
  `Game over: Black wins (resign)`，浏览器 `BLACK_WIN`。成功运行房间
  `7SUYW3`（流程）与 `2N9BLT`（延迟关闭浏览器以抓桌面终局）。桌面端为
  主机脚本/窗口自动化，未进 CI。
- TLS：nginx 自签证书 `CN=localhost`，TLSv1.3；`openssl s_client` 手工升级
  收到 `HTTP/1.1 101`；access log 记录两次 HeadlessChrome 的 WSS 升级。
- 浏览器验证同时修复：在线会话此前只在转场期间轮询，稳定态大厅/对局不更新；
  终局 HUD 结果与原因缺少分隔符；Web 壳页仍写着“无网络同步”。

## 6. 已知限制

1. **真实网络未测**：浏览器双客户端已在本地 HTTPS/WSS 完成一局，但延迟/
   丢包/乱序只在回环与本地代理上模拟；Safari/Firefox 未测。
2. **TLS/WSS**：进程内不实现；本地 nginx 终止 TLS 已实测（自签、浏览器
   `ignoreHTTPSErrors`）；生产证书链、HSTS、代理超时未验证。
3. **复盘**：在线终局可查看棋盘，但未把网络对局导入 Reversi Lab 复盘。
4. **账号/排位/Elo/观战**：明确不属于本阶段。
5. **房间持久化**：服务端重启丢失全部房间；无磁盘会话。
6. **日志**：stderr 文本；无轮转/集中脱敏。
7. **UI 交互自动化**：大厅与网络对局路径没有点击回归；`app.net` 只验证
   渲染。网络对局 UI 的输入门控来自代码审查。
8. **桌面线程客户端**：重连由会话层驱动；极端时序（同时断线与服务端
   重启）未做长时间压力测试。

## 7. 验收判断

- 规则正确、状态一致的完整对局：**PASS（两名独立 C 客户端）**。
- 浏览器 ↔ 浏览器（本地 HTTPS/WSS）完成一局：**PASS**。
- “从不同客户端（桌面 + Web 浏览器）”完成一局：**PASS**（桌面 `ws://`、
  浏览器 `wss://`，同一服务端；房间 `7SUYW3`/`2N9BLT`）。
- 断线/异常网络不导致非法棋局或服务端崩溃：**PASS（回环与模糊测试）**；
  真实网络 **NOT TESTED**。

因此本阶段的核心验收（权威服务端、房间、桌面↔Web 联机、本地 TLS）
已满足；仍为 **PARTIAL** 的仅是真实网络/生产证书与 Lab 复盘；
Safari/Firefox 未测，但不在本阶段验收范围。

## 8. 回滚

```bash
git revert <phase-14-commit>
```

局部回滚：删除 `src/net/`、`src/platform/web/net_client_web.c`、`server/`、
`src/ui/net_view.*`、网络测试与文档；移除菜单 Online 项与
`APP_SCREEN_NET`；`VERSION` 回到 `1.4.0`。核心规则、AI、Lab、实验平台
不受影响。

## 9. 下一步

1. 补浏览器端与桌面端的断线重连自动化用例。
2. 在局域网做延迟/丢包注入，补充真实网络证据。
3. 验证可信证书链、HSTS、生产反向代理与 Safari/Firefox。
4. 在线终局导出棋谱到 Reversi Lab 复盘。
