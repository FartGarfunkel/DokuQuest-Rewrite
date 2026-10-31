#!/usr/bin/env python3
"""Spawn eggs for every creature (idempotent; data_patches.py runs it):
  python3 tools/port/spawn_eggs.py <data-dir>
- one item <mob>_spawn_egg per creature in mobs.json (creative catalogue, "Spawn Eggs" tab; sim/usables.cpp spawns it)
- one tile spawn_egg_<mob> per egg, painted from the base pack's grey spawn_egg + spawn_egg_overlay pictures tinted with
  two colours taken from the creature's own pictures, so every egg has a picture even with no pack
- packmap entries so a pack's own <name>_spawn_egg picture (Minecraft 1.20.5+ layout) replaces the painted one
Never makes an egg for creatures that only the story brings (SKIP)."""
import json, sys, pathlib, hashlib
from PIL import Image

SKIP = {'herobrine', 'end_crystal'}
# our creature -> the Minecraft name its pack egg picture uses, where ours stands in for theirs
MC = {'ghoul': 'zombie', 'fuseling': 'creeper', 'stone_mite': 'silverfish', 'zombie_pigman': 'zombified_piglin',
      'magma_slime': 'magma_cube', 'snow_fox': 'fox_snow'}
# fallback colours (primary, secondary) for creatures with no pictures of their own
KNOWN = {'verdant': ((74, 140, 62), (196, 214, 92)), 'sand': ((214, 186, 120), (140, 98, 52)),
         'frost': ((176, 214, 236), (70, 120, 170)), 'bog': ((84, 98, 58), (60, 44, 30)),
         'ember': ((176, 52, 24), (250, 170, 50)), 'storm': ((86, 98, 130), (210, 220, 250)),
         'tide': ((40, 130, 140), (190, 236, 226)), 'ender': ((34, 22, 44), (190, 90, 230)),
         'fuseling': ((84, 168, 70), (20, 20, 20)), 'cinder': ((60, 44, 40), (240, 120, 40)),
         'raider': ((92, 92, 96), (60, 48, 40)), 'village_guard': ((150, 150, 160), (60, 80, 140)),
         'pirate': ((120, 40, 40), (220, 200, 160)), 'zombie_pigman': ((220, 140, 140), (70, 110, 60)),
         'aechor': ((96, 170, 90), (220, 120, 180)), 'lich': ((60, 60, 90), (150, 240, 220)),
         'warden_sun': ((240, 190, 60), (120, 70, 20)), 'warden_moon': ((170, 180, 220), (40, 50, 90)),
         'hollow': ((60, 50, 80), (200, 170, 255)), 'gaoler': ((80, 80, 80), (40, 30, 30)),
         'valkyrie': ((230, 220, 180), (120, 150, 210)), 'zephyr': ((220, 230, 245), (120, 140, 180)),
         'aerwhale': ((150, 180, 220), (230, 240, 250)), 'magma': ((80, 20, 10), (250, 160, 40))}


def mean(im):
    px = [p for p in im.convert('RGBA').get_flattened_data() if p[3] > 128]
    if not px: return None
    return tuple(sum(p[i] for p in px) // len(px) for i in range(3))


def colours(d, key, model, tiles, files):
    names = [n for n in tiles if n.startswith(key + '_') or n.startswith(model + '_')]
    pref = sorted(names, key=lambda n: (0 if n.endswith(('_skin', '_hide', '_fur', '_body', '_feather', '_gel', '_scales')) else
                                        2 if n.endswith(('_face', '_eye', '_eyes')) else 1, n))
    cols = []
    for n in pref:
        try: c = mean(Image.open(d / files[tiles[n]]))
        except Exception: c = None
        if c and c not in cols: cols.append(c)
        if len(cols) >= 2: break
    if not cols:
        for k, v in KNOWN.items():
            if k in key: return v
        h = hashlib.md5(key.encode()).digest()
        cols = [(60 + h[0] % 160, 60 + h[1] % 160, 60 + h[2] % 160)]
    p = cols[0]
    s = cols[1] if len(cols) > 1 else tuple(max(0, int(v * 0.45)) for v in p)
    if sum(abs(a - b) for a, b in zip(p, s)) < 70:   # too alike: spots in a darker or lighter shade
        s = tuple(max(0, int(v * 0.45)) for v in p) if sum(p) > 300 else tuple(min(255, int(v * 1.8) + 40) for v in p)
    return p, s


def paint(base, over, c1, c2):
    out = Image.new('RGBA', base.size)
    bmax = max([p[0] for p in base.get_flattened_data() if p[3]] or [255]) or 255
    omax = max([p[0] for p in over.get_flattened_data() if p[3]] or [255]) or 255
    bp, op, o = base.load(), over.load(), out.load()
    for y in range(base.size[1]):
        for x in range(base.size[0]):
            r, g, b, a = bp[x, y]
            if a: o[x, y] = tuple(min(255, int(c1[i] * r / bmax * 1.1)) for i in range(3)) + (a,)
            r2, _, _, a2 = op[x, y]
            if a2: o[x, y] = tuple(min(255, int(c2[i] * r2 / omax * 1.1)) for i in range(3)) + (max(a, a2),)
    return out


def title(k): return ' '.join(w.capitalize() for w in k.split('_'))


def main(d):
    d = pathlib.Path(d)
    pack = d / 'resourcepacks' / 'DokuQuest Base' / 'assets' / 'minecraft' / 'textures' / 'item'
    base = Image.open(pack / 'spawn_egg.png').convert('RGBA').resize((32, 32), Image.NEAREST)
    over = Image.open(pack / 'spawn_egg_overlay.png').convert('RGBA').resize((32, 32), Image.NEAREST)
    mobs = json.loads((d / 'mobs.json').read_text())
    items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text())
    pm = json.loads((d / 'packmap.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text())
    tiles, files = tj['names'], tj['files']
    inPm = {e[0] for e in pm}
    itemTiles = set(sh['itemTiles'])
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))   # (res may stop short of the last tiles: those are full 32 px)
    made = 0
    for key, m in mobs.items():
        if key in SKIP: continue
        tname, rel = 'spawn_egg_' + key, 'textures/item/%s_spawn_egg.png' % key
        c1, c2 = colours(d, key, m.get('model', key), tiles, files)
        paint(base, over, c1, c2).save(d / rel)
        if tname not in tiles:
            tiles[tname] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if tname not in inPm:
            pm.append([tname, [], [MC.get(key, key) + '_spawn_egg']]); inPm.add(tname)
        if tname not in itemTiles: sh['itemTiles'].append(tname); itemTiles.add(tname)
        items[key + '_spawn_egg'] = {'key': key + '_spawn_egg', 'name': title(key) + ' Spawn Egg', 'maxStack': 64,
                                     'icon': tname, 'spawnEgg': key}
        made += 1
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    print('spawn eggs:', made)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
