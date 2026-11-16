#!/usr/bin/env python3
"""Look at a Soulrend place without the game: dq-gentool poi dumps it (generated with the land round it) and this draws
one picture of it: the top view (shaded by height) and four cross-sections (along x and z, at the middle and a quarter in),
each cut 1 block thick, with the land in earth colours and everything else in its own; plus a row of numbers (floating
floors, ground in rooms, unknown blocks). Colours come from Minecraft's textures (the 1.20.1 client jar).
  python3 tools/local/structure_check.py <dump.bin> <out.png>"""
import json, sys, zipfile, io, pathlib, re
import numpy as np
from PIL import Image, ImageDraw

JAR = pathlib.Path.home() / '.local/share/PrismLauncher/libraries/com/mojang/minecraft/1.20.1/minecraft-1.20.1-client.jar'
OURS = {'planks': 'oak_planks', 'log': 'oak_log', 'grass': 'grass_block_top', 'wool': 'white_wool', 'carpet': 'white_carpet', 'slate': 'deepslate',
        'cobbled_slate': 'cobbled_deepslate', 'polished_slate': 'polished_deepslate', 'slate_bricks': 'deepslate_bricks', 'slate_tiles': 'deepslate_tiles',
        'hay_bale': 'hay_block_side', 'flower_red': 'poppy', 'monster_spawner': 'spawner', 'snowy_grass': 'snow', 'tall_grass_double': 'tall_grass_top',
        'leaves': 'oak_leaves', 'chest': 'oak_planks', 'chalk': 'calcite', 'claystone': 'terracotta', 'zinc_ore': 'iron_ore', 'tin_ore': 'iron_ore'}
LAND = {'stone', 'dirt', 'grass', 'snowy_grass', 'sand', 'red_sand', 'gravel', 'deepslate', 'slate', 'granite', 'diorite', 'andesite', 'chalk', 'claystone',
        'coarse_dirt', 'podzol', 'mud', 'clay', 'sandstone', 'red_sandstone', 'snow_block', 'snow', 'tuff', 'calcite', 'terracotta', 'mycelium'}
_jar = None; _cache = {}
def colour(k):
    global _jar
    if k in _cache: return _cache[k]
    if _jar is None: _jar = zipfile.ZipFile(JAR)
    base = OURS.get(k, k)
    base = re.sub(r'(^|_)slate', r'\1deepslate', base) if 'deepslate' not in base and 'slate' in base else base
    for w in ('_branch', '_wood'): base = base.replace(w, '_log')
    m = re.match(r'(\w+?)_(banner|bed|carpet)$', base)
    if m: base = m.group(1) + '_wool'
    if base.endswith('campfire'): base = 'campfire_log'
    stems = [base]
    for suf in ('_stairs', '_slab', '_wall', '_fence_gate', '_fence', '_pressure_plate', '_button', '_vertical_slab', '_trapdoor', '_door', '_sign', '_pane'):
        if base.endswith(suf):
            b = base[:-len(suf)]
            stems += [b, b + '_planks', b + 's', b.replace('brick', 'bricks')]
    c = None
    for st in stems:
        for nm in (st, st + '_top', st + '_side', st + '_front', st + '_0', st + '_bottom'):
            try: data = _jar.read('assets/minecraft/textures/block/%s.png' % nm)
            except KeyError: continue
            im = Image.open(io.BytesIO(data)).convert('RGBA'); a = np.asarray(im)[:16, :16].reshape(-1, 4).astype(float)
            a = a[a[:, 3] > 128]
            if len(a): c = a[:, :3].mean(0); break
        if c is not None: break
    if c is not None and (k in ('grass', 'tall_grass', 'tall_grass_double', 'fern', 'vine') or 'leaves' in k): c = c * np.array([0.55, 0.85, 0.45])
    if k == 'water': c = np.array([50, 90, 200.])
    if k == 'lava': c = np.array([230, 110, 20.])
    if c is None and any(w in k for w in ('leaves', 'grass', 'bush', 'flower', 'fern', 'bluebell')): c = np.array([70, 140, 60.])
    if c is None and any(w in k for w in ('stone', 'ore', 'marble', 'limestone', 'loose')): c = np.array([128, 128, 128.])
    if c is None and 'dirt' in k: c = np.array([120, 85, 55.])
    _cache[k] = c
    return c

def main(src, dst):
    raw = open(src, 'rb').read(); nl = raw.index(b'\n'); h = json.loads(raw[:nl]); sx, sy, sz = h['sx'], h['sy'], h['sz']
    n = sx * sy * sz
    v = np.frombuffer(raw[nl + 1:nl + 1 + 2 * n], dtype=np.uint16).reshape(sy, sz, sx)
    m8 = np.frombuffer(raw[nl + 1 + 2 * n:], dtype=np.uint8).reshape(sy, sz, sx)
    nat = m8 == 1   # the land's own (unchanged by the structure)
    lostm = m8 == 2  # the structure's blocks that aren't there
    pal = h['palette']; air = pal.index('air') if 'air' in pal else -1
    unknown = [k for k in pal if k != 'air' and colour(k) is None]
    col = np.zeros((len(pal), 3)); land = np.zeros(len(pal), bool)
    for i, k in enumerate(pal):
        c = colour(k); col[i] = c if c is not None else (255, 0, 255); land[i] = k in LAND
    solid = v != air
    # top view: the highest block in each column, darker the lower it is
    top = np.where(solid.any(0), sy - 1 - np.argmax(solid[::-1], 0), 0)
    zz, xx = np.indices((sz, sx)); ids = v[top, zz, xx]
    shade = 0.45 + 0.55 * (top - top.min()) / max(1, top.max() - top.min())
    tv = (col[ids] * shade[..., None]).clip(0, 255).astype(np.uint8); tv[~solid.any(0)] = 0
    # the structure's own footprint and floor
    bx0, bz0, bx1, bz1 = h['box']; fy = h['y'] - h['y0']
    fx0, fz0, fx1, fz1 = bx0 - h['x0'], bz0 - h['z0'], bx1 - h['x0'], bz1 - h['z0']
    def section(axis, at):
        s = v[:, at, :] if axis == 'z' else v[:, :, at]
        img = col[s].copy(); img[s == air] = (25, 25, 35)
        lm = nat[:, at, :] if axis == 'z' else nat[:, :, at]
        img[lm] = img[lm] * 0.35 + np.array([150, 80, 20]) * 0.65   # the land's own: orange-brown, so ground in a room shows
        lo = lostm[:, at, :] if axis == 'z' else lostm[:, :, at]
        img[lo] = (255, 0, 0)   # lost blocks: bright red
        return img[::-1].clip(0, 255).astype(np.uint8)
    secs = [('x-cut z=mid', section('z', (fz0 + fz1) // 2)), ('x-cut z=1/4', section('z', fz0 + (fz1 - fz0) // 4)),
            ('z-cut x=mid', section('x', (fx0 + fx1) // 2)), ('z-cut x=1/4', section('x', fx0 + (fx1 - fx0) // 4))]
    # numbers: land inside the footprint above the floor (ground in rooms); the structure's lowest blocks over air
    inside = v[fy + 1:, max(fz0, 0):fz1 + 1, max(fx0, 0):fx1 + 1]
    ground_in = int(nat[fy + 1:, max(fz0, 0):fz1 + 1, max(fx0, 0):fx1 + 1].sum())
    # floating: the structure's own blocks in its bottom few rows with nothing (air or water) under them
    st = (v != air) & ~nat
    low = st[max(fy - 2, 1):fy + 3, max(fz0, 0):fz1 + 1, max(fx0, 0):fx1 + 1]
    below = v[max(fy - 2, 1) - 1:fy + 2, max(fz0, 0):fz1 + 1, max(fx0, 0):fx1 + 1]
    wat = [i for i, k in enumerate(pal) if k in ('air', 'water')]
    floats = int((low & np.isin(below, wat)).sum())
    S = 4 if max(sx, sz) < 90 else 2
    def big(a, f): return Image.fromarray(a).resize((a.shape[1] * f, a.shape[0] * f), Image.NEAREST)
    tiles = [('top', big(tv, S))] + [(n, big(a, S)) for n, a in secs]
    W = max(t.width for _, t in tiles[1:]) + tiles[0][1].width + 30
    H = max(tiles[0][1].height, sum(t.height + 18 for _, t in tiles[1:])) + 40
    out = Image.new('RGB', (W, H), (12, 12, 12)); d = ImageDraw.Draw(out)
    d.text((6, 4), '%s (%s) floor y%d  land-in-rooms %d  floating %d  lost %d/%d %s' % (h['data'].get('poi', h['name']), h['name'], h['y'], ground_in, floats, h.get('lost', 0), h.get('expected', 0), h.get('lostAs', [])[:3]), fill=(255, 255, 0))
    out.paste(tiles[0][1], (6, 24)); d.rectangle([6 + fx0 * S, 24 + fz0 * S, 6 + fx1 * S, 24 + fz1 * S], outline=(255, 0, 0))
    y = 24; x = tiles[0][1].width + 20
    for n, t in tiles[1:]:
        d.text((x, y), n, fill=(200, 200, 200)); out.paste(t, (x, y + 14))
        fl = y + 14 + (sy - 1 - fy) * S; d.line([x, fl, x + t.width, fl], fill=(255, 0, 0))   # the floor's level
        y += t.height + 18
    out.save(dst)
    print(json.dumps({'key': h['data'].get('poi'), 'ground_in': ground_in, 'floats': floats, 'lost': h.get('lost', 0), 'expected': h.get('expected', 0), 'lostAs': h.get('lostAs', [])[:5], 'size': [sx, sy, sz], 'unknown': unknown}))

if __name__ == '__main__': main(sys.argv[1], sys.argv[2])
