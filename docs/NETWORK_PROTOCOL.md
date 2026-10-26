# Reversi Online 协议 v1

日期：2026-10-26  
传输：WebSocket 文本帧（RFC 6455），单条消息 ≤ 2048 字节  
编码：ASCII；一行一条消息，字段以空格分隔，`key=value`

## 1. 版本与方向

- 客户端首条消息 `hello v=1`；服务端 `welcome` 也带 `v=1`。
- 版本不一致由服务端拒绝；客户端必须检查欢迎版本。
- 客户端 → 服务端：`hello create join ready move resign ping rejoin leave`
- 服务端 → 客户端：`welcome room start state error opponent gameover roomclosed pong`
- 未知键、重复键、缺失必需字段、方向不符一律拒绝。

## 2. 字段

| 字段 | 值 |
|---|---|
| `v` | 协议版本，当前 `1` |
| `code` | 6 位大写字母/数字（排除易混淆字符） |
| `token` | 32 位小写十六进制会话令牌 |
| `color` / `turn` | `B` 黑 / `W` 白 |
| `seq` | 状态序号，0 开始，每次接受的落子/Pass 递增 |
| `result` | `O` 进行中 / `B` 黑胜 / `W` 白胜 / `D` 平局 |
| `pass` | `0`/`1`，上一步后对手被自动跳过 |
| `cell` | 客户端落子坐标，如 `d3` |
| `last` | 服务端状态中的最后一步，`-` 表示无 |
| `board` | 64 字符，行主序，`0` 空 / `1` 黑 / `2` 白 |
| `player` | 座位 `1`/`2`（1 为房主） |
| `host` / `ready` | `0`/`1` |
| `status` | `waiting`/`playing`/`finished`/`connected`/`disconnected`/`reconnected`/`normal`/`resign`/`timeout`/`left`/`expired`/`abandoned` |
| `error` | 错误码（见 §6） |

## 3. 大厅与房间

```text
C: hello v=1
C: create color=B
S: welcome v=1 token=<32hex> player=1
S: room code=AB12CD color=B player=1 host=1 ready=0 status=waiting
C: join code=AB12CD color=W
S: welcome ... player=2
S: room ... player=2 host=0
S: room ... (广播给双方)
C: ready
S: room ... ready=1
（双方 ready 后）
S: start turn=B seq=0 board=<64>
```

- 创建者选颜色；加入者必须选另一色，否则 `color_taken`。
- 双方就座且都 ready 才自动开始；房主离开等待中的房间会关闭房间。
- 房间代码随机生成并查重；房间 TTL 10 分钟（等待）/2 分钟（结束）。

## 4. 对局状态

```text
C: move cell=d3 seq=0
S: state seq=1 turn=W board=<64> last=d3 pass=0 result=O
```

- 服务端验证：房间进行中、发送者颜色等于当前回合、`seq` 等于当前序号、
  格点合法；任一失败返回错误且不改变状态。
- 重复/乱序请求返回 `stale_state`（或 `not_your_turn`）；客户端必须以
  服务端 `state` 为准，不做本地胜负判定。
- 落子导致对手无着法时，`state.pass=1`；终局时 `state.result` 非 `O`，
  随后发送 `gameover`。
- 每步 120 秒超时；超时方判负。

## 5. 断线、重连与结束

```text
S: opponent status=disconnected
C: rejoin token=<旧token>
S: welcome v=1 token=<新token> player=1
S: room ...
S: state ...
S: opponent status=reconnected   (发给对手)
```

- 座位保留 60 秒；期间对手看到 `disconnected`。
- 重连成功令牌轮换；旧令牌失效。
- 超时未重连：对手在线则判对手胜（`status=disconnect`）；双方都离线则
  关闭房间（`roomclosed status=abandoned`）。
- 投降：`resign` → `gameover status=resign`，对方胜。
- 离开：等待/结束房间直接关闭；进行中离开按弃权处理。
- 服务端重启：房间在内存中，全部丢失；旧代码 `room_not_found`。

## 6. 错误码

`bad_message`、`message_too_large`、`rate_limited`、`not_in_room`、
`already_in_room`、`not_playing`、`not_ready_state`、`not_your_turn`、`stale_state`、
`illegal_move`、`room_not_found`、`room_full`、`room_in_progress`、
`color_taken`、`rejoin_failed`、`server_full`、`server_busy`、
`unexpected_message`。

客户端对未知错误只展示文本，不改变本地棋局；任何状态以服务端 `state`
为准。

## 7. 示例：完整流程

```text
C: hello v=1
C: create color=B
S: welcome v=1 token=... player=1
S: room code=K7M2QP color=B player=1 host=1 ready=0 status=waiting
C: join code=K7M2QP color=W
S: welcome v=1 token=... player=2
S: room code=K7M2QP color=W player=2 host=0 ready=0 status=waiting
C: ready            (双方)
S: start turn=B seq=0 board=...
C: move cell=d3 seq=0
S: state seq=1 turn=W board=... last=d3 pass=0 result=O
...
S: state seq=60 turn=B board=... last=h4 pass=0 result=B
S: gameover result=B status=normal
```

## 8. 兼容策略

- 协议版本独立于游戏版本；不兼容版本拒绝连接，不尝试猜测字段。
- v1 只保证与 v1 客户端/服务端互通；新增字段必须升版本。
- WebSocket 子协议、TLS 终止与反向代理不属于应用层协议。
