#!/usr/bin/env node
/*
 * Non-destructive health check.
 *
 * Opens a WebSocket, sends hello + ping and waits for pong, then hangs up.
 * hello only flips a flag on the connection and ping answers with pong, so no
 * room, no seat and no player state is created: this is safe to run against a
 * live server as often as you like — unlike scripts/check-server-shutdown.mjs,
 * which sends SIGTERM and exists for staging rehearsals only.
 *
 *   node scripts/check-server-health.mjs --url ws://127.0.0.1:8080/
 *   NODE_TLS_REJECT_UNAUTHORIZED=0 node scripts/check-server-health.mjs \
 *     --url wss://ws.example.com/
 *
 * Exit 0 when the handshake completes within --timeout (default 3000 ms).
 */
const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : fallback;
}

const url = arg("--url", "ws://127.0.0.1:8080/");
const timeout = Number(arg("--timeout", "3000"));
const result = { url, reply: null, latency_ms: null, failures: [] };
let socket = null;
let done = false;

const finish = (code) => {
  if (done) return;
  done = true;
  try {
    if (socket) socket.close();
  } catch { /* already gone */ }
  console.log(JSON.stringify(result, null, 2));
  process.exit(code);
};

const timer = setTimeout(() => {
  result.failures.push(`no pong within ${timeout} ms`);
  finish(1);
}, timeout);

const started = Date.now();
try {
  socket = new WebSocket(url);
} catch (error) {
  result.failures.push(String(error));
  finish(1);
}

socket.onopen = () => {
  socket.send("hello v=1");
  socket.send("ping");
};
socket.onerror = () => result.failures.push("websocket error");

socket.onmessage = (event) => {
  const text = String(event.data);
  if (text.startsWith("pong")) {
    result.reply = text;
    result.latency_ms = Date.now() - started;
    clearTimeout(timer);
    finish(0);
    return;
  }
  result.failures.push(`unexpected reply: ${text.slice(0, 80)}`);
  clearTimeout(timer);
  finish(1);
};

socket.onclose = () => {
  if (result.reply === null) {
    result.failures.push("connection closed before pong");
    clearTimeout(timer);
    finish(1);
  }
};
