'use strict';

/*
 * Browser regression tests for the WebAssembly build.
 *
 *   REVERSI_WEB_URL=http://127.0.0.1:8080 \
 *   REVERSI_WEB_DEBUG_URL=http://127.0.0.1:8081 \
 *   node tests/web/run-tests.js [--case name]
 *
 * The debug URL is optional; without it the full-game autoplay case is
 * reported as SKIP. Requires the system Chrome (Playwright channel).
 */
const fs = require('fs');
const {
  VIEWPORTS, launch, openPage, geometry, toScreen, press, startLocal,
  startLocalMobile, startPve, startPveHard, playOpeningMove,
  waitForAiActive, waitForFrames, waitForMoves, autosaveMoves, assert
} = require('./lib');
const SCREENSHOT_DIR = 'tests/web/screenshots';
fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

const URL = process.env.REVERSI_WEB_URL || 'http://127.0.0.1:8080';
const DEBUG_URL = process.env.REVERSI_WEB_DEBUG_URL || '';
const ONLY = (() => {
  const index = process.argv.indexOf('--case');
  return index >= 0 ? process.argv[index + 1] : null;
})();

const results = [];

async function runCase(name, fn) {
  if (ONLY && ONLY !== name) {
    return;
  }
  const started = Date.now();
  try {
    const outcome = await fn();
    if (outcome === 'skip') {
      results.push({ name, status: 'SKIP', ms: Date.now() - started });
      console.log(`SKIP ${name}`);
      return;
    }
    results.push({ name, status: 'PASS', ms: Date.now() - started });
    console.log(`PASS ${name} (${Date.now() - started} ms)`);
  } catch (error) {
    results.push({ name, status: 'FAIL', ms: Date.now() - started,
                   error: String(error && error.message || error) });
    console.error(`FAIL ${name}: ${error && error.message || error}`);
  }
}

async function withPage(browser, fn, options) {
  const session = await openPage(browser, URL, options || {});
  try {
    await fn(session);
  } finally {
    await session.context.close();
  }
}

(async () => {
  const browser = await launch();
  try {
    await runCase('smoke', async () => {
      await withPage(browser, async ({ page, errors }) => {
        const geo = await geometry(page);
        const screen = await page.evaluate(() => window.__reversiTest.screen());
        assert(errors.length === 0, 'console/page errors: ' + errors.join(' | '));
        assert(screen.w === geo.size.w && screen.h === geo.size.h,
               `screen ${screen.w}x${screen.h} != canvas ${geo.size.w}x${geo.size.h}`);
        assert(screen.w > 0 && screen.h > 0, 'empty screen');
      });
    });

    await runCase('gameplay', async () => {
      await withPage(browser, async ({ page, errors }) => {
        const geo = await geometry(page);
        await startPveHard(page, geo);
        await playOpeningMove(page, geo);
        const active = await waitForAiActive(page);
        assert(active, 'AI search never became active');
        await page.waitForTimeout(2500);
        const stillActive = await page.evaluate(() => window.__reversiTest.aiActive());
        assert(!stillActive, 'AI search did not finish');
        assert(errors.length === 0, 'errors: ' + errors.join(' | '));
      });
    });

    await runCase('persistence', async () => {
      await withPage(browser, async ({ page, errors }) => {
        let geo = await geometry(page);
        await startLocal(page, geo);
        await playOpeningMove(page, geo);
        const keys = await page.evaluate(() => {
          const out = [];
          for (let i = 0; i < localStorage.length; i += 1) out.push(localStorage.key(i));
          return out;
        });
        assert(keys.includes('reversi:autosave.rvs'), 'autosave key missing');
        await page.reload();
        await page.waitForFunction(
          () => window.__reversiTest && window.__reversiTest.ready(),
          null, { timeout: 120000 });
        await page.waitForTimeout(500);
        geo = await geometry(page);
        await press(page, geo, 640, 217); // Continue
        const screen = await page.evaluate(() => window.__reversiTest.screenIndex());
        assert(screen === 2, `expected game screen after Continue, got ${screen}`);
        assert(errors.length === 0, 'errors: ' + errors.join(' | '));
      });
    });

    await runCase('responsive', async () => {
      for (const viewport of VIEWPORTS) {
        const session = await openPage(browser, URL, {
          context: {
            viewport: { width: viewport.width, height: viewport.height },
            deviceScaleFactor: viewport.dpr,
            isMobile: !!viewport.mobile,
            hasTouch: !!viewport.mobile
          }
        });
        try {
          const geo = await geometry(session.page);
          const metrics = await session.page.evaluate(() => {
            const canvas = document.getElementById('canvas');
            return {
              screen: window.__reversiTest.screen(),
              scrollHeight: document.documentElement.scrollHeight,
              innerHeight: window.innerHeight,
              client: { w: canvas.clientWidth, h: canvas.clientHeight }
            };
          });
          assert(metrics.screen.w === geo.size.w && metrics.screen.h === geo.size.h,
                 `${viewport.name}: screen/canvas mismatch`);
          assert(metrics.scrollHeight <= metrics.innerHeight + 1,
                 `${viewport.name}: page scrolls (${metrics.scrollHeight} > ${metrics.innerHeight})`);
          assert(metrics.client.w > 0 && metrics.client.h > 0,
                 `${viewport.name}: canvas not laid out`);
          assert(session.errors.length === 0,
                 `${viewport.name}: ${session.errors.join(' | ')}`);
          await session.page.screenshot({
            path: `${SCREENSHOT_DIR}/${viewport.name}.png`
          });
        } finally {
          await session.context.close();
        }
      }
    });

    await runCase('difficulties', async () => {
      for (const level of ['easy', 'medium', 'hard']) {
        const session = await openPage(browser, URL, {});
        try {
          const geo = await geometry(session.page);
          await startPve(session.page, geo, level);
          await playOpeningMove(session.page, geo);
          const replied = await waitForMoves(session.page, 2, 20000);
          const moves = await autosaveMoves(session.page);
          assert(replied,
                 `${level}: expected the AI reply to be recorded, got ${moves} moves`);
          assert(session.errors.length === 0,
                 `${level}: ${session.errors.join(' | ')}`);
        } finally {
          await session.context.close();
        }
      }
    });

    await runCase('mobile-touch', async () => {
      const session = await openPage(browser, URL, {
        context: {
          viewport: { width: 390, height: 844 },
          deviceScaleFactor: 3,
          isMobile: true,
          hasTouch: true
        }
      });
      try {
        const { page, context, errors } = session;
        const geo = await geometry(page);
        await startLocalMobile(page, geo);
        const before = await autosaveMoves(page);
        const client = await context.newCDPSession(page);
        const point = toScreen(geo, 159, 372); // legal d3 at 390x653
        let after = before;
        for (let attempt = 0; attempt < 3 && after <= before; attempt += 1) {
          await client.send('Input.dispatchTouchEvent', {
            type: 'touchStart',
            touchPoints: [{ x: point.x, y: point.y, radiusX: 4, radiusY: 4,
                            force: 1, id: 1 }]
          });
          await waitForFrames(page, 2);
          await page.waitForTimeout(150);
          await client.send('Input.dispatchTouchEvent', {
            type: 'touchEnd', touchPoints: []
          });
          await waitForFrames(page, 2);
          await page.waitForTimeout(700);
          after = await autosaveMoves(page);
        }
        assert(after > before,
               `touch move not persisted (${before} -> ${after})`);
        assert(errors.length === 0, 'errors: ' + errors.join(' | '));
      } finally {
        await session.context.close();
      }
    });

    await runCase('pagehide', async () => {
      await withPage(browser, async ({ page, errors }) => {
        const geo = await geometry(page);
        await startLocal(page, geo);
        await playOpeningMove(page, geo);
        await page.evaluate(() => localStorage.removeItem('reversi:autosave.rvs'));
        await page.evaluate(() => window.dispatchEvent(new Event('pagehide')));
        await page.waitForTimeout(300);
        const moves = await autosaveMoves(page);
        assert(moves >= 1, 'pagehide did not persist the game');
        assert(errors.length === 0, 'errors: ' + errors.join(' | '));
      });
    });

    await runCase('cancel', async () => {
      await withPage(browser, async ({ page, errors }) => {
        const geo = await geometry(page);
        const workers = [];
        page.on('worker', (worker) => workers.push(worker));
        await startPveHard(page, geo);
        await playOpeningMove(page, geo);
        const active = await waitForAiActive(page);
        assert(active, 'AI search never became active');
        for (let i = 0; i < 50 && workers.length === 0; i += 1) {
          await page.waitForTimeout(20);
        }
        assert(workers.length > 0, 'no Web Worker was created');
        await page.evaluate(() => window.__reversiTest.cancelAi());
        await page.waitForTimeout(400);
        const aiActive = await page.evaluate(() => window.__reversiTest.aiActive());
        assert(!aiActive, 'AI still active after cancel');
        let terminated = false;
        try {
          await workers[0].evaluate(() => 1);
        } catch (error) {
          terminated = true;
        }
        assert(terminated, 'worker was not terminated');
        const before = await page.evaluate(() => window.__reversiTest.frames());
        await page.waitForTimeout(500);
        const after = await page.evaluate(() => window.__reversiTest.frames());
        assert(after > before, 'main loop stalled after cancel');
        assert(errors.length === 0, 'errors: ' + errors.join(' | '));
      });
    });

    await runCase('autoplay', async () => {
      if (!DEBUG_URL) {
        console.log('  (set REVERSI_WEB_DEBUG_URL to run the full-game case)');
        return 'skip';
      }
      const session = await openPage(browser, DEBUG_URL, {});
      try {
        const started = await session.page.evaluate(
          () => window.__reversiTest.autoplay(7));
        assert(started, 'autoplay hook unavailable (needs a Debug build)');
        await session.page.waitForFunction(
          () => window.__reversiTest.gameOver(), null, { timeout: 120000 });
        const result = await session.page.evaluate(
          () => window.__reversiTest.result());
        assert(result >= 0 && result <= 3, 'invalid game result: ' + result);
        assert(session.errors.length === 0,
               'errors: ' + session.errors.join(' | '));
      } finally {
        await session.context.close();
      }
    });
  } finally {
    await browser.close();
  }

  const failed = results.filter((entry) => entry.status === 'FAIL');
  const skipped = results.filter((entry) => entry.status === 'SKIP');
  console.log('\nSummary:');
  for (const entry of results) {
    console.log(`  ${entry.status} ${entry.name} (${entry.ms} ms)`
      + (entry.error ? `\n    ${entry.error}` : ''));
  }
  if (skipped.length > 0 && process.env.REVERSI_WEB_ALLOW_SKIP !== '1') {
    console.error('Skipped cases make the run incomplete; set '
      + 'REVERSI_WEB_ALLOW_SKIP=1 to accept.');
    process.exit(1);
  }
  process.exit(failed.length === 0 ? 0 : 1);
})();
