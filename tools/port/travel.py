#!/usr/bin/env python3
# tools/port/travel.py — things for the road: saddle bags (an 18-slot store a saddled mount carries; sim/creature_beasts.cpp
# useOnCreature puts them on, sneak-use opens them) and the galleon (the bigger ship). Pictures drawn here. Hooked into
# data_patches.py.
#   python3 tools/port/travel.py data
import json, pathlib, sys
from PIL import Image, ImageDraw


def saddle_bags_icon():
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); g = ImageDraw.Draw(im)
    dark, mid, light, buckle = (74, 44, 24, 255), (122, 76, 42, 255), (158, 104, 60, 255), (214, 184, 92, 255)
    g.rectangle([9, 5, 22, 8], fill=mid, outline=dark)                 # the strap over the mount's back
    for x0 in (3, 18):                                                 # the two pouches
        g.rounded_rectangle([x0, 8, x0 + 11, 26], 3, fill=mid, outline=dark)
        g.rectangle([x0 + 1, 9, x0 + 10, 14], fill=light)              # the flap
        g.line([x0 + 1, 15, x0 + 10, 15], fill=dark)
        g.rectangle([x0 + 4, 14, x0 + 7, 17], fill=buckle, outline=dark)
        g.line([x0 + 2, 20, x0 + 9, 20], fill=dark)                    # a stitched seam
        for sx in range(x0 + 2, x0 + 10, 2): im.putpixel((sx, 23), light)
    return im


def galleon_icon(ship):
    im = ship.copy(); px = im.load()
    fore = ship.crop((6, 4, 24, 20)).resize((11, 10), Image.NEAREST)
    for y in range(fore.height):              # a foremast's sail behind the mainsail
        for x in range(fore.width):
            c = fore.getpixel((x, y)); tx, ty = x + 1, y + 9
            if c[3] > 8 and px[tx, ty][3] < 8: px[tx, ty] = c
    for y in range(20, 32):                   # a darker, heavier hull
        for x in range(32):
            r, g, b, a = px[x, y]
            if a > 8: px[x, y] = (int(r * 0.72), int(g * 0.66), int(b * 0.62), a)
    return im


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text()); rc = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    k = 'saddle_bags'; rel = 'textures/item/%s.png' % k
    saddle_bags_icon().save(d / rel)
    if k not in tiles: tiles[k] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    items[k] = {'key': k, 'name': 'Saddle Bags', 'maxStack': 1, 'icon': k,
                'desc': 'Put them on a saddled mount of yours; sneak and use it to open them. 18 slots that go where it goes.'}
    if k not in sh['itemTiles']: sh['itemTiles'].append(k)
    rc['recipes'] = [r for r in rc['recipes'] if r.get('out') != k]
    rc['recipes'].append({'out': k, 'n': 1, 'shape': ['LSL', 'LCL'], 'key': {'L': 'leather', 'S': 'string', 'C': 'chest'}, 'group': 'transport'})
    # the galleon: the sailing ship built bigger, two masts and a 54-slot hold (sim/vehicles.cpp BOATS)
    k = 'galleon'; rel = 'textures/item/%s.png' % k
    galleon_icon(Image.open(d / 'textures/item/sailing_ship.png').convert('RGBA')).save(d / rel)
    if k not in tiles: tiles[k] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
    items[k] = {'key': k, 'name': 'Galleon', 'maxStack': 1, 'icon': k, 'boat': 'galleon', 'rarity': 'epic',
                'desc': 'The sailing ship built bigger: two masts, a wide deck and a hold of 54 slots. Use it to climb aboard; use the wheel to sail.'}
    if k not in sh['itemTiles']: sh['itemTiles'].append(k)
    rc['recipes'] = [r for r in rc['recipes'] if r.get('out') != k]
    rc['recipes'].append({'out': k, 'n': 1, 'shape': ['WWW', 'XCX'], 'key': {'W': '#wool', 'X': 'sailing_ship', 'C': 'chest'}, 'group': 'transport'})
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rc, separators=(',', ':'), ensure_ascii=False))
    print('travel ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
