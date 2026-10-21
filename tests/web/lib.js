'use strict';

const { chromium } = require('playwright');

const CHROME_ARGS = [
  '--use-gl=angle',
  '--use-angle=swiftshader',
  '--enable-unsafe-swiftshader'
];

const VIEWPORTS = [
  { name: '1920x1080', width: 1920, height: 1080, dpr: 1 },
  { name: '1366x768', width: 1366, height: 768, dpr: 1 },
  { name: '768x1024', width: 768, height: 1024, dpr: 2, mobile: true },
  { name: '390x844', width: 390, height: 844, dpr: 3, mobile: true },
  { name: '360x640', width: 360, height: 640, dpr: 3, mobile: true }
];

async function launch() {
  return chromium.launch({
    channel: 'chrome',
    headless: true,
    args: CHROME_ARGS
  });
}

async function openPage(browser, url, options = {}) {
  const context = await browser.newContext(options.context || {
    viewport: options.viewport || { width: 1280, height: 800 }
  });
  const page = await context.newPage();
  const errors = [];
  page.on('pageerror', (error) => errors.push('pageerror: ' + String(error)));
  page.on('console', (message) => {
    if (message.type() === 'error') {
      errors.push('console: ' + message.text());
    }
  });
  if (options.initScript) {
    await page.addInitScript(options.initScript);
  }
  await page.goto(url, { waitUntil: 'load' });
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready(),
    null, { timeout: 120000 });
  await page.waitForTimeout(400);
  return { context, page, errors };
}

async function geometry(page) {
  const box = await page.locator('#canvas').boundingBox();
  const size = await page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    return { w: canvas.width, h: canvas.height };
  });
  return { box, size };
}

function toScreen(geo, x, y) {
  return {
    x: geo.box.x + x * geo.box.width / geo.size.w,
    y: geo.box.y + y * geo.box.height / geo.size.h
  };
}

async function press(page, geo, x, y, hold = 160, waitAfter = 650) {
  const point = toScreen(geo, x, y);
  await page.mouse.move(point.x, point.y);
  await page.mouse.down();
  await page.waitForTimeout(hold);
  await page.mouse.up();
  if (waitAfter > 0) {
    await page.waitForTimeout(waitAfter);
  }
}

async function startPveHard(page, geo) {
  await press(page, geo, 640, 261); // New game
  await press(page, geo, 780, 204); // Computer
  await press(page, geo, 829, 385); // Hard
  await press(page, geo, 640, 501); // Start game
  await page.waitForTimeout(1200);
}

async function playOpeningMove(page, geo) {
  // Return immediately: the caller polls for the AI search window, which is
  // shorter than a normal settle wait.
  await press(page, geo, 616, 379, 160, 0); // legal d3
}

async function waitForAiActive(page, timeoutMs = 4000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await page.evaluate(() => window.__reversiTest.aiActive())) {
      return true;
    }
    await page.waitForTimeout(20);
  }
  return false;
}

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

module.exports = {
  VIEWPORTS,
  launch,
  openPage,
  geometry,
  toScreen,
  press,
  startPveHard,
  playOpeningMove,
  waitForAiActive,
  assert
};
