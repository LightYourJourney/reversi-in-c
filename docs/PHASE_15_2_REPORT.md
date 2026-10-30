# Phase 15.2 — 最终发布阻碍收敛

日期：2026-10-30  
基线：`ab89717`（Phase 15 收尾后），源码版本 2.0.0 RC  
范围：只修真实问题；未部署公网、未购买云资源、未创建标签或 GitHub Release。

## 1. 结论摘要

| 问题 | 结论 |
|---|---|
| 任务一 部署工程 | **PASS（本地）**：发布包现在自带服务端与 Web 客户端；systemd/nginx/发行目录指向同一个 `current` 符号链接；生产验收改用无损健康检查；回滚演练可重复通过。systemd 本体仍未在真实 systemd 上执行（NOT TESTED，同前）。 |
| 任务二 桌面 WSS | **未交付实现，已明确限制**：原生客户端没有任何 TLS 栈。新增硬性拦截——解析出的目标地址不是回环/私网时，**在建立套接字之前**以 `tls_required` 失败，桌面端不会回退到明文公网。设计、必须覆盖的用例写进 `NETWORK_SECURITY.md` §5。 |
| 任务三 主线程网络等待 | **PASS**：桌面发送改为 16 KiB 有界异步队列，主线程只入队；卡死服务器下实测单次调用最慢 6 ms（原实现单次最长 1 s）。 |
| 任务四 文档一致性 | **PASS**：README/ROADMAP/TESTING/NETWORK_SECURITY/RELEASE_READINESS 与实测对齐；历史阶段报告保持原样。 |
| 任务五 RC 验收 | **RC 保留**：本地 Debug/Release/ASan 各 26/26，浏览器三引擎联机与 9/9 自动化通过；Safari 本轮未重跑（见 §7）。 |

**正式 Release 仍不建议**：剩余阻碍见 §8。

## 2. 核查基线

- `HEAD = ab89717`，工作区干净；CI `41026243071`（`ab89717`）**9/9 成功**。
- 未经核实的说法逐条查过源码：`src/net/net_client.c` 仅有明文 WebSocket
  （工程内无 OpenSSL/mbedTLS/SChannel/Secure Transport 依赖）；
  `net_client_send` 原实现持锁调用 `socket_write_all`，最长等待
  `REVERSI_NET_CLIENT_SEND_TIMEOUT_MS = 1000` ms——两项均属实。

## 3. 任务一：部署工程

### 3.1 发现的问题

1. **发布包不含服务端**：`reversi_server` 没有 `install()` 规则，而
   `deploy/systemd/reversi-server.service` 却指向
   `/opt/reversi/bin/reversi_server`。按文档部署必然失败。
2. **目录与符号链接三处不一致**：systemd 用 `/opt/reversi/bin/...`，
   部署文档用 `/opt/reversi/releases/<ver>` + `current`，nginx 用
   `/var/www/reversi`。三处各自成立，合起来对不上。
3. **生产验收里有破坏性步骤**：`docs/PRODUCTION_DEPLOYMENT.md` §8 把
   `check-server-shutdown.mjs`（发 SIGTERM）列为“每次部署”的验收命令——
   它会主动结束线上进程。

### 3.2 修改

- `CMakeLists.txt`：新增
  `install(TARGETS reversi_server RUNTIME DESTINATION bin)`，并在 Web 构建
  存在时 `install(DIRECTORY web/dist/ DESTINATION web)`。发布包成为
  “一个目录即全部”。
- `deploy/systemd/reversi-server.service`：`ExecStart`/`WorkingDirectory`
  改为 `/opt/reversi/current/...`，与发布目录的符号链接一致。
- `deploy/nginx/reversi.conf`：静态根改为 `/opt/reversi/current/web`，
  与 systemd 共用同一个符号链接——切一次同时换掉服务端与前端，回滚同理。
- `docs/PRODUCTION_DEPLOYMENT.md` §4/§8 重写为同一套布局；
  验收改用新的无损健康检查；优雅关闭标注为**仅 staging 演练**。
- `scripts/verify-package.sh`：新增“包内必须有 `bin/reversi_server` 且能
  启动、必须有 `web/index.html`”的断言。
- 新增 `scripts/check-server-health.mjs`：`hello` + `ping` → `pong`，
  不建房、不占座、不碰任何玩家状态。
- 新增 `scripts/rehearse-rollback.sh`：在临时目录里解两版、故意打坏新版、
  验证健康检查拒绝，再切回旧版验证恢复，并断言健康检查没有建房。

### 3.3 证据（本机 macOS 26.6.2）

```text
Composer: Reversi-2.0.0-Darwin-arm64.tar.gz
包内:   bin/reversi_server, web/index.html, docs/, Reversi.app
解压后: bin/reversi_server --help → usage: ... (exit 0)
verify-package.sh (ZIP): PASS: /tmp/rvpkg/Reversi-2.0.0-Darwin-arm64.zip
check-server-health.mjs: {"reply":"pong","latency_ms":166,"failures":[]}
rehearse-rollback.sh: PASS 坏发布被拒 / PASS 回滚恢复 / PASS 未建房
```

**包内容与 CI 的差异**：`scripts/package.sh` 现在会先跑
`scripts/build-web.sh`（有 emsdk 时），因此按发布流程构建的归档同时包含
`bin/` 与 `web/`；CI 的 `package.yml` 不构建 Web，其产物只有 `bin/`，
`verify-package.sh` 会打印 “Web client: absent” 而不是失败。

**未验证**：systemd 单元与 logrotate 仍未在真实 systemd 上执行
（开发机为 macOS，容器内无法安装；这是既有 NOT TESTED，本轮未改善）。

## 4. 任务二：桌面 WSS

**核实**：`src/net/net_client.c` 只实现明文 `ws://`，工程内不存在任何
TLS 依赖；握手是手写的 HTTP Upgrade。

**本轮交付**：

- 硬性拦截：`client_thread` 在 `getaddrinfo` 之后检查地址，
  只允许回环、10/8、172.16/12、192.168/16、169.254/16、100.64/10；
  其余**在打开套接字之前**以 `tls_required` 失败。桌面端不会把协议明文
  发往公网，也不存在“先明文试一次”的路径。
- 新增回归用例 `test_public_address_refused`（`8.8.8.8:443` → 1 s 内
  `FAILED` + `tls_required`）；会话层把该原因翻成大厅可见的提示
  “No TLS in this build: use a local or LAN server”，并用
  `test_session_plaintext_refused` 覆盖。
- `docs/NETWORK_SECURITY.md` §5 记录现状、设计（证书链 + 主机名校验、
  TLS 1.2+、与现有非阻塞模型对齐、发送仍走异步队列、重连语义）与
  必须覆盖的七类用例（有效证书、自签、主机名不匹配、过期、握手中断、
  连接中断、服务器重启）。

**未交付**：WSS 传输本身。因此**桌面端公网联机标记为 NOT SUPPORTED**，
发布说明必须写明；桌面↔Web 只在同一台机器的回环地址上验证过。

## 5. 任务三：主线程网络等待

**改前**：`net_client_send()` 持锁调用 `socket_write_all()`，服务器不读时
调用线程最长阻塞 1000 ms。桌面端该调用发生在 UI 主线程（落子、ready、
ping 都由 `net_session` 直接调用）。

**改后**：

- 新增 16 KiB 环形发送队列（与浏览器端同量级）。`net_client_send` 只做
  编码 + 入队，任何线程都不碰套接字。
- 工作线程每轮（≤20 ms）先把队列尽量写出；部分写按偏移续写，顺序保持；
  写失败 → `send_failed` 断开；队列满 → `send_queue_full` 断开并由会话层
  重连重同步。
- 主动停止（`net_client_disconnect`/队列溢出）不再被工作线程的 EOF 覆盖成
  `closed`，失败原因保持可读。
- `net_client.h` 写明语义；`NETWORK_ARCHITECTURE.md`、`TESTING.md` 同步。

**证据**：`test_send_to_stalled_server` 用假服务器完成握手后停止读取：
入队 1639 帧后队列满、以 `send_queue_full` 断开；整段耗时 7 ms，
**单次调用最慢 6 ms**；断言 `slowest < 100 ms`（旧实现在这里会是 ~1000 ms）。

## 6. 任务四：文档一致性与回归

### 6.1 修正的矛盾

| 位置 | 原文 | 现在 |
|---|---|---|
| `NETWORK_ARCHITECTURE.md` | 客户端“1 s 有界写入” | 16 KiB 有界异步队列 |
| `TESTING.md` | `net.client` 57 条断言、有界写超时 | 63 条，含队列饱和与零等待 |
| `NETWORK_SECURITY.md` §4 | 桌面发送“无可复现用例” | 已改；并新增桌面无 TLS 的限制 |
| `PRODUCTION_DEPLOYMENT.md` | 生产验收含 SIGTERM | 无损健康检查 + staging 演练 |
| `PRODUCTION_DEPLOYMENT.md` §4 | `install -m 0755 build/bin/...` | 发布包自带 `bin/` + `current` 符号链接 |

历史阶段报告（`PHASE_0..15_REPORT/REVIEW`）保持原样，作为历史记录。

### 6.2 回归（本机，2026-10-30）

| 套件 | 结果 |
|---|---|
| Debug `ctest` | **26/26 通过**，合计 **1,211,001** 条断言 |
| Release `ctest` | 26/26 通过 |
| ASan+UBSan `ctest`（`halt_on_error=1`） | 26/26 通过 |
| 浏览器联机 E2E（HTTPS/WSS） | Chrome `UV4QP5`、Firefox `SB6UEM`、WebKit `5B6XD4`，均 0 失败 |
| 运行时检查（字体/音效/存储） | 三引擎全通过（标题带亮色像素 10.3–10.6%） |
| 浏览器自动化套件 | **9/9 通过**（含此前一直 SKIP 的 `autoplay`：本轮重建 Debug Web 产物后通过） |
| 安装包验收 | ZIP `verify-package.sh` PASS（含新增的服务端/Web 断言） |
| 回滚演练 | 3 项 PASS |

DMG/包体、GUI 桌面会话、桌面↔Web 实机对局**本轮未重跑**：桌面 UI 未改动，
改动集中在 `NetClient` 传输层，已由 `net.client`（63）与 `net.session`
（100，真实套接字整局）覆盖。

## 7. 未完成 / 未验证

| 项 | 状态 | 说明 |
|---|---|---|
| Safari 整局 | **本轮未重跑** | 会话不可见：`document.visibilityState=hidden`、帧计数 0，点击无法驱动。上一轮的 `CD8GDJ` / `VFDHTZ` 证据对应的 Web 产物与当前仅差一个只读钩子（`audioReady`）。 |
| iOS Safari / Android Chrome | NOT TESTED | 无真机/模拟器 |
| 可信证书链、公网 HSTS/DNS | NOT TESTED | 需真实域名，未授权 |
| Windows 物理桌面 GUI | NOT TESTED | CI 为托管 VM |
| systemd / logrotate 实机 | NOT TESTED | 同上，属既有缺口 |
| 桌面 WSS | **未实现** | 见 §4；桌面公网联机 NOT SUPPORTED |

## 8. 真实发布阻碍

1. **桌面端无法安全公网联机**（无 TLS，且现在会拒绝公网地址）。若要发布
   “桌面公网可玩”，必须先完成 §4 的 WSS 实现与七类用例。
2. **iOS Safari / Android Chrome 未验证**。
3. **可信证书链与真实公网未验证**（本地自签/隔离代理不构成生产 PASS）。
4. **Windows 物理桌面**未验证。
5. **systemd/logrotate 模板**未在真实 systemd 执行。

## 9. 回滚方案

发布目录即回滚单位：

```bash
# 当前：/opt/reversi/current -> /opt/reversi/releases/2.0.0
ln -sfn /opt/reversi/releases/2.0.0 /opt/reversi/current
systemctl restart reversi-server
node scripts/check-server-health.mjs --url wss://ws.example.com/

# 回滚
ln -sfn /opt/reversi/releases/1.0.1 /opt/reversi/current
systemctl restart reversi-server
nginx -t && systemctl reload nginx
node scripts/check-server-health.mjs --url wss://ws.example.com/
```

`current` 同时决定服务端二进制与静态站点，所以回滚是“一次符号链接 + 一次
重启”。协议 v1 未变、无存档格式变更，回滚不影响本地存档。演练脚本
`scripts/rehearse-rollback.sh` 在隔离目录复现了“坏发布被拒 → 切回恢复”。

## 10. 未执行（需授权）

- 未部署任何公网服务、未申请证书、未修改 DNS。
- 未创建版本标签、未创建 GitHub Release、未推送打包产物。
