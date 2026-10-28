# Web 在线联机与 TLS 本地验证

日期：2026-10-28  
范围：本地回环验证；**未部署公网、未购买云资源**  
环境：macOS 26.6.2 arm64，Chrome 155（Headless，Playwright channel
`chrome`），nginx 1.31.8（Homebrew），OpenSSL 3.x，Emscripten 6.0.13

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

注意：静态服务器必须为 `.wasm` 返回 `application/wasm`（nginx 默认类型表
已包含；缺失时 Emscripten 回退到 ArrayBuffer 仍可运行）。

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
  | openssl s_client -quiet -connect 127.0.0.1:8444 -servername localhost
# HTTP/1.1 101 Switching Protocols
# Server: nginx/1.31.8
```

浏览器侧：页面为 `https://localhost:8443`，控制台无混合内容错误；
nginx access log 记录每次连接的 `GET / HTTP/1.1" 101`，User-Agent 为
HeadlessChrome/155.0.0.0。

## 4. 浏览器双客户端流程

Playwright 用两个**独立 BrowserContext**（`ignoreHTTPSErrors: true`，
viewport 1100×800），每个上下文一个页面；不能用同一上下文开两个页面
（测试环境的页面会被回收）。坐标按 canvas 的 CSS/像素缩放换算；界面在
高度 637 时走紧凑布局，棋盘坐标需按 `game_layout` 的窄屏公式计算。

步骤与判定：

| 步骤 | 判定 | 结果 |
|---|---|---|
| 打开两页并进入 Online | `screenIndex() == 10`（NET） | PASS |
| 端口改为 8444 并 Connect | `netPhase() == 2`（LOBBY） | PASS |
| A 建房 | `netPhase() == 3`（WAITING），`netCode()` 非空 | PASS（`MGQW82`） |
| B 切白方并加入 | 双方 `netPhase() == 3` | PASS |
| 双方 Ready | `screenIndex() == 2`（GAME），`netPhase() == 4` | PASS |
| A 点 d3 | B 棋盘显示 d3 黑子、d4 翻黑，状态 `Last move d3` | PASS |
| B 点 Resign | 双方 `gameOver() == true`、`result() == 1`（BLACK_WIN），`netPhase() == 5` | PASS |

终局 HUD 文本为 `Game over: Black wins (resign)`。截图与 nginx/server
日志保留在本机 `~/rv-e2e/`，未纳入仓库。

## 5. 验证中发现并修复的问题

1. **会话只在转场期间轮询**：`net_session_update` 原先仅在
   `app_update_passive` 调用，稳定态大厅/对局不轮询，界面永远 “Offline”。
   已移到 `app_update` 每帧一次。
2. **终局 HUD 拼接缺分隔**：`Black wins` 与 `resign` 直接相连，已改为
   `Black wins (resign)`。
3. **Web 壳页文案过时**：“No account, no network sync.” 与在线功能矛盾，
   已改为说明需要自托管服务器。
4. 为浏览器自动化增加只读访问器 `reversi_web_net_code` /
   `reversi_web_net_phase`（`__reversiTest.netCode/netPhase`）。

## 6. 未覆盖

- Safari/Firefox；真实设备与移动端；可信证书链、HSTS、生产代理配置。
- 公网部署、NAT/防火墙、跨网络延迟与丢包。
- 浏览器端断线重连与多局连续对战未做自动化。
