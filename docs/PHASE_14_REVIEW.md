# Phase 14 独立验收与代码质量审计：Reversi Online

日期：2026-10-27  
审查范围：Phase 14（在线对战）全部改动  
审计前 HEAD：`dfe2d33`；基线：`be5b90d`（Phase 14 起点）  
审计环境：macOS 26.6.2 arm64，Apple clang 21.0.0（工程构建），
Homebrew clang 23.1.4（TSan），CMake 4.4.4，Emscripten 6.0.13  
状态标记：PASS / FAIL / PARTIAL / NOT TESTED

本审计不采信 `docs/PHASE_14_REPORT.md` 的既有结论，重新阅读需求、diff、
源码、测试与架构文档，并重跑测试。

## 1. 审查范围

- 需求与设计：用户 Phase 14 任务（网络层、权威服务端、房间、重连、安全、
  大厅、桌面/Web、版本化协议、测试与报告）；`NETWORK_ARCHITECTURE.md`、
  `NETWORK_PROTOCOL.md`、`NETWORK_SECURITY.md`、`ARCHITECTURE.md`、
  `TESTING.md`。
- 代码：`git diff be5b90d...dfe2d33`（38 个文件，+6544/−36，审计前）；
  `src/net/*`、`server/main.c`、`src/platform/web/net_client_web.c`、
  `src/ui/net_view.*`、`src/app/app.c` 网络路径、`src/core/match.*`、
  `CMakeLists.txt`、`.github/workflows/ci.yml`。
- 测试：`tests/test_net_{protocol,ws,server,client,session}.c`、
  `tests/test_match.c`、`app.net` 冒烟。
- 不审查：Phase 1–13 既有规则/AI/Lab/实验平台实现（仅做回归）。

## 2. 发现的问题（按严重程度）

### 高 0（浏览器 E2E 追加发现）：在线会话只在转场期间轮询

`net_session_update` 原先只从 `app_update_passive` 调用，而该函数只在页面
转场时执行。进入大厅或对局稳定态后，桌面/Web 客户端不再轮询 socket，
状态回调与消息永远不处理，界面停留在 “Offline”；浏览器实测中 WSS 已
升级（nginx `101`）但 `NetSession` 相位始终为 IDLE。该缺陷使 GUI 在线
流程实际不可用，此前的冒烟/单元测试均未覆盖。

### 高 1：客户端拒绝服务端的权威终局快照

`net_protocol_state_from_board` 调用 `game_is_consistent`，而超时/投降终局
棋盘未满且仍有合法着法，必然判为“不一致”。服务端 `room_finish` 先广播
`state` 再发 `gameover`；客户端静默丢弃该 `state`。重连到已结束房间时
（此前没有棋盘）`NetSession.game` 保持全零，`app_build_net_match` 同步
失败，但 `APP_SCREEN_NET` 仍切换到对局页，留下无效/陈旧棋盘。违反
`NETWORK_PROTOCOL.md` “客户端必须以服务端 state 为准”。

### 高 2：服务端发送失败可递归关闭房间并丢失终局消息

`connection_schedule_close` 会立即 detach/close。`room_finish` 先把
`playing` 置 false，再广播 `state`；若某连接发送队列溢出，`send_message`
同步关闭该连接 → `detach_player` 见非对局状态 → `room_close` 重置房间，
随后 `room_finish` 继续对已重置房间发 `gameover`，对手只收到
`roomclosed`。同类重入也存在于 `room_close` 广播期间。另一个副作用：
`rate_limited` 等错误先入队、随即关 socket，**错误消息从未送达**。

### 中 3：同步失败仍进入在线对局

`app_build_net_match` 无返回值，调用方无条件 `net_mode = true` 并切屏，
掩盖状态同步失败。

### 中 4：重连已结束房间时结束原因失真

`send_room_snapshot` 固定发送 `gameover status=normal`，即使房间因
`timeout`/`resign`/`disconnect` 结束。

### 中 5：安全与生命周期测试缺口被报告掩盖

报告声称“输入校验、大小/频率限制、权限、重连”等已实现且测试覆盖；
实际没有限速、重连到期、满房、终局重连的测试，`message_too_large`
分支因 WS 帧上限等于消息上限而不可达。

### 低

- Web 客户端把 `NetClientStatus` 序号 `2/3/4` 硬编码进 JS，枚举重排会
  静默破坏浏览器构建。
- 服务端 `socket_read` 把 `EINTR` 当作有序关闭。
- `now_ms()`、`GameResult`→字符串三元式、`test_sleep_ms` 重复；CMake 五个
  网络测试块未复用辅助函数（可维护性，非缺陷）。
- `net_protocol` 存在 `message_too_large` 死分支（见中 5）。

## 3. 修复的内容

0. **每帧轮询会话**（`src/app/app.c`）：`net_session_update` 移到
   `app_update` 顶部，每帧一次；浏览器 E2E 验证 LOBBY→WAITING→PLAYING→
   FINISHED 全流程。
1. **终局快照**（`src/net/net_protocol.c`）：`result != O` 时只校验棋盘
   合法性、回合与结果枚举，由服务端权威决定胜负；`ongoing` 仍要求本地
   一致性。`app_build_net_match` 返回 `bool`，仅在同步成功时进入对局。
   （`GAMEOVER` 应用结果并标记脏状态在此前提交 `83cbbf7` 已完成。）
2. **延迟关闭**（`src/net/net_server.c`）：新增 `closing` 标志与
   `connection_close_pending`，在 poll 末尾统一关闭；正常可写时先刷新
   排队输出，使 `rate_limited` 等错误送达（socket 持续不可写时仍可能
   丢弃，见 §5）；广播期间的发送失败不再重入房间。
3. **结束原因**：`ServerRoom.finish_status` 记录真实原因，重连快照回放
   正确的 `status`。
4. **Web 状态序号**：`_Static_assert` 锁定 `NetClientStatus` 序号。
5. **EINTR**：服务端读取将 `EINTR` 视为暂时无数据。
6. **测试**：新增终局快照协议用例；限速（45 条突发 → `rate_limited`
   先送达）、重连到期判负、第三名玩家 `room_full`、重连进已结束房间
   （`finished` + 真实 `resign` 原因）用例。计数：`net.protocol` 28→33，
   `net.server` 663→713（另含重连到期后用旧令牌重连被拒、限速后连接关闭）。

未做的重构：重复的 `now_ms`/三元式/测试 sleep、CMake 测试块合并、
`NetMessage` 数据团块。均记录为可维护性事项，不改变行为。

## 4. 实际执行的测试

| 配置 | 命令 | 结果 |
|---|---|---|
| Debug 无窗口 | `ctest --test-dir build` | 26/26 PASS |
| Release 无窗口 | `ctest --test-dir build-release` | 26/26 PASS |
| ASan+UBSan | `ctest --test-dir build-sanitize -E "app.smoke\|app.autoplay\|app.lab\|app.net"` | 26/26 PASS，无 Sanitizer 报告 |
| Debug + GUI | `ctest --test-dir build-gui` | 30/30 PASS（含 `app.net`） |
| 网络压力 | `reversi_net_{server,session,client}_tests` 各 40 次 | 120/120 PASS |
| TSan（会话） | `clang -fsanitize=thread ... test_net_session.c` | PASS，无数据竞争报告 |
| TSan（客户端） | 同上，`test_net_client.c` ×5 | PASS |
| Web | `cmake --build build-web` | PASS（`reversi.wasm` 394,629 B） |
| GitHub Actions | 运行 `40479705658`（提交 `37d219d`） | 9/9 作业 PASS：Linux/Windows/macOS Debug 26/26；ASan+UBSan 26/26 与 Xvfb GUI 4/4；三平台打包与 Linux 包验收 |

断言计数（本机实跑）：`core.match` 1011、`net.protocol` 33、
`net.ws` 20,026、`net.server` 713、`net.client` 26、`net.session` 32。

关键证据：

- 回滚超时判负到旧逻辑，新增用例失败（2 项），恢复后 PASS——测试非假阳性。
- 限速用例验证错误在连接关闭前送达，并断言随后连接被关闭（延迟关闭
  修复的直接证据；仅覆盖 socket 可写的正常路径）。
- 重连进已结束房间收到 `room status=finished`、终局 `state` 与
  `gameover status=resign`。

回归：

- 旧功能：全部既有测试通过；本地双人/AI/Lab/实验路径未改（GUI 30/30，
  实验回归 `experiment.regression` 通过）。
- 旧存档：`REVERSI_SAVE_FORMAT_VERSION` 仍为 1，Phase 14 diff 未触及
  `src/storage/`；同版本往返测试通过。**跨版本旧存档 fixture 未实测**
  ——NOT TESTED。
- 桌面/Web：桌面三平台 CI 通过（见 CI 记录）；Web 构建通过；本地
  Web↔Web 与桌面↔Web 联机 PASS（房间 `7SUYW3`/`2N9BLT`）。
- Debug/Release：两者均跑全部无窗口测试；网络代码无 `NDEBUG` 分支。

性能：

- 无新增性能/强度声明。客户端网络在工作线程/JS 回调，UI 每帧只做
  `net_session_update`（轮询队列）与状态变化时的棋盘重建；服务端单线程
  `select` + 48 连接上限。**未做延迟/吞吐/CPU 基准** —— NOT TESTED。

## 5. 仍未解决的风险

1. **浏览器/桌面覆盖有限**：本地 Headless Chrome 双客户端与桌面↔Web
   各完成一局；桌面↔Web 为主机自动化（依赖 macOS 窗口/事件注入），未进
   CI。Safari/Firefox、移动端、真实网络、可信证书链与生产代理配置仍
   NOT TESTED。
2. **TLS 仅本地实测**：nginx 自签证书 + `ignoreHTTPSErrors`；HSTS、
   证书轮换、代理超时与公网握手 NOT TESTED。
3. **真实网络延迟/丢包/乱序 NOT TESTED**：只在回环。
4. **公网部署/DoS/NAT/防火墙**：未部署、未测试（按约束禁止）。
5. **服务端资源边界**：连接上限（`server_full`）、握手超时、空闲超时、
   房间 TTL 未写成自动化测试 —— NOT TESTED（逻辑实读，未执行边界）。
   重连窗口到期判负与旧令牌拒绝已测；重连成功的客户端路径由
   `net.client` 覆盖；会话层重连见 `net.session` 的断线恢复逻辑（无独立用例）。
6. **并发**：TSan 仅本机手动运行 net.client/net.session，未进 CI；
   服务端单线程无共享状态，但长时间抖动未压测。
7. **`message_too_large`** 分支不可达：WS 帧上限 = 消息上限，超长帧直接
   触发 WS 错误并关闭；协议错误码保留但永不发送。
8. **重入修复未被饱和测试**：16 KB 发送队列溢出路径没有自动化用例
   （测试会污染后续状态），只靠代码审查与延迟关闭的单元行为验证。
9. **在线终局入屏仅有本地 E2E**：`app_build_net_match` 成功门控依赖 GUI
   交互；本地浏览器 E2E（投降→GAME_OVER）覆盖，但未进 CI。
10. **UI 交互自动化**：`app.net` 仅渲染冒烟；大厅/对局点击由本地
    `scripts/web-online-e2e.mjs` 覆盖，未进 CI。
11. **在线终局导入 Lab** 未实现。
12. **日志**：stderr 无轮转/结构化；令牌未记录（代码审查确认）。

## 6. 是否达到验收标准

| Phase 14 任务 | 结论 | 依据 |
|---|---|---|
| 网络架构（独立层、权威服务端） | PASS | 模块边界、服务端重算、测试通过 |
| 房间系统 | PASS | 创建/加入/颜色/ready/满房/离开/重连/终局重连 |
| 网络对局 | PASS | 完整对局、非法/重复、Pass、双向超时、投降、seq |
| 安全设计 | PARTIAL | 限速、重放、令牌、资源上限有实现与部分测试；本地 TLS 终止 PASS，生产配置 NOT TESTED |
| 联网体验 | PARTIAL | 大厅/HUD/重连/投降/终局 PASS；Lab 复盘未接入 |
| 跨平台 | PASS（附注） | 桌面三平台 CI PASS；桌面↔Web 与 Chrome↔Chrome 本地联机 PASS；Safari/Firefox NOT TESTED |
| 测试 | PARTIAL | 服务端/协议/客户端/会话与浏览器 E2E 覆盖；真实网络延迟 NOT TESTED |

**结论：核心在线对战验收 PASS；桌面↔Web、Web↔Web 与本地 TLS 已补测
PASS；原始阶段验收仍为 PARTIAL，仅因生产网络/证书、Safari/Firefox 与
Lab 复盘等明确未测项。**
未达成的部分全部是“真实部署/浏览器/网络环境”类验证，按本阶段约束
（不部署、不开放公网）无法在本机补齐，不得声称完整通过。

## 7. 是否建议进入下一 Phase

**建议可以进入下一 Phase 的功能开发**，但附三点约束：

1. 不得把 Reversi Online 标记为“公网可用/生产就绪”；
2. 浏览器双客户端（Headless Chrome）与本地 nginx TLS/WSS 已补测；若下
   一阶段涉及部署或公开服务，仍需补 Safari/Firefox、可信证书链、真实
   网络延迟/丢包验证，并取得用户明确授权；
3. 把本报告第 5 节的 NOT TESTED 项带入后续阶段待办，不得在文档中改写
   为 PASS。

## 附：可复现命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREVERSI_FORCE_FETCH_RAYLIB=ON
cmake --build build -j8 && ctest --test-dir build --output-on-failure
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Debug \
      -DREVERSI_ENABLE_GUI_TESTS=ON -DREVERSI_FORCE_FETCH_RAYLIB=ON
cmake --build build-gui -j8 && ctest --test-dir build-gui
clang -std=c11 -g -O1 -fsanitize=thread -D_POSIX_C_SOURCE=200809L \
  -Isrc -Itests tests/test_net_session.c src/net/net_client.c \
  src/net/net_server.c src/net/net_protocol.c src/net/net_ws.c \
  src/net/net_session.c src/core/{board,bitboard,rules,game,history}.c \
  -o /tmp/tsan_session && /tmp/tsan_session
```
