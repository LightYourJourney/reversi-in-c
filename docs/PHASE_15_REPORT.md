# Phase 15 报告：Reversi 2.0.0 发布加固（Release Candidate）

日期：2026-10-29  
源码版本：`VERSION` = 2.0.0（RC，未发布）  
本机平台：macOS 26.6.2（Darwin 25.6.0），Apple M3 / arm64，
AppleClang 21.0.0，Homebrew clang 23.1.4（TSan），CMake 4.4.4，
Emscripten 6.0.13，nginx 1.31.8，OpenSSL 4.1.0，
Chrome 155（Headless）、Playwright Firefox 157、Playwright WebKit 27.2  
状态标记：PASS / PARTIAL / FAIL / NOT TESTED

本阶段不新增玩法；目标是 2.0.0 正式发布前的可靠性、安全性、兼容性与
验收。所有结论以本机实际运行与本仓库代码为准，不采信历史报告。

## 1. 起点核查（实际状态，非引用）

- `git status` 干净；HEAD `86860d0`；CI 运行 `40707272332` 为 9/9
  成功（Phase 14 审计提交）。VERSION 2.0.0；最新发行版 v1.0.1。
- 本机 Debug 基线：26/26 PASS（约 101 s）。
- 已确认的缺陷（有代码证据）：
  1. `net_client_web.c` 接收队列满时 `return` 静默丢消息；
  2. `net_client.c` 在阻塞套接字上于 UI 线程 `send`，服务器不读时界面可
     被冻结；
  3. `detach_player()` 在等待房间断线时立即 `room_close()`，对手被踢；
  4. `handle_rejoin()` 要求 `player->connection < 0`，旧连接未被回收时
     `rejoin_failed`，客户端清空座位；
  5. `room->expires` 只在创建时设置，晚加入的房间可能立刻过期；
  6. `dispatch()` 对 `hello` 不做任何处理，版本/顺序未强制（文档却声称
     首条消息为 hello）；
  7. `join` 失败没有独立上限（仅每连接 20 msg/s）；
  8. `main()` 收到 SIGTERM 直接销毁，客户端看到连接重置；
  9. 握手/空闲/房间 TTL 写死在宏里，缺少边界测试；
  10. `docs/RELEASE.md` 仍以 1.0.1 为当前版本。

## 2. 修复清单（每项均有对应测试或证据）

| # | 问题 | 修复 | 证据 |
|---|---|---|---|
| 1 | Web 队列溢出静默丢弃 | 溢出时 `FAILED("queue_overflow")`、关闭套接字、清空队列：**房间内**由会话自动重连并重同步，**大厅内**转为 FAILED 需手动 Connect；发送失败同样断开；重连前关闭旧套接字 | `scripts/web-transport-failure-check.mjs` 三引擎：溢出后 phase 7、手动重连后 2 |
| 2 | 桌面发送阻塞 UI | 连接后置非阻塞；`socket_write_all` 以 select 等待，1 s 上限；部分帧写入失败即 `shutdown` 并 `DISCONNECTED("send_failed")`；pong 路径同样处理 | `net.client` 57 断言（含假服务器不读的自动化用例）；TSan 无竞争 |
| 3 | 等待房间断线即销毁 | 断线保留座位到重连窗口；通知对手 `disconnected`；结束房间仍按原行为关闭（E2E 结果保留依赖它） | `test_waiting_room_reconnect` PASS |
| 4 | 旧连接占用时无法重连 | 令牌持有者的 `rejoin` 接管座位：清旧连接的 room/seat 并关闭，令牌轮换 | `test_rejoin_takeover` PASS（旧套接字被关闭；新连接可落子） |
| 5 | 房间 TTL 未刷新 | 加入/重连等待房间时 `expires = now + room_wait_ms` | `test_room_ttl`、`test_room_ttl_refresh` PASS |
| 6 | hello 顺序未强制 | 首条消息必须是 `hello`；重复 hello → `unexpected_message`；其余 → `hello_required`；会话重连先 hello 再 rejoin | `test_hello_order`、`net.session` PASS；6 处既有测试补发 hello |
| 7 | 房间代码枚举 | 每连接 8 次 join/rejoin 失败 → `rate_limited` 并断开；成功加入清零 | `test_join_failure_limit` PASS |
| 8 | 无优雅关闭 | `net_server_shutdown()` 广播 `roomclosed status=shutdown`，刷出队列；`main` 在 SIGTERM 后调用 | `test_server_shutdown`、`scripts/check-server-shutdown.mjs`（房间 `DFB5D2`，退出码 0） |
| 9 | 边界不可测 | `NetServerConfig` 新增 `handshake_timeout_ms` / `idle_timeout_ms` / `room_wait_ms` / `room_done_ms`（≤0 取默认） | `test_handshake_timeout`、`test_idle_timeout`、`test_room_ttl` PASS |
| 10 | 文档版本不一致 | `RELEASE.md` 改为 2.0.0（RC）+ 最新已发布 1.0.1；README/TESTING/CHANGELOG/ROADMAP/NETWORK_* 同步 | 见 §7 |

复审补充：连接上限（48）现在向被拒连接写回 `HTTP/1.1 503`，客户端可区分
“服务器满”与网络故障；Web 发送增加 `bufferedAmount` 上限（64 KiB）并在
发送失败时断开重连。

第二轮补充（按用户决定执行）：

- **连接阶段可中断**：`connect` 改为非阻塞 + 5 s 上限，等待以 100 ms 切片
  检查停止标志；握手等待同样可取消。修复“连接不可达主机时点 Disconnect
  或退出会冻结到 OS 超时（约 75 s）”。
- `handle_leave` 用 `room_seat_for_connection` 防御性校验座位；
  `message_too_large` 死分支保留并注释（帧上限与消息上限相同）。
- **发送队列接缝**：`NetServerConfig` 新增 `send_queue_bytes`（默认 16 KiB）
  与 `send_buffer_bytes`（默认 OS）；新增饱和用例（Linux/Windows 运行，
  macOS 因内核自动调优无法压小接收窗口而跳过；Linux 容器实测通过）。
- **在线终局导入 Lab**：客户端按 seq 记录服务端确认的落子；终局 HUD 增加
  Review，重放进新的 Lab 会话（快照去重、缺步时禁用）。
- Web 检查扩展为“队列溢出 + 发送失败”，三引擎验证。

未做的重构：重复的 `now_ms()`、测试 pump 循环、结果映射三元式、
CMake 测试块。均为可维护性事项，不改变行为。

## 3. 新增测试（网络模拟 vs 真实网络）

| 用例 | 类型 | 断言 |
|---|---|---|
| Hello 顺序与重复 | 协议顺序 | `test_hello_order` |
| 握手 100 ms 超时 | 服务端边界 | `test_handshake_timeout` |
| 连接上限返回 HTTP 503 | 资源上限 | `test_connection_cap`（读取 49 号连接的 `HTTP/1.1 503`） |
| 空闲 300 ms 超时 | 服务端边界 | `test_idle_timeout` |
| 等待房间 TTL 到期 / 加入刷新 | 服务端边界 | `test_room_ttl`、`test_room_ttl_refresh` |
| 48 连接上限与释放后恢复 | 资源上限 | `test_connection_cap` |
| 32 房间上限、离开后恢复 | 资源上限 | `test_room_cap` |
| 加入失败 8 次限速 | 安全 | `test_join_failure_limit` |
| 等待房间断线重连 | 重连时序 | `test_waiting_room_reconnect` |
| 旧连接在线时接管 | 重连时序 | `test_rejoin_takeover` |
| 优雅关闭消息 | 运维 | `test_server_shutdown` |
| **20 ms 单向延迟代理整局** | **进程内网络模拟** | `test_latency_game`（双客户端经测试内 TCP 代理完成整局，逐手重放校验） |
| 连续 8 局完整对局 | 稳定性 | `test_soak_games`（默认 8 局；`REVERSI_SOAK_MS` 长时） |
| 日志脱敏 | 安全 | `test_logs_redacted`（redirect stderr，断言 token/`token=`/`board=` 不出现；POSIX） |
| 接收队列溢出（桌面） | 客户端 | `net.client::test_queue_overflow` |
| 服务器不读数据（桌面） | 客户端 | `net.client::test_send_to_stalled_server`（假服务器完成握手后停止读取，写入必须在 1 s 上限内失败并断开） |
| 中途服务端重启 → 重连回到大厅 | 会话 | `net.session::test_session_server_restart` |
| 重连预算耗尽 → FAILED | 会话 | `net.session::test_session_reconnect_budget` |
| v1.0.1 存档/设置 | 兼容性 | `storage.save::test_v101_fixture`、`storage.settings::test_v101_fixture` |
| v1.4.0 棋谱 | 兼容性 | `analysis.session::test_record_v140_fixture`（`be5b90d` 源码写出的记录，读入并重写一致） |
| 浏览器队列溢出 / 发送失败 | 真实浏览器 | `web-transport-failure-check.mjs`（三引擎，各 7 → 2） |
| 连接中取消 | 客户端 | `net.client::test_connect_cancelled`（TEST-NET-1，2 s 内返回） |
| 连接 5 s 上限 | 客户端 | `net.client::test_connect_timeout` |
| 服务端发送队列饱和 | 服务端 | `net.server::test_send_queue_saturation`（Linux/Windows；macOS 跳过） |
| 在线落子记录 | 会话 | `net.session::test_session_move_log` |
| 在线终局 → Lab 复盘 | GUI | 房间 `HQ7EUE`：Review 打开 Lab 终局，导航回开局 |
| 浏览器渲染/缩放/触摸/存档/Worker | 真实浏览器 | `tests/web/run-tests.js` 9/9（Chrome；套件坐标改为按 UI 布局公式计算） |

**模拟与真实的区分**：延迟代理是回环 TCP 上的用户态延迟，不含路由、
NAT、丢包、乱序、带宽限制。真实跨网络 **NOT TESTED**。字节级丢包不做
注入：TCP 会重传，应用层观察不到；连接级丢失由断开/重连用例覆盖。

## 4. 实际执行的测试与结果

| 配置 / 命令 | 结果 |
|---|---|
| `cmake --build build && ctest --test-dir build` | PASS 26/26（34.8 s） |
| `ctest --test-dir build-release` | PASS 26/26（29.0 s） |
| `ctest --test-dir build-sanitize -E "app.*"`（ASan+UBSan） | PASS 26/26，无报告（49.5 s） |
| `ctest --test-dir build-gui` | **PASS 30/30**（本机 GUI 会话）；CI Linux Xvfb GUI 4/4 PASS |
| `docker run gcc:14`（Linux） | `net.server` 5,765 断言 PASS（含饱和用例） |
| 代理 `limit_conn` | PASS：4 条并发升级 101，第 5 条 503 |
| TSan `net.session` / `net.client`（Homebrew clang 23） | PASS 96 / 57 断言，无竞争报告 |
| `net.server` | PASS 5,758 断言（macOS，含延迟代理与 8 局 soak）；Linux 容器 5,765（含发送队列饱和） |
| 浏览器 Chrome 155 | PASS 房间 `38RSUR`；生产式代理下 `9J25DT` |
| 桌面↔Web（本机 GUI 会话） | PASS：桌面 `ws://127.0.0.1:8080` 黑方建房，Chrome `wss://127.0.0.1:8444` 白方加入；房间 `D7AKVC`，桌面 d3、浏览器 seq 1、浏览器投降、双方 BLACK_WIN；浏览器关闭后桌面显示 `Online room closed … opponent left` 并保留 `Game over: Black wins (resign)` |
| Web 自动化套件（Chrome） | PASS 9/9：smoke、gameplay、persistence、responsive（5 视口）、difficulties、mobile-touch、pagehide、cancel、autoplay |
| 浏览器 Firefox 157 | PASS 房间 `45H8XY` |
| 浏览器 WebKit 27.2 | PASS 房间 `V3U9MB`（引擎代理，不等于 Safari） |
| Web 队列溢出（三引擎） | PASS：7 → 重连 → 2 |
| Emscripten `scripts/build-web.sh` | PASS（wasm 395,038 B） |
| 本地 TLS/HSTS/限速/关闭 | PASS，见 `PRODUCTION_DEPLOYMENT.md` §7 |
| 10 分钟 ASan soak `REVERSI_SOAK_MS=600000` | PASS：`PASS net.server (865909 checks)`，退出码 0，无 ASan 报告 |
| 安装包 | CI 三平台打包 PASS（`40818759364`）；Linux 包在 Xvfb 下验收 PASS；Windows 包 `--version` PASS；macOS 包已生成、未实际启动 |
| CI（本阶段提交） | **PASS 9/9**：运行 `40924395184`（`ccfa435`）三平台 Debug、ASan+UBSan、Xvfb GUI、三平台打包与 Linux 包验收全部 success（Windows 首次运行连接取消/饱和/发送超时用例）。此前 `40923405109` 仅 Windows Debug 失败（测试在 `WSAStartup` 前创建 socket），修复见 `ccfa435`；再前 `40816665469` 的 Windows 503 失败见 `2ba11ab` |

断言数（本机 Debug）：`net.protocol` 33、`net.ws` 20,026、`net.server`
5,758、`net.client` 57、`net.session` 96、`analysis.session` 112、
`storage.save` 102、`storage.settings` 68；全部无窗口测试合计 1,210,368 断言
（Linux/Windows 另加饱和用例的 7 条）。

### 4.1 假阳性检查

临时回退修复后重建并运行，确认新增断言确实失败：

| 回退 | 失败断言 |
|---|---|
| 取消座位接管 | `test_rejoin_takeover` 在 WELCOME/ROOM/STATE、`test_wait_closed` 处失败 |
| 等待房间断线改回立即关闭 | `test_waiting_room_reconnect` 在 `opponent disconnected`、rejoin WELCOME 处失败 |
| 删除加入时 TTL 刷新 | `test_room_ttl_refresh` 在加入 ROOM 处失败 |
| 取消 hello 强制 | `test_hello_order` 在 `hello_required` 处失败；既有测试出现顺序错误 |
| 队列溢出改回静默丢弃 | `net.client::test_queue_overflow` 在 `FAILED`/`queue_overflow` 处失败 |

## 5. 长时间稳定性（soak）

命令：

```bash
REVERSI_SOAK_MS=600000 ASAN_OPTIONS=halt_on_error=1 \
  ./build-sanitize/reversi_net_server_tests
```

结果：`PASS net.server (865909 checks)`，进程退出码 0，无 ASan/UBSan 报告。

该断言数包含 net.server 的常规用例（5,758）与 600 秒内的连续完整
对局；按每局约 489 条断言推算，10 分钟完成约 **1,750 局**完整对局
（两名客户端建房、加入、ready、随机合法落子、终局、断开）。过程中没有
连接泄漏、崩溃、协议错误或断言失败。断言总数随 machine speed 变化，
不以该数作为性能指标。

## 6. 未验证项目（不得改写为 PASS）

1. 桌面 GUI：**macOS 本机 30/30 PASS**（含 `app.smoke/app.net/app.lab/
   app.autoplay`，2026-10-29 GUI 会话）；CI Linux Xvfb 4/4 PASS。
   Windows 物理桌面 GUI 未验证。
2. 桌面↔Web：**PASS**（房间 `D7AKVC`，2026-10-29；Phase 14 的 `HVVFKP`
   为旧证据）。
3. Safari、iOS Safari、Android Chrome：**NOT TESTED**。字体渲染与音效
   **NOT TESTED**（浏览器 smoke 只证明无控制台错误；桌面字体/音效为
   Phase 14 GUI 证据，本阶段无显示器未复验）。
4. 真实公网：可信证书链、HSTS 实际生效、DNS、NAT/防火墙、跨地域延迟
   与丢包：**NOT TESTED**。
5. Windows/Linux 物理桌面：**NOT TESTED**（CI 为托管 VM）。2.0.0
   安装包由 CI 生成并校验：Linux 在 Xvfb 下执行 `verify-package.sh` PASS，
   Windows `--version` PASS，macOS 包未启动。
6. 桌面端“服务器不读数据”的发送超时：实现有界写，但无自动化用例。
7. 服务端发送队列（16 KB）饱和：只有代码审查（应用层队列需要先填满内核
   发送缓冲，未做自动化）。
8. 代理层 `limit_conn` 并发行为：配置与语法通过，未压测。
9. 10 分钟 soak 之外的小时级运行：未做。

## 7. 发布版本与文档一致性

- `VERSION` 保持 2.0.0；未创建标签、未发布 Release。
- 已更新：README（平台矩阵、浏览器引擎、测试结果）、TESTING（断言数、
  Phase 15 章节、脚本）、CHANGELOG（2.0.0 增补）、ROADMAP（Phase 15）、
  NETWORK_PROTOCOL / NETWORK_SECURITY / NETWORK_ARCHITECTURE、
  WEB_ONLINE_TESTING、RELEASE.md（2.0.0 RC）。
- 新增：`RELEASE_READINESS.md`、`PRODUCTION_DEPLOYMENT.md`、
  `deploy/nginx/reversi.conf`、`deploy/systemd/reversi-server.service`、
  `deploy/logrotate/reversi-server`、
  `scripts/check-server-shutdown.mjs`、
  `scripts/web-transport-failure-check.mjs`。
- 历史文档（PHASE_14_*、PHASE_7_*）保持原样，其中 Safari/Firefox
  NOT TESTED 是当时的结论。
- `tests/web/lib.js` 的点击坐标改为按 `menu_view.c` / `setup_view.c` 的
  布局公式计算：Phase 14 把菜单从 6 项扩到 9 项后，旧硬编码坐标已指向
  错误按钮，套件自那时起实际失效（本阶段发现并修复）。

## 8. 生产安全评估

| 维度 | 结论 |
|---|---|
| 服务端权威与输入校验 | PASS（core 重算、严格协议/帧、模糊、ASan） |
| 资源上限 | PASS（连接/房间/队列/限速，均有测试） |
| 重放/伪装 | PASS（seq、128 位令牌、轮换、日志不含令牌） |
| 枚举 | PARTIAL（每连接 8 次失败 + 代理层限速；多 IP 未覆盖） |
| 传输安全 | PARTIAL（本地自签 TLS 1.2/1.3、HSTS 头、TLS1.1 拒绝；可信证书与公网未测） |
| 客户端健壮性 | PARTIAL（队列溢出、有界写、重连已修；卡死服务器路径未自动化） |
| 运维安全 | PARTIAL（systemd 沙箱、优雅关闭、日志脱敏；监控无指标端点） |

**不得**把本地自签 TLS 结果标记为生产 PASS；不得声称公网就绪。

## 9. 是否具备 Public Beta / 正式 Release 条件

- **Public Beta：PARTIAL（有条件可以）** —— 仅限“源码 + 自托管”，
  发布说明必须写明：未做公网安全评估、真实网络与 Safari/iOS/Android
  未验证。核心在线对战、三引擎浏览器、TLS 代理与优雅关闭均有本地证据。
- **正式 Release：FAIL（接近但暂不具备）** —— CI 9/9、安装包、macOS GUI、
  桌面↔Web、发送队列饱和（Linux/Windows）与本地 limit_conn 均已验证；
  剩余阻碍：Safari/iOS/Android 未验证、可信证书与真实公网未测、Windows
  物理桌面未验证、systemd/logrotate 模板未在真实 systemd 执行。详见
  `RELEASE_READINESS.md` §6。

## 10. 下阶段建议

1. 在 Safari 中启用“Allow Remote Automation”后执行 Safari 联机 E2E。
2. 用真实域名与 Let's Encrypt 做一次隔离环境部署，验证证书链、HSTS
   与代理超时；并在真实 systemd 主机上执行 `systemd-analyze verify` 与
   `logrotate -d`。
3. 补 Safari（`safaridriver`）与 Android Chrome（真机/模拟器）验证。
4. 需要公网级 DoS 防护时，评估边缘限流/IP 信誉或 WAF；应用层队列饱和
   仍建议保留“关闭连接 + 座位保留”的策略。

## 11. 回滚

```bash
git revert <phase-15-commit>
```

局部回滚：恢复 `src/net/net_client.c`、`net_client_web.c`、`net_server.c`、
`net_session.c`、`server/main.c`、测试与文档；删除 `deploy/` 与两个新
脚本。协议、存档与设置格式均未改变，回滚不影响已有数据。网络层架构未改。
