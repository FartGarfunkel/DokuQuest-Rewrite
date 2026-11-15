#!/usr/bin/env python3
"""tools/port/polish_data.py — the "polish and feel" data (idempotent; data_patches.py runs it last):

  * the feeding trough (Animal Feeding Trough's): a wooden trough holding a stack of food; the farm animals that eat it
    come to it and feed themselves into the mood to breed (sim/trough.cpp), the food showing in it as it fills;
  * the waystone as a pillar two blocks tall: a stepped plinth of dark brick, a shaft with runes cut down its four faces,
    a stepped (pagoda) cap. The lower block sets the upper above it and breaking either takes both (sim/extra_items.cpp's
    two-block statues); the runes light up on the ones you're attuned to (render/waystone_layer.cpp);

  python3 tools/port/polish_data.py [data]"""
import json, pathlib, sys
from PIL import Image

# the runes cut down a shaft face: three glyphs, 32 px tile, drawn in 2 px strokes (x0, y0, x1, y1 inclusive, in cells of 2)
GLYPHS = [
    [(3, 0, 4, 0), (3, 1, 3, 3), (4, 3, 4, 3)],          # a hook
    [(2, 0, 2, 3), (5, 0, 5, 3), (3, 1, 4, 1), (3, 3, 4, 3)],   # a gate
    [(3, 0, 4, 0), (2, 1, 2, 2), (5, 1, 5, 2), (3, 3, 4, 3), (3, 2, 3, 2)],   # an eye
]


def rune_cells():
    """the glyph cells of a shaft face (x, y in 0..15, 2 px each in a 32 px tile; y down), three glyphs one over another"""
    cells = set()
    for g, glyph in enumerate(GLYPHS):
        oy = 1 + g * 5
        for x0, y0, x1, y1 in glyph:
            for x in range(x0, x1 + 1):
                for y in range(y0, y1 + 1): cells.add((x + 4, y + oy))
    return cells


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    sp = json.loads((d / 'shapes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    bkeys = {b['key']: b for b in bj['blocks'] if isinstance(b, dict) and 'key' in b}

    def block_tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    # ---- the waystone's stone: the stone bricks darkened and cooled, and the same with the runes cut in ----
    src = Image.open(d / files[tiles['stone_bricks']]).convert('RGBA').resize((32, 32), Image.NEAREST)
    brick = Image.new('RGBA', (32, 32))
    for y in range(32):
        for x in range(32):
            r, g, b, a = src.getpixel((x, y))
            l = (r + g + b) / 3 * 0.62
            brick.putpixel((x, y), (int(l * 0.92), int(l * 0.96), int(min(255, l * 1.14)), 255))
    rune = brick.copy()
    for (cx, cy) in rune_cells():   # a groove: dark, its lower lip catching the light
        for x in (cx * 2, cx * 2 + 1):
            for y in (cy * 2, cy * 2 + 1):
                r, g, b, _ = brick.getpixel((x, y))
                rune.putpixel((x, y), (int(r * 0.42), int(g * 0.44), int(b * 0.52), 255))
    t_brick = block_tile('waystone_brick', brick)
    t_rune = block_tile('waystone_rune', rune)
    (d / 'waystone_runes.json').write_text(json.dumps({'_comment': 'the rune cells of a waystone shaft face (16 x 16, y down): render/waystone_layer.cpp lights them', 'cells': sorted(rune_cells())}))

    # ---- the pillar: two model blocks ----
    sp['shapes']['waystone_lower'] = [
        {'b': [0, 0, 0, 16, 2, 16], 'side': 'waystone_brick', 'end': 'waystone_brick'},      # the plinth, stepping in
        {'b': [1, 2, 1, 15, 4, 15], 'side': 'waystone_brick', 'end': 'waystone_brick'},
        {'b': [2, 4, 2, 14, 5, 14], 'side': 'waystone_brick', 'end': 'waystone_brick'},
        {'b': [3, 5, 3, 13, 16, 13], 'side': 'waystone_rune', 'end': 'waystone_brick'}]      # the shaft, runes down it
    sp['shapes']['waystone_upper'] = [
        {'b': [3, 0, 3, 13, 9, 13], 'side': 'waystone_rune', 'end': 'waystone_brick'},
        {'b': [2, 9, 2, 14, 10.5, 14], 'side': 'waystone_brick', 'end': 'waystone_brick'},   # the collar
        {'b': [1, 10.5, 1, 15, 12, 15], 'side': 'waystone_brick', 'end': 'waystone_brick'},  # the eave, then the steps up
        {'b': [3, 12, 3, 13, 13.5, 13], 'side': 'waystone_brick', 'end': 'waystone_brick'},
        {'b': [5, 13.5, 5, 11, 15, 11], 'side': 'waystone_brick', 'end': 'waystone_brick'},
        {'b': [7, 15, 7, 9, 16, 9], 'side': 'waystone_brick', 'end': 'waystone_brick'}]      # the finial
    w = bkeys['waystone']
    w.update({'render': 6, 'opaque': False, 'shape': 'waystone_lower', 'statueLower': 'waystone_upper', 'tex': 'waystone_rune'})
    bj['faces'][w['id']] = [t_rune] * 6
    if 'waystone_upper' not in bkeys:
        u = dict(w); u.pop('statueLower', None)
        u.update({'key': 'waystone_upper', 'shape': 'waystone_upper', 'statueUpper': 'waystone', 'drop': 'waystone', 'id': len(bj['blocks'])})
        bj['blocks'].append(u); bj['faces'].append([t_rune] * 6); bkeys['waystone_upper'] = u
    else:
        bkeys['waystone_upper'].update({'shape': 'waystone_upper', 'statueUpper': 'waystone', 'drop': 'waystone', 'render': 6, 'opaque': False, 'waystone': True, 'light': w.get('light', 8)})

    # ---- the feeding trough: on two runners, a plank floor, four plank walls (the food shows inside: render/trough_layer.cpp) ----
    P = 'planks'
    sp['shapes']['feeding_trough'] = [
        {'b': [1, 0, 1, 3, 3, 15], 'side': P, 'end': P}, {'b': [13, 0, 1, 15, 3, 15], 'side': P, 'end': P},     # the runners
        {'b': [0, 3, 1, 16, 4, 15], 'side': P, 'end': P},                                                      # the floor
        {'b': [0, 4, 1, 16, 9, 2.5], 'side': P, 'end': P}, {'b': [0, 4, 13.5, 16, 9, 15], 'side': P, 'end': P},  # the long walls
        {'b': [0, 4, 2.5, 1.5, 9, 13.5], 'side': P, 'end': P}, {'b': [14.5, 4, 2.5, 16, 9, 13.5], 'side': P, 'end': P}]
    if 'feeding_trough' not in bkeys:
        t = {'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 2, 'tool': 'axe', 'tier': 0, 'drop': 'feeding_trough',
             'light': 0, 'lightAtt': 0, 'sound': 'wood', 'key': 'feeding_trough', 'name': 'Feeding Trough', 'tex': P, 'shape': 'feeding_trough',
             'trough': True, 'fuel': 1.5, 'id': len(bj['blocks'])}
        bj['blocks'].append(t); bj['faces'].append([tiles[P]] * 6); bkeys['feeding_trough'] = t
    items = json.loads((d / 'items.json').read_text())
    if 'feeding_trough' not in items:
        items['feeding_trough'] = {'key': 'feeding_trough', 'name': 'Feeding Trough', 'maxStack': 64, 'icon': 'block:feeding_trough', 'block': 'feeding_trough', 'fuel': 1.5,
                                   'desc': 'Fill it with an animal\'s food: the animals that eat it come and feed themselves, and breed.'}
        (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=False))
    rj = json.loads((d / 'recipes.json').read_text())
    # planks in a U with wheat in the bottom (P P / PPP alone is the rowboat's, S S / PPP the shelves')
    want = {'out': 'feeding_trough', 'n': 1, 'shape': ['P P', 'PWP'], 'key': {'P': '#planks', 'W': 'wheat'}, 'h': 2, 'w': 3}
    old = [r for r in rj['recipes'] if r.get('out') == 'feeding_trough']
    if len(old) != 1 or any(old[0].get(k) != v for k, v in want.items()):
        rj['recipes'] = [r for r in rj['recipes'] if r.get('out') != 'feeding_trough']
        rj['recipes'].append(dict(want, idx=len(rj['recipes'])))
        (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))

    # ---- the fallen adventurers (our own, after Realm RPG's idea): an adventurer's bones where they fell, their gear about
    #      them, a few of their things to find (sim/fallen.cpp: the loot by where they lie, rolled the first time they're
    #      opened; now and then a trap). Silk Touch takes them whole (they're items too); else bones.
    BONE, SKULL = 'bone_block_side', 'bone_block_top'
    def bx(b, t): return {'b': b, 'side': t, 'end': t}
    sitting = [bx([5, 0, 6, 11, 2, 10], BONE), bx([5.5, 2, 7.5, 10.5, 8, 9], BONE), bx([5, 0.5, 0, 7, 2, 6], BONE), bx([9, 0.5, 0, 11, 2, 6], BONE),
               bx([3.5, 2, 7, 5, 7.5, 8.5], BONE), bx([11, 2, 7, 12.5, 7.5, 8.5], BONE), bx([5.5, 7.5, 5.5, 10.5, 12, 10], SKULL)]
    lying = [bx([1, 0, 5.5, 5.5, 4, 10.5], SKULL), bx([5.5, 0, 6, 11, 2.5, 10], BONE), bx([11, 0, 6, 16, 1.5, 7.5], BONE), bx([11, 0, 8.5, 16, 1.5, 10], BONE),
             bx([6, 0, 3, 10, 1.2, 4.5], BONE), bx([6, 0, 11.5, 10, 1.2, 13], BONE)]
    kneeling = [bx([5, 3, 7, 11, 5, 10], BONE), bx([5.5, 5, 7.5, 10.5, 10, 9], BONE), bx([5, 0, 3, 7, 3, 9], BONE), bx([9, 0, 3, 11, 3, 9], BONE),
                bx([3.5, 4, 7, 5, 9, 8.5], BONE), bx([11, 4, 5, 12.5, 6, 8.5], BONE), bx([5.5, 9.5, 6, 10.5, 14, 10.5], SKULL)]
    EYE = 'coal_block'   # the skulls' eye sockets: dark hollows on the face the skull looks out of
    sitting += [bx([6.4, 9.6, 5.3, 7.9, 11, 5.6], EYE), bx([8.1, 9.6, 5.3, 9.6, 11, 5.6], EYE)]
    lying += [bx([2, 3.8, 6.3, 3.6, 4.1, 7.8], EYE), bx([2, 3.8, 8.2, 3.6, 4.1, 9.7], EYE)]
    kneeling += [bx([6.4, 11.6, 5.8, 7.9, 13, 6.1], EYE), bx([8.1, 11.6, 5.8, 9.6, 13, 6.1], EYE)]
    REMAINS = [   # key, name, pose, gear boxes
        ('remains_traveller', 'Fallen Traveller', sitting, [bx([5, 11, 5, 11, 13.5, 11], 'iron_block'), bx([5, 2, 9, 11, 8, 12.5], 'brown_wool')]),
        ('remains_desert', 'Fallen Desert Wanderer', lying, [bx([0.5, 3, 5, 6, 5, 11], 'gold_block'), bx([6, 0, 5.5, 11, 3, 10.5], 'wool')]),
        ('remains_snowy', 'Frozen Explorer', sitting, [bx([5, 10.5, 5, 11, 13, 11], 'wool'), bx([4.5, 2, 6.5, 11.5, 8.5, 9.5], 'wool')]),
        ('remains_jungle', 'Lost Explorer', lying, [bx([5.5, 0, 5.5, 11, 3, 10.5], 'green_wool'), bx([3, 0, 1, 14, 0.5, 15], 'lime_wool')]),
        ('remains_swamp', 'Drowned Wanderer', lying, [bx([5.5, 0, 5.5, 11, 2.8, 10.5], 'brown_wool')]),
        ('remains_miner', 'Fallen Miner', kneeling, [bx([5, 13, 5.5, 11, 15, 11], 'iron_block'), bx([12.5, 0, 8, 13.5, 11, 9], 'planks'), bx([10, 10, 7.5, 16, 11.5, 9.5], 'iron_block'), bx([2, 0, 11, 5, 4, 14], 'lantern')]),
    ]
    items = json.loads((d / 'items.json').read_text()); ichanged = False
    for key, name, pose, gear in REMAINS:
        sp['shapes'][key] = pose + gear
        if key not in bkeys:
            r = {'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 1, 'tool': 'pickaxe', 'tier': 0, 'drop': 'bone', 'dropCount': 3,
                 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name, 'tex': BONE, 'shape': key, 'facingModel': True,
                 'container': 9, 'remains': key.split('_', 1)[1], 'id': len(bj['blocks'])}
            bj['blocks'].append(r); bj['faces'].append([tiles[BONE]] * 6); bkeys[key] = r
        if key not in items:
            items[key] = {'key': key, 'name': name, 'maxStack': 16, 'icon': 'block:' + key, 'block': key, 'desc': 'Someone who went this way before you. Use it to see what they left.'}
            ichanged = True
    if ichanged: (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=False))

    # its item: the pillar itself (assets/item_models.cpp shapedBlockBoxes stacks a two-block statue's halves), not the
    # old cube of rune tiles itemshapes.json had for it
    ish = json.loads((d / 'itemshapes.json').read_text())
    if ish['blockItems'].pop('waystone', None) is not None: (d / 'itemshapes.json').write_text(json.dumps(ish, separators=(',', ':')))

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'shapes.json').write_text(json.dumps(sp, indent=1))
    print('polish_data: waystone pillar (%d tiles)' % tj['count'])
    ore_drops(d)
    station_packmap(d)


# ---- ore blocks give their ore (the raw ore), not the block: iron ore -> Iron Ore (the immersive ore piece, the
# same item); it smelts into its ingot in a furnace or a blast furnace. The rarer metals get their own ore items, the
# iron one's picture in their colour. Immersive worlds are as they were (their pieces go to the bloomery; smeltResult
# refuses ore pieces there); silk touch still gives the block (sim/enchanting.cpp).
RARE_ORE = {'mithril': (120, 200, 230), 'adamantite': (200, 60, 80), 'thallasium': (110, 210, 190), 'gravitite': (200, 120, 220)}


def ore_drops(d):
    from PIL import Image
    import numpy as np
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    sm = rj['smelting']; tiles = tj['names']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    base = d / tj['files'][tiles['imm_iron_ore_piece']] if 'imm_iron_ore_piece' in tiles else None
    n = 0
    for b in bj['blocks']:
        k = b['key']
        if not k.endswith('_ore') or k.startswith('nether_gold'): continue
        out = sm.get(k, '')
        if not (out.endswith('_ingot') or out == 'gravitite_plate'): continue
        metal = out[:-len('_ingot')] if out.endswith('_ingot') else 'gravitite'
        piece = metal + '_ore_piece'
        if piece not in items:
            if metal not in RARE_ORE or base is None: continue
            name = 'imm_%s_ore_piece' % metal
            rel = 'textures/item/immersive/%s.png' % name
            a = np.asarray(Image.open(base).convert('RGBA')).astype(np.float32)
            l = (a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11) / 255.0
            c = RARE_ORE[metal]
            bright = l > np.quantile(l[a[..., 3] > 0], 0.6) if (a[..., 3] > 0).any() else l > 1
            for ch in range(3): a[..., ch] = np.where(bright, np.clip(l * c[ch] * 1.5, 0, 255), a[..., ch])   # its flecks in the metal's colour
            Image.fromarray(a.astype(np.uint8), 'RGBA').save(d / rel)
            if name not in tiles: tiles[name] = tj['count']; tj['files'].append(rel); tj['res'].append(32); tj['count'] += 1
            items[piece] = {'key': piece, 'name': metal.title() + ' Ore', 'icon': name, 'maxStack': 64, 'ore': metal}
        if b.get('drop') != piece: b['drop'] = piece; n += 1
        sm.setdefault(piece, out)
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('polish_data: %d ore blocks give their ore' % n)


# ---- stations' own tiles take the pack's pictures (packmap: [our tile, as a block, as an item]): the enchanting table
# drew its built-in tiles under every pack, the pack's enchanting_table_* never looked for
PACK_NAMES = {'ench_top': 'enchanting_table_top', 'ench_side': 'enchanting_table_side', 'ench_bottom': 'enchanting_table_bottom',
              'brew_base': 'brewing_stand_base'}


def station_packmap(d):
    pm = json.loads((d / 'packmap.json').read_text())
    n = 0
    for e in pm:
        if e[0] in PACK_NAMES:
            want = [PACK_NAMES[e[0]], e[0]]
            if e[1] != want: e[1] = want; n += 1
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':')))
    print('polish_data: %d station tiles take the pack\'s pictures' % n)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
