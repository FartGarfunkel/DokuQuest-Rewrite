#!/usr/bin/env python3
# tools/port/horse_gear.py — horse armour, in the wolf armour's six materials (sim/mount_gear.cpp: a tamed horse wears it
# and takes less harm; render/creature_layer.cpp draws the barding). The pictures are painted here (a horse's barding seen
# from the side: the face plate, the neck plate and the body's cloth with its rivets); a resource pack with Minecraft's
# iron / golden / diamond horse armour pictures shows those instead (packmap.json; DokuQuest Base has them; its leather
# one is a grey base for dye, so leather keeps ours). Nothing is written into a resource pack. Hooked into data_patches.py.
#   python3 tools/port/horse_gear.py data
import json, pathlib, sys
from PIL import Image, ImageDraw

# key, name, colour, armour points, durability, what it's made of, the pack's picture
MATS = [('leather', 'Leather', (138, 90, 50), 3, 120, 'leather', None),
        ('chainmail', 'Chainmail', (138, 144, 152), 4, 240, 'chainlink', None),
        ('iron', 'Iron', (200, 200, 204), 5, 360, 'iron_ingot', 'iron_horse_armor'),
        ('gold', 'Gold', (232, 192, 64), 6, 165, 'gold_ingot', 'golden_horse_armor'),
        ('diamond', 'Diamond', (90, 220, 208), 9, 780, 'diamond', 'diamond_horse_armor'),
        ('steel', 'Steel', (106, 114, 128), 8, 900, 'steel_ingot', None)]


def shade(c, k): return tuple(max(0, min(255, int(v * k))) for v in c) + (255,)


def icon(base, mat):
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); g = ImageDraw.Draw(im)
    dark, mid, light, hi = shade(base, 0.5), shade(base, 0.92), shade(base, 1.18), shade(base, 1.45)
    # the body's cloth (a horse facing left): a rounded saddle-cloth with a scalloped hem
    g.rounded_rectangle([13, 11, 30, 23], 3, fill=mid, outline=dark)
    for x in range(14, 30, 4): g.pieslice([x, 20, x + 4, 26], 0, 180, fill=mid, outline=dark)
    g.line([15, 12, 28, 12], fill=light)                                  # the light along its back
    # the neck plate, from the cloth up to the poll
    g.polygon([(8, 5), (13, 3), (19, 12), (15, 17), (11, 11)], fill=mid, outline=dark)
    g.line([(13, 4), (17, 11)], fill=light)
    # the face plate, tapering down to the muzzle, with the eye hole
    g.polygon([(2, 11), (8, 5), (11, 11), (5, 17), (2, 16)], fill=light, outline=dark)
    g.line([(3, 12), (8, 7)], fill=hi)
    im.putpixel((7, 10), (0, 0, 0, 0)); im.putpixel((7, 11), dark)
    # what the material shows: rivets on metal, stitches on leather, rings on mail, a sparkle on diamond
    if mat == 'leather':
        for x in range(15, 29, 2): im.putpixel((x, 18), dark)
    elif mat == 'chainmail':
        for y in range(14, 22, 2):
            for x in range(15 + (y // 2) % 2, 29, 2): im.putpixel((x, y), dark)
    else:
        for x in range(16, 29, 3): im.putpixel((x, 14), hi); im.putpixel((x, 15), dark)
        g.line([15, 19, 28, 19], fill=dark)
    if mat == 'diamond':
        for p in ((20, 16), (25, 17), (4, 13)): im.putpixel(p, (255, 255, 255, 255))
    if mat == 'gold':
        g.line([16, 17, 27, 17], fill=hi)
    return im


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text()); rc = json.loads((d / 'recipes.json').read_text())
    pm = json.loads((d / 'packmap.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    for mat, name, col, pts, dur, ing, packpic in MATS:
        k = mat + '_horse_armor'; rel = 'textures/item/%s.png' % k
        icon(col, mat).save(d / rel)
        if k not in tiles: tiles[k] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        items[k] = {'key': k, 'name': name + ' Horse Armour', 'maxStack': 1, 'icon': k,
                    'horseArmor': {'points': pts, 'durability': dur, 'mat': mat},
                    'desc': 'For a horse you have tamed: use it on the horse, or sneak and use the horse with an empty hand to open its gear.'}
        if k not in sh['itemTiles']: sh['itemTiles'].append(k)
        rc['recipes'] = [r for r in rc['recipes'] if r.get('out') != k]
        if ing in items:
            if mat == 'leather': rc['recipes'].append({'out': k, 'n': 1, 'shape': ['X X', 'XXX', 'X X'], 'key': {'X': ing}, 'h': 3, 'w': 3, 'group': 'transport'})
            else: rc['recipes'].append({'out': k, 'n': 1, 'shape': ['X X', 'XLX', 'X X'], 'key': {'X': ing, 'L': 'leather'}, 'h': 3, 'w': 3, 'group': 'transport'})
        pm = [e for e in pm if e[0] != k]
        pm.append([k, [], [packpic] if packpic else []])   # (no entry name: our own picture stays)
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rc, separators=(',', ':'), ensure_ascii=False))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':'), ensure_ascii=False))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
