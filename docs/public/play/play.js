// OpenRF in the browser: openrf.wasm in gasm's Worker mode. The page keeps input, display and
// audio; the worker runs the game and reads the CD on demand, either from the site's OPFS copy
// (FileSystemSyncAccessHandle) or straight from the picked files (FileReaderSync).
//
// Test and debug query parameters:
//   skip_intro, play, play2, level, viewer, demo, p1, p2, cam_h, sfx_log   passed to the game
//   hashframes=N   run N frames with no input on virtual time as fast as possible, then print the
//                  same "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__openrfResult)
//   autoplay       start the imported CD right away
//   filter=sharp|nearest|xbr|fsr|crt, integer   how the frames are scaled up (also in the toolbar)
import { BrowserInput, ProcExit, Resampler } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { FILTERS, GlPresenter } from './vendor/gasm/gasm-present.js';
import * as cd from './cd.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const GAME_PARAMS = ['skip_intro', 'play', 'play2', 'level', 'viewer', 'demo', 'p1', 'p2', 'cam_h', 'sfx_log'];
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const hasOpfs = !!navigator.storage?.getDirectory;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- keyboard and gamepads ------------------------------------------------------------------
// The game reads the keyboard itself (raw keys, the original bindings; input_mode KEYS_RAW), so the page
// only collects it. Gamepads are virtual pads 0-3, in connection order.
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
const keyboard = new BrowserInput($('screen'), { ignore: (e) => !running || typing(e) }).attach();
// While playing, keys shouldn't scroll the page or open the browser's menus (Alt); browser shortcuts
// (Cmd/Ctrl + key, F4-F12) still work. A tap of Escape goes to the game (leave the level); holding it for a
// second stops the game, like gasm-run.
const browserKey = (e) => e.metaKey || (e.ctrlKey && !/^(Control|Shift)/.test(e.code)) || /^F([4-9]|1[0-2])$/.test(e.code);
let escDown = 0;
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  if (e.code === 'Escape') { if (!e.repeat) escDown = performance.now(); return; }
  if (!browserKey(e)) e.preventDefault();
});
addEventListener('keyup', (e) => { if (e.code === 'Escape') escDown = 0; });

// W3C "standard" gamepad mapping -> gasm button bit (A east, B south, X north, Y west, like gasm's app.js).
const PAD = { 1: 0, 0: 1, 3: 2, 2: 3, 4: 4, 5: 5, 6: 4, 7: 5, 8: 6, 9: 7, 12: 8, 13: 9, 14: 10, 15: 11 };
function readPads() {
  const pads = [0, 0, 0, 0];
  let n = 0;
  for (const gp of navigator.getGamepads?.() ?? []) {
    if (!gp || n > 3) continue;
    let m = 0;
    for (const [btn, bit] of Object.entries(PAD)) if (gp.buttons[btn]?.pressed) m |= 1 << bit;
    const [x = 0, y = 0] = gp.axes;
    if (x < -0.5) m |= 1 << 10; if (x > 0.5) m |= 1 << 11;
    if (y < -0.5) m |= 1 << 8; if (y > 0.5) m |= 1 << 9;
    pads[n++] |= m;
  }
  return pads;
}

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('openrf.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('openrf.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

// ---- display ------------------------------------------------------------------------------
// gasm's presenter (WebGL 2): letterboxed at the canvas's device-pixel size with an upscaling filter
// (?filter= / the toolbar, kept in localStorage; default sharp = plain pixel doubling at whole factors),
// optionally whole multiples only (?integer). Without WebGL 2, a canvas 2D scaled up by CSS.
const canvas = $('screen');
const view = {
  filter: FILTERS.includes(query.get('filter')) ? query.get('filter') : localStorage.getItem('openrf.filter') ?? 'sharp',
  integerScale: query.has('integer') || localStorage.getItem('openrf.integer') === '1',
};
if (!FILTERS.includes(view.filter)) view.filter = 'sharp';
let presenter = null, ctx2d = null, shown = null;   // shown: the frame on screen [rgba, w, h]
function present(rgba, w, h) {
  shown = [rgba, w, h];
  if (!ctx2d && !presenter) {
    presenter = GlPresenter.create(canvas);
    if (presenter) canvas.classList.add('gl');
    else { ctx2d = canvas.getContext('2d'); show('view-controls', false); }
  }
  if (presenter) {
    const size = [Math.round(canvas.clientWidth * devicePixelRatio) || w, Math.round(canvas.clientHeight * devicePixelRatio) || h];
    return presenter.draw(rgba, w, h, size, view);
  }
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
  ctx2d.putImageData(new ImageData(rgba, w, h), 0, 0);
}
const redraw = () => { if (shown) present(...shown); };
new ResizeObserver(redraw).observe(canvas);
for (const f of FILTERS) $('filter').add(new Option(f, f));
$('filter').value = view.filter;
$('integer').checked = view.integerScale;
$('filter').onchange = () => { view.filter = $('filter').value; localStorage.setItem('openrf.filter', view.filter); redraw(); };
$('integer').onchange = () => {
  view.integerScale = $('integer').checked;
  localStorage.setItem('openrf.integer', view.integerScale ? '1' : '0');
  redraw();
};
/** The frame on screen at its own size, as a PNG data URL (tests; independent of the filter). */
globalThis.__openrfFramePng = () => {
  if (!shown) return null;
  const [rgba, w, h] = shown, c = document.createElement('canvas');
  c.width = w; c.height = h;
  c.getContext('2d').putImageData(new ImageData(rgba, w, h), 0, 0);
  return c.toDataURL('image/png');
};
$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, wasmModule = null, lastLog = '';
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

/** openrf.wasm, compiled once; every start shares the module with its worker. */
async function loadWasm() {
  if (!wasmModule) {
    const r = await fetch(new URL('openrf.wasm', import.meta.url));
    if (!r.ok) throw new Error(`openrf.wasm: HTTP ${r.status}`);
    wasmModule = await WebAssembly.compile(await r.arrayBuffer());
  }
  return wasmModule;
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false; escDown = 0;
  const w = worker; worker = null;
  await w?.exit();          // flushes the high scores, releases the OPFS handles
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  show('setup', true);
  refreshImported();
}

/** Start the game from 'opfs' (the imported CD) or a checked source (read in place). */
async function play(source) {
  startAudio();                        // still inside the click's user gesture
  message('');
  await stopGame();
  lastLog = '';
  show('game', true);
  show('setup', HASH_FRAMES > 0);      // keep the page visible in test runs
  $('status').textContent = 'Starting...';
  const params = Object.fromEntries(GAME_PARAMS.filter((k) => query.has(k)).map((k) => [k, query.get(k)]));
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm(), assets: cd.assetSpecs(source), params, storage: 'openrf',
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio,
    });
  } catch (e) {
    worker = null;
    show('game', false);
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported CD is in use by another OpenRF tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}.`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = 'Return Fire | OpenRF';
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  $('stage').focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  if (escDown && now - escDown >= 1000) {   // Escape held: stop (the tap already went to the game)
    $('stop').onclick();
    return;
  }
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);    // clamp after tab switches
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {          // one batch in flight: the worker sets the pace
    const pads = readPads();
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ pads, input: keyboard.frame(k === 0) }));
    worker.frames(steps, true).then((r) => {
      inflight = false;
      fpsN += due;
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;       // fell far behind: resync
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s`; fpsN = 0; fpsT = now; }
}

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
async function hashRun(n) {
  let left = n, r = null;
  try {
    while (left > 0) {
      const k = Math.min(250, left);
      r = await worker.frames(Array.from({ length: k }, () => [0, 0, 0, 0]), left === k);
      if (r.frame) present(r.frame.rgba, r.frame.width, r.frame.height);
      left -= k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__openrfResult = out;
}

$('stop').onclick = async () => { await stopGame(); show('game', false); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the CD ----------------------------------------------------------
let checked = null; // result of cd.folderFrom... / cd.checkImage

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const what = result.source.kind === 'image' ? 'disc image' : 'folder';
  item(`${what} "${result.source.name}": ${result.source.kind === 'image' ? mbText(result.bytes) : `${result.files} files, ${mbText(result.bytes)}`}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
    item('This does not look like the Return Fire CD for Windows 95.', false);
  } else {
    item(result.source.kind === 'image' ? 'RFIRE.BIN, ART and SOUND found on the disc' : 'RFIRE.BIN, ART/ART.CAR and SOUND/SCORE.WAV found', true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__openrfChecked = { problems: result.problems, files: result.files, bytes: result.bytes };
}
const mbText = cd.mb;

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click(); // Firefox, Safari
  let handle;
  try { handle = await showDirectoryPicker({ id: 'openrf-cd', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await cd.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(cd.folderFromFileList(e.target.files));
  e.target.value = '';
};
$('pick-image').onclick = () => { message(''); $('image-input').click(); };
$('image-input').onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = '';
  if (f) showCheck(await cd.checkImage(f));
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = $('pick-image').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await cd.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${mbText(p.bytes)} of ${mbText(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${mbText(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__openrfImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? 'Not enough browser storage for the CD (about 280 MB). Free some space, or play without importing.'
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await cd.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = $('pick-image').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await cd.removeImport(); message('The imported CD was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await cd.requestPersist();
  message(ok ? 'The browser will keep the imported CD.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await cd.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your CD' : 'Choose your CD';
  if (info) {
    $('imported-desc').textContent = `${info.kind === 'image' ? 'disc image' : 'folder'} "${info.name}", ${info.files} files, ${mbText(info.bytes)}`;
    const s = await cd.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${mbText(s.usage)} of the ${mbText(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the CD can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenRF (it needs WebAssembly and Web Workers).', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__openrfReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.openrfPlay = { play, stopGame, refreshImported, cd };
