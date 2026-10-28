#!/usr/bin/env node
/*
 * Browser check for the Web receive-queue overflow policy.
 *
 * Before v2.0.0 hardening the browser transport dropped a message silently
 * when its 32-entry queue was full, so the client kept displaying a stale
 * board with no error. The transport now reports FAILED("queue_overflow"),
 * closes the socket and the session either reconnects or shows the failure.
 *
 * The queue only fills if more messages arrive between two frames, which a
 * normal server cannot produce; this check injects the browser-side callback
 * directly, so it exercises the real C path.
 *
 *   NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-queue-overflow-check.mjs \
 *     [--browser chrome] [--base https://localhost:9443/index.html] \
 *     [--ws-port 9444]
 */
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
let chromium;
let firefox;
let webkit;
try {
  ({ chromium, firefox, webkit } = require("playwright"));
} catch {
  console.error("Playwright not found. See scripts/web-online-e2e.mjs.");
  process.exit(2);
}

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : fallback;
}
const baseUrl = arg("--base", "https://localhost:9443/index.html");
const wsPort = arg("--ws-port", "9444");
const browserName = arg("--browser", "chrome");
const viewport = { width: 1100, height: 800 };

const failures = [];
function check(name, condition, detail) {
  if (!condition) failures.push(detail ? `${name} (${detail})` : name);
}

async function geom(page) {
  return page.evaluate(() => {
    const canvas = document.getElementById("canvas");
    const rect = canvas.getBoundingClientRect();
    const screen = window.__reversiTest.screen();
    return { left: rect.left, top: rect.top, bw: rect.width, bh: rect.height,
             sw: screen.w, sh: screen.h };
  });
}

async function clickR(page, x, y) {
  const g = await geom(page);
  await page.mouse.move(g.left + x * (g.bw / g.sw),
                        g.top + y * (g.bh / g.sh));
  await page.waitForTimeout(50);
  await page.mouse.down();
  await page.waitForTimeout(110);
  await page.mouse.up();
  await page.waitForTimeout(200);
}

const netPhase = (page) => page.evaluate(() => window.__reversiTest.netPhase());
const screenIndex = (page) =>
  page.evaluate(() => window.__reversiTest.screenIndex());

function buttons(g) {
  const panel = Math.min(560, g.sw - 40);
  const x = (g.sw - panel) / 2;
  const top = Math.max(80, g.sh * 0.16);
  const bw = (panel - 16) / 3;
  return {
    port: [x + 175, top + 64],
    connect: [x + bw / 2, top + 259],
  };
}

const result = { failures };
let browser;
try {
  const engines = {
    chrome: { type: chromium, options: { channel: "chrome" } },
    chromium: { type: chromium, options: {} },
    firefox: { type: firefox, options: {} },
    webkit: { type: webkit, options: {} },
  };
  const engine = engines[browserName];
  if (!engine || !engine.type) throw new Error(`unsupported browser ${browserName}`);
  browser = await engine.type.launch({ headless: true, ...engine.options });
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
  await context.close();
} catch (error) {
  failures.push(String(error));
} finally {
  if (browser) await browser.close().catch(() => {});
}
console.log(JSON.stringify(result, null, 2));
process.exit(failures.length === 0 ? 0 : 1);
