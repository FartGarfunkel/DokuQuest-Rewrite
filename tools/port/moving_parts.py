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

    # the lift: a rail (an iron track with a toothed rack down its middle) and a stop (an iron plate with a red call button)
    rl = Image.new('RGBA', (32, 32), DARK); g = ImageDraw.Draw(rl)
    g.rectangle([1, 0, 30, 31], fill=PIN); g.rectangle([11, 0, 20, 31], fill=DARK)
    for y in range(1, 32, 4): g.rectangle([13, y, 18, y + 1], fill=IRON)
    g.line([1, 0, 1, 31], fill=IRON); g.line([30, 0, 30, 31], fill=IRON)
    st = framed(Image.new('RGBA', (32, 32), PIN)); g = ImageDraw.Draw(st)
    g.rectangle([9, 9, 22, 22], fill=DARK); g.ellipse([11, 11, 20, 20], fill=(196, 40, 36, 255), outline=(110, 18, 16, 255)); g.ellipse([13, 12, 16, 15], fill=(240, 120, 110, 255))
    t['lift_rail'] = tile('lift_rail', rl); t['lift_stop'] = tile('lift_stop', st); t['lift_stop_side'] = tile('lift_stop_side', framed(Image.new('RGBA', (32, 32), PIN)))

    blocks = bj['blocks']
    faces = bj['faces']
    for bb in blocks:   # hanging chains climb like ropes and ladders (strung ones: sim/strung.cpp)
        if bb['key'] == 'chain': bb['climb'] = True
    while len(faces) < len(blocks): faces.append(None)   # (faces are read by the block's place in the list)
    for key, name, front, side in (('hinge', 'Hinge', 'hinge_front', 'hinge_side'), ('slider', 'Slider', 'slider_front', 'slider_side')):
        if any(b['key'] == key for b in blocks): continue
        blocks.append({'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 2.5, 'tool': 'pickaxe',
                       'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'wood', 'key': key, 'name': name, 'machine': key,
                       'tex': {'front': front, 'side': side, 'top': side, 'back': 'machine_back'}, 'facing6': True,
                       'f6': {'front': t[front], 'frontV': t[front], 'back': t['machine_back'], 'backOn': t['machine_back'], 'side': t[side], 'top': t[side]}})
        faces.append([t[side], t[side], t[side], t[side], t[front], t[side]])
    for key, name, tex in (('lift_rail', 'Lift Rail', 'lift_rail'), ('lift_stop', 'Lift Stop', 'lift_stop')):
        if any(bb['key'] == key for bb in blocks): continue
        side = 'lift_stop_side' if key == 'lift_stop' else tex
        blocks.append({'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 2.5, 'tool': 'pickaxe',
                       'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'metal', 'key': key, 'name': name, 'lift': key,
                       'tex': {'side': tex, 'top': side, 'bottom': side} if key == 'lift_stop' else tex})
        faces.append([t[tex], t[tex], t[side], t[side], t[tex], t[tex]] if key == 'lift_stop' else [t[tex]] * 6)
    DESC = {'hinge': 'Swings what is glued to its face. Facing up or down it turns sideways, like a gate; on a wall it swings up and down, like a drawbridge. Use it, or any block of what it carries, to open and shut it. Sneak-use: which way it turns.',
            'slider': 'Slides what is glued to its face straight out, then back. Use it, or any block of what it carries. Sneak-use: how far (1 to 16 blocks).',
            'lift_rail': 'A lift runs on these. Stack them in a line beside a Slider, alongside the way its piece slides: the slider then carries its piece along the whole rail, stopping at each Lift Stop.',
            'lift_stop': 'Put it beside a Lift Rail to make a stop. Use its button to call the lift there; it gives a redstone signal while the lift is at it.',
            'glue': 'Use on the face between two blocks to stick them together (again to unstick). A hinge or slider moves everything glued to it, up to 512 blocks; glued faces show while you hold it.'}
    for key, name in (('hinge', 'Hinge'), ('slider', 'Slider')):
        items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key, 'desc': DESC[key]}
    for key, name in (('lift_rail', 'Lift Rail'), ('lift_stop', 'Lift Stop')):
        items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key, 'desc': DESC[key]}
    items['glue'] = {'key': 'glue', 'name': 'Glue', 'maxStack': 64, 'icon': 'glue', 'desc': DESC['glue']}
    K = {'I': 'iron_ingot', 'P': 'planks', 'R': 'redstone'}
    recipe('hinge', 1, ['IPI', 'PRP', 'IPI'], K)
    recipe('slider', 1, ['PIP', 'IRI', 'PIP'], K)
    recipe('lift_rail', 8, ['I I', 'I I', 'I I'], {'I': 'iron_ingot'})
    recipe('lift_stop', 1, ['III', 'IBI', 'III'], {'I': 'iron_ingot', 'B': 'stone_button'})
    recipe('glue', 4, items_=['slime_ball'])
    recipe('glue', 4, items_=['honey_bottle'])

    # hanging chains and ropes drawn as the strung ones are (render/strung_layer.cpp): a chain as interlocking iron links,
    # each a quarter turn from the last, two to a block and hooked through each other and into the next block; a rope as
    # three strands twisting round each other (sizes in 16ths of a block)
    shp = json.loads((d / 'shapes.json').read_text())
    S = shp['shapes']
    link = []
    def ringXY(y0, y1):   # a link facing front-back: an oval of four bars, 1 thick, in the middle z
        z0, z1 = 7.5, 8.5
        return [[6, y0 + 1, z0, 7, y1 - 1, z1], [9, y0 + 1, z0, 10, y1 - 1, z1], [6.5, y0, z0, 9.5, y0 + 1, z1], [6.5, y1 - 1, z0, 9.5, y1, z1]]
    def ringZY(y0, y1):   # the next, a quarter turn round
        x0, x1 = 7.5, 8.5
        return [[x0, y0 + 1, 6, x1, y1 - 1, 7], [x0, y0 + 1, 9, x1, y1 - 1, 10], [x0, y0, 6.5, x1, y0 + 1, 9.5], [x0, y1 - 1, 6.5, x1, y1, 9.5]]
    for b in ringXY(0, 9) + ringZY(7, 16): link.append({'b': b, 'side': 'iron_block', 'link': True})
    S['chain'] = link

    # a hanging light's chain (lanterns, soul lanterns...: every "<model>_hang") is the same interlocking links, scaled down
    # to fit between its cap and the ceiling: two links, the lower hooked into the cap, the upper into the ceiling (or the
    # chain above, which the mesher adds in full-size links: render/mesh_shapes.cpp emitModel). "link" marks them.
    def ring(y0, y1, hw, t, zy):   # an oval of four bars in the middle of the block, facing front-back (zy: a quarter turn round)
        c0, c1 = 8 - t / 2, 8 + t / 2
        bars = [[8 - hw, y0 + t, 8 - hw + t, y1 - t], [8 + hw - t, y0 + t, 8 + hw, y1 - t], [8 - hw + t / 2, y0, 8 + hw - t / 2, y0 + t], [8 - hw + t / 2, y1 - t, 8 + hw - t / 2, y1]]
        r3 = lambda v: round(v, 4)
        return [[r3(c0), r3(a1), r3(a0), r3(c1), r3(b1), r3(b0)] if zy else [r3(a0), r3(a1), r3(c0), r3(b0), r3(b1), r3(c1)] for a0, a1, b0, b1 in bars]
    for name in [n for n in S if n.endswith('_hang')]:
        boxes = S[name]
        old = [x for x in boxes if x.get('side') == 'chain' or x.get('link')]
        if not old: continue
        bot = min(x['b'][1] for x in old); top = max(x['b'][4] for x in old)   # (the old strip: from the cap's top to the ceiling)
        keep = [x for x in boxes if not (x.get('side') == 'chain' or x.get('link'))]
        span = top - bot; h = span * 0.65; t = 0.75; hw = 1.5
        links = ring(bot - t, bot - t + h, hw, t, True) + ring(top - h, top, hw, t, False)
        S[name] = keep + [{'b': b, 'side': 'iron_block', 'link': True} for b in links]
    import math
    rope = []
    for k in range(8):
        y0, a0 = k * 2, k * math.pi / 4
        for j in range(3):
            a = a0 + j * 2 * math.pi / 3
            cx, cz = 8 + 0.55 * math.cos(a), 8 + 0.55 * math.sin(a)
            rope.append({'b': [round(cx - 0.55, 3), y0, round(cz - 0.55, 3), round(cx + 0.55, 3), y0 + 2, round(cz + 0.55, 3)], 'side': 'rope', 'end': 'rope'})
    S['rope'] = rope
    (d / 'shapes.json').write_text(json.dumps(shp, indent=1, ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
