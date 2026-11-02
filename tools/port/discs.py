#!/usr/bin/env python3
# tools/port/discs.py — more music discs: each plays its own piece in a jukebox (audio/music.cpp discMood: the same
# tune every time). Pictures are the Sunrise disc's with the label recoloured. Hooked into data_patches.py.
#   python3 tools/port/discs.py data
import colorsys, json, pathlib, sys
from PIL import Image

DISCS = [  # key, name, label colour (hue 0..1, saturation)
    ('skylark', 'Skylark', 0.55, 0.55), ('void', 'Void', 0.78, 0.5), ('aurora', 'Aurora', 0.40, 0.6), ('lantern', 'Lantern', 0.11, 0.75),
    ('harvest', 'Harvest', 0.08, 0.7), ('frost', 'Frost', 0.53, 0.35), ('dune', 'Dune', 0.13, 0.5), ('crag', 'Crag', 0.0, 0.65)]


def relabel(im, hue, sat):
    im = im.copy(); px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a < 8: continue
            h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            if s < 0.22: continue   # the black vinyl and its grooves stay
            nr, ng, nb = colorsys.hls_to_rgb(hue, l, min(1.0, sat + s * 0.3))
            px[x, y] = (int(nr * 255), int(ng * 255), int(nb * 255), a)
    return im


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text()); mg = json.loads((d / 'magic.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    src = Image.open(d / 'textures/item/music_disc_sun.png').convert('RGBA')
    for key, name, hue, sat in DISCS:
        k = 'music_disc_' + key
        rel = 'textures/item/%s.png' % k
        relabel(src, hue, sat).save(d / rel)
        if k not in tiles: tiles[k] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        items[k] = {'key': k, 'name': 'Music Disc — ' + name, 'maxStack': 1, 'icon': k, 'disc': key, 'rarity': 'rare', 'desc': 'Put it in a jukebox.'}
        if k not in sh['itemTiles']: sh['itemTiles'].append(k)
        if 'alch' in mg and isinstance(mg['alch'], dict): mg['alch'][k] = 3
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'magic.json').write_text(json.dumps(mg, separators=(',', ':'), ensure_ascii=False))
    print('discs ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
