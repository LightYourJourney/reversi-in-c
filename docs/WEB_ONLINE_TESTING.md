# 在线联机（Web / 桌面↔Web）与 TLS 本地验证

日期：2026-10-28（桌面↔Web 追加于同日；2026-10-29 Phase 15 补充
Chrome/Firefox/WebKit 与生产式代理）
范围：本地回环验证；**未部署公网、未购买云资源**  
环境：macOS 26.6.2 arm64，Chrome 155（Headless，Playwright 1.64.1，
channel `chrome`），Playwright Firefox 157，Playwright WebKit 27.2，
nginx 1.31.8（Homebrew），OpenSSL 4.1.0，Emscripten 6.0.13

本文件记录“浏览器双客户端联机”和“TLS/WSS 终止”的验证拓扑、可复现步骤与
结果。自动化使用 Playwright，通过 `window.__reversiTest` 的只读接口
（`screenIndex`、`netPhase`、`netCode`）判定状态。

## 1. 拓扑

```text
浏览器 A ── https://localhost:8443 ── nginx(静态, TLS)
         └ wss://localhost:8444 ───── nginx(TLS) ── 127.0.0.1:8080 reversi_server
浏览器 B ── 同上
```

页面与 WSS 使用不同端口，避免与静态站点根路径冲突；Web 客户端按页面协议
自动选择 `wss://`。服务端仍只监听回环地址，公网不可达。

## 2. 自签证书与 nginx

```bash
mkdir -p ~/rv-e2e
openssl req -x509 -newkey rsa:2048 -nodes -days 2 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
  -keyout ~/rv-e2e/key.pem -out ~/rv-e2e/cert.pem
```

`/ABS/PATH/rv-e2e/nginx.conf`（nginx 不展开 `~`，需写绝对路径；日志路径省略）：

```nginx
events { worker_connections 128; }
http {
    include /opt/homebrew/etc/nginx/mime.types;  # 路径随安装位置调整
    types { application/wasm wasm; }
    map $http_upgrade $connection_upgrade { default upgrade; '' close; }
    server {                     # 静态页面（HTTPS）
        listen 8443 ssl;
        server_name localhost;
        ssl_certificate     /ABS/PATH/rv-e2e/cert.pem;
        ssl_certificate_key /ABS/PATH/rv-e2e/key.pem;
        root  /path/to/repo/web/dist;
        index index.html;
        location / { try_files $uri $uri/ /index.html; }
    }
    server {                     # WSS 终止并转发到回环服务端
        listen 8444 ssl;
        server_name localhost;
        ssl_certificate     /ABS/PATH/rv-e2e/cert.pem;
        ssl_certificate_key /ABS/PATH/rv-e2e/key.pem;
        location / {
            proxy_pass http://127.0.0.1:8080;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection $connection_upgrade;
            proxy_read_timeout 300s;
        }
    }
}
```

注意：静态服务器必须为 `.wasm` 返回 `application/wasm`。Homebrew nginx 的
默认 `mime.types` **不含** `.wasm`，实测会返回 `text/plain`，浏览器回退到
ArrayBuffer 实例化（仍可运行但会打印错误）。上面的配置显式补充了该类型。

启动：

```bash
scripts/build-web.sh
./build/bin/reversi_server --bind 127.0.0.1 --port 8080
nginx -c /ABS/PATH/rv-e2e/nginx.conf -p /ABS/PATH/rv-e2e
```

## 3. TLS/WSS 证据

```bash
openssl s_client -connect 127.0.0.1:8444 -servername localhost </dev/null
# subject=CN=localhost; issuer=CN=localhost
# Protocol: TLSv1.3; Cipher: TLS_AES_256_GCM_SHA384
# Verify return code: 18 (self-signed certificate)

printf 'GET / HTTP/1.1\r\nHost: localhost:8444\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n' \
  | timeout 3 openssl s_client -quiet -connect 127.0.0.1:8444 -servername localhost
# HTTP/1.1 101 Switching Protocols
# Server: nginx/1.31.8
# timeout 用于在收到 101 后主动结束（s_client 会保持连接）。
```

浏览器侧：页面为 `https://localhost:8443`，控制台无混合内容错误；
nginx access log 记录每次连接的 `GET / HTTP/1.1" 101`，User-Agent 为
HeadlessChrome/155.0.0.0。

## 4. 浏览器双客户端流程

已提交脚本 `scripts/web-online-e2e.mjs`（Playwright 不随仓库分发）：

```bash
mkdir -p ~/rv-e2e/pw && cd ~/rv-e2e/pw && npm init -y
PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm i playwright
npx playwright install firefox webkit      # 可选：非 Chrome 引擎
cd /path/to/repo
NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-online-e2e.mjs \
  --browser chrome \
  --base https://localhost:8443/index.html --ws-port 8444 \
  --shots ~/rv-e2e
```

脚本用两个**独立 BrowserContext**（`ignoreHTTPSErrors: true`，
viewport 1100×800），每个上下文一个页面；同一上下文开两个页面会被测试
环境的页面回收策略关闭。坐标按 canvas 的 CSS/像素缩放换算；界面高度
637 时走紧凑布局，棋盘坐标按 `game_layout` 的窄屏公式计算。
脚本通过 `__reversiTest.screenIndex/netPhase/netCode/netSeq` 断言，
`--shots` 可选；`--browser chrome|firefox|webkit` 选择引擎；任一步失败则
以非零码退出。清空端口输入框时按键保持 90 ms：Web 端按帧采样按键，
自动化工具的瞬时 down+up 可能落在同一帧被丢弃（人工输入不受影响）。终局后脚本会关闭对手上下文，
再断言本方仍保留 GAME/FINISHED 与结果。

步骤与判定：

| 步骤 | 判定 | 结果 |
|---|---|---|
| 打开两页并进入 Online | `screenIndex() == 10`（NET） | PASS |
| 端口改为 8444 并 Connect | `netPhase() == 2`（LOBBY） | PASS |
| A 建房 | `netPhase() == 3`（WAITING），`netCode()` 非空 | PASS |
| B 切白方并加入 | 双方 `netPhase() == 3` | PASS |
| 双方 Ready | `screenIndex() == 2`（GAME），`netPhase() == 4` | PASS |
| A 点 d3 | 双方 `netSeq() == 1`；B 棋盘显示 d3 黑子、d4 翻黑，`Last move d3` | PASS |
| B 点 Resign | 双方 `gameOver() == true`、`result() == 1`（BLACK_WIN），`netPhase() == 5` | PASS |
| B 关闭浏览器 | A 仍为 GAME/FINISHED、`BLACK_WIN`，`netCode()` 清空（证明 ROOMCLOSED 已处理） | PASS |

脚本每次生成新房间码。Phase 14 审计运行 `EZJ69N`；Phase 15 三个引擎
分别为 Chrome `38RSUR`、Firefox `45H8XY`、WebKit `V3U9MB`，并经
`deploy/nginx/reversi.conf` 同构的本地配置（9443/9444，HSTS + 限速）
复跑 Chrome 得到 `9J25DT`。终局 HUD 文本为
`Game over: Black wins (resign)`（自然终局的 `normal` 状态不再显示）。截图与 nginx 访问日志保留在本机 `~/rv-e2e/`，未纳入仓库；
服务端日志需自行重定向，默认写入 stderr。

## 5. 桌面 ↔ Web 联机

拓扑：桌面应用（原生窗口）连接 `ws://127.0.0.1:8080`（默认服务器/端口），
浏览器经 `wss://localhost:8444` 加入同一房间。桌面创建房间（黑方），
浏览器选白方加入；桌面 Ready，浏览器 Ready；桌面落子 d3；浏览器投降。

桌面端在 macOS 上以窗口自动化执行：

- 通过 `CGWindowListCopyWindowInfo` 找到窗口并 `screencapture -l<id>` 按窗口
  截图（Retina 2×；raylib 坐标 = 像素/2 − 标题栏 32pt）。
- 通过 `CGEventPost`/`postToPid` 投递鼠标事件。GLFW 忽略非活动窗口的按键，
  因此验证期间会短暂把 Reversi 置前；事件按窗口/进程定向，结束后恢复原
  应用，不会向其他应用输入文本。
- 浏览器使用 Playwright，连接 WSS 并加入给定房间码。

结果（2026-10-28，Phase 14）：

| 项 | 结果 |
|---|---|
| 房间 | `HVVFKP`（完整流程 + 浏览器立即关闭后保留）；早期 `7SUYW3`/`2N9BLT` 为方法调试 |
| 桌面 | `ws://127.0.0.1:8080`，黑方（host），ping 17–27 ms |
| 浏览器 | `wss://localhost:8444`，白方 |
| 双方 Ready | 双方进入 GAME，桌面显示 “Black to move” |
| 桌面 d3 | 浏览器 `netSeq()==1`；桌面显示 d3 黑子、d4 翻黑、4:1 |
| 浏览器投降 | 浏览器 `BLACK_WIN`、FINISHED；桌面 HUD `Game over: Black wins (resign)` |
| 浏览器立即关闭 | 桌面仍保留 GAME/FINISHED 与 `Black wins (resign)`；HUD 为 `Online room closed … opponent left` |

Phase 15 复验（2026-10-29）：桌面 `build-gui/bin/Reversi.app`（当前源码）
经 `ws://127.0.0.1:8080` 建房（黑，host），Playwright Chrome 经
`wss://127.0.0.1:8444` 加入（白）。房间 `D7AKVC`：

| 步骤 | 证据 |
|---|---|
| 双方就座并 Ready | 服务端日志 `room D7AKVC created/joined/started`；双方 GAME/PLAYING |
| 桌面 d3 | 浏览器 `seq 1`；桌面 `Last move d3`、4:1 |
| 浏览器投降 | 浏览器 `{phase:5, over:true, result:1}`；桌面 `Game over: Black wins (resign)` |
| 浏览器关闭 | 桌面 HUD `Online room closed … opponent left`，棋盘与 `Black wins (resign)` 保留 |

桌面坐标按窗口 bounds（1100×832，标题栏 32 pt）与 raylib 逻辑坐标换算；
窗口需经 `AXRaise`/激活后才能接收系统级点击。该流程仍是手工/主机脚本
（`~/rv-e2e/cgclick2.swift`、`browser_join.mjs`），未纳入仓库与 CI。

## 6. 验证中发现并修复的问题

1. **会话只在转场期间轮询**：`net_session_update` 原先仅在
   `app_update_passive` 调用，稳定态大厅/对局不轮询，界面永远 “Offline”。
   已移到 `app_update` 每帧一次。
2. **终局 HUD 拼接缺分隔**：`Black wins` 与 `resign` 直接相连，已改为
   `Black wins (resign)`。
3. **Web 壳页文案过时**：“No account, no network sync.” 与在线功能矛盾，
   已改为说明需要自托管服务器。
4. 为浏览器自动化增加只读访问器 `reversi_web_net_code` /
   `reversi_web_net_phase`（`__reversiTest.netCode/netPhase`）。

## 7. Phase 15 补充检查

```bash
# 接收队列溢出：注入 40 条服务端消息（> 32 条队列）
NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-transport-failure-check.mjs \
  --browser chrome
# 期望：netPhase 7（FAILED）→ 重新 Connect 后 2（LOBBY）
```

三引擎均为 `phaseAfterOverflow=7`、`phaseAfterReconnect=2`（第二次点击
Connect），并对连接中的套接字注入“不可发送”状态（`bufferedAmount` 超限），
同样得到 `phaseAfterSendFailure=7`、重连后 `2`：证明浏览器端不再静默丢
消息，且发送失败与手动重连都会关闭旧套接字。该检查
运行在大厅（`active=false`），因此断言的是“失败可见 + 手动恢复”；房间内
（`active=true`）的自动重连由会话层用例覆盖。实现细节与限制见
`docs/PHASE_15_REPORT.md`。

## 8. 未覆盖

- Safari、iOS Safari、Android Chrome；真实设备与移动端。
- 可信证书链、HSTS 预加载、公网 DNS。
- 公网部署、NAT/防火墙、跨网络延迟与丢包。
- 浏览器端自动重连 UI 的断网长时抖动（只有会话层与重启用例）。
- 脚本未纳入 CI（需要本地 nginx/TLS 与服务端）。
