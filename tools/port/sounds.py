"""Renders the JS game's synthesized sounds (game/sfxlib.js SND_MAT + SND_FX) to WAV and packs them:
   python3 tools/port/sounds.py <DokuQuest-0.9.0.90 dir>  → crates/game_engine/data/sounds.bin + sounds.tsv"""
import re, sys, json, base64, pathlib
from playwright.sync_api import sync_playwright
root = pathlib.Path(sys.argv[1]); out = pathlib.Path(__file__).resolve().parents[2] / 'crates/game_engine/data'
order = re.findall(r"'([a-z0-9_/]+\.js)'", re.search(r"const ORDER = \[(.*?)\];", (root/'tools/build.js').read_text(), re.S).group(1))
parts = []
for f in order:
    if f == 'boot/main.js': continue
    parts.append((root/'src/js'/f).read_text())
    if f == 'game/sfxlib.js': parts.append("window.__S={SND_MAT,SND_FX,SndBuf,mulberry32,seedFromString};")
bundle = "(function(){'use strict';\n" + "\n".join(parts) + "\n})();"
render = r"""() => { const S = window.__S, sr = 16000, out = [];
  const wav = (d) => { const n = d.length, v = new DataView(new ArrayBuffer(44 + n * 2)); const w = (o, s) => { for (let i = 0; i < s.length; i++) v.setUint8(o + i, s.charCodeAt(i)); };
    w(0, 'RIFF'); v.setUint32(4, 36 + n * 2, true); w(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
    v.setUint32(24, sr, true); v.setUint32(28, sr * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, n * 2, true);
    for (let i = 0; i < n; i++) v.setInt16(44 + i * 2, Math.max(-1, Math.min(1, d[i])) * 32767, true);
    let s = ''; const u = new Uint8Array(v.buffer); for (let i = 0; i < u.length; i += 8192) s += String.fromCharCode.apply(null, u.subarray(i, i + 8192)); return btoa(s); };
  const one = (key, spec) => { for (let t = 0; t < (key.startsWith('fx.') ? 1 : 2); t++) { try { const rng = S.mulberry32(S.seedFromString(key) + t * 7919); const b = new S.SndBuf(sr, spec[0], rng);
      spec[2](b, t, rng); b.finish(0.9); out.push([key, spec[1], wav(b.d)]); } catch (e) {} } };
  for (const [m, acts] of Object.entries(S.SND_MAT)) for (const [a, spec] of Object.entries(acts)) if (Array.isArray(spec)) one(m + '.' + a, spec);
  for (const [k, spec] of Object.entries(S.SND_FX)) if (Array.isArray(spec)) one('fx.' + k, spec);
  return out; }"""
with sync_playwright() as p:
    b = p.chromium.launch(); pg = b.new_page()
    pg.set_content((root/'src/index.html').read_text().split('<script>')[0] + '</body></html>')
    pg.add_script_tag(content=bundle)
    sounds = pg.evaluate(render); b.close()
blob, idx = bytearray(), []
for key, vol, data in sounds:
    raw = base64.b64decode(data); idx.append(f'{key}\t{len(blob)}\t{len(raw)}\t{vol}'); blob += raw
(out/'sounds.bin').write_bytes(bytes(blob)); (out/'sounds.tsv').write_text('\n'.join(idx) + '\n')
keys = sorted({k for k, _, _ in sounds}); print(len(sounds), 'takes', len(blob) // 1024, 'KB'); print(' '.join(keys))
