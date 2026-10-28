/*
 * Shared helpers for the browser checks (web-online-e2e.mjs and
 * web-queue-overflow-check.mjs).
 *
 * Playwright is resolved through NODE_PATH, as documented in
 * docs/WEB_ONLINE_TESTING.md: NODE_PATH=~/rv-e2e/pw/node_modules.
 */
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

export function loadPlaywright() {
  return require("playwright");
}

export function arg(args, name, fallback) {
  const index = args.indexOf(name);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : fallback;
}

export async function launchBrowser(name) {
  const { chromium, firefox, webkit } = loadPlaywright();
  const engines = {
    chrome: { type: chromium, options: { channel: "chrome" } },
    chromium: { type: chromium, options: {} },
    firefox: { type: firefox, options: {} },
    webkit: { type: webkit, options: {} },
  };
  const engine = engines[name];
  if (!engine || !engine.type) {
    throw new Error(`unsupported browser ${name}`);
  }
  return engine.type.launch({ headless: true, ...engine.options });
}

export async function geom(page) {
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

/* Presses a point in logical screen coordinates, scaled to the canvas. The
 * hold spans several frames: the Web build samples input per frame. */
export async function clickR(page, x, y) {
  const g = await geom(page);
  await page.mouse.move(g.left + x * (g.bw / g.sw),
                        g.top + y * (g.bh / g.sh));
  await page.waitForTimeout(50);
  await page.mouse.down();
  await page.waitForTimeout(110);
  await page.mouse.up();
  await page.waitForTimeout(200);
}

export const screenIndex = (page) =>
  page.evaluate(() => window.__reversiTest.screenIndex());
export const netPhase = (page) => page.evaluate(() => window.__reversiTest.netPhase());
export const netSeq = (page) => page.evaluate(() => window.__reversiTest.netSeq());
export const netCode = (page) => page.evaluate(() => window.__reversiTest.netCode());

/* Lobby widget positions, mirroring src/ui/net_view.c. */
export function buttons(g) {
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

export async function openOnline(page) {
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
