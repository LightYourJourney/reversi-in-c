#!/usr/bin/env node
/*
 * Android Chrome online play, driven through the Chrome DevTools Protocol
 * over adb. The emulator reaches the host as 10.0.2.2.
 *
 * Prerequisites:
 *   ~/android-sdk with platform-tools, emulator and a google_apis arm64 image
 *   avdmanager create avd -n rv -k "system-images;android-35;google_apis;arm64-v8a"
 *   emulator -avd rv -no-window -no-audio -no-snapshot -gpu swiftshader_indirect &
 *   adb shell "echo '_ --remote-debugging-port=9222' \
 *     > /data/local/tmp/chrome-command-line"
 *   adb shell am set-debug-app --persistent com.android.chrome
 *   adb forward tcp:9222 localabstract:chrome_devtools_remote
 *   build/bin/reversi_server on 127.0.0.1:8080
 *   scripts/serve-web.sh 8000
 *
 * Run:
 *   NODE_PATH=~/rv-e2e/pw/node_modules node scripts/android-web-e2e.mjs \
 *     --page http://10.0.2.2:8000/index.html --port 8080
 *
 * Android plays black (it creates the room), desktop Chrome joins as white.
 * Coordinates come from window.__reversiTest, so no screenshots are parsed.
 */
import { execFileSync } from "node:child_process";
import {
  arg, buttons, clickR, geom, launchBrowser, loadPlaywright, netCode,
  netPhase, netSeq, openOnline, screenIndex,
} from "./web-e2e-lib.mjs";

try {
  loadPlaywright();
} catch {
  console.error("Playwright not found. See scripts/web-online-e2e.mjs.");
  process.exit(2);
}

const args = process.argv.slice(2);
const pageUrl = arg(args, "--page", "http://10.0.2.2:8000/index.html");
const port = arg(args, "--port", "8080");
const cdpUrl = arg(args, "--cdp", "http://127.0.0.1:9222");
const viewport = { width: 1100, height: 800 };

const failures = [];
const check = (name, ok, detail) =>
  void (ok ? null : failures.push(detail ? `${name} (${detail})` : name));

const adb = (...argv) =>
  execFileSync("adb", argv, { encoding: "utf8" }).trim();

/* Menu geometry from src/ui/menu_view.c. */
function menuCenter(g, index) {
  const block = 9 * 40 + 8 * 6;
  const top = Math.min(Math.max(g.sh * 0.3, 190), g.sh - 70 - block);
  return [g.sw / 2, top + index * 46 + 20];
}

/* Board layout from src/ui/game_view.c (both branches). */
function cellCenter(g, row, col) {
  if (g.sw >= 1000 && g.sh >= 680) {
    const margin = 32;
    const panel = Math.min(320, Math.max(260, g.sw * 0.26));
    const availW = g.sw - margin * 2 - panel - 28;
    const availH = g.sh - margin * 2 - 28;
    let size = Math.min(availW, availH);
    let cell = Math.floor(size / 8);
    if (cell < 32) cell = 32;
    size = cell * 8;
    const bx = margin + (availW - size) / 2;
    const by = margin + (availH - size) / 2;
    return [bx + col * cell + cell / 2, by + row * cell + cell / 2];
  }
  const margin = g.sw < 520 ? 12 : 24;
  const topHeight = 74;
  const bottomHeight = g.sw < 520 ? 108 : 84;
  const areaTop = margin + topHeight + 12;
  const areaBottom = g.sh - margin - bottomHeight - 12;
  const availW = g.sw - margin * 2;
  const availH = areaBottom - areaTop;
  let size = Math.min(availW, availH - 24);
  let cell = Math.floor(size / 8);
  if (cell < 28) cell = 28;
  size = cell * 8;
  const bx = (g.sw - size) / 2;
  const by = areaTop + (availH - 24 - size) / 2;
  return [bx + col * cell + cell / 2, by + row * cell + cell / 2];
}

/* Taps go through CDP as real touch events; the canvas samples per frame, so
 * the press is held for a few frames like the other scripts do. */
async function tap(page, x, y) {
  const g = await geom(page);
  const cx = g.left + x * (g.bw / g.sw);
  const cy = g.top + y * (g.bh / g.sh);
  const client = await page.context().newCDPSession(page);
  await client.send("Input.dispatchTouchEvent", {
    type: "touchStart",
    touchPoints: [{ x: Math.round(cx), y: Math.round(cy) }],
  });
  await page.waitForTimeout(120);
  await client.send("Input.dispatchTouchEvent", {
    type: "touchEnd", touchPoints: [],
  });
  await page.waitForTimeout(220);
  await client.detach();
}

const result = { steps: [], failures };
let browser = null;
try {
  /* The emulator's Chrome is already running with the CDP socket exposed. */
  browser = await (await loadPlaywright().chromium).connectOverCDP(cdpUrl);
  const context = browser.contexts()[0];
  const pages = context.pages();
  const page = pages.length > 0 ? pages[0] : await context.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(String(error)));

  if (!pageUrl.startsWith("about:")) {
    adb("shell", "am", "start", "-a", "android.intent.action.VIEW",
        "-d", pageUrl, "com.android.chrome");
    for (let i = 0; i < 100; i += 1) {
      const url = page.url();
      if (url.includes("index.html")) {
        break;
      }
      await page.waitForTimeout(200);
    }
  }
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready(), null,
    { timeout: 120000 });
  await page.waitForTimeout(1000);
  check("android page loaded", page.url().includes("index.html"), page.url());
  result.androidUrl = page.url();

  const g = await geom(page);
  result.geom = g;
  check("android canvas has a size", g.sw > 0 && g.sh > 0,
        JSON.stringify(g));

  const [mx, my] = menuCenter(g, 3); // Online play
  await tap(page, mx, my);
  await page.waitForTimeout(1200);
  check("android online screen", (await screenIndex(page)) !== 0,
        String(await screenIndex(page)));
  result.steps.push("android on the online screen");

  const b = buttons(await geom(page));
  await tap(page, b.port[0], b.port[1]);
  await tap(page, b.connect[0], b.connect[1]);
  await page.waitForTimeout(2500);
  check("android lobby", (await netPhase(page)) === 2,
        String(await netPhase(page)));

  await tap(page, b.create[0], b.create[1]);
  await page.waitForTimeout(1200);
  const code = await netCode(page);
  result.code = code;
  check("android room code", /^[A-Z0-9]{6}$/.test(code || ""), code);
  result.steps.push("android created a room");

  /* Desktop Chrome answers the room. */
  const desktop = await launchBrowser("chrome");
  const desktopContext = await desktop.newContext({ viewport });
  const desktopPage = await desktopContext.newPage();
  desktopPage.on("pageerror", (error) => failures.push(`desktop: ${error}`));
  await desktopPage.goto(
    pageUrl.replace("10.0.2.2", "127.0.0.1"), { waitUntil: "load" });
  await desktopPage.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready());
  await desktopPage.waitForTimeout(900);
  const opened = await openOnline(desktopPage);
  check("desktop online screen", opened);

  const dg = await geom(desktopPage);
  const db = buttons(dg);
  await clickR(desktopPage, db.port[0], db.port[1]);
  for (let i = 0; i < 4; i += 1) {
    await desktopPage.keyboard.down("Backspace");
    await desktopPage.waitForTimeout(90);
    await desktopPage.keyboard.up("Backspace");
    await desktopPage.waitForTimeout(90);
  }
  await desktopPage.keyboard.type(port);
  await clickR(desktopPage, db.connect[0], db.connect[1]);
  await desktopPage.waitForTimeout(1800);
  check("desktop lobby", (await netPhase(desktopPage)) === 2);

  await clickR(desktopPage, db.color[0], db.color[1]);
  await desktopPage.waitForTimeout(300);
  await clickR(desktopPage, db.code[0], db.code[1]);
  await desktopPage.keyboard.type(code);
  await clickR(desktopPage, db.join[0], db.join[1]);
  await desktopPage.waitForTimeout(1200);
  check("desktop joined", (await netPhase(desktopPage)) === 3);

  await tap(page, b.ready[0], b.ready[1]);
  await desktopPage.waitForTimeout(400);
  await clickR(desktopPage, db.ready[0], db.ready[1]);
  await desktopPage.waitForTimeout(2000);
  check("android playing", (await netPhase(page)) === 4,
        String(await netPhase(page)));
  check("desktop playing", (await netPhase(desktopPage)) === 4,
        String(await netPhase(desktopPage)));
  check("both on the game screen", (await screenIndex(page)) === 2
        && (await screenIndex(desktopPage)) === 2);
  result.steps.push("both PLAYING");

  /* Android is black and moves first. */
  const [bx, by] = cellCenter(await geom(page), 5, 3); // d3
  await tap(page, bx, by);
  for (let i = 0; i < 40; i += 1) {
    await page.waitForTimeout(100);
    if (await netSeq(page) > 0) break;
  }
  check("android move reaches the server", (await netSeq(page)) === 1,
        String(await netSeq(page)));
  check("desktop sees the move", (await netSeq(desktopPage)) === 1,
        String(await netSeq(desktopPage)));
  result.steps.push("one move on both clients");

  result.pageErrors = errors;
  result.failures.push(...errors);
  await browser.close().catch(() => {});
  await desktop.close().catch(() => {});
} catch (error) {
  failures.push(String(error));
} finally {
  if (browser !== null) {
    await browser.close().catch(() => {});
  }
}
console.log(JSON.stringify(result, null, 2));
process.exit(failures.length === 0 ? 0 : 1);
