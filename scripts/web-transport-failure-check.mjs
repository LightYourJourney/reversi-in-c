#!/usr/bin/env node
/*
 * Browser checks for the Web transport failure policies.
 *
 * 1. Receive-queue overflow: before the hardening the transport dropped a
 *    message silently when its 32-entry queue was full, leaving a stale board
 *    with no error. It now reports FAILED("queue_overflow") and closes the
 *    socket. The queue only fills between frames, which a normal server
 *    cannot produce, so the check injects the browser-side callback directly
 *    and still exercises the real C path.
 * 2. Send failure: a socket that cannot queue a frame (full buffer or a
 *    closing socket) must drop the connection and report FAILED("send_failed")
 *    instead of leaving the UI authoritative in its own head.
 *
 *   NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-transport-failure-check.mjs \
 *     [--browser chrome] [--base https://localhost:9443/index.html] \
 *     [--ws-port 9444]
 */
import {
  arg, buttons, clickR, geom, launchBrowser, loadPlaywright, netPhase,
  screenIndex,
} from "./web-e2e-lib.mjs";

try {
  loadPlaywright();
} catch {
  console.error("Playwright not found. See scripts/web-online-e2e.mjs.");
  process.exit(2);
}

const args = process.argv.slice(2);
const baseUrl = arg(args, "--base", "https://localhost:9443/index.html");
const wsPort = arg(args, "--ws-port", "9444");
const browserName = arg(args, "--browser", "chrome");
const viewport = { width: 1100, height: 800 };

const failures = [];
function check(name, condition, detail) {
  if (!condition) failures.push(detail ? `${name} (${detail})` : name);
}

const result = { failures };
let browser;
try {
  browser = await launchBrowser(browserName);
  const context = await browser.newContext({ ignoreHTTPSErrors: true, viewport });
  const page = await context.newPage();
  page.on("pageerror", (error) => failures.push(`pageerror: ${error}`));
  await page.goto(baseUrl, { waitUntil: "load" });
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready());
  await page.waitForTimeout(900);

  const g0 = await geom(page);
  const block = 9 * 40 + 8 * 6;
  const top = Math.min(Math.max(g0.sh * 0.3, 190), g0.sh - 70 - block);
  let onNet = false;
  for (let attempt = 0; attempt < 3 && !onNet; attempt += 1) {
    await clickR(page, g0.sw / 2, top + 3 * 46 + 20);
    await page.waitForTimeout(900);
    onNet = (await screenIndex(page)) === 10;
  }
  check("online screen", onNet);

  const b = buttons(await geom(page));
  await clickR(page, b.port[0], b.port[1]);
  for (let i = 0; i < 4; i += 1) {
    await page.keyboard.down("Backspace");
    await page.waitForTimeout(90);
    await page.keyboard.up("Backspace");
    await page.waitForTimeout(90);
  }
  await page.keyboard.type(wsPort);
  await clickR(page, b.connect[0], b.connect[1]);
  await page.waitForTimeout(1800);
  check("lobby connected", (await netPhase(page)) === 2);

  const injected = await page.evaluate(() => {
    const module = window.__reversiTest.module();
    if (!module || !module.ccall) {
      return -1;
    }
    // ccall converts the JS string to a C pointer; the exported function
    // itself takes a char*.
    for (let i = 0; i < 40; i += 1) {
      module.ccall("reversi_net_client_web_message", null, ["string"],
                   ["pong"]);
    }
    return 1;
  });
  check("overflow callback exported", injected !== -1);
  await page.waitForTimeout(600);
  result.phaseAfterOverflow = await netPhase(page);
  check("overflow reported as failed",
        result.phaseAfterOverflow === 7,
        String(result.phaseAfterOverflow));

  await clickR(page, b.connect[0], b.connect[1]);
  await page.waitForTimeout(1800);
  result.phaseAfterReconnect = await netPhase(page);
  check("reconnect works after overflow",
        result.phaseAfterReconnect === 2,
        String(result.phaseAfterReconnect));

  // A socket that cannot queue frames must be treated as a failure.
  const installed = await page.evaluate(() => {
    const socket = globalThis.__reversiNetSocket;
    if (!socket || typeof socket.send !== "function") {
      return false;
    }
    globalThis.__reversiNetSocket = {
      readyState: 1,
      bufferedAmount: 1 << 20,
      send() {},
      close() {},
    };
    return true;
  });
  check("connected socket present before send check", installed === true);
  let sendFailed = false;
  for (let attempt = 0; attempt < 40; attempt += 1) {
    await page.waitForTimeout(100);
    if ((await netPhase(page)) === 7) {
      sendFailed = true;
      break;
    }
  }
  result.phaseAfterSendFailure = await netPhase(page);
  check("send failure reported as failed",
        sendFailed && result.phaseAfterSendFailure === 7,
        String(result.phaseAfterSendFailure));

  await clickR(page, b.connect[0], b.connect[1]);
  await page.waitForTimeout(1800);
  result.phaseAfterSendReconnect = await netPhase(page);
  check("reconnect works after send failure",
        result.phaseAfterSendReconnect === 2,
        String(result.phaseAfterSendReconnect));
  await context.close();
} catch (error) {
  failures.push(String(error));
} finally {
  if (browser) await browser.close().catch(() => {});
}
console.log(JSON.stringify(result, null, 2));
process.exit(failures.length === 0 ? 0 : 1);
