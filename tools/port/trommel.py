#!/usr/bin/env python3
"""tools/port/trommel.py — Better than Adventure 8.0's Trommel (idempotent; data_patches.py runs it):

  python3 tools/port/trommel.py <data-dir>

The block (trommel, and lit_trommel while it turns), its recipe (BTA's: cobblestone round an iron mesh over a row of
logs), the two things BTA's trommel gives that DokuQuest lacked — pebbles (2x2 make 2 cobblestone, with flint they make
gravel) and olivine (a green gem; with glass it makes tinted glass) — and the sifting table, BTA's recipes/trommel.json
with each BTA item swapped for this game's (sulfur -> sulphur, nether coal -> ember coal, raw iron / gold -> crushed iron
/ gold, the dye -> an ink sac, cloth -> rotten cloth, chainlink -> chain), written to data/trommel.json for
sim/trommel.cpp. Pictures: the DokuQuest Base pack's own (cobblestone, iron bars, gravel, oak log), put together the
way BTA's trommel looks: a stone frame round a dark mesh drum (full of gravel while it turns), logs underneath.
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures'

# BTA's table: input -> [(weight, item, min, max)]   (recipes/trommel.json, BTA 8.0.1)
SIFT = {
    'dirt':      [(35, 'pebble', 1, 2), (15, 'clay_ball', 1, 4), (7.5, 'flint', 1, 2), (2, 'gunpowder', 1, 1), (0.5, 'crushed_iron', 1, 1), (0.25, 'olivine', 1, 1), (0.25, 'quartz', 1, 1)],
    'gravel':    [(25, 'flint', 1, 2), (30, 'olivine', 1, 3), (15, 'pebble', 1, 4), (5, 'crushed_iron', 1, 2), (5, 'ink_sac', 2, 6), (4, 'redstone', 2, 4), (0.5, 'quartz', 1, 1)],
    'sand':      [(25, 'quartz', 1, 2), (20, 'clay_ball', 2, 5), (15, 'pebble', 1, 3), (10, 'bone', 1, 2), (4, 'redstone', 2, 4), (5, 'flint', 1, 2), (5, 'olivine', 1, 1), (0.5, 'crushed_gold', 1, 1)],
    'clay':      [(30, 'clay_ball', 2, 4), (15, 'pebble', 1, 2), (10, 'gunpowder', 1, 1), (1, 'crushed_gold', 1, 1)],
    'mud':       [(4, 'bone', 1, 1), (5, 'string', 1, 3), (0.5, 'slime_ball', 1, 4), (0.5, 'rotten_cloth', 1, 2), (0.25, 'chain', 1, 1), (30, 'pebble', 1, 2), (20, 'clay_ball', 1, 3)],
    'soul_sand': [(20, 'flint', 1, 3), (10, 'bone', 1, 6), (2, 'crushed_gold', 1, 1), (1, 'crushed_iron', 1, 1), (5, 'glowstone_dust', 1, 6), (3, 'ember_coal', 1, 1), (15, 'pebble', 1, 2)],
    'brimsand':  [(35, 'pebble', 2, 3), (25, 'gunpowder', 1, 2), (5, 'bone', 1, 2), (10, 'glowstone_dust', 1, 2), (5, 'flint', 1, 2), (10, 'sulphur', 1, 2), (2.5, 'rubyglass_crystal', 1, 1)],
    'rich_scorched_dirt': [(17, 'crushed_gold', 1, 2), (17, 'crushed_iron', 1, 2), (24, 'quartz', 1, 4), (24, 'olivine', 1, 4), (17, 'ink_sac', 1, 4), (8, 'clay_ball', 4, 8), (5, 'pebble', 1, 5)],
    'sulphur_block': [(30, 'glowstone_dust', 2, 5), (20, 'sulphur', 2, 4), (10, 'bone', 1, 2), (5, 'flint', 1, 2), (4, 'crushed_iron', 1, 2)],
}
# BTA's trommel_dirt group: dirt, scorched dirt, grass, retro grass, scorched grass, path, farmland
ALIAS = {'scorched_dirt': 'dirt', 'grass': 'dirt', 'grass_block': 'dirt', 'dirt_path': 'dirt', 'farmland': 'dirt', 'coarse_dirt': 'dirt', 'rooted_dirt': 'dirt',
         'brimstone': 'sulphur_block'}


def pic(d, rel):
    p = d / PACK / rel
    if not p.exists(): return None
    im = Image.open(p).convert('RGBA')
    w, h = im.size
    if h > w: im = im.crop((0, 0, w, w))
    if im.size[0] != 32: im = im.resize((32, 32), Image.NEAREST)
    return np.asarray(im).astype(np.float32)


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def drum(base, inner, mesh, active):
    """a stone face with the round mesh drum in it"""
    a = base.copy()
    yy, xx = np.mgrid[0:32, 0:32]
    r = np.hypot(xx - 15.5, yy - 15.5)
    rim, hole = (r < 13) & (r >= 11), r < 11
    a[rim, :3] = a[rim, :3] * 0.45 + np.array([150, 150, 156]) * 0.25          # the iron hoop
    a[hole, :3] = inner[hole, :3] * (0.55 if active else 0.22) + 8                # inside: gravel turning, or the dark
    m = (mesh[..., 3] > 100) & hole
    a[m, :3] = a[m, :3] * 0.3 + mesh[m, :3] * 0.55                                # the mesh over it
    edge = (r >= 13) & (r < 14)
    a[edge, :3] *= 0.6
    return a


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); recipes = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}
    cob, bars, grav = pic(d, 'block/cobblestone.png'), pic(d, 'block/iron_bars.png'), pic(d, 'block/gravel.png')
    log, planks, emer = pic(d, 'block/oak_log.png'), pic(d, 'block/oak_planks.png'), pic(d, 'item/emerald.png')
    if cob is None or bars is None or grav is None or log is None:
        print('trommel: the DokuQuest Base pack is missing - skipped'); return

    def tile(name, a, kind='block'):
        rel = 'textures/%s/%s.png' % (kind, name)
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA').save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    # ---- the block's faces ----
    side = cob.copy(); side[16:] = log[16:]; side[15, :, :3] *= 0.55                 # stone above, logs below
    top = cob.copy(); m = (bars[..., 3] > 100); top[m, :3] = top[m, :3] * 0.35 + bars[m, :3] * 0.6
    tile('trommel_side', side); tile('trommel_top', top); tile('trommel_bottom', planks if planks is not None else log)
    tile('trommel_front', drum(cob, grav, bars, False)); tile('trommel_front_lit', drum(cob, grav, bars, True))
    faces = lambda front: [tiles['trommel_side'], tiles['trommel_side'], tiles['trommel_top'], tiles['trommel_bottom'], tiles[front], tiles['trommel_side']]
    base = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 3.5, 'tool': 'pickaxe', 'tier': 0, 'drop': 'trommel',
            'light': 0, 'lightAtt': 0, 'sound': 'stone', 'furnace': 'trommel', 'carry': True, 'bta': True}
    for key, front in (('trommel', 'trommel_front'), ('lit_trommel', 'trommel_front_lit')):
        b = dict(base, key=key, name='Trommel', tex={'top': 'trommel_top', 'bottom': 'trommel_bottom', 'side': 'trommel_side', 'front': front})
        if key == 'lit_trommel': b['unlit'] = 'trommel'
        if key in at: b['id'] = at[key]; blocks[at[key]].update(b); bj['faces'][at[key]] = faces(front)
        else: b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces(front)); at[key] = b['id']
    items.setdefault('trommel', {'key': 'trommel', 'name': 'Trommel', 'maxStack': 64, 'icon': 'block:trommel', 'block': 'trommel',
                                 'desc': 'Sifts dirt, gravel, sand, clay, mud, soul sand, brimsand and sulphur for what\'s in them. Burns furnace fuel; what it finds pops out of its side, or into a chest there.'})

    # ---- pebbles and olivine ----
    yy, xx = np.mgrid[0:32, 0:32]
    peb = np.zeros((32, 32, 4), np.float32)
    for cx, cy, rr in ((13, 18, 7.5), (20, 13, 6), (21, 21, 5)):
        mm = np.hypot((xx - cx) / 1.0, (yy - cy) / 0.85) < rr
        peb[mm] = cob[mm]; peb[mm, 3] = 255
    ring = (peb[..., 3] == 0) & ((np.roll(peb[..., 3], 1, 0) > 0) | (np.roll(peb[..., 3], -1, 0) > 0) | (np.roll(peb[..., 3], 1, 1) > 0) | (np.roll(peb[..., 3], -1, 1) > 0))
    peb[ring] = [24, 22, 20, 255]
    tile('pebble', peb, 'item')
    if emer is not None:
        oli = emer.copy(); mm = oli[..., 3] > 8
        l = lum(oli); lo, hi = np.percentile(l[mm], 2), np.percentile(l[mm], 98); t = np.clip((l - lo) / max(1, hi - lo), 0, 1)[..., None]
        oli[mm, :3] = (np.array([46, 70, 18]) * (1 - t) + np.array([196, 222, 96]) * t)[mm]
        tile('olivine', oli, 'item')
    items.setdefault('pebble', {'key': 'pebble', 'name': 'Pebble', 'maxStack': 64, 'icon': 'pebble', 'desc': 'Four make two cobblestone; with flint, gravel.'})
    items.setdefault('olivine', {'key': 'olivine', 'name': 'Olivine', 'maxStack': 64, 'icon': 'olivine' if 'olivine' in tiles else 'emerald', 'desc': 'A green gem the trommel finds. With glass it makes tinted glass.'})

    def recipe(r):
        if not any(x.get('out') == r['out'] and x.get('shape') == r.get('shape') and x.get('key') == r.get('key') for x in recipes['recipes']):
            recipes['recipes'].append(r)
    recipe({'out': 'trommel', 'n': 1, 'shape': ['CCC', 'CMC', 'LLL'], 'key': {'C': '#stone_tool', 'M': 'iron_mesh', 'L': '#log'}, 'h': 3, 'w': 3})
    recipe({'out': 'cobblestone', 'n': 2, 'shape': ['PP', 'PP'], 'key': {'P': 'pebble'}, 'h': 2, 'w': 2})
    recipe({'out': 'gravel', 'n': 1, 'shape': ['PF', 'FP'], 'key': {'P': 'pebble', 'F': 'flint'}, 'h': 2, 'w': 2})
    if 'tinted_glass' in items: recipe({'out': 'tinted_glass', 'n': 4, 'shape': ['GO', 'OG'], 'key': {'G': 'glass', 'O': 'olivine'}, 'h': 2, 'w': 2})

    # ---- the sifting table (only what this game has) ----
    out = {'sift': {}, 'alias': {}}
    for k, rows in SIFT.items():
        if k not in at and k not in items: continue
        out['sift'][k] = [[w, it, a, b] for (w, it, a, b) in rows if it in items]
    for k, v in ALIAS.items():
        if k in at and v in out['sift']: out['alias'][k] = v
    (d / 'trommel.json').write_text(json.dumps(out, separators=(',', ':')))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'recipes.json').write_text(json.dumps(recipes, separators=(',', ':'), ensure_ascii=False))
    print('trommel: blocks trommel / lit_trommel, %d inputs sift (%d by alias)' % (len(out['sift']), len(out['alias'])))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
