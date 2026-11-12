#!/usr/bin/env python3
"""Run the original DokuQuest 0.9 JS game (all modules except boot) inside headless Chromium and
evaluate a JS expression, printing/saving its JSON. Used to extract data (blocks, items, recipes,
mobs, rigs, textures...) so the C++ port stays faithful to the original.

  python3 tools/port/jsdump.py <js-source-root> <out.json> "<js expression returning a JSON-able value>"
  (the expression may use any top-level const/function of the game, e.g. BLOCKS, ITEMS, MOB_TYPES)
"""
import re, json, sys, pathlib
from playwright.sync_api import sync_playwright
root = pathlib.Path(sys.argv[1]); out = sys.argv[2]; expr = sys.argv[3]
order = re.findall(r"'([a-z0-9_/]+\.js)'", re.search(r"const ORDER = \[(.*?)\];", (root/'tools/build.js').read_text(), re.S).group(1))
parts = [(root/'src/js'/f).read_text() for f in order if f != 'boot/main.js']
parts.append("""try{buildAtlas()}catch(e){(window.__E=window.__E||[]).push('atlas '+e)}
try{finishAtlas()}catch(e){(window.__E=window.__E||[]).push('finish '+e)}
try{resolveBlockTextures()}catch(e){(window.__E=window.__E||[]).push('faces '+e)}
window.__clean = (v) => { const seen = new WeakSet(); const c = (v, d) => { if (typeof v === 'function') return undefined;
  if (v instanceof Map) v = Object.fromEntries(v); if (v instanceof Set) v = [...v];
  if (ArrayBuffer.isView(v)) return Array.from(v);
  if (v && typeof v === 'object') { if (seen.has(v) || d > 12) return undefined; seen.add(v);
    if (Array.isArray(v)) { const r = v.map(x => c(x, d + 1)); seen.delete(v); return r; }
    const o = {}; for (const k of Object.keys(v)) { const x = c(v[k], d + 1); if (x !== undefined) o[k] = x; } seen.delete(v); return o; }
  if (typeof v === 'number' && !isFinite(v)) return null; return v; }; return c(v, 0); };
window.__EVAL = (src) => JSON.stringify(window.__clean(eval(src)));""")
bundle = "(function(){'use strict';\n" + "\n".join(parts) + "\n})();"
with sync_playwright() as p:
    b = p.chromium.launch(args=['--use-gl=swiftshader', '--enable-unsafe-swiftshader'])
    pg = b.new_page(); errors = []
    pg.on('pageerror', lambda e: errors.append(str(e)[:300]))
    pg.set_content((root/'src/index.html').read_text().split('<script>')[0] + '</body></html>')
    try: pg.add_script_tag(content=bundle)
    except Exception as e: errors.append('tag: ' + str(e)[:300])
    try: res = pg.evaluate("(s) => window.__EVAL(s)", expr)
    except Exception as e: print('EVAL ERROR', str(e)[:2000]); res = 'null'
    errs = pg.evaluate("window.__E || []")
    b.close()
pathlib.Path(out).write_text(res)
print('wrote', out, len(res), 'bytes; page errors:', (errors + errs)[:5])
