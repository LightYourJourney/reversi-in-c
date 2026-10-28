# Reversi Online 生产部署方案

日期：2026-10-29  
适用源码：2.0.0（Release Candidate）  
状态：**设计完成并在本地隔离环境验证；未部署公网、未购买云资源、未修改
DNS、未申请可信证书**。本地自签 TLS 结果不作为生产 PASS。

## 1. 边界与前置条件

- 服务端 `reversi_server` **进程内不实现 TLS**：只绑定 `127.0.0.1`，
  默认端口 8080，由反向代理终止 HTTPS/WSS。
- 无账号系统；房间与令牌都在内存中，进程重启即丢失。因此部署方案不涉及
  数据库、备份或跨实例共享状态。
- Web 客户端连接 `<host>:<port>/`（路径固定为 `/`），所以 WebSocket 需要
  独立的**主机名**（推荐 `ws.example.com`），静态站点用另一个主机名
  （`reversi.example.com`）。两者可共用同一张证书（SAN 或通配符）。
- 必需组件：Linux x86_64/arm64、systemd、nginx ≥ 1.25（`http2 on;`）、
  certbot（或等价 ACME 客户端）、一个公网域名。

## 2. 拓扑

```text
浏览器 ── https://reversi.example.com ── nginx(443, TLS) ── /var/www/reversi
       └─ wss://ws.example.com ────────── nginx(443, TLS, 限速) ── 127.0.0.1:8080
桌面客户端 ── ws://127.0.0.1:8080（仅本机或可信内网；公网请同样经代理）
```

服务端只监听回环地址；即使 nginx 配置错误也不会直接暴露游戏端口。

## 3. 证书与 TLS

```bash
# 两个主机名签发同一张证书
certbot certonly --webroot -w /var/www/acme \
  -d reversi.example.com -d ws.example.com
```

- `ssl_protocols TLSv1.2 TLSv1.3`；TLS 1.0/1.1 由服务端拒绝（本地实测
  alert 70）。
- 证书续期由 `certbot renew` + `deploy-hook "systemctl reload nginx"` 完成。
- HSTS 从 `max-age=86400` 起步；确认所有入口都只有 HTTPS 后再提高。提交
  preload 列表是**不可逆**动作，不在本阶段范围内。

配置模板见 [`deploy/nginx/reversi.conf`](../deploy/nginx/reversi.conf)。
关键点：

| 项 | 值 | 理由 |
|---|---|---|
| `add_header` 位置 | 只放在 `server` 级 | nginx 中任何 `location` 级的 `add_header` 会**丢弃**服务器级安全头；静态缓存用 `expires` 而不是 `add_header` |
| WSS 限速 | `limit_req 20r/m burst=10` 每 IP | 一局一次连接；阻止房间代码枚举（应用层另有每连接 8 次失败上限） |
| 连接数 | `limit_conn 4` 每 IP | 每玩家最多开 1–2 条连接，重连短暂叠加 |
| `proxy_read_timeout` | 300 s | 服务端空闲 120 s、客户端每 2 s ping；300 s 只兜底卡死的升级请求 |
| `client_max_body_size` | 1k | 应用层消息 ≤ 2048 B；WebSocket 升级请求远小于此 |
| 静态 MIME | `application/wasm` | 否则浏览器回退到 ArrayBuffer 实例化并打印错误 |

## 4. 服务进程

使用 `deploy/systemd/reversi-server.service`：

- 专用用户 `reversi`，`ProtectSystem=strict`、`PrivateTmp`、无 capability。
- `Restart=on-failure`，`RestartSec=2s`。
- `MemoryMax=256M`：服务端上限为 48 连接 × 16 KB 发送缓冲 + 32 房间，
  正常占用远低于此；超限重启可防止泄漏累积。
- `KillSignal=SIGTERM`：进程收到信号后广播 `roomclosed status=shutdown`，
  刷出队列后以退出码 0 结束（本地实测见 §7）。
- `LimitNOFILE=4096` 覆盖 48 连接 + 监听套接字 + nginx 侧连接。

```bash
install -m 0755 build/bin/reversi_server /opt/reversi/bin/reversi_server
systemctl daemon-reload
systemctl enable --now reversi-server
journalctl -u reversi-server -f
```

## 5. 资源上限（应用层）

| 资源 | 上限 | 超限行为 |
|---|---|---|
| 连接 | 48 | 新连接收到 `HTTP/1.1 503` 后关闭；已有连接不受影响（测试覆盖） |
| 房间 | 32 | `create` 返回 `server_full`；房间释放后可再创建（测试覆盖） |
| 每连接消息 | 20 msg/s，burst 40 | `rate_limited` 并断开 |
| 加入失败 | 8 次/连接 | `rate_limited` 并断开（房间代码枚举防护） |
| 握手 | 5 s | 未完成 WebSocket 升级则断开 |
| 空闲 | 120 s | 无任何帧则断开（客户端每 2 s ping） |
| 等待房间 TTL | 10 min（加入/重连时刷新） | `roomclosed status=expired` |
| 结束房间 TTL | 2 min | `roomclosed status=expired` |
| 发送队列 | 16 KB/连接 | 关闭该连接，座位保留重连窗口 |
| 消息/帧 | 2048 B | `message_too_large` / 帧错误关闭 |

以上都可通过 `NetServerConfig` 在测试或私有部署中调整；命令行只暴露
`--bind/--port/--quiet`。

## 6. 日志与监控

- 日志只写 stderr（systemd 收集到 journald），内容为房间代码、座位号与
  状态；**不记录令牌、不记录消息负载**。`net.server` 的 `test_logs_redacted`
  会捕获 stderr 并断言令牌与 `token=`/`board=` 不出现。
- 轮转：journald 默认按大小/时间轮转；若改为文件输出，使用
  `deploy/logrotate/reversi-server`（`copytruncate`，不改文件描述符）。
- 可观测信号（无指标端点，需从日志/系统状态推断）：
  - `restarts`：`systemctl show -p NRestarts reversi-server`；
  - `room X created/joined/started/finished`：房间生命周期与对局速率；
  - `shutdown: closed N room(s)`：计划内停机；
  - `rate_limited` 由 nginx 访问日志的 503 与应用层断开间接体现。
- 建议告警：进程反复重启、nginx 5xx 比例、证书剩余天数。

## 7. 本地隔离验证记录（2026-10-29）

环境：macOS 26.6.2 arm64；nginx 1.31.8（Homebrew）；OpenSSL 4.1.0；
自签证书 `CN=localhost`（SAN `localhost,127.0.0.1`）；服务端
`build/bin/reversi_server --bind 127.0.0.1 --port 8080`；代理配置与
`deploy/nginx/reversi.conf` 相同，仅把主机名/路径/端口改为本机
（静态 9443、WSS 9444）。

| 项 | 命令 | 结果 |
|---|---|---|
| 语法 | `nginx -t -c ...` | PASS |
| 安全头 | `curl -skI https://localhost:9443/index.html` | PASS：`strict-transport-security: max-age=86400`、`x-content-type-options`、`referrer-policy`、`x-frame-options`、`cache-control: no-cache` |
| 静态资源 | `curl -skI .../reversi.wasm` | PASS：`content-type: application/wasm`、`cache-control: max-age=604800` |
| TLS 1.3 | `openssl s_client -tls1_3` | PASS：`TLS_AES_256_GCM_SHA384` |
| TLS 1.2 | `openssl s_client -tls1_2` | PASS：`ECDHE-RSA-AES256-GCM-SHA384` |
| TLS 1.1 | `openssl s_client -tls1_1 -cipher 'DEFAULT@SECLEVEL=0'` | PASS（拒绝）：`tlsv1 alert protocol version`（alert 70） |
| HTTP→HTTPS | `curl -sI http://localhost/` | PASS：`301` |
| WSS 升级 | `openssl s_client` + 手工 `GET / Upgrade` | PASS：`HTTP/1.1 101 Switching Protocols` |
| 浏览器整局 | `scripts/web-online-e2e.mjs --base https://localhost:9443 --ws-port 9444` | PASS：房间 `9J25DT`，LOBBY→PLAYING→seq 1→投降→BLACK_WIN→对手关闭后保留结果 |
| 代理层限速 | 连续 20 次升级请求 | PASS：前 11 次被转发（上游对非 WebSocket 握手返回 502），之后 `503`；error.log 记录 `limiting requests` |
| 连接数限制 | `limit_conn 4` | 已配置并通过语法检查；**未做并发压测**（NOT TESTED） |
| 优雅关闭 | `scripts/check-server-shutdown.mjs --pid <pid>` + `SIGTERM` | PASS：房间 `DFB5D2` 收到 `roomclosed status=shutdown`；进程退出码 0；日志 `shutdown: closed 1 room(s)` |
| 服务重启后的客户端 | `net.session` 的服务器重启用例 | PASS：客户端进入 DISCONNECTED、保留棋盘、不崩溃；房间丢失后回到大厅 |

**明确不构成生产 PASS 的部分**：可信证书链、HSTS 预加载、公网 DNS、
真实 HTTPS 访问、DDoS/CC 压测、跨地域延迟。这些都需要真实域名与网络。

## 8. 部署与回滚

```bash
# 1) 构建与校验（在构建机）
scripts/package.sh                      # Release 构建 + CTest + CPack
sha256sum build-release/package/*.zip   # macOS 用 shasum -a 256

# 2) 发布目录用符号链接原子切换
REV=/opt/reversi/releases/2.0.0
mkdir -p "$REV"
unzip -q Reversi-2.0.0-Linux-x86_64.zip -d "$REV"
ln -sfn "$REV" /opt/reversi/current
systemctl restart reversi-server

# 3) 静态站点同样原子切换
rsync -a --delete web/dist/ /var/www/reversi-next/
mv /var/www/reversi-next /var/www/reversi-live   # 需要两次 rename 时保持旧目录
```

验收（每次部署）：

```bash
systemctl is-active reversi-server
journalctl -u reversi-server -n 20 --no-pager
curl -skI https://reversi.example.com/ | grep -i strict-transport
scripts/check-server-shutdown.mjs --pid "$(pidof reversi_server)" \
  --url wss://ws.example.com/
# 浏览器：开两个窗口各完成一局（建房/加入/落子/投降/重连）
```

回滚：

1. `ln -sfn /opt/reversi/releases/<上一版本> /opt/reversi/current`，
   `systemctl restart reversi-server`；
2. 静态站点指回上一版目录（`nginx -t` 通过后 `systemctl reload nginx`）；
3. 协议 v1 未变，旧客户端/新客户端互通；**没有存档格式变更**，回滚不影响
   本地存档（v1.0.1 的存档与设置已在 `storage.*` 测试中加载验证）。
4. 回滚后同样执行上面的验收命令。

## 9. 故障处理

| 故障 | 现象 | 处理 |
|---|---|---|
| 进程崩溃 | systemd 自动重启；所有房间丢失 | 客户端收到断线并尝试重连 5 次，最终回到大厅；无需干预 |
| nginx 重载失败 | `nginx -t` 报错，旧配置继续服务 | 修正配置后 reload；`systemctl reload nginx` 原子切换 |
| 证书过期 | 浏览器证书错误 | 检查 `certbot renew` 与 hook；重新签发后 reload |
| 连接被打满 | 新玩家无法连接（accept 后关闭） | 检查是否存在恶意连接；必要时下调代理层 `limit_conn` |
| 房间被枚举 | 代理 503 与应用层 `rate_limited` | 确认 `limit_req` 生效；提高 `burst` 只应针对可信来源 |

## 10. 明确未做

- 未申请可信证书、未配置公网 DNS、未开放任何端口。
- 未做真实公网压测、DDoS 防护、WAF。
- 未做多实例/负载均衡（房间在内存中，需要粘性会话或房间路由）。
- 未验证 Windows/Linux 物理桌面上的 GUI 客户端与代理组合。
- `limit_conn` 的并发行为未压测。
