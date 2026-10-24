#!/usr/bin/env python3
"""Extract the original game's data into data/*.json + data/tiles.png (run once per JS source update).
  python3 tools/port/extract_all.py <js-source-root> <data-dir>"""
import json, sys, subprocess, pathlib, base64
from PIL import Image
src, outd = sys.argv[1], pathlib.Path(sys.argv[2]); outd.mkdir(parents=True, exist_ok=True)
tmp = outd / '_dump.json'
expr = r"""({ blocks: BLOCKS, faces: BLOCK_FACES, render: RENDER, shapeKind: SHAPE_KIND, items: ITEMS, recipes: RECIPES,
  smelting: SMELTING, tags: TAGS, mobs: MOB_TYPES, models: (typeof MODEL_DEFS !== 'undefined' ? MODEL_DEFS : null),
  aliases: BLOCK_ALIASES, tiles: ATLAS.tile, res: ATLAS.res, count: ATLAS.count, TILE, ATLAS_TILES,
  packc: Object.keys(ATLAS.tile).map(n => [n, packCandidates(n, false), packCandidates(n, true)]),
  pix: (() => { const px = ATLAS.pixels.data; let s = ''; for (let i = 0; i < px.length; i += 8192) s += String.fromCharCode.apply(null, px.subarray(i, i + 8192)); return btoa(s); })(),
  size: [ATLAS.pixels.width, ATLAS.pixels.height] })"""
subprocess.run([sys.executable, str(pathlib.Path(__file__).parent / 'jsdump.py'), src, str(tmp), expr], check=True)
d = json.loads(tmp.read_text()); tmp.unlink()
w, h = d['size']; img = Image.frombytes('RGBA', (w, h), base64.b64decode(d['pix']))
img.save(outd / 'tiles.png', optimize=True)
def dump(name, obj): (outd / name).write_text(json.dumps(obj, separators=(',', ':'), sort_keys=False))
dump('blocks.json', {'render': d['render'], 'shapeKind': d['shapeKind'], 'aliases': d['aliases'], 'blocks': d['blocks'], 'faces': d['faces']})
dump('items.json', d['items'])
dump('recipes.json', {'recipes': d['recipes'], 'smelting': d['smelting'], 'tags': d['tags']})
dump('mobs.json', d['mobs'])
dump('models.json', d['models'])
dump('tiles.json', {'tile': d['TILE'], 'perRow': d['ATLAS_TILES'], 'count': d['count'], 'names': d['tiles'], 'res': d['res']})
dump('packmap.json', d['packc'])
print('blocks', len(d['blocks']), 'items', len(d['items']), 'recipes', len(d['recipes']), 'mobs', len(d['mobs']), 'tiles', d['count'], img.size)
