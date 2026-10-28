# Reversi Online 网络架构（Phase 14）

日期：2026-10-26  
版本：2.0.0（设计）

## 1. 架构审计结论

现状：

- `core/` 提供唯一的规则与对局状态机；`Match` 只接受本地落子，`app`
  通过 `match_apply_move` 驱动 UI。
- `src/platform/web/` 已有 Emscripten 适配层（存储、AI Worker）；Web 端
  可以复用同一份 C UI 与 `ai_worker` 的模式。
- 没有任何 socket、HTTP、WebSocket 或序列化网络代码；没有账号系统。
- `CMakeLists.txt` 已有桌面/Web 双构建、CTest、CI；新增服务端需要新的
  可执行目标与仅桌面构建的依赖。

结论：网络层可以作为**独立模块**加入，不修改 `core/` 的规则语义；服务端
链接 `core`，客户端只负责传输与展示。GUI 只消费网络会话状态，不包含
socket/协议逻辑。

## 2. 目标与边界

目标：两名玩家从桌面或 Web 客户端进入同一房间，完成一局规则正确、状态
一致的翻转棋；断线与异常网络不产生非法棋局或服务端崩溃。

边界：

- 服务端权威：所有落子、Pass、终局、超时与投降由服务端用 `core/rules`、
  `core/game` 判定；客户端提交的棋盘永远不被信任。
- 无账号：临时房间 + 房间代码 + 随机会话 token。
- 不部署公网、不购买云资源；本轮只做本地可控环境验证。
- TLS 不在进程内实现：本地 `ws://`；生产要求在反向代理终止 WSS。

## 3. 分层

```text
server/main.c
   └── net_server       房间/会话/事件循环/权威对局
          ├── net_protocol   文本协议解析、校验、序列化
          ├── net_ws         RFC6455 握手/帧编解码
          └── core          rules/game（唯一规则实现）

app (desktop / Web)
   └── net_client      平台传输：POSIX/Win32 socket 或 JS WebSocket
          └── net_session 客户端会话状态机（房间、座位、seq、重连）
                 └── net_protocol
   └── ui/net_view     大厅与网络对局状态展示
```

- `core/` 不依赖 `net/`；`net_server` 依赖 `core`；`net_client` 不链接
  `net_server`；UI 不直接持有 socket。
- Web 端 `net_client` 由 `src/platform/web/net_client_web.c` 实现，通过
  `EM_JS` 与页面 WebSocket 通信；桌面端用 POSIX/Win32 socket。

## 4. 传输选择与可行性

选择：**内置最小 WebSocket 服务端**（RFC 6455 文本帧），桌面客户端实现
同样的客户端握手；Web 端使用浏览器原生 `WebSocket`。

理由与成本：

- 避免新增第三方依赖与离线构建风险；服务端是单线程非阻塞事件循环，
  代码量可控（握手、帧、掩码、分片、ping/pong/close）。
- 需要自行实现 SHA-1、base64 与严格的帧长度/掩码校验，并用单元测试与
  模糊测试覆盖；这是本阶段的主要安全风险。
- TLS：进程内不实现；回环单元测试用 `ws://127.0.0.1`，本地浏览器验证由
  nginx 自签证书终止 WSS（2026-10-28 实测），生产由代理终止并转发到
  本地服务端。文档与安全审计明确这一边界。
- Windows 使用 Winsock，POSIX 使用 socket/poll；服务端与桌面客户端共享
  一层薄 socket 抽象。

## 5. 房间与会话状态机

```text
WAITING ──双方就座+ready──► PLAYING ──终局/投降/超时──► FINISHED
   │                             │
   └──── host leave/close ───────┴── 任一方离开 ──► CLOSED
```

- 房间代码：6 位大写字母/数字，排除易混淆字符；随机生成并查重。
- 座位：`black` / `white`；创建者选择颜色，加入者取另一色。
- 双方 `ready` 后自动开始；服务端发送初始状态。
- 断线：等待与对局中都保留座位 60 秒；对手收到 `opponent disconnected`。
  重连用 `rejoin token=` 恢复；旧连接尚未回收时由新连接接管座位。
  超过窗口或对局超时（120 秒/步）则结束。
- TTL：等待房间 10 分钟，加入/重连时刷新；结束房间 2 分钟。
- 计划内停机：`net_server_shutdown()` 广播 `roomclosed status=shutdown`
  并刷出队列后退出。
- 服务端重启：房间在内存中，重启后全部丢失；客户端收到
  `room not found` 并回到大厅，不崩溃。持久化不在本阶段。

## 6. 协议与一致性

- 文本协议 v1，单条消息 ≤ 2048 字节；`type` + `key=value`，严格解析。
- `hello` 必须是会话首条消息（重连也先 `hello` 再 `rejoin`）。
- 服务端状态包含 `seq`（每次接受的落子/Pass 递增）、棋盘 64 字符、
  当前回合、最后一步、Pass 标记与结果。
- 客户端落子带 `seq`；服务端只接受当前回合且 `seq` 匹配的请求。
  重复/乱序请求返回 `stale`，客户端以服务端状态为准。
- 客户端每次收到状态后用 `match_sync_network` 重建 UI 状态；超时/投降
  终局接受服务端结果，不做本地胜负判定。
- 协议版本写在 `hello` 与欢迎消息中；不兼容版本直接拒绝并提示。

## 7. 安全模型

| 风险 | 措施 |
|---|---|
| 客户端伪造棋盘/结果 | 服务端只用 `core` 重算；客户端消息不含可信棋盘 |
| 非法着法 | 服务端 `rules_is_legal_move` + 回合检查 |
| 重复/重放 | 每连接会话 token + 每步 `seq`；token 重连后轮换 |
| 消息过大/畸形 | 2048 字节上限、严格字段长度、帧长度与掩码检查 |
| 频率攻击 | 每连接令牌桶（20 msg/s，burst 40）；加入失败 8 次/连接；握手/空闲超时 |
| 资源耗尽 | 最大连接/房间/发送队列；房间 TTL；断线宽限 |
| 房间权限 | 只有两名就座玩家可 ready/move；非本房间 token 拒绝 |
| 身份伪造 | 128 位随机会话 token，只在握手响应中发送；日志不记录 |
| 日志敏感数据 | 不记录 token；房间代码可记录；错误不包含原始负载 |
| 传输窃听 | 本地 `ws://` 仅限回环；本地 WSS 由 nginx 自签终止已验证；生产 WSS 未测 |
| 客户端阻塞 | 桌面套接字非阻塞 + 1 s 有界写入；写失败即断开重连，不阻塞 UI |
| 浏览器队列溢出 | 置 `FAILED(queue_overflow)` 并关闭套接字；房间内自动重连重同步，大厅内需手动重连，不静默丢弃 |

## 8. 测试计划

- 协议：合法/非法字段、长度、版本、数字边界、棋盘编码；模糊测试随机
  字节与截断消息。
- WebSocket：握手向量、掩码、长度 126/127、分片、ping/pong/close、
  超大帧拒绝、随机帧模糊。
- 服务端集成：回环 socket 上完成正常对局、非法落子、重复/乱序、延迟、
  断线重连、双方退出、房间关闭、服务端重启、多房间并发、资源泄漏
  （ASan + 反复连接）。
- 客户端会话：seq 失效、重连恢复、错误展示。
- 桌面与 Web 构建；Web 浏览器端到端在本地服务可用时执行，否则标记
  NOT TESTED。

## 9. 本地部署

- `reversi_server --port 8080 --bind 127.0.0.1`。
- `scripts/serve-web.sh` 提供静态页面（HTTP）；浏览器也可经本地 nginx
  以 HTTPS + WSS 连接，见 `WEB_ONLINE_TESTING.md`。
- 不提供公网部署脚本、不购买资源；WSS 反向代理示例只作为文档。

## 10. 可行性结论

可行，但工作量大：核心风险是 WebSocket 帧/握手的正确性与跨平台 socket
生命周期。按阶段拆分：协议与帧 → 服务端与集成测试 → 客户端库 → UI →
Web 桥接与浏览器验证。若时间不足，优先交付服务端 + 协议 + 集成测试，
UI/浏览器未完成项必须标记 NOT TESTED，不得声称已完成。
