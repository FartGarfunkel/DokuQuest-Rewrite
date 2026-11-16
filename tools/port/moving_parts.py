#!/usr/bin/env python3
"""tools/port/moving_parts.py — the moving structures' parts (docs/superpowers/specs/2026-11-16-moving-structures-design.md):
the Hinge and the Slider (six-way blocks: the face that pointed at you when placed is the one that moves what's glued to
it) and Glue, with their pictures drawn here and their recipes. Idempotent; data_patches.py runs it.

  python3 tools/port/moving_parts.py [data-dir]
"""
import json, pathlib, sys
from PIL import Image, ImageDraw


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text()); bj = json.loads((d / 'blocks.json').read_text())
    sh = json.loads((d / 'itemshapes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/block').mkdir(parents=True, exist_ok=True); (d / 'textures/item').mkdir(parents=True, exist_ok=True)

    def pic(name):
        return Image.open(d / files[tiles[name]]).convert('RGBA').resize((32, 32), Image.NEAREST)

    def tile(name, im, item=False):
        rel = 'textures/%s/%s.png' % ('item' if item else 'block', name)
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if item and name not in sh['itemTiles']: sh['itemTiles'].append(name)
        return tiles[name]

    def recipe(out, n, shape=None, key=None, items_=None):
        if any(r.get('out') == out and r.get('items') == items_ and r.get('shape') == shape for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r['items'] = items_
        rj['recipes'].append(r)

    IRON, DARK, PIN = (196, 198, 204, 255), (92, 94, 102, 255), (150, 152, 160, 255)
    planks = pic('planks')

    def framed(base):   # planks with an iron band round the edge
        im = base.copy(); g = ImageDraw.Draw(im)
        g.rectangle([0, 0, 31, 31], outline=DARK); g.rectangle([1, 1, 30, 30], outline=IRON)
        return im

    # the hinge: an iron knuckle on its face, its pin showing; the sides banded planks
    hf = framed(planks); g = ImageDraw.Draw(hf)
    g.rectangle([12, 4, 19, 27], fill=IRON, outline=DARK)
    for y in (9, 15, 21): g.line([12, y, 19, y], fill=DARK)
    g.rectangle([14, 2, 17, 29], outline=PIN)
    hs = framed(planks); g = ImageDraw.Draw(hs); g.rectangle([2, 13, 29, 18], fill=IRON, outline=DARK)
    # the slider: a rail groove across its face, its carriage in the middle
    sf = framed(planks); g = ImageDraw.Draw(sf)
    g.rectangle([3, 13, 28, 18], fill=DARK); g.line([3, 15, 28, 15], fill=IRON); g.rectangle([12, 10, 19, 21], fill=IRON, outline=DARK)
    ss = framed(planks); g = ImageDraw.Draw(ss); g.line([2, 15, 29, 15], fill=IRON); g.line([2, 16, 29, 16], fill=DARK)
    back = framed(planks)
    t = {n: tile(n, im) for n, im in (('hinge_front', hf), ('hinge_side', hs), ('slider_front', sf), ('slider_side', ss), ('machine_back', back))}
    # glue: a small pot of amber paste
    gl = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); g = ImageDraw.Draw(gl)
    g.rectangle([9, 14, 22, 27], fill=(120, 84, 52, 255), outline=(70, 46, 26, 255))
    g.ellipse([8, 9, 23, 17], fill=(232, 166, 46, 255), outline=(150, 96, 20, 255)); g.ellipse([12, 11, 17, 14], fill=(255, 214, 120, 255))
    tile('glue', gl, item=True)

    blocks = bj['blocks']
    faces = bj['faces']
    while len(faces) < len(blocks): faces.append(None)   # (faces are read by the block's place in the list)
    for key, name, front, side in (('hinge', 'Hinge', 'hinge_front', 'hinge_side'), ('slider', 'Slider', 'slider_front', 'slider_side')):
        if any(b['key'] == key for b in blocks): continue
        blocks.append({'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 2.5, 'tool': 'pickaxe',
                       'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'wood', 'key': key, 'name': name, 'machine': key,
                       'tex': {'front': front, 'side': side, 'top': side, 'back': 'machine_back'}, 'facing6': True,
                       'f6': {'front': t[front], 'frontV': t[front], 'back': t['machine_back'], 'backOn': t['machine_back'], 'side': t[side], 'top': t[side]}})
        faces.append([t[side], t[side], t[side], t[side], t[front], t[side]])
    for key, name in (('hinge', 'Hinge'), ('slider', 'Slider')):
        items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
    items['glue'] = {'key': 'glue', 'name': 'Glue', 'maxStack': 64, 'icon': 'glue'}
    K = {'I': 'iron_ingot', 'P': 'planks', 'R': 'redstone'}
    recipe('hinge', 1, ['IPI', 'PRP', 'IPI'], K)
    recipe('slider', 1, ['PIP', 'IRI', 'PIP'], K)
    recipe('glue', 4, items_=['slime_ball'])
    recipe('glue', 4, items_=['honey_bottle'])

    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
