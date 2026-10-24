#!/usr/bin/env python3
"""Extract the original DokuQuest 0.9 sound recipes (game/sfxlib.js, sounds2.js, voices3.js, sound3.js,
blocksounds.js and every module that adds to SND_FX / VOX) as DATA for the C++ synth (src/audio/synth*.cpp).

The original builds each sound from a small DSP kit (SndBuf.noise/grains/modes/thump/pluck/vox/throat/hiss/
clack/room/shape) with a seeded random per take, or plays it live through WebAudio (Sfx.tone/noiseHit/voice/
swell). Here the whole game runs headless in Chromium (like jsdump.py); the kit's top-level calls are recorded
for every take of every recipe (with the original's seeds, so each take keeps its own numbers), and every sound
name the game plays is resolved through the original's own dispatch chain (voices3 -> sfxlib -> sounds2 ->
audio.js) to either a rendered recipe or a list of live calls. No audio is stored: only the recipes.

  python3 tools/port/sfx_extract.py /home/claude/src_in/DokuQuest-0.9.0.90 data/sfx.json [refdir]
  (refdir: also saves the original's rendered takes of a few recipes as .f32 files, to check the C++ port)
"""
import re, json, sys, pathlib
from playwright.sync_api import sync_playwright

root = pathlib.Path(sys.argv[1]); out = pathlib.Path(sys.argv[2]); refdir = sys.argv[3] if len(sys.argv) > 3 else None
order = re.findall(r"'([a-z0-9_/]+\.js)'", re.search(r"const ORDER = \[(.*?)\];", (root/'tools/build.js').read_text(), re.S).group(1))
parts = [(root/'src/js'/f).read_text() for f in order if f != 'boot/main.js']

# every sound name the game asks for by a literal
names = set()
for f in (root/'src/js').rglob('*.js'):
    for m in re.finditer(r"sfx\.(?:play|playAt)\(\s*'([a-z0-9_]+)'", f.read_text()): names.add(m.group(1))
base_cases = re.findall(r"case '([a-z0-9_]+)':", (root/'src/js/game/audio.js').read_text())
names |= set(base_cases)

RECORD = r"""
window.__SFX = async (names, refKeys, liveRefs) => {
  const SR = 48000, out = { recipes: {}, names: {}, errors: [], refs: {} };
  // ---- 0. (checks) the original's live WebAudio sounds rendered offline, twice each, before anything is patched ---------
  for (const name of liveRefs) {
    for (let k = 0; k < 2; k++) {
      const oac = new OfflineAudioContext(1, SR * 3, SR), S0 = new Sfx();
      S0.ctx = oac; S0.master = oac.createGain(); S0.master.connect(oac.destination); S0.volume = 1; S0.ok = () => true;
      S0.noise = oac.createBuffer(1, SR, SR); const nd = S0.noise.getChannelData(0); for (let i = 0; i < SR; i++) nd[i] = Math.random() * 2 - 1;
      try { S0.play(name); const buf = await oac.startRendering(); (out.refs['live.' + name] = out.refs['live.' + name] || []).push(Array.from(buf.getChannelData(0))); }
      catch (e) { out.errors.push('ref ' + name + ': ' + e); }
    }
  }
  const rnd = (x) => typeof x === 'number' ? (Number.isInteger(x) ? x : +x.toPrecision(5)) : x;
  const clean = (v) => Array.isArray(v) ? v.map(clean) : v && typeof v === 'object' ? Object.fromEntries(Object.entries(v).map(([k, x]) => [k, clean(x)])) : rnd(v);
  // ---- 1. rendered recipes: record the top-level kit calls of every take -----------------------------------------------
  let depth = 0, ops = null, draws = null;
  for (const m of ['noise', 'grains', 'modes', 'thump', 'pluck', 'vox', 'throat', 'hiss', 'clack', 'room', 'shape']) {
    const f = SndBuf.prototype[m];
    SndBuf.prototype[m] = function (...a) { if (depth === 0 && ops) ops.push([m, ...a.map(clean)]); depth++; try { return f.apply(this, a); } finally { depth--; } };
  }
  const baseBq = sndBiquad;
  sndBiquad = function (type, f, Q, sr) { if (depth === 0 && ops) ops.push(['_bq', type, rnd(f), rnd(Q)]); return baseBq(type, f, Q, sr); };
  const recipe = (key, spec) => {
    const [len, gain, fn, pv] = spec, n = SND_TAKES(len), takes = [], ref = [];
    for (let v = 0; v < n; v++) {
      const rng = mulberry32(seedFromString(key) + v * 7919);
      const r = () => { const x = rng(); if (depth === 0 && draws) draws.push(rnd(x)); return x; };
      const b = new SndBuf(SR, len, r);
      ops = []; draws = [];
      try { fn(b, v, r); } catch (e) { out.errors.push(key + ': ' + e); }
      const o = ops; ops = null; o.draws = draws; draws = null;
      // stick-slip creaks and the pop sweep write samples themselves: keep what they need
      const t = o.filter(x => x[0] !== '_bq');
      const bq = o.filter(x => x[0] === '_bq');
      if (bq.length) t.unshift(['_raw', ...bq.map(x => x.slice(1))]);
      if (key === 'fx.pop') t.unshift(['_pop', rnd(380 + o.draws[0] * 260)]);
      takes.push(t);
      if (refKeys.includes(key)) { b.finish(0.9); ref.push(Array.from(b.d)); }
    }
    out.recipes[key] = { len, gain, pv: pv ?? null, takes };
    if (ref.length) out.refs[key] = ref;
  };
  for (const [m, set] of Object.entries(SND_MAT)) for (const [k, spec] of Object.entries(set)) recipe(m + '.' + k, spec);
  for (const [k, spec] of Object.entries(SND_FX)) recipe('fx.' + k, spec);
  for (const [who, set] of Object.entries(VOX)) for (const [kind, spec] of Object.entries(set)) recipe('vx.' + who + (kind === 'say' ? '' : '_' + kind), spec);
  // ---- 2. what each name plays: follow the original's dispatch with the primitives recorded ------------------------------
  const S = new Sfx();
  S.ctx = { currentTime: 0, sampleRate: SR, state: 'running', createBuffer: (c, n, sr) => ({ getChannelData: () => new Float32Array(n) }) };
  S.master = {}; S.volume = 1; S.ok = () => true;
  let log = null, delay = 0;
  sndTakes = (ctx, key, spec) => { if (log) log.push(['take', key]); return [{}]; };
  Sfx.prototype.sample = function (list, gain, pv) { if (log) log.push(['sample', rnd(gain), rnd(pv)]); };
  const P = Sfx.prototype;
  P.tone = function (freq, dur, type, gain, slideTo, d = 0) { log.push(['tone', rnd(d + delay), rnd(freq), rnd(dur), type, rnd(gain), rnd(slideTo || 0)]); };
  P.noiseHit = function (dur, freq, q, gain, type = 'bandpass', pan = 0, d = 0) { log.push(['nh', rnd(d + delay), rnd(dur), rnd(freq), rnd(q), rnd(gain), type]); };
  P.voice = function (o) { const c = clean(o); delete c.pan; c.delay = rnd((o.delay || 0) + delay); log.push(['voice', c]); };
  P.swell = function (o) { const c = clean(o); delete c.pan; c.delay = rnd((o.delay || 0) + delay); log.push(['swell', c]); };
  const realTimeout = window.setTimeout;
  window.setTimeout = (fn, ms) => { const d0 = delay; delay += (ms || 0) / 1000; try { fn(); } finally { delay = d0; } return 0; };
  const all = new Set(names);
  for (const k of Object.keys(SND_FX)) all.add(k);
  for (const [who, set] of Object.entries(VOX)) for (const kind of Object.keys(set)) all.add(kind === 'say' ? who : who + '_' + kind);
  for (const k of Object.keys(VOICES)) { all.add(k); for (const s of ['_hurt', '_death', '_angry']) all.add(k + s); }
  for (const k of [...Object.keys(CAVE_SOUNDS), ...Object.keys(WORLD_SOUNDS)]) all.add(k);
  for (const name of [...all].sort()) {
    const takes = [];
    for (let k = 0; k < 3; k++) {
      log = [];
      try { S.play(name); } catch (e) { log.push(['error', String(e).slice(0, 120)]); }
      takes.push(log); log = null;
      const t = takes[k];
      if (!t.length || t[0][0] === 'take') break;     // rendered (or unknown): one look is enough
    }
    const first = takes[0];
    // the original's minimum gap between two plays of the name, and whether it is a creature's voice (mob volume)
    const mm = /^(.*?)(?:_(hurt|death|angry))?$/.exec(name), vset = VOX[mm[1]], vspec = vset && (vset[mm[2] || 'say'] || (mm[2] === 'angry' ? vset.say : null));
    const vm = /^(.*)_(hurt|death|angry)$/.exec(name), voc = VOICES[name] || (vm && VOICES[vm[1]]);
    const gap = vspec ? 0.08 : SND_FX[name] ? (name === 'eat' ? 0.4 : 0.04) : voc ? 0.08 : CAVE_SOUNDS[name] ? 1 : WORLD_SOUNDS[name] ? 0.05 : 0.04;
    const mob = !!(vspec || voc) && !name.startsWith('mstep_') ? 1 : 0;
    const extra = (o) => Object.assign(o, { gap }, mob ? { mob } : {});
    if (!first.length) { out.names[name] = extra({ none: 1 }); continue; }
    if (first[0][0] === 'take') { out.names[name] = extra({ r: first[0][1], gain: first[1] ? first[1][1] : 1, pv: first[1] ? first[1][2] : 0.07 }); continue; }
    if (first.some(x => x[0] === 'error')) { out.names[name] = extra({ special: 1 }); out.errors.push(name + ': ' + first.find(x => x[0] === 'error')[1]); continue; }
    out.names[name] = extra({ live: takes });
  }
  window.setTimeout = realTimeout;
  // ---- 3. the pack table: which of our sounds a pack's events stand in for -------------------------------------------
  out.packEvents = PACK_SOUND_EVENTS;
  return out;
};
"""
REFS = ['stone.step', 'stone.dig', 'grass.step', 'wood.dig', 'glass.break', 'metal.place', 'fx.door_open', 'fx.chest', 'fx.pop', 'fx.explode',
        'fx.levelup', 'fx.bow', 'fx.bow_draw', 'vx.ghoul', 'vx.moo', 'vx.cluck', 'vx.hurt', 'vx.spider', 'fx.cave_b', 'vx.rattle', 'leaves.break', 'mud.step', 'fx.splash']
LIVE_REFS = ['chirp', 'cricket', 'drip', 'tick', 'boar', 'deer', 'thunder', 'hiss', 'roar', 'portal', 'bell', 'warn', 'cave_whisper', 'rabbit', 'wolf_howl']
bundle = "(function(){'use strict';\n" + "\n".join(parts) + "\n" + RECORD + "\n})();"
with sync_playwright() as p:
    b = p.chromium.launch(args=['--use-gl=swiftshader', '--enable-unsafe-swiftshader'])
    pg = b.new_page(); errors = []
    pg.on('pageerror', lambda e: errors.append(str(e)[:300]))
    pg.set_content((root/'src/index.html').read_text().split('<script>')[0] + '</body></html>')
    pg.add_script_tag(content=bundle)
    res = pg.evaluate("([n, r, l]) => window.__SFX(n, r, l)", [sorted(names), REFS if refdir else [], LIVE_REFS if refdir else []])
    b.close()
refs = res.pop('refs')
if refdir:
    import numpy as np
    d = pathlib.Path(refdir); d.mkdir(parents=True, exist_ok=True)
    for k, takes in refs.items():
        for i, t in enumerate(takes): np.asarray(t, dtype=np.float32).tofile(d / f'{k}.{i}.f32')
print('recipes', len(res['recipes']), 'names', len(res['names']), 'errors', res['errors'][:12], 'page errors', errors[:3])
out.write_text(json.dumps(res, separators=(',', ':')))
print('wrote', out, out.stat().st_size, 'bytes')
