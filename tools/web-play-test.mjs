#!/usr/bin/env node
// Headless Chrome test of the browser play page (docs/public/play/), through the page's own code:
// the folder is handed to its <input webkitdirectory> (DOM.setFileInputFiles, as if picked), it is
// checked, imported into OPFS and run in gasm's Worker mode; then the imported copy is run again
// (later visit), the folder is run without importing (File/Blob provider), and the disc image too.
// Every run must print the hashes gasm-run --headless prints for the same parameters.
//
//   cd docs && pnpm build && cd ..
//   node tools/web-play-test.mjs [cd folder] [disc image] [out dir]
// Defaults: ./cd, $OPENRF_IMAGE, /tmp/openrf-play. Needs Chrome
// (CHROME=<binary> to override). Serves docs/.vitepress/dist on :8792. Never opens a window.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const CD = resolve(process.argv[2] ?? join(repo, 'cd'));
const imgArg = process.argv[3] ?? process.env.OPENRF_IMAGE;   // the user's disc image (.bin/.iso), optional
const IMG = imgArg ? resolve(imgArg) : '';
const OUT = resolve(process.argv[4] ?? '/tmp/openrf-play');
const DIST = join(repo, 'docs/.vitepress/dist');
const PORT = 8792, DEBUG = 9335;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const RUN = process.env.GASM_RUN ?? join(repo, '.deps/gasm-runner-macos-universal/gasm-run');   // tools/fetch-gasm-runner.sh
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
mkdirSync(OUT, { recursive: true });
if (!existsSync(join(DIST, 'play/openrf.wasm'))) { console.error('build the site first (docs/scripts/copy-wasm.sh, pnpm build)'); process.exit(1); }

const CASES = [
  { name: 'fire', frames: 689, params: 'skip_intro=1&play=1&demo=fire' },
  { name: '2p', frames: 900, params: 'skip_intro=1&play2=1&demo=2p' },
];

// Reference hashes from the native runner (same module, same parameters).
function native(c) {
  if (!existsSync(RUN)) return null;
  const args = [join(repo, 'build-gasm/openrf.wasm'), '--asset-dir', CD, '--headless', String(c.frames)];
  for (const kv of c.params.split('&')) args.push('--param', kv);
  return execFileSync(RUN, args, { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim().split('\n').join(' ');
}

const PROFILE = mkdtempSync(join(tmpdir(), 'openrf-play-chrome-'));
// python3 -m http.server listens with a backlog of 5: Chrome fetching gasm-host's modules (lib/*.js, 0.6.0) in
// parallel got connections reset and the page's module graph failed now and then. Same server, backlog 128.
const SERVE = `import http.server as h, functools as f
class S(h.ThreadingHTTPServer): request_queue_size = 128
S(('', ${PORT}), f.partial(h.SimpleHTTPRequestHandler, directory='.')).serve_forever()`;
const server = spawn('python3', ['-c', SERVE], { cwd: DIST, stdio: 'ignore' });
const chromeArgs = ['--headless=new', `--remote-debugging-port=${DEBUG}`, `--user-data-dir=${PROFILE}`,
  '--autoplay-policy=no-user-gesture-required', 'about:blank'];
if (process.env.CI) chromeArgs.unshift('--no-sandbox');
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' });

function rendererRssMb() {
  return execFileSync('ps', ['-axo', 'rss=,command=']).toString().split('\n')
    .filter((l) => l.includes(PROFILE) && l.includes('--type=renderer'))
    .reduce((s, l) => s + Number(l.trim().split(/\s+/)[0]) / 1024, 0);
}

async function cdp() {
  let target;
  for (let i = 0; i < 50 && !target; i++) {
    await sleep(200);
    target = await fetch(`http://127.0.0.1:${DEBUG}/json`).then((r) => r.json()).then((t) => t.find((x) => x.type === 'page')).catch(() => null);
  }
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown') logs.push(`EXCEPTION ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`);
  };
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable'); await send('DOM.enable');
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result?.value;
  const until = async (expr, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { const v = await evaluate(expr); if (v) return v; await sleep(200); }
    throw new Error(`timeout waiting for ${expr}\n  ${logs.slice(-8).join('\n  ')}`);
  };
  const setFiles = async (selector, files) => {
    const doc = await send('DOM.getDocument');
    const node = await send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector });
    const r = await send('DOM.setFileInputFiles', { nodeId: node.result.nodeId, files });
    if (r.error) throw new Error(`setFileInputFiles: ${r.error.message}`);
  };
  const open = async (url) => {
    await send('Page.navigate', { url });
    const t0 = Date.now();
    try {
      await until('document.readyState === "complete" && !!globalThis.__openrfReady', 20000);
    } catch (e) {   // say where it stalled, and whether it was only slow
      const state = await evaluate('JSON.stringify({ ready: document.readyState, play: !!globalThis.openrfPlay, url: location.href })');
      try {
        await until('document.readyState === "complete" && !!globalThis.__openrfReady', 40000);
        console.log(`      (slow page load: ${((Date.now() - t0) / 1000).toFixed(1)} s; at 20 s ${state})`);
      } catch { throw new Error(`${e.message}\n  page at 20 s: ${state}`); }
    }
  };
  return { send, evaluate, until, setFiles, open, logs };
}

/** The frame on screen at its own size (play.js keeps it; the canvas is WebGL at display size). */
async function saveCanvas(page, file) {
  const url = await page.evaluate('globalThis.__openrfFramePng()');
  writeFileSync(file, Buffer.from(url.split(',')[1], 'base64'));
}

/** Wait for the hash line; sample the renderer's resident memory meanwhile. */
async function result(page, before, ms = 600000) {
  let peak = before;
  const t0 = Date.now();
  for (;;) {
    const r = await page.evaluate('globalThis.__openrfResult');
    if (r) return { hash: r, peakMb: peak, grewMb: peak - before, secs: (Date.now() - t0) / 1000 };
    const err = await page.evaluate('document.getElementById("message").hidden ? "" : document.getElementById("message").textContent');
    if (/could not|stopped|failed/i.test(err)) throw new Error(`page: ${err}`);
    peak = Math.max(peak, rendererRssMb());
    if (Date.now() - t0 > ms) throw new Error('timed out');
    await sleep(200);
  }
}

let failed = false;
const report = (ok, label, detail) => { failed ||= !ok; console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `\n      ${detail}` : ''}`); };
const base = `http://localhost:${PORT}/play/`;
try {
  const page = await cdp();
  await page.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 1, mobile: false });
  await page.open(base);
  await sleep(3000); // let a fresh profile settle
  for (const c of CASES) {
    const want = native(c);
    const url = `${base}?hashframes=${c.frames}&${c.params}`;
    const check = (label, got) => report(!want || got.hash === want, `${c.name}: ${label}`,
      `${got.hash}  (${got.secs.toFixed(1)} s, renderer RSS +${got.grewMb.toFixed(0)} MB over ${(got.peakMb - got.grewMb).toFixed(0)} MB before)${want && got.hash !== want ? `\n      native: ${want}` : ''}`);

    // 1. first visit: pick the folder, check it, import into OPFS, play from OPFS
    await page.open(url);
    if (await page.evaluate('!document.getElementById("imported").hidden')) await page.evaluate('document.getElementById("remove").click()');
    await page.until('document.getElementById("imported").hidden', 10000);
    await page.setFiles('#folder-input', [CD]);
    const checked = await page.until('globalThis.__openrfChecked', 30000);
    report(checked.problems.length === 0, `${c.name}: folder check`, `${checked.files} files, ${(checked.bytes / 1048576).toFixed(0)} MB ${checked.problems.join('; ')}`);
    const t0 = Date.now(), before1 = rendererRssMb();
    await page.evaluate('document.getElementById("import").click()');
    const imported = await page.until('globalThis.__openrfImported', 600000);
    console.log(`      imported ${imported.files} files, ${(imported.bytes / 1048576).toFixed(0)} MB into OPFS in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
    const r1 = await result(page, before1);
    check('import + OPFS worker', r1);
    await saveCanvas(page, join(OUT, `${c.name}-opfs.png`));
    await page.evaluate('document.getElementById("game").scrollIntoView()');
    const shot = await page.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(OUT, `${c.name}-page.png`), Buffer.from(shot.result.data, 'base64'));

    // 2. later visit: the import is detected and played at once
    await page.open(url);
    const seen = await page.evaluate('!document.getElementById("imported").hidden');
    report(seen, `${c.name}: existing import detected`);
    const before2 = rendererRssMb();
    await page.evaluate('document.getElementById("play-opfs").click()');
    check('OPFS worker (later visit)', await result(page, before2));

    // 3. play without importing: the picked folder via the File/Blob provider
    await page.open(url);
    await page.setFiles('#folder-input', [CD]);
    await page.until('globalThis.__openrfChecked', 30000);
    const before3 = rendererRssMb();
    await page.evaluate('document.getElementById("play-direct").click()');
    check('folder without importing (File/Blob worker)', await result(page, before3));

    // 4. the disc image without importing
    if (existsSync(IMG)) {
      await page.open(url);
      await page.setFiles('#image-input', [IMG]);
      const ci = await page.until('globalThis.__openrfChecked', 30000);
      report(ci.problems.length === 0, `${c.name}: image check`, ci.problems.join('; '));
      const before4 = rendererRssMb();
      await page.evaluate('document.getElementById("play-direct").click()');
      const r4 = await result(page, before4);
      check('disc image without importing', r4);
      await saveCanvas(page, join(OUT, `${c.name}-image.png`));
    }
  }
  // Real-time play with the keyboard: title screen, then F2 starts a one-player game (the game reads the raw
  // keyboard, original bindings).
  await page.open(`${base}?skip_intro=1`);
  await page.evaluate('document.getElementById("play-opfs").click()');
  await page.until('/frames\\/s/.test(document.getElementById("status").textContent)', 20000);
  await sleep(2500);
  await saveCanvas(page, join(OUT, 'live-title.png'));
  const canvasUrl = () => page.evaluate('globalThis.__openrfFramePng()');
  const title = await canvasUrl();
  const key = (type) => page.send('Input.dispatchKeyEvent', { type, code: 'F2', key: 'F2', windowsVirtualKeyCode: 113 });
  await key('keyDown'); await sleep(300); await key('keyUp');
  await sleep(4000);
  await saveCanvas(page, join(OUT, 'live-after-f2.png'));
  const left = (await canvasUrl()) !== title;
  const fps = await page.evaluate('document.getElementById("status").textContent');
  report(/^\d+ frames\/s$/.test(fps) && left, 'real-time play, F2 starts a game',
    `${fps}${left ? '' : ', still on the title screen'}; live-title.png, live-after-f2.png`);
  await page.evaluate('document.getElementById("stop").click()');
  await sleep(500);
  // Page screenshot (the browser's own rendering of the page, not the desktop).
  await page.open(base);
  const shot = await page.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT, 'page.png'), Buffer.from(shot.result.data, 'base64'));
  const bad = page.logs.filter((l) => l.startsWith('EXCEPTION'));
  report(!bad.length, 'no uncaught exceptions', bad.slice(0, 3).join('\n      '));
  console.log(`screenshots in ${OUT}`);
} catch (e) {
  failed = true;
  console.error(`FAIL  ${e.message}`);
} finally {
  const exited = new Promise((r) => chrome.once('exit', r));
  chrome.kill(); server.kill();
  await Promise.race([exited, sleep(5000)]);
  try { rmSync(PROFILE, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); } catch {}
}
process.exit(failed ? 1 : 0);
