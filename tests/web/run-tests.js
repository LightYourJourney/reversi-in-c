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
  VIEWPORTS, launch, openPage, geometry, press, startPveHard,
  playOpeningMove, waitForAiActive, assert
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
        await press(page, geo, 640, 261); // New game
        await press(page, geo, 640, 501); // Start game (local default)
        await page.waitForTimeout(1200);
        await press(page, geo, 616, 379); // legal d3
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
  console.log('\nSummary:');
  for (const entry of results) {
    console.log(`  ${entry.status} ${entry.name} (${entry.ms} ms)`
      + (entry.error ? `\n    ${entry.error}` : ''));
  }
  process.exit(failed.length === 0 ? 0 : 1);
})();
