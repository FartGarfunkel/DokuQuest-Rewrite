import re, json, sys, pathlib
from playwright.sync_api import sync_playwright
root = pathlib.Path(sys.argv[1]); out = sys.argv[2]
order = re.findall(r"'([a-z0-9_/]+\.js)'", re.search(r"const ORDER = \[(.*?)\];", (root/'tools/build.js').read_text(), re.S).group(1))
EXP = {'core/items.js': "window.__DQ=window.__DQ||{};Object.assign(window.__DQ,{BLOCKS,ITEMS,RECIPES,SMELTING,B,TAGS});",
       'game/entities.js': "try{window.__DQ.MOB_TYPES=MOB_TYPES}catch(e){}"}
parts, errs = [], []
for f in order:
    if f == 'boot/main.js': continue
    parts.append(f"try{{eval({json.dumps((root/'src/js'/f).read_text())})}}catch(e){{(window.__ERR=window.__ERR||[]).push('{f}: '+e)}}" if False else (root/'src/js'/f).read_text())
    if f in EXP: parts.append(EXP[f])
parts.append("""try{buildAtlas()}catch(e){(window.__E=window.__E||[]).push('atlas '+e)}
try{finishAtlas()}catch(e){(window.__E=window.__E||[]).push('finish '+e)}
try{resolveBlockTextures()}catch(e){(window.__E=window.__E||[]).push('faces '+e)}
Object.assign(window.__DQ,{ATLAS,BLOCK_FACES,SHAPE_KIND,MODEL_DEFS,TILE,ATLAS_TILES});try{window.__DQ.PACKC=Object.keys(ATLAS.tile).map(n=>[n,packCandidates(n,false),packCandidates(n,true)])}catch(e){(window.__E=window.__E||[]).push('packc '+e)}""")
bundle = "(function(){'use strict';\n" + "\n".join(parts) + "\n})();"
dump = r"""() => { const seen = new WeakSet();
  const clean = (v, d) => { if (typeof v === 'function') return undefined; if (v && typeof v === 'object') {
      if (seen.has(v) || d > 4) return undefined; seen.add(v);
      if (Array.isArray(v)) return v.map(x => clean(x, d + 1));
      const o = {}; for (const k of Object.keys(v)) { const c = clean(v[k], d + 1); if (c !== undefined) o[k] = c; } return o; }
    return v; };
  const D = window.__DQ || {}; return JSON.stringify({blocks: clean(D.BLOCKS, 0), items: clean(D.ITEMS, 0), recipes: clean(D.RECIPES, 0), smelting: clean(D.SMELTING, 0), tags: clean(D.TAGS, 0), mobs: clean(D.MOB_TYPES, 0), faces: clean(D.BLOCK_FACES, 0), shapes: D.SHAPE_KIND, models: clean(D.MODEL_DEFS, 0),
  tiles: D.ATLAS && D.ATLAS.tile, packc: D.PACKC, res: D.ATLAS && D.ATLAS.res, jsErr: window.__E || null,
  pix: (() => { const A = D.ATLAS, T = D.TILE, N = D.ATLAS_TILES, n = A.count, px = A.pixels.data, W = T * N, out = new Uint8Array(n * 1024);
    for (let t = 0; t < n; t++) { const tx = (t % N) * T, ty = Math.floor(t / N) * T, r = A.res[t] || 16, st = T / 16;
      for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) { const si = ((ty + Math.floor(y * st)) * W + tx + Math.floor(x * st)) * 4, di = t * 1024 + (y * 16 + x) * 4;
        out[di] = px[si]; out[di+1] = px[si+1]; out[di+2] = px[si+2]; out[di+3] = px[si+3]; } }
    let s = ''; for (let i = 0; i < out.length; i += 8192) s += String.fromCharCode.apply(null, out.subarray(i, i + 8192)); return btoa(s); })()
  }); }"""
with sync_playwright() as p:
    b = p.chromium.launch(args=['--use-gl=swiftshader', '--enable-unsafe-swiftshader'])
    pg = b.new_page(); errors = []
    pg.on('pageerror', lambda e: errors.append(str(e)[:200]))
    pg.set_content((root/'src/index.html').read_text().split('<script>')[0] + '</body></html>')
    try: pg.add_script_tag(content=bundle)
    except Exception as e: errors.append('tag: ' + str(e)[:200])
    data = json.loads(pg.evaluate(dump)); data['err'] = errors
    b.close()
json.dump(data, open(out, 'w'))
print({k: (len(v) if hasattr(v, '__len__') else v) for k, v in data.items() if k != 'err'}, errors[:3])
