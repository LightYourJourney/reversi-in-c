#!/usr/bin/env node
/*
 * Process-level graceful-shutdown check.
 *
 * Connects over WSS to a running server, creates a room, sends SIGTERM to the
 * server process and asserts that the client receives
 * `roomclosed status=shutdown` instead of a bare TCP reset.
 *
 *   NODE_TLS_REJECT_UNAUTHORIZED=0 node scripts/check-server-shutdown.mjs \
 *     --pid <server pid> --url wss://127.0.0.1:9444/
 *
 * The TLS warning is expected: this is a local self-signed test endpoint.
 */
const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : fallback;
}

const pid = Number(arg("--pid", "0"));
const url = arg("--url", "wss://127.0.0.1:9444/");
if (!pid) {
  console.error("usage: check-server-shutdown.mjs --pid <server pid> [--url ...]");
  process.exit(2);
}

const failures = [];
const socket = new WebSocket(url);
let room = null;
let closed = null;
let done = false;

const finish = (code) => {
  if (done) return;
  done = true;
  console.log(JSON.stringify({
    failures,
    room,
    closed,
  }, null, 2));
  process.exit(code);
};

const timer = setTimeout(() => {
  failures.push("timed out waiting for the shutdown notice");
  finish(1);
}, 10000);

socket.onerror = () => failures.push("websocket error");

socket.onopen = () => {
  socket.send("hello v=1");
  socket.send("create color=B");
};

socket.onmessage = (event) => {
  const text = String(event.data);
  if (text.startsWith("room ")) {
    const match = /code=([A-Z0-9]+)/.exec(text);
    room = match ? match[1] : text;
    setTimeout(() => {
      try {
        process.kill(pid, "SIGTERM");
      } catch (error) {
        failures.push(`kill failed: ${error}`);
        finish(1);
      }
    }, 200);
  } else if (text.startsWith("roomclosed ")) {
    closed = text;
    clearTimeout(timer);
    if (!text.includes("status=shutdown")) {
      failures.push(`unexpected roomclosed: ${text}`);
    }
    finish(failures.length === 0 ? 0 : 1);
  }
};

socket.onclose = () => {
  if (!closed && !done) {
    failures.push("socket closed before the shutdown notice");
    clearTimeout(timer);
    finish(1);
  }
};
