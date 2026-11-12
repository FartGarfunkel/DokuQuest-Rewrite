#!/usr/bin/env python3
"""Split data/tiles.png (the built-in tile atlas) into resource-pack style folders:
   data/textures/block/<name>.png, item/, entity/ (mob parts), misc/ (everything else, unnamed frames as _tile_<n>.png).
tiles.json keeps the index (names, res) and gains "files": the picture of every tile index, which the game loads
(assets/tileset.cpp loadBuiltinTiles). Edit the PNGs freely (any square size: they are scaled to the tile size).
Run again only to rebuild the folders from a regenerated tiles.png."""
import json, os, sys
from PIL import Image
data = sys.argv[1] if len(sys.argv) > 1 else 'data'
tj = json.load(open(os.path.join(data, 'tiles.json')))
atlas = Image.open(os.path.join(data, 'tiles.png')).convert('RGBA')
S, per, count = tj['tile'], tj['perRow'], tj['count']
def strings(o, out):
    if isinstance(o, str): out.add(o)
    elif isinstance(o, dict):
        for k, v in o.items(): out.add(k); strings(v, out)
    elif isinstance(o, list):
        for v in o: strings(v, out)
    return out
uses = {k: strings(json.load(open(os.path.join(data, f))), set()) for k, f in (('block', 'blocks.json'), ('item', 'items.json'), ('entity', 'models.json'))}
byIndex = {}
for name, i in tj['names'].items(): byIndex.setdefault(i, name)
files = []
for i in range(count):
    name = byIndex.get(i)
    cat = 'misc'
    if name:
        for c in ('block', 'item', 'entity'):
            if name in uses[c]: cat = c; break
    rel = f'textures/{cat}/{name}.png' if name else f'textures/misc/_tile_{i}.png'
    os.makedirs(os.path.join(data, os.path.dirname(rel)), exist_ok=True)
    x, y = (i % per) * S, (i // per) * S
    atlas.crop((x, y, x + S, y + S)).save(os.path.join(data, rel))
    files.append(rel)
tj['files'] = files
json.dump(tj, open(os.path.join(data, 'tiles.json'), 'w'), separators=(',', ':'))
from collections import Counter
print(Counter(f.split('/')[1] for f in files))
