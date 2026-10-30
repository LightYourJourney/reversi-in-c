#!/usr/bin/env node
/*
 * Real Safari online play: Safari is driven through safaridriver's WebDriver
 * endpoint and opposed by a Playwright Chrome client. Plays a full game and
 * checks both sides agree on the result.
 *
 * Prerequisites:
 *   sudo safaridriver --enable
 *   Safari > Develop > Allow Remote Automation
 *   safaridriver -p 4445 &
 *   build/bin/reversi_server on 127.0.0.1:8080
 *   scripts/serve-web.sh 8000    (plain HTTP, so the page's ws:// is allowed)
 *   Playwright resolvable, as in scripts/web-online-e2e.mjs
 *
 * Run (plain HTTP + ws://):
 *   NODE_PATH=~/rv-e2e/pw/node_modules node scripts/safari-online-e2e.mjs \
 *     --base http://127.0.0.1:8000/index.html --port 8080 \
 *     --driver http://127.0.0.1:4445 --shots /tmp/safari-shots
 * Run over the local TLS terminator (https://localhost:8443 + wss://:8444):
 *   ... --base https://localhost:8443/index.html --port 8444 --insecure
 * --insecure maps to the acceptInsecureCerts capability, so Safari accepts
 * the self-signed certificate for the page and its WebSocket without a
 * keychain change. It proves the WSS path, not certificate trust.
 *
 * Two Safari quirks shape this script:
 *   - WebDriver pointer actions deliver mousedown/mouseup but no mousemove,
 *     so the engine never learns where the pointer is. The page position is
 *     therefore set with a dispatched mousemove, and the click itself is a
 *     real HID event (scripts/macos-click.swift) at the mapped screen point.
 *   - The lobby defaults (port 8080, colour black) mean Safari needs no
 *     keyboard input: it creates the room and plays black, Chrome joins white.
 *
 * HTTP rather than the local self-signed TLS: Safari has no equivalent of
 * --ignore-certificate-errors, so a self-signed page would test certificate
 * trust rather than the game.
 */
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
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

const here = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const baseUrl = arg(args, "--base", "http://127.0.0.1:8000/index.html");
const wsPort = arg(args, "--port", "8080");
const driverUrl = arg(args, "--driver", "http://127.0.0.1:4445");
const shots = arg(args, "--shots", "");
const insecure = args.includes("--insecure");
const viewport = { width: 1200, height: 760 };

const failures = [];
const check = (name, ok, detail) =>
  void (ok ? null : failures.push(detail ? `${name} (${detail})` : name));

/* ---- real-input helper ----------------------------------------------- */

function buildClicker() {
  const source = join(here, "macos-click.swift");
  const binary = join(tmpdir(), "reversi-macos-click");
  if (!existsSync(binary) || statSync(binary).mtimeMs < statSync(source).mtimeMs) {
    execFileSync("swiftc", ["-O", source, "-o", binary], { stdio: "inherit" });
  }
  return binary;
}

/* ---- WebDriver client ------------------------------------------------ */

class SafariDriver {
  constructor(base) {
    this.base = base.replace(/\/$/, "");
    this.id = null;
  }

  async call(method, path, body) {
    const res = await fetch(this.base + path, {
      method,
      headers: { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const json = await res.json().catch(() => ({}));
    const value = json.value;
    if (!res.ok || (value && value.error)) {
      throw new Error(`${method} ${path}: ${res.status} ${JSON.stringify(value)}`);
    }
    return value;
  }

  async start(insecure) {
    const value = await this.call("POST", "/session", {
      capabilities: { alwaysMatch: { browserName: "safari", acceptInsecureCerts: insecure } },
    });
    this.id = value.sessionId;
  }

  /* W3C key actions: Safari delivers these to the page even though its
   * pointer actions are incomplete. */
  keys(values) {
    const actions = [];
    for (const value of values) {
      actions.push({ type: "keyDown", value }, { type: "keyUp", value });
    }
    return this.call("POST", `/session/${this.id}/actions`, {
      actions: [{ type: "key", id: "keyboard", actions }],
    });
  }

  async quit() {
    if (this.id !== null) {
      await this.call("DELETE", `/session/${this.id}`).catch(() => {});
      this.id = null;
    }
  }

  navigate(url) {
    return this.call("POST", `/session/${this.id}/url`, { url });
  }

  windowRect() {
    return this.call("GET", `/session/${this.id}/window/rect`);
  }

  setRect(width, height, x, y) {
    return this.call("POST", `/session/${this.id}/window/rect`, {
      width, height, x, y,
    });
  }

  exec(script) {
    return this.call("POST", `/session/${this.id}/execute/sync`, {
      script, args: [],
    });
  }

  async screenshot(path) {
    const value = await this.call("GET", `/session/${this.id}/screenshot`);
    writeFileSync(path, Buffer.from(value, "base64"));
  }
}

const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/* ---- Safari page helpers --------------------------------------------- */

async function safariGeom(safari) {
  return safari.exec(`var c = document.getElementById('canvas');
    var r = c.getBoundingClientRect();
    var s = window.__reversiTest.screen();
    return { left: r.left, top: r.top, bw: r.width, bh: r.height,
             sw: s.w, sh: s.h, iw: window.innerWidth,
             ih: window.innerHeight, ch: window.outerHeight - window.innerHeight };`);
}

/* Sets the page pointer, then posts a real HID click at the mapped screen
 * point. Returns the logical position the engine reports back. */
async function safariClick(safari, clicker, pid, origin, geom, x, y) {
  const clientX = geom.left + x * (geom.bw / geom.sw);
  const clientY = geom.top + y * (geom.bh / geom.sh);
  const at = `clientX: ${clientX}, clientY: ${clientY}, bubbles: true, `
    + "cancelable: true, view: window";
  await safari.exec(`document.getElementById('canvas').dispatchEvent(
    new MouseEvent('mousemove', { ${at} })); return true;`);
  execFileSync(clicker, [String(pid),
                         String(Math.round(origin.x + clientX)),
                         String(Math.round(origin.y + geom.ch + clientY)),
                         "150", "click"], { stdio: "ignore" });
  await pause(200);
  return safari.exec("return window.__reversiTest.mouse();");
}

const safariScreen = (s) => s.exec("return window.__reversiTest.screenIndex();");
const safariPhase = (s) => s.exec("return window.__reversiTest.netPhase();");
const safariSeq = (s) => s.exec("return window.__reversiTest.netSeq();");
const safariCode = (s) => s.exec("return window.__reversiTest.netCode();");

async function safariOpenOnline(safari, clicker, pid, origin) {
  const g = await safariGeom(safari);
  const block = 9 * 40 + 8 * 6;
  const top = Math.min(Math.max(g.sh * 0.3, 190), g.sh - 70 - block);
  for (let attempt = 0; attempt < 3; attempt += 1) {
    await safariClick(safari, clicker, pid, origin, g, g.sw / 2, top + 3 * 46 + 20);
    await pause(900);
    if ((await safariScreen(safari)) !== 0) {
      return true;
    }
  }
  return false;
}

/* ---- Reversi model (chooses legal moves without a test hook) ---------- */

const DIRS = [[-1, -1], [-1, 0], [-1, 1], [0, -1], [0, 1], [1, -1], [1, 0], [1, 1]];

function newBoard() {
  const board = Array.from({ length: 8 }, () => Array(8).fill(0));
  board[3][3] = 1; // d5 black
  board[3][4] = 2; // e5 white
  board[4][3] = 2; // d4 white
  board[4][4] = 1; // e4 black
  return board;
}

function flips(board, player, row, col) {
  if (board[row][col] !== 0) {
    return [];
  }
  const out = [];
  for (const [dr, dc] of DIRS) {
    const line = [];
    let r = row + dr;
    let c = col + dc;
    while (r >= 0 && r < 8 && c >= 0 && c < 8 && board[r][c] === 3 - player) {
      line.push([r, c]);
      r += dr;
      c += dc;
    }
    if (line.length > 0 && r >= 0 && r < 8 && c >= 0 && c < 8
        && board[r][c] === player) {
      out.push(...line);
    }
  }
  return out;
}

function legalMoves(board, player) {
  const out = [];
  for (let r = 0; r < 8; r += 1) {
    for (let c = 0; c < 8; c += 1) {
      if (flips(board, player, r, c).length > 0) {
        out.push([r, c]);
      }
    }
  }
  return out;
}

function apply(board, player, row, col) {
  for (const [r, c] of flips(board, player, row, col)) {
    board[r][c] = player;
  }
  board[row][col] = player;
}

function tally(board) {
  let black = 0;
  let white = 0;
  for (const row of board) {
    for (const cell of row) {
      if (cell === 1) black += 1;
      if (cell === 2) white += 1;
    }
  }
  return { black, white };
}

/* Mirrors game_layout() in src/ui/game_view.c: wide needs 1000x680. */
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

/* ---- main ------------------------------------------------------------ */

const result = { steps: [], failures, clicks: 0 };
let safari = null;
let browser = null;
try {
  if (shots) {
    mkdirSync(shots, { recursive: true });
  }
  const clicker = buildClicker();
  const safariPid = execFileSync("pgrep", ["-x", "Safari"]).toString().trim()
    .split("\n")[0];

  safari = new SafariDriver(driverUrl);
  await safari.start(insecure);
  await safari.setRect(viewport.width, viewport.height, 60, 60);
  await safari.navigate(baseUrl);
  for (let i = 0; i < 100; i += 1) {
    if (await safari.exec(
      "return !!(window.__reversiTest && window.__reversiTest.ready());")) {
      break;
    }
    await pause(100);
  }
  await pause(600);

  const rect = await safari.windowRect();
  const origin = { x: rect.x, y: rect.y };
  const geom0 = await safariGeom(safari);
  check("safari loaded", geom0.sw > 0, JSON.stringify(geom0));
  if (shots) await safari.screenshot(`${shots}/1-safari-menu.png`);

  browser = await launchBrowser("chrome");
  const context = await browser.newContext({ viewport, ignoreHTTPSErrors: insecure });
  const page = await context.newPage();
  page.on("pageerror", (error) => failures.push(`chrome pageerror: ${error}`));
  await page.goto(baseUrl, { waitUntil: "load" });
  await page.waitForFunction(
    () => window.__reversiTest && window.__reversiTest.ready());
  await page.waitForTimeout(900);

  check("safari online screen",
        await safariOpenOnline(safari, clicker, safariPid, origin));
  const cg = await geom(page);
  check("chrome online screen", await openOnline(page));
  result.steps.push("both on the online screen");

  const sg = await safariGeom(safari);
  const sb = buttons(sg);
  /* The lobby defaults to 8080; a TLS run terminates WSS on another port and
   * the field has to be retyped. Safari's key actions reach the page even
   * though its pointer actions do not. */
  if (wsPort !== "8080") {
    await safariClick(safari, clicker, safariPid, origin, sg,
                      sb.port[0], sb.port[1]);
    const keys = [];
    for (let i = 0; i < 8; i += 1) keys.push("\uE003");
    for (const digit of wsPort) keys.push(digit);
    await safari.keys(keys);
    await pause(400);
  }
  await safariClick(safari, clicker, safariPid, origin, sg, sb.connect[0], sb.connect[1]);
  await pause(1800);
  const phaseAfterConnect = await safariPhase(safari);
  check("safari lobby", phaseAfterConnect === 2, String(phaseAfterConnect));

  const cb = buttons(cg);
  await clickR(page, cb.port[0], cb.port[1]);
  for (let i = 0; i < 4; i += 1) {
    await page.keyboard.down("Backspace");
    await page.waitForTimeout(90);
    await page.keyboard.up("Backspace");
    await page.waitForTimeout(90);
  }
  await page.keyboard.type(wsPort);
  await clickR(page, cb.connect[0], cb.connect[1]);
  await page.waitForTimeout(1800);
  check("chrome lobby", (await netPhase(page)) === 2, String(await netPhase(page)));
  result.steps.push("both in the LOBBY");
  if (shots) await safari.screenshot(`${shots}/2-safari-lobby.png`);

  await safariClick(safari, clicker, safariPid, origin, sg, sb.create[0], sb.create[1]);
  await pause(1000);
  const code = await safariCode(safari);
  result.code = code;
  check("room code", typeof code === "string" && code.length === 6, code);
  check("safari waiting", (await safariPhase(safari)) === 3);

  await clickR(page, cb.color[0], cb.color[1]);
  await page.waitForTimeout(300);
  await clickR(page, cb.code[0], cb.code[1]);
  await page.keyboard.type(code);
  await clickR(page, cb.join[0], cb.join[1]);
  await page.waitForTimeout(1000);
  check("chrome waiting", (await netPhase(page)) === 3);
  check("safari sees join", (await safariPhase(safari)) === 3);

  await safariClick(safari, clicker, safariPid, origin, sg, sb.ready[0], sb.ready[1]);
  await pause(500);
  await clickR(page, cb.ready[0], cb.ready[1]);
  await page.waitForTimeout(1800);
  check("safari game screen", (await safariScreen(safari)) === 2);
  check("chrome game screen", (await screenIndex(page)) === 2);
  check("safari playing", (await safariPhase(safari)) === 4);
  check("chrome playing", (await netPhase(page)) === 4);
  result.steps.push("both PLAYING");
  if (shots) await safari.screenshot(`${shots}/3-safari-start.png`);

  const board = newBoard();
  let player = 1; // black = Safari (the creator's default colour)
  let moves = 0;
  let seq = 0;
  let stuck = false;

  const waitSeq = async () => {
    /* Both clients must reach the same seq before the next click: a client
     * that is one broadcast behind would reject the move as "not my turn". */
    for (let i = 0; i < 60; i += 1) {
      await pause(100);
      const [safariValue, chromeValue] = [await safariSeq(safari),
                                          await netSeq(page)];
      if (safariValue > seq && chromeValue > seq) {
        return safariValue === chromeValue ? safariValue : null;
      }
    }
    return null;
  };

  const stallReport = async () => {
    const [visibility, framesA, framesB, phase, chromeSeqNow] = [
      await safari.exec("return document.visibilityState;"),
      await safari.exec("return window.__reversiTest.frames();"),
      await pause(600).then(() => safari.exec("return window.__reversiTest.frames();")),
      await safariPhase(safari),
      await netSeq(page),
    ];
    return { visibility, framesA, framesB, phase,
             safariSeq: await safariSeq(safari), chromeSeq: chromeSeqNow };
  };

  while (moves < 70 && !stuck) {
    const options = legalMoves(board, player);
    if (options.length === 0) {
      if (legalMoves(board, 3 - player).length === 0) break;
      player = 3 - player; // the opponent has to pass
      continue;
    }
    let best = options[0];
    let bestFlips = -1;
    for (const [r, c] of options) {
      const n = flips(board, player, r, c).length;
      if (n > bestFlips) {
        bestFlips = n;
        best = [r, c];
      }
    }

    /* Retries stay on the same cell: a late-landing click must never let the
     * model apply a different move than the server did. */
    let landed = null;
    for (let attempt = 0; attempt < 2 && landed === null; attempt += 1) {
      if (player === 1) {
        const g = await safariGeom(safari);
        const [x, y] = cellCenter(g, best[0], best[1]);
        await safariClick(safari, clicker, safariPid, origin, g, x, y);
      } else {
        const g = await geom(page);
        const [x, y] = cellCenter(g, best[0], best[1]);
        await clickR(page, x, y);
      }
      landed = await waitSeq();
      if (landed !== null && player === 1) {
        result.clicks += 1;
      }
    }
    if (landed === null) {
      result.stall = await stallReport();
      if (shots) await safari.screenshot(`${shots}/stall.png`);
      failures.push(`no seq advance for ${player === 1 ? "black" : "white"} at `
                    + `${JSON.stringify(best)} after ${moves} moves: `
                    + JSON.stringify(result.stall));
      stuck = true;
      break;
    }
    seq = landed;
    apply(board, player, best[0], best[1]);
    moves += 1;
    player = 3 - player;
  }

  result.moves = moves;
  result.seq = seq;
  const expected = tally(board);
  const expectedResult = expected.black > expected.white ? 1
    : expected.white > expected.black ? 2 : 3;
  result.expected = expected;
  result.expectedResult = expectedResult;

  for (let i = 0; i < 40; i += 1) {
    await pause(100);
    if (await safari.exec("return window.__reversiTest.gameOver();")
        && await page.evaluate(() => window.__reversiTest.gameOver())) {
      break;
    }
  }

  const safariFinal = await safari.exec(`return {
    over: window.__reversiTest.gameOver(),
    result: window.__reversiTest.result(),
    phase: window.__reversiTest.netPhase(),
    seq: window.__reversiTest.netSeq(),
    screen: window.__reversiTest.screenIndex() };`);
  const chromeFinal = await page.evaluate(() => ({
    over: window.__reversiTest.gameOver(),
    result: window.__reversiTest.result(),
    phase: window.__reversiTest.netPhase(),
    seq: window.__reversiTest.netSeq(),
    screen: window.__reversiTest.screenIndex(),
  }));
  result.safari = safariFinal;
  result.chrome = chromeFinal;

  check("safari game over", safariFinal.over === true, JSON.stringify(safariFinal));
  check("chrome game over", chromeFinal.over === true, JSON.stringify(chromeFinal));
  check("safari finished", safariFinal.phase === 5);
  check("chrome finished", chromeFinal.phase === 5);
  check("agree on seq", safariFinal.seq === chromeFinal.seq,
        `${safariFinal.seq} vs ${chromeFinal.seq}`);
  check("seq equals moves", safariFinal.seq === moves,
        `${safariFinal.seq} vs ${moves}`);
  check("agree on result", safariFinal.result === chromeFinal.result);
  check("result matches board", safariFinal.result === expectedResult,
        `${safariFinal.result} vs ${expectedResult}`);
  if (shots) {
    await safari.screenshot(`${shots}/4-safari-over.png`);
    await page.screenshot({ path: `${shots}/5-chrome-over.png` });
  }
  result.steps.push("full game",
                    `result ${safariFinal.result} (${expected.black}-${expected.white})`);

  await context.close();
} catch (error) {
  failures.push(String(error));
} finally {
  if (safari !== null) {
    await safari.quit();
  }
  if (browser !== null) {
    await browser.close().catch(() => {});
  }
}
console.log(JSON.stringify(result, null, 2));
process.exit(failures.length === 0 ? 0 : 1);
