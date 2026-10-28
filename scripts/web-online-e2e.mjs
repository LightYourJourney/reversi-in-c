#!/usr/bin/env node
/*
 * Browser two-client online play, verified over local HTTPS/WSS.
 *
 * Prerequisites:
 *   - build/bin/reversi_server on 127.0.0.1:8080
 *   - nginx (or any TLS terminator) serving web/dist on HTTPS and proxying
 *     WSS to the server; see docs/WEB_ONLINE_TESTING.md
 *   - Playwright installed where Node can resolve it:
 *       mkdir -p ~/rv-e2e/pw && cd ~/rv-e2e/pw && npm init -y
 *       PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm i playwright
 *     Run with:
 *       NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-online-e2e.mjs
 *
 * Uses the system Chrome channel (headless), so no Playwright browser download
 * is needed. Exits non-zero when any assertion fails.
 *
 * Covers: lobby connect, create/join, colour choice, ready, one move reaching
 * both clients (seq 1), resign, game over with BLACK_WIN and FINISHED phase.
 * Does not cover desktop<->web play, Safari/Firefox, or a trusted certificate.
 */
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch {
  console.error("Playwright not found. See the header of this file.");
  process.exit(2);
}

const args = process.argv.slice(2);
function arg(name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : fallback;
}
const baseUrl = arg("--base", "https://localhost:8443/index.html");
const wsPort = arg("--ws-port", "8444");
const shots = arg("--shots", "");
const viewport = { width: 1100, height: 800 };

const failures = [];
function check(name, condition, detail) {
  if (!condition) {
    failures.push(detail ? `${name} (${detail})` : name);
  }
}

async function openPage(browser, tag) {
  const context = await browser.newContext({
    ignoreHTTPSErrors: true,
    viewport,
  });
  const page = await context.newPage();
  page.on("pageerror", (error) => failures.push(`${tag} pageerror: ${error}`));
  await page.goto(baseUrl, { waitUntil: "load" });
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready(),
  );
  await page.waitForTimeout(900);
  return { context, page };
}

async function geom(page) {
  return page.evaluate(() => {
    const canvas = document.getElementById("canvas");
    const rect = canvas.getBoundingClientRect();
    const screen = window.__reversiTest.screen();
    return {
      left: rect.left,
      top: rect.top,
      bw: rect.width,
      bh: rect.height,
      sw: screen.w,
      sh: screen.h,
    };
  });
}

async function clickR(page, x, y) {
  const g = await geom(page);
  await page.mouse.move(
    g.left + x * (g.bw / g.sw),
    g.top + y * (g.bh / g.sh),
  );
  await page.waitForTimeout(50);
  await page.mouse.down();
  await page.waitForTimeout(110);
  await page.mouse.up();
  await page.waitForTimeout(200);
}

const screenIndex = (page) =>
  page.evaluate(() => window.__reversiTest.screenIndex());
const netPhase = (page) => page.evaluate(() => window.__reversiTest.netPhase());
const netSeq = (page) => page.evaluate(() => window.__reversiTest.netSeq());
const netCode = (page) => page.evaluate(() => window.__reversiTest.netCode());

async function openOnline(page) {
  const g = await geom(page);
  const block = 9 * 40 + 8 * 6;
  const top = Math.min(Math.max(g.sh * 0.3, 190), g.sh - 70 - block);
  for (let attempt = 0; attempt < 3; attempt += 1) {
    await clickR(page, g.sw / 2, top + 3 * 46 + 20);
    await page.waitForTimeout(900);
    if ((await screenIndex(page)) !== 0) {
      return true;
    }
  }
  return false;
}

function buttons(g) {
  const panel = Math.min(560, g.sw - 40);
  const x = (g.sw - panel) / 2;
  const top = Math.max(80, g.sh * 0.16);
  const bw = (panel - 16) / 3;
  return {
    port: [x + 175, top + 64],
    code: [x + 195, top + 110],
    connect: [x + bw / 2, top + 259],
    color: [x + bw + 8 + bw / 2, top + 259],
    create: [x + 2 * (bw + 8) + bw / 2, top + 259],
    join: [x + bw / 2, top + 305],
    ready: [x + bw + 8 + bw / 2, top + 305],
  };
}

async function connectLobby(page) {
  if (!(await openOnline(page))) {
    return null;
  }
  const g = await geom(page);
  const b = buttons(g);
  await clickR(page, b.port[0], b.port[1]);
  for (let i = 0; i < 4; i += 1) {
    await page.keyboard.press("Backspace");
  }
  await page.keyboard.type(wsPort);
  await clickR(page, b.connect[0], b.connect[1]);
  await page.waitForTimeout(1800);
  return { g, b };
}

// Board layout matches src/ui/game_view.c.
function cellCenter(g, row, col) {
  const w = g.sw;
  const h = g.sh;
  if (w >= 1000 && h >= 680) {
    const margin = 32;
    const panel = Math.min(320, Math.max(260, w * 0.26));
    const availW = w - margin * 2 - panel - 28;
    const availH = h - margin * 2 - 28;
    let size = Math.min(availW, availH);
    let cell = Math.floor(size / 8);
    if (cell < 32) cell = 32;
    size = cell * 8;
    const bx = margin + (availW - size) / 2;
    const by = margin + (availH - size) / 2;
    return [bx + col * cell + cell / 2, by + row * cell + cell / 2];
  }
  const margin = w < 520 ? 12 : 24;
  const topHeight = 74;
  const bottomHeight = w < 520 ? 108 : 84;
  const areaTop = margin + topHeight + 12;
  const areaBottom = h - margin - bottomHeight - 12;
  const availW = w - margin * 2;
  const availH = areaBottom - areaTop;
  let size = Math.min(availW, availH - 24);
  let cell = Math.floor(size / 8);
  if (cell < 28) cell = 28;
  size = cell * 8;
  const bx = (w - size) / 2;
  const by = areaTop + (availH - 24 - size) / 2;
  return [bx + col * cell + cell / 2, by + row * cell + cell / 2];
}

async function shot(page, name) {
  if (shots) {
    await page.screenshot({ path: `${shots}/${name}.png` });
  }
}

const result = { steps: [], failures };
let browser;
try {
  const browserType = chromium;
  browser = await browserType.launch({ headless: true, channel: "chrome" });
  const a = await openPage(browser, "A");
  const b = await openPage(browser, "B");

  const lobbyA = await connectLobby(a.page);
  const lobbyB = await connectLobby(b.page);
  check("A connected", lobbyA !== null);
  check("B connected", lobbyB !== null);
  check("A lobby phase", (await netPhase(a.page)) === 2);
  check("B lobby phase", (await netPhase(b.page)) === 2);
  await shot(a.page, "1-a-lobby");

  await clickR(a.page, lobbyA.b.create[0], lobbyA.b.create[1]);
  await a.page.waitForTimeout(900);
  const code = await netCode(a.page);
  result.code = code;
  check("room code", typeof code === "string" && code.length === 6, code);
  check("A waiting", (await netPhase(a.page)) === 3);

  await clickR(b.page, lobbyB.b.color[0], lobbyB.b.color[1]);
  await b.page.waitForTimeout(300);
  await clickR(b.page, lobbyB.b.code[0], lobbyB.b.code[1]);
  await b.page.keyboard.type(code);
  await clickR(b.page, lobbyB.b.join[0], lobbyB.b.join[1]);
  await b.page.waitForTimeout(1000);
  check("B waiting", (await netPhase(b.page)) === 3);
  check("A sees join", (await netPhase(a.page)) === 3);
  await shot(b.page, "2-b-joined");

  await clickR(a.page, lobbyA.b.ready[0], lobbyA.b.ready[1]);
  await a.page.waitForTimeout(500);
  await clickR(b.page, lobbyB.b.ready[0], lobbyB.b.ready[1]);
  await a.page.waitForTimeout(1800);
  check("A game screen", (await screenIndex(a.page)) === 2);
  check("B game screen", (await screenIndex(b.page)) === 2);
  check("A playing", (await netPhase(a.page)) === 4);
  check("B playing", (await netPhase(b.page)) === 4);
  check("A start seq", (await netSeq(a.page)) === 0);
  await shot(a.page, "3-a-start");
  await shot(b.page, "4-b-start");

  const [mx, my] = cellCenter(await geom(a.page), 5, 3); // d3
  await clickR(a.page, mx, my);
  await b.page.waitForTimeout(1400);
  check("A move seq", (await netSeq(a.page)) === 1);
  check("B move seq", (await netSeq(b.page)) === 1);
  await shot(b.page, "5-b-after-move");

  const g = await geom(b.page);
  await clickR(b.page, g.sw - 210 + 45, 27); // Resign
  await b.page.waitForTimeout(1600);
  check("A game over", (await a.page.evaluate(
    () => window.__reversiTest.gameOver())) === true);
  check("B game over", (await b.page.evaluate(
    () => window.__reversiTest.gameOver())) === true);
  check("A result black win", (await a.page.evaluate(
    () => window.__reversiTest.result())) === 1);
  check("B result black win", (await b.page.evaluate(
    () => window.__reversiTest.result())) === 1);
  check("A finished", (await netPhase(a.page)) === 5);
  check("B finished", (await netPhase(b.page)) === 5);
  await shot(a.page, "6-a-over");
  await shot(b.page, "7-b-over");

  result.steps.push("A/B LOBBY", "A WAITING", "B WAITING", "PLAYING",
                    "seq 1", "resign -> BLACK_WIN");

  // The loser closes the browser: the remaining client keeps the result.
  await b.context.close();
  await a.page.waitForTimeout(1500);
  const retained = await a.page.evaluate(() => ({
    screen: window.__reversiTest.screenIndex(),
    phase: window.__reversiTest.netPhase(),
    over: window.__reversiTest.gameOver(),
    result: window.__reversiTest.result(),
  }));
  result.retained = retained;
  check("A retains result after opponent closes",
        retained.screen === 2 && retained.phase === 5
        && retained.over === true && retained.result === 1,
        JSON.stringify(retained));
  await shot(a.page, "8-a-retained");
  await a.context.close();
} catch (error) {
  failures.push(String(error));
} finally {
  if (browser) {
    await browser.close().catch(() => {});
  }
}
console.log(JSON.stringify(result, null, 2));
process.exit(failures.length === 0 ? 0 : 1);
