#!/usr/bin/env python3
"""Fixes on top of the extracted data (run after extract_all.py; idempotent):
  python3 tools/port/data_patches.py <data-dir>
- gate_barrier (a walled town's gate while shut) is drawn: the original drew the gate as a moving model from
  whether the barrier blocks were there; here the barrier blocks themselves are the iron-banded timber gate.
- stacked vines get a body picture (<key>_plant) next to their tip picture, so a strand reads as one plant: the body
  is the tip's strands run the full height of the tile. Packs supply their own *_plant pictures (packmap)."""
import json, sys, pathlib
from PIL import Image

d = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'data')

# ---- blocks ----
bj = json.loads((d / 'blocks.json').read_text())
tnames = json.loads((d / 'tiles.json').read_text())['names']
blocks = bj['blocks']
for b in (blocks.values() if isinstance(blocks, dict) else blocks):
    if isinstance(b, dict) and b.get('key') == 'gate_barrier':
        b['render'] = 2; b['tex'] = 'spruce_door_lower'; b['selfCull'] = True
        bj['faces'][b['id']] = [tnames['spruce_door_lower']] * 6
(d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), sort_keys=False))

# ---- tiles: body pictures for stacked vines ----
tj = json.loads((d / 'tiles.json').read_text())
names, T, per = tj['names'], tj['tile'], tj['perRow']
img = Image.open(d / 'tiles.png').convert('RGBA')
new = []
for tip in ['weeping_vines', 'twisting_vines', 'cave_vines', 'cave_vines_lit']:
    body = tip.replace('cave_vines', 'cave_vines_plant') if tip.startswith('cave_vines') else tip + '_plant'
    if tip not in names:
        continue
    i = names[tip]; sx, sy = (i % per) * T, (i // per) * T
    src = img.crop((sx, sy, sx + T, sy + T)); px = src.load()
    out = Image.new('RGBA', (T, T), (0, 0, 0, 0)); po = out.load()
    for x in range(T):
        col = [px[x, y] for y in range(T) if px[x, y][3] > 0]
        if len(col) < T // 4:
            continue   # a stray leaf, not a strand
        for y in range(T):
            po[x, y] = col[y % len(col)]
    k = names.get(body, tj['count'])
    if body not in names:
        names[body] = k; tj['count'] += 1
    img.paste(out, ((k % per) * T, (k // per) * T))
    new.append(body)
img.save(d / 'tiles.png', optimize=True)
(d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), sort_keys=False))

# ---- packmap: the pack's own body pictures ----
pm = json.loads((d / 'packmap.json').read_text())
have = {e[0] for e in pm}
for n in new:
    if n not in have:
        pm.append([n, [n], [n]])
(d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), sort_keys=False))
print('patched: gate_barrier drawn; body tiles', new, 'count', tj['count'])

# ---- spawn eggs for every creature (tools/port/spawn_eggs.py) ----
import importlib.util as _u
_s = _u.spec_from_file_location('spawn_eggs', pathlib.Path(__file__).with_name('spawn_eggs.py')); _m = _u.module_from_spec(_s); _s.loader.exec_module(_m); _m.main(d)
