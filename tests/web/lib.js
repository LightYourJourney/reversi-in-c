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

async function press(page, geo, x, y, hold = 250, waitAfter = 650) {
  const point = toScreen(geo, x, y);
  await page.mouse.move(point.x, point.y);
  await page.mouse.down();
  await page.waitForTimeout(hold);
  await page.mouse.up();
  if (waitAfter > 0) {
    await page.waitForTimeout(waitAfter);
  }
}

const DIFFICULTY_BUTTONS = {
  easy: [392, 385],
  medium: [640, 385],
  hard: [829, 385]
};

async function waitForScreen(page, index, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await page.evaluate(() => window.__reversiTest.screenIndex())
        === index) {
      return true;
    }
    await page.waitForTimeout(50);
  }
  return false;
}

/* Slow or contended machines can drop a press between frames; retry until the
   expected screen appears. */
async function pressUntilScreen(page, geo, x, y, index, attempts = 3) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    await press(page, geo, x, y, 400, 200);
    if (await waitForScreen(page, index, 8000)) {
      return true;
    }
  }
  return false;
}

async function startLocal(page, geo) {
  if (!(await pressUntilScreen(page, geo, 640, 261, 1))) {
    throw new Error('setup screen did not appear');
  }
  if (!(await pressUntilScreen(page, geo, 640, 501, 2))) {
    throw new Error('game screen did not appear');
  }
}

async function startLocalMobile(page, geo) {
  if (!(await pressUntilScreen(page, geo, 195, 264, 1))) {
    throw new Error('setup screen did not appear');
  }
  if (!(await pressUntilScreen(page, geo, 195, 487, 2))) {
    throw new Error('game screen did not appear');
  }
  await page.waitForTimeout(700); // let the page transition finish
}

async function startPve(page, geo, difficulty = 'hard') {
  const button = DIFFICULTY_BUTTONS[difficulty];
  if (!button) {
    throw new Error('unknown difficulty: ' + difficulty);
  }
  for (let attempt = 0; attempt < 3; attempt += 1) {
    if (!(await pressUntilScreen(page, geo, 640, 261, 1))) {
      throw new Error('setup screen did not appear');
    }
    await page.waitForTimeout(600); // let the setup transition finish
    await press(page, geo, 780, 204, 400); // Computer
    await press(page, geo, button[0], button[1], 400);
    if (!(await pressUntilScreen(page, geo, 640, 501, 2))) {
      throw new Error('game screen did not appear');
    }
    if (await page.evaluate(() => window.__reversiTest.mode()) === 1) {
      return;
    }
    // The Computer press was dropped; restart cleanly and try again.
    await page.reload();
    await page.waitForFunction(
      () => window.__reversiTest && window.__reversiTest.ready(),
      null, { timeout: 120000 });
    await page.waitForTimeout(500);
  }
  throw new Error('could not start a PvE game');
}

async function startPveHard(page, geo) {
  await startPve(page, geo, 'hard');
}

async function waitForFrames(page, delta, timeoutMs = 10000) {
  const start = await page.evaluate(() => window.__reversiTest.frames());
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const now = await page.evaluate(() => window.__reversiTest.frames());
    if (now >= start + delta) {
      return true;
    }
    await page.waitForTimeout(30);
  }
  return false;
}

async function waitForMoves(page, minimum, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await autosaveMoves(page) >= minimum) {
      return true;
    }
    await page.waitForTimeout(50);
  }
  return false;
}

async function autosaveMoves(page) {
  return page.evaluate(() => {
    const raw = localStorage.getItem('reversi:autosave.rvs');
    if (!raw) {
      return -1;
    }
    const match = /moves (\d+)/.exec(raw);
    return match ? parseInt(match[1], 10) : -1;
  });
}

async function playOpeningMove(page, geo) {
  // Cross-frame press: a short click can fall entirely between frames on a
  // contended machine. Confirm the move through the autosave before returning
  // so callers can poll for the (possibly very short) AI window.
  const point = toScreen(geo, 616, 379); // legal d3
  for (let attempt = 0; attempt < 3; attempt += 1) {
    await page.mouse.move(point.x, point.y);
    await page.mouse.down();
    await waitForFrames(page, 2);
    await page.waitForTimeout(80);
    await page.mouse.up();
    const deadline = Date.now() + 4000;
    while (Date.now() < deadline) {
      if (await autosaveMoves(page) >= 1) {
        return;
      }
      await page.waitForTimeout(50);
    }
  }
  throw new Error('opening move was not applied');
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
  waitForScreen,
  startLocal,
  startLocalMobile,
  startPve,
  startPveHard,
  playOpeningMove,
  waitForAiActive,
  waitForFrames,
  waitForMoves,
  autosaveMoves,
  assert
};
