# Phase 14 独立验收与代码质量审计（第二次）

日期：2026-10-28  
审查对象：Phase 14 Reversi Online；基线 `be5b90d`；审计起点 HEAD `e8bf9ff`  
历史版本：2026-10-27 的首次审计见 git 历史  
修复归属：高 1–中 3 由上一审查轮提交 `5125957` 修复（本次审计复核并补端到端
证据）；本次审计的 E2E 断言与文档修复提交为 `5652399`。  
环境：macOS 26.6.2 arm64，Apple clang 21.0.0，Homebrew clang 23.1.4（TSan），
CMake 4.4.4，Emscripten 6.0.13，nginx 1.31.8，Chrome 155（Headless）  
状态标记：PASS / FAIL / PARTIAL / NOT TESTED

本次审计不采信既有报告，重新核对需求、diff、源码、测试与文档，并重跑
全部可复现测试。上一轮之后新增了浏览器联机、桌面↔Web 与本地 TLS 验证，
本次重点复核这些路径及其后续修复。

## 1. 审查范围

- 需求：用户 Phase 14 任务（独立网络层、权威服务端、房间、重连、安全、
  大厅、桌面 + Web、版本化协议、测试与报告、禁止未经授权部署）。
- 文档：`NETWORK_ARCHITECTURE`、`NETWORK_PROTOCOL`、`NETWORK_SECURITY`、
  `WEB_ONLINE_TESTING`、`PHASE_14_REPORT`、`ARCHITECTURE`、`TESTING`、
  `ROADMAP`、README、CHANGELOG。
- 代码：`git diff be5b90d...e8bf9ff`（43 文件，+7,650/−41）；重点
  `src/net/*`、`server/main.c`、`src/platform/web/net_client_web.c`、
  `src/ui/net_view.*`、`src/app/app.c`、`src/core/match.*`、CMake 与 CI。
- 测试：`tests/test_net_*`、`tests/test_match.c`、`app.net`、
  `scripts/web-online-e2e.mjs`，以及桌面↔Web 的主机自动化流程。
- 不审查：Phase 1–13 的规则/AI/Lab/实验平台实现（仅回归）。

架构复核：网络层（协议/WS/服务端/客户端/会话）独立于 UI；UI 只读
`NetSession`，`app` 负责动作；`Match` 仍由 `match_sync_network` 单一写入；
桌面 worker 线程与主线程通过互斥量/原子量交互，Web 使用主线程回调；服务端
单线程 `select`。未发现循环依赖或新增跨层回调。重复的 `now_ms()`、测试
pump 循环与结果映射、Web/桌面队列溢出策略不一致记录为可维护性事项。

正确性核对：输入边界由协议/WS 的严格字段与长度检查、20k 随机帧模糊和
速率限制覆盖；状态一致性由服务端 `seq`/规则校验与客户端快照同步测试
覆盖；ASan 无报告；连接/房间/输出队列生命周期由延迟关闭与测试覆盖；
错误路径返回协议错误码且不改变状态；并发仅本机 TSan；协议 v1 与存档
格式 v1 未变。搜索取消（AI）不涉及本阶段 —— NOT APPLICABLE。

## 2. 发现的问题（按严重程度）

### 高 1（上轮修复 `5125957`，本次复核确认）：保留结算时，房间关闭消息覆盖真实结束原因

上一轮修复在 `ROOMCLOSED` 时保留 FINISHED，但仍把房间生命周期状态写入
`reason`。对手离开或房间到期后，HUD 会把 `resign` 显示成
`disconnected`/`expired`，与“结束原因真实”的既有修复和协议语义矛盾。

### 高 2（上轮修复 `5125957`，本次复核确认）：终局后 socket 断开仍会丢弃结果

`FINISHED` + `active=false` 后，任何 socket 断开都会进入
`DISCONNECTED`/`FAILED` 分支并回落到 LOBBY/FAILED，结算画面消失，
与 CHANGELOG“保留直到玩家自行离开”不符（服务端重启即可触发）。

### 中 3（上轮修复 `5125957`，本次复核确认）：保留画面显示过期 HUD

房间关闭后 `code` 已被清空，HUD 仍渲染 “Online room   you … opponent
connected”，房间已不存在但界面显示仍连接。

### 中 4（本次审计发现）：验收口径与数字不一致

- 任务 6 标 PASS 但同格仍写 Safari/Firefox NOT TESTED，审查结论仍称
  因 Safari 而 PARTIAL；
- `ROADMAP`/审查报告的 wasm 大小滞后（394,855/394,629）；
- `WEB_ONLINE_TESTING` 自相矛盾（“不切换前台” vs “必须在前台”），
  两局房间码未说明用途；
- `TESTING` 的 CI 段落漏记 `net.session` 计数。

### 中 5（本次复审发现）：E2E 保留断言未证明房间关闭被处理

原断言只重复比较已为真的 screen/phase/result；若 `ROOMCLOSED` 丢失或客户端
在 FINISHED 停止轮询，仍会通过。已加入 `netCode()` 清空轮询并断言 `code==""`。

### 低/遗留

- `message_too_large` 分支不可达（WS 帧上限等于消息上限）；
- Web 队列溢出静默丢弃，桌面端则置 FAILED，两端策略不一致；
- `now_ms()`、测试 pump 循环、结果→`GameResult` 三元式重复；
- TSan 仅本机手动运行，未进 CI；桌面↔Web 自动化依赖 macOS 窗口事件；
- 连接上限、握手/空闲超时、房间 TTL 未写成测试；性能无基准。

## 3. 修复的内容

1. `ROOMCLOSED` 仅在未结束时更新 `reason`；保留 FINISHED 与真实原因
   （`src/net/net_session.c`）。
2. `NET_CLIENT_DISCONNECTED` / `NET_CLIENT_FAILED` 遇到 FINISHED 不再改
   相位，服务端重启后结果仍在。
3. HUD 在房间关闭后显示 `room closed / opponent left`，不再显示空房间码
   与过期对手状态（`src/app/app.c`）。
4. 会话测试改为 LEAVE 路径，断言 `reason=resign`、`active=false`，追加
   “服务端关闭后仍保留 FINISHED”用例（`net.session` 32→37）。
5. 浏览器 E2E 脚本增加“对手关闭后本方保留结果”的断言，并轮询
   `netCode()` 清空以证明房间关闭消息已处理（`5652399` + 本次复审修正）；
   桌面↔Web 以浏览器立即关闭复验，桌面仍显示结算。
6. 文档一致性：任务 6 按实际证据标为 PARTIAL（桌面 + Chrome Web 已测，
   Safari/Firefox 未测）；统一 wasm 394,922 B、会话 37 断言与 CI 运行号；
   修正 Web 验证方法的矛盾表述，合并房间码证据（Web `EZJ69N`、桌面
   `HVVFKP`；早期调试房间不再作为结果）。

未做的重构：重复工具函数、测试 pump 循环、协议/结果映射表、CMake 测试
块合并。均为可维护性事项，不改变行为。

## 4. 实际执行的测试

| 配置 | 命令 | 结果 |
|---|---|---|
| Debug 无窗口 | `ctest --test-dir build` | 26/26 PASS |
| Release 无窗口 | `ctest --test-dir build-release` | 26/26 PASS |
| ASan+UBSan | `ctest --test-dir build-sanitize -E "app.smoke\|app.autoplay\|app.lab\|app.net"` | 26/26 PASS |
| Debug + GUI | `ctest --test-dir build-gui` | 30/30 PASS（含 `app.net`） |
| TSan（会话） | `clang -fsanitize=thread … test_net_session.c` | PASS（37 断言，无竞争报告） |
| 浏览器 E2E | `scripts/web-online-e2e.mjs` | PASS（`EZJ69N`：LOBBY→WAITING→PLAYING→seq 1→投降；对手关闭后本方仍 GAME/FINISHED/BLACK_WIN） |
| 桌面↔Web | 桌面 `ws://` 建房、浏览器 WSS 加入 | PASS（`HVVFKP`：桌面 d3、浏览器 seq 1；投降并关闭浏览器后桌面仍显示 `Game over: Black wins (resign)`，HUD `Online room closed … opponent left`） |
| 本地 TLS/WSS | nginx 自签 TLSv1.3，`openssl s_client` + 浏览器 | PASS（`101`，CN=localhost） |
| Emscripten | `scripts/build-web.sh` | PASS（wasm 394,922 B） |

断言计数（本机实跑）：`core.match` 1011、`net.protocol` 33、`net.ws`
20,026、`net.server` 713、`net.client` 26、`net.session` 37。

假阳性检查：回退“保留结算/原因”改动后新增会话断言失败；历史回退
（双向超时判负、终局快照）的用例同样失败。E2E 的保留断言现在还会在
`ROOMCLOSED` 丢失时失败（`netCode()` 不清空）。

测试质量说明：core 规则有独立 oracle 测试（`test_oracle.h`）；网络完整
对局用同一 `core` 重放作为一致性检查（非独立 oracle），另由 WS 20k 随机帧、
协议边界与服务端模糊覆盖。异常路径（非法/重复/越权/限速/满房/重连到期）
均有用例；浏览器仅覆盖 Chrome。

回归：

- 旧功能：全部既有测试通过；本地双人/AI/Lab/实验路径未改（GUI 30/30）。
- 旧存档：`REVERSI_SAVE_FORMAT_VERSION` 仍为 1，Phase 14 未触及
  `src/storage/`；同版本往返通过。**跨版本 fixture NOT TESTED**。
- 桌面/Web：桌面三平台 CI 通过；Web 构建与双浏览器/桌面↔Web 本地联机
  通过；Debug/Release 均跑全部无窗口测试，网络代码无 `NDEBUG` 分支。

性能：

- 无新增性能/强度声明。UI 每帧一次 `net_session_update`（非阻塞轮询），
  服务端单线程 `select`；**延迟/吞吐/CPU 基准未执行** —— NOT TESTED。

CI：本次审计提交 `5652399` 的运行 `40692227710` 9/9 成功；此前代码修复
`5125957` 的运行 `40685189666` 亦为 9/9。

## 5. 仍未解决的风险

1. **真实网络 NOT TESTED**：延迟/丢包/乱序仅回环与本地代理模拟。
2. **生产 TLS NOT TESTED**：本地 nginx 自签（浏览器 `ignoreHTTPSErrors`）
   已测；可信证书链、HSTS、代理超时、公网握手未测。
3. **Safari/Firefox、移动端 NOT TESTED**（不属本阶段验收范围）。
4. **服务端边界**：`server_full`、握手超时、空闲超时、房间 TTL 未写成
   自动化测试；`message_too_large` 分支不可达。
5. **并发**：TSan 仅本机手动运行，未进 CI；长时间抖动未压测。
6. **桌面↔Web 自动化**依赖 macOS 窗口/事件注入，未进 CI；成功两局
   未脚本化。
7. **Web 队列溢出**静默丢弃，与桌面的 FAILED 策略不一致。
8. **在线终局导入 Lab** 未实现；大厅/网络 HUD 的点击由浏览器 E2E 覆盖，
   桌面 GUI 点击仍依赖主机自动化。
9. **发送队列/重入**：16 KB 队列溢出仅在代码层审查，未做饱和压测；
   日志无轮转/结构化；公网 DoS/NAT/防火墙未测。
10. **性能**：无网络基准，不能声称吞吐/延迟指标。

## 6. 是否达到验收标准

| Phase 14 任务 | 结论 | 依据 |
|---|---|---|
| 1. 网络架构 | PASS | 独立网络层、服务端权威、复用 core |
| 2. 房间系统 | PASS | 创建/加入/颜色/ready/满房/离开/重连/终局重连 |
| 3. 网络对局 | PASS | 完整对局、非法/重复、Pass、双向超时、投降、seq |
| 4. 安全设计 | PARTIAL | 限速/重放/令牌/资源上限已实现并部分测试；本地 TLS 已测，生产 TLS 未测 |
| 5. 联网体验 | PARTIAL | 大厅/HUD/重连/投降/终局 PASS；Lab 复盘未接入 |
| 6. 跨平台 | PARTIAL | 桌面↔Chrome Web 本地完成一局；Safari/Firefox NOT TESTED |
| 7. 测试 | PARTIAL | 单元/集成/浏览器/桌面↔Web 覆盖；真实网络 NOT TESTED |

**结论：核心在线对战（权威服务端、房间、桌面 + Chrome Web 联机、本地
TLS）可用；整体仍未完全达到原始验收（PARTIAL）**：浏览器仅 Chrome 验证、
真实网络与生产 TLS 未测、Lab 复盘未接入。不得声称“公网/生产就绪”。

## 7. 是否建议进入下一 Phase

**建议可以进入下一 Phase 的功能开发**，约束不变：

1. 不得把在线功能标记为公网/生产就绪；
2. 涉及部署前必须补：可信证书链/HSTS、生产反向代理、真实网络延迟/丢包
   与 Safari/Firefox 验证，并取得用户明确授权；
3. 第 5 节 NOT TESTED 项带入后续待办，不得在文档中改写为 PASS。

## 附：可复现命令

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-gui && ctest --test-dir build-gui
clang -std=c11 -g -O1 -fsanitize=thread -D_POSIX_C_SOURCE=200809L \
  -Isrc -Itests tests/test_net_session.c src/net/net_client.c \
  src/net/net_server.c src/net/net_protocol.c src/net/net_ws.c \
  src/net/net_session.c src/core/{board,bitboard,rules,game,history}.c \
  -o /tmp/tsan_session && /tmp/tsan_session

# Browser E2E (local nginx TLS + server, see docs/WEB_ONLINE_TESTING.md)
NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-online-e2e.mjs
```
