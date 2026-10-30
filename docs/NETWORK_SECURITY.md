# Reversi Online 安全审计（Phase 14，Phase 15 加固）

日期：2026-10-26（Phase 15 更新于 2026-10-29）  
范围：本地开发实现；**未部署公网、未购买云资源**

## 1. 信任边界

- 服务端权威：棋盘、回合、合法着法、Pass、终局与超时全部由 `core` 重算。
  客户端消息只包含意图（落子坐标、seq、房间代码、令牌），不包含可信结果。
- 客户端不自行判定胜负；收到 `state` 后用服务端棋盘重建 UI。
- 无账号系统；房间是临时资源，关闭即失效。

## 2. 控制措施

| 风险 | 实现 |
|---|---|
| 非法着法/越权落子 | 回合、颜色、`rules_is_legal_move`、`seq` 四重检查 |
| 重复/重放 | 每步 `seq`；令牌仅用于座位恢复，重连后轮换 |
| 消息畸形/过大 | WS 帧长度 ≤2048；严格字段、板面 64 字符与坐标校验 |
| 帧层攻击 | 校验 RSV、opcode、掩码方向、扩展长度、控制帧长度；分片重组上限 |
| 频率滥用 | 每连接令牌桶：20 msg/s，burst 40；超限关闭 |
| 房间代码枚举 | 同一连接累计 8 次 `join`/`rejoin` 失败 → `rate_limited` 并断开；生产另加代理层 `limit_req` |
| 资源耗尽 | 48 连接（超限回 HTTP 503）/ 32 房间上限；握手 5 s、空闲 120 s、等待房间 10 min（加入/重连刷新）；发送队列 16 KB |
| 协议顺序 | 会话首条消息必须是 `hello`；其余顺序错误不改变任何状态 |
| 房间权限 | 只有就座玩家可 ready/move；非本房间令牌拒绝 |
| 身份伪造 | 128 位 OS 随机令牌；房间代码随机查重；不记录令牌 |
| 服务端崩溃 | 单线程非阻塞事件循环；所有偏移/长度检查；ASan 回归 |
| 日志泄露 | 只记录房间代码/座位/结果，不记录令牌或完整负载；`test_logs_redacted` 捕获 stderr 并断言 |
| 客户端卡死 | 桌面套接字非阻塞 + 有界写入（1 s）；服务器不读数据时断开并重连，不阻塞 UI 线程 |
| Web 状态静默漂移 | 接收队列（32）溢出不再静默丢弃：置 `FAILED(queue_overflow)`、关闭套接字；房间内自动重连重同步，大厅内需手动重连 |
| 计划内停机 | `SIGTERM` 广播 `roomclosed status=shutdown` 并刷出队列后才退出 |

## 3. TLS/WSS

- 进程内**不实现 TLS**。本地测试只绑定 `127.0.0.1`，使用 `ws://`。
- 生产必须在反向代理（nginx/Caddy）终止 TLS，并把 `wss://` 转发到本地
  回环端口。
- **本地实测（2026-10-28）**：nginx 自签证书（CN/SAN `localhost`）终止
  TLSv1.3，静态页 `https://localhost:8443`、WSS 代理 `8444 → 8080`；
  `openssl s_client` 手工升级与两个 HeadlessChrome 客户端均收到
  `101 Switching Protocols`，并完成一局。见 `WEB_ONLINE_TESTING.md`。
- 生产证书链、HSTS、代理超时、公网握手仍 **NOT TESTED**。
- 不提供公网部署脚本；`reversi_server` 默认只绑定回环地址。

## 4. 已知限制与 NOT TESTED

1. **WSS/TLS 仅本地实测**：进程内不实现，由本地 nginx 终止；生产证书
   链、HSTS 预加载与公网握手未测试。
2. **公网 DoS/模糊流量压力**未测试；仅有本地随机帧模糊、连接上限与
   代理层限速（本地 503 实测）。
3. **房间代码暴力枚举**：代码空间约 32^6；应用层每连接 8 次失败即断开，
   但攻击者可用多 IP 重连。公网部署必须叠加代理层 `limit_req`，并视情况
   在边缘做 IP 信誉/挑战。
4. **崩溃持久化**：服务端崩溃后房间与令牌全部丢失；没有磁盘会话存储。
5. **日志**写到 stderr，由 systemd/journald 收集；未实现应用内轮转（部署
   层轮转见 `docs/PRODUCTION_DEPLOYMENT.md`）。
6. **无账号**：无法跨设备找回房间；令牌只在客户端内存中。
7. **发送队列饱和**：服务端 16 KB 发送队列溢出会关闭该连接（座位保留
   重连）。macOS 无法在合理时间内填满内核缓冲（自动调优到 326 KB），
   该用例在 Linux/Windows 的 CI 上执行。
8. **桌面端发送**：2026-10-30 起改为 16 KiB 有界异步队列 + 断开重连，
   主线程不再等待网络可写；“服务器不读”场景有自动化用例
   （`tests/test_net_client.c` 的 `test_send_to_stalled_server`）。
9. **桌面端没有 TLS**：原生客户端只有明文 `ws://`，见 §5。
10. **连接上限与代理层 `limit_conn`** 未做并发压测。

## 5. 桌面端 WSS：现状、设计与限制

**现状（2026-10-30 核实）**：`src/net/net_client.c` 只实现明文 WebSocket，
工程内没有任何 TLS 依赖（无 OpenSSL/mbedTLS/SChannel/Secure Transport），
握手是手写的 `GET ... Upgrade` 加 `net_ws_parse_handshake`。

**因此加入的硬性限制**：`client_thread` 在 `getaddrinfo` 之后校验目标地址，
只允许回环、10/8、172.16/12、192.168/16、169.254/16 与 100.64/10；其余地址
在**打开套接字之前**即以 `tls_required` 失败。这样桌面端不会把协议明文送
到公网，也不会“先试明文、失败再想办法”。公网联机在 WSS 落地前不提供。

**WSS 传输的设计要求**（下一阶段实施，本轮未实现）：

1. 证书链校验到系统信任库；**主机名校验必须用用户输入的 `host`**
   （SNI 同名），不接受仅 DNS SAN 之外的 CN 回退。
2. TLS 1.2+，禁用重协商；不做任何“校验失败继续”的开关。
3. 与现有非阻塞模型对齐：`SSL_ERROR_WANT_READ/WRITE` 映射到已有的
   `socket_wait_readable/writable` 与 5 s 连接预算；握手期间可被取消。
4. 发送仍走 §8 的异步队列：`SSL_write` 的部分写按偏移续写，语义不变。
5. 断线重连沿用会话层（2 s 间隔、5 次预算、`rejoin` 令牌），TLS 失败
   归类为 `tls_handshake_failed` / `tls_certificate_invalid`。

**候选实现**：OpenSSL 3.x（三平台同一套代码与行为，需打包动态库或静态
链接并履行许可证）；平台 API（Secure Transport / SChannel / OpenSSL）能
避免额外依赖，但要写三份校验逻辑，风险与测试面更大。建议选 OpenSSL，
并在 `THIRD_PARTY_NOTICES.md` 里登记。

**必须覆盖的测试**（缺一不可）：

| 场景 | 期望 |
|---|---|
| 有效证书（本地 CA，正确主机名） | 连接成功，完成一局 |
| 自签/不受信证书 | 拒绝，`tls_certificate_invalid`，不发送任何应用数据 |
| 主机名不匹配 | 拒绝，`tls_certificate_invalid` |
| 证书过期 | 拒绝 |
| 握手中途被断开 | 有界失败，可重连 |
| 连接建立后 TLS 中断 | 进入 DISCONNECTED，重连并 `rejoin` |
| 服务器重启 | 重连 + 重新同步（现有 `net.session` 用例扩展到 WSS） |

**在这些测试通过之前，桌面端公网联机一律标记为 NOT SUPPORTED。**
