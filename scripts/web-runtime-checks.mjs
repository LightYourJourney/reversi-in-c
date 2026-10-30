#!/usr/bin/env node
/*
 * Runtime checks that the gameplay suite does not cover: text actually
 * reaches the canvas, the WebAudio path runs, and a browser with storage
 * disabled degrades to the banner instead of breaking.
 *
 * Run:
 *   NODE_PATH=~/rv-e2e/pw/node_modules node scripts/web-runtime-checks.mjs \
 *     --base https://localhost:8443/index.html            # TLS, self-signed
 *   ... --base http://127.0.0.1:8000/index.html           # plain HTTP
 *   --browser chrome|firefox|webkit   (default: all three, one process each)
 *
 * What this does NOT prove: that the sound is audible (only that the device,
 * all six cues and the running context are in place), that the font matches a
 * reference, or that a real device's storage behaves the same way.
 */
import { arg, clickR, geom, launchBrowser, loadPlaywright } from "./web-e2e-lib.mjs";

try {
  loadPlaywright();
} catch {
  console.error("Playwright not found. See scripts/web-online-e2e.mjs.");
  process.exit(2);
}

const args = process.argv.slice(2);
const baseUrl = arg(args, "--base", "http://127.0.0.1:8000/index.html");
const only = arg(args, "--browser", "");
const insecure = baseUrl.startsWith("https:");
const viewport = { width: 1200, height: 760 };
const engines = only ? [only] : ["chrome", "firefox", "webkit"];

/* Menu geometry from src/ui/menu_view.c. */
function menuCenter(g, index) {
  const block = 9 * 40 + 8 * 6;
  const top = Math.min(Math.max(g.sh * 0.3, 190), g.sh - 70 - block);
  return [g.sw / 2, top + index * 46 + 20];
}

/* Tracks every AudioContext the engine creates so the check can tell a
 * suspended context (autoplay policy) from a running one. */
const AUDIO_HOOK = () => {
  window.__audio = { contexts: 0, all: [] };
  const Native = window.AudioContext || window.webkitAudioContext;
  if (!Native) {
    return;
  }
  const wrap = function () {
    const context = new (Function.prototype.bind.apply(
      Native, [null].concat(Array.prototype.slice.call(arguments))))();
    window.__audio.contexts += 1;
    window.__audio.all.push(context);
    return context;
  };
  wrap.prototype = Native.prototype;
  window.AudioContext = wrap;
  window.webkitAudioContext = wrap;
};

const STORAGE_BLOCKED = () => {
  Object.defineProperty(window, "localStorage", {
    configurable: true,
    get() {
      throw new Error("storage disabled for the test");
    },
  });
};

async function openPage(browser, initScript) {
  const context = await browser.newContext({ viewport, ignoreHTTPSErrors: insecure });
  if (initScript) {
    await context.addInitScript(initScript);
  }
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", (error) => errors.push(String(error)));
  await page.goto(baseUrl, { waitUntil: "load" });
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready(), null,
    { timeout: 120000 });
  await page.waitForTimeout(700);
  return { context, page, errors };
}

/* Copies the WebGL canvas inside a frame and measures how much of the title
 * band is bright ink rather than background. */
async function inkRatio(page) {
  return page.evaluate(() => new Promise((resolve) => {
    requestAnimationFrame(() => {
      const source = document.getElementById("canvas");
      const copy = document.createElement("canvas");
      copy.width = source.width;
      copy.height = source.height;
      const context = copy.getContext("2d");
      context.drawImage(source, 0, 0);
      const top = Math.floor(copy.height * 0.18);
      const height = Math.floor(copy.height * 0.14);
      const data = context.getImageData(0, top, copy.width, height).data;
      let bright = 0;
      let total = 0;
      for (let i = 0; i < data.length; i += 4) {
        total += 1;
        const luma = 0.2126 * data[i] + 0.7152 * data[i + 1]
          + 0.0722 * data[i + 2];
        if (luma > 140) {
          bright += 1;
        }
      }
      resolve({ bright, total, ratio: total ? bright / total : 0 });
    });
  }));
}

async function checkEngine(name) {
  const browser = await launchBrowser(name);
  const result = { browser: name, failures: [] };
  try {
    /* Text + audio, in the normal page. */
    const run = await openPage(browser, AUDIO_HOOK);
    const g = await geom(await run.page);
    const ink = await inkRatio(run.page);
    result.ink = ink;
    if (!(ink.ratio > 0.004)) {
      result.failures.push(`title band has no ink (ratio ${ink.ratio.toFixed(4)})`);
    }
    /* The first gesture only unlocks the suspended context; let it resume
     * before asking for a cue, or the sound is dropped. */
    await run.page.keyboard.press("ArrowDown");
    await run.page.waitForTimeout(700);
    const [mx, my] = menuCenter(g, 1); // New game (0 is a disabled Continue)
    await clickR(run.page, mx, my);
    await run.page.waitForTimeout(1200);
    const audio = await run.page.evaluate(() => ({
      contexts: window.__audio.contexts,
      states: window.__audio.all.map((context) => context.state),
      ready: window.__reversiTest.audioReady(),
      screen: window.__reversiTest.screenIndex(),
    }));
    result.audio = audio;
    /* audioReady() is audio_init()'s verdict: it closes the device and
     * returns false if the device or any of the six synthesized cues fails,
     * so true means every cue is loaded and playable. */
    if (audio.ready !== true) {
      result.failures.push("audio_init() did not produce a ready device");
    }
    if (audio.contexts < 1) {
      result.failures.push("no AudioContext was created");
    }
    if (!audio.states.includes("running")) {
      result.failures.push(`no running audio context (${audio.states.join(",")})`);
    }
    if (audio.screen === 0) {
      result.failures.push("clicking New game did not change the screen");
    }
    result.pageErrors = run.errors;
    result.failures.push(...run.errors);
    await run.context.close();

    /* Storage disabled, in a separate context. */
    const blocked = await openPage(browser, STORAGE_BLOCKED);
    const banner = await blocked.page.evaluate(() => {
      const error = document.getElementById("error");
      return {
        hidden: error ? error.hidden : true,
        text: error ? error.textContent.trim() : "",
        screen: window.__reversiTest.screenIndex(),
        ready: window.__reversiTest.ready(),
      };
    });
    result.storage = banner;
    if (banner.hidden) {
      result.failures.push("storage-disabled banner was not shown");
    } else if (!/storage is unavailable/i.test(banner.text)) {
      result.failures.push(`unexpected banner text: ${banner.text}`);
    }
    if (!banner.ready) {
      result.failures.push("module did not start with storage disabled");
    }
    result.storageErrors = blocked.errors;
    result.failures.push(...blocked.errors);
    await blocked.context.close();
  } catch (error) {
    result.failures.push(String(error));
  } finally {
    await browser.close().catch(() => {});
  }
  return result;
}

const results = [];
for (const engine of engines) {
  results.push(await checkEngine(engine));
}
const failed = results.filter((entry) => entry.failures.length > 0);
console.log(JSON.stringify({ base: baseUrl, results }, null, 2));
process.exit(failed.length === 0 ? 0 : 1);
