#!/usr/bin/env python3
"""tools/port/betternether_blocks.py — BetterNether's lands' blocks and plants for the Nether band (idempotent;
data_patches.py runs it):  python3 tools/port/betternether_blocks.py <data-dir>

After BetterNether (github.com/quiqueck/BetterNether, MIT; see docs/CREDITS.md): its jungle, swamp, gravel desert,
magma land, soul plain and old warped woods (gen/nether_bn.cpp paints them, terrain 13). The pictures are the
DokuQuest Base pack's nearest, recoloured to BetterNether's colours (as bta_nether_blocks.py does) — no BetterNether art.

  turf: jungle_grass, swampland_grass, mushroom_grass, netherrack_moss; soul_sandstone
  plants: nether_reed (grows by lava, 3 tall), nether_cactus (3 tall, pricks), black_bush, black_apple_bush (black
  apples: food), lucis_mushroom (glows), jellyfish_mushroom (soft: no fall damage), black / golden / blooming / eye
  vines (hang), smoker (smokes)
  trees: willow_log / willow_leaves, rubeus_log / rubeus_leaves (gen/nether_bn.cpp grows them)
"""
import importlib.util as _u, json, pathlib
import numpy as np

_s = _u.spec_from_file_location('bta_nether_blocks', pathlib.Path(__file__).with_name('bta_nether_blocks.py'))
_b = _u.module_from_spec(_s); _s.loader.exec_module(_b)
pack_pic, recolour, veins, save = _b.pack_pic, _b.recolour, _b.veins, _b.save


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}

    def tile(name, a):
        rel = 'textures/block/%s.png' % name
        save(a).save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    def block(key, name, props, tex, item=True):
        faces_of = lambda t: [t['side'], t['side'], t['top'], t['bottom'], t['side'], t['side']] if isinstance(t, dict) else [t] * 6
        tid = {k: tiles[v] for k, v in tex.items()} if isinstance(tex, dict) else tiles[tex]
        b = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe', 'tier': 0, 'drop': key,
             'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(props); b['tex'] = tex; b['betternether'] = True
        if key in at and not blocks[at[key]].get('betternether'): raise SystemExit('betternether_blocks: %s is already another block' % key)
        if key in at: blocks[at[key]].update(b); bj['faces'][at[key]] = faces_of(tid)
        else:
            b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces_of(tid)); at[key] = b['id']
        if item and key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    nyl_t = pack_pic(d, ['crimson_nylium_top', 'crimson_nylium']); nyl_s = pack_pic(d, ['crimson_nylium_side'])
    rack = pack_pic(d, ['netherrack']); soil = pack_pic(d, ['soul_soil'])
    sst_t = pack_pic(d, ['sandstone_top']); sst_s = pack_pic(d, ['sandstone']); sst_b = pack_pic(d, ['sandstone_bottom', 'sandstone_top'])
    cane = pack_pic(d, ['sugar_cane']); cact = pack_pic(d, ['cactus_side']); cact_t = pack_pic(d, ['cactus_top'])
    bush = pack_pic(d, ['sweet_berry_bush_small_cross', 'sweet_berry_bush', 'dead_bush']); berry = pack_pic(d, ['sweet_berry_bush', 'sweet_berry_bush_cross'])
    brown = pack_pic(d, ['brown_mushroom']); red = pack_pic(d, ['red_mushroom'])
    vine = pack_pic(d, ['weeping_vines_plant', 'weeping_vines']); twist = pack_pic(d, ['cave_vines', 'weeping_vines'])
    lit = pack_pic(d, ['cave_vines_lit', 'cave_vines'])
    jlog = pack_pic(d, ['jungle_log']); jtop = pack_pic(d, ['jungle_log_top']); mlog = pack_pic(d, ['mangrove_log', 'jungle_log'])
    mtop = pack_pic(d, ['mangrove_log_top', 'jungle_log_top']); leaves = pack_pic(d, ['jungle_leaves', 'mangrove_leaves'])
    lichen = pack_pic(d, ['glow_lichen']); deadb = pack_pic(d, ['dead_bush'])

    # turf: BetterNether's floor colours (measured from its pictures: averages only)
    for key, nm, top, side in [('nether_jungle_grass', 'Nether Jungle Grass', 0x3E7A2A, 0x4F7230), ('swampland_grass', 'Nether Swampland Grass', 0x5A4466, 0x5B4762),
                               ('mushroom_grass', 'Nether Mushroom Grass', 0x7C5A6A, 0x77566A), ('netherrack_moss', 'Netherrack Moss', 0x8B5A2E, 0x7E4A34)]:
        tile('bn_%s_top' % key, recolour(nyl_t, top, 1.15)); tile('bn_%s_side' % key, recolour(nyl_s, side, 1.1))
        block(key, nm, {'hardness': 0.4, 'tier': 1, 'drop': 'netherrack', 'sound': 'grass'},
              {'top': 'bn_%s_top' % key, 'bottom': 'netherrack', 'side': 'bn_%s_side' % key})
    tile('bn_soul_sandstone_top', recolour(sst_t, 0x5E4A3C, 1.1)); tile('bn_soul_sandstone_side', recolour(sst_s, 0x56443A, 1.1))
    tile('bn_soul_sandstone_bottom', recolour(sst_b, 0x4E3E34, 1.1))
    block('soul_sandstone', 'Soul Sandstone', {'hardness': 0.8},
          {'top': 'bn_soul_sandstone_top', 'bottom': 'bn_soul_sandstone_bottom', 'side': 'bn_soul_sandstone_side'})

    cross = {'render': 3, 'solid': False, 'opaque': False, 'replaceable': False, 'hardness': 0, 'tool': None, 'sound': 'grass', 'needsSupport': True}
    hang = dict(cross, sound='leaves', hangs=True, stackPlant=True, climb=True)
    tile('bn_nether_reed', recolour(cane, 0x6E8C3A, 1.2))
    block('nether_reed', 'Nether Reed', dict(cross, stackPlant=True, netherReed=True, randomTick=True), 'bn_nether_reed')
    tile('bn_nether_cactus_side', recolour(cact, 0x7A5A2C, 1.2)); tile('bn_nether_cactus_top', recolour(cact_t, 0x8A6630, 1.2))
    block('nether_cactus', 'Nether Cactus', {'render': 7, 'solid': True, 'opaque': False, 'hardness': 0.4, 'tool': None, 'sound': 'wool', 'shapeKind': 35,
          'contactDamage': 1, 'needsSupport': True, 'netherCactus': True, 'randomTick': True},
          {'top': 'bn_nether_cactus_top', 'bottom': 'bn_nether_cactus_top', 'side': 'bn_nether_cactus_side'})
    tile('bn_black_bush', recolour(bush, 0x2A2226, 1.3))
    block('black_bush', 'Black Bush', dict(cross, replaceable=True), 'bn_black_bush')
    ab = recolour(berry, 0x2C2A24, 1.2); ab = veins(ab, 0x14100E, 0.1)
    tile('bn_black_apple_bush', ab)
    block('black_apple_bush', 'Black Apple Bush', dict(cross, drop='black_apple'), 'bn_black_apple_bush')
    tile('bn_lucis_mushroom', veins(recolour(brown, 0xC8A040, 1.1), 0xFFE070, 0.25))
    block('lucis_mushroom', 'Lucis Mushroom', dict(cross, light=15, sound='wool'), 'bn_lucis_mushroom')
    tile('bn_jellyfish_mushroom', recolour(red, 0x5AB8C8, 1.0))
    block('jellyfish_mushroom', 'Jellyfish Mushroom', dict(cross, light=9, sound='slime', softLanding=True), 'bn_jellyfish_mushroom')
    for key, nm, col, glow in [('black_vine', 'Black Vine', 0x2A2428, 0), ('golden_vine', 'Golden Vine', 0xC8A030, 12),
                               ('blooming_vine', 'Blooming Vine', 0x7A3A5C, 8), ('eye_vine', 'Eye Vine', 0x8A2A2A, 6)]:
        a = recolour(lit if glow else vine, col, 1.2)
        if key == 'eye_vine': a = veins(a, 0xF0E0C0, 0.06)
        if key == 'blooming_vine': a = veins(a, 0xF08AC8, 0.1)
        tile('bn_' + key, a)
        block(key, nm, dict(hang, light=glow), 'bn_' + key)
    tile('bn_smoker', recolour(deadb, 0x4A3E36, 1.3))
    block('bn_smoker', 'Nether Smoker', dict(cross, hardness=0.3, sound='stone', smokes=True), 'bn_smoker')
    # trees
    LOG = {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5}
    LEAF = {'render': 2, 'solid': True, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'isLeaves': True, 'lightAtt': 1, 'drop': None}
    for key, nm, side, top, lc in [('willow', 'Willow', 0x5A3C3A, 0x7A5048, 0x7C3A5A), ('rubeus', 'Rubeus', 0x6A2E2A, 0x8E4A3C, 0xB43E5A)]:
        tile('bn_%s_log' % key, recolour(mlog if key == 'willow' else jlog, side, 1.15))
        tile('bn_%s_log_top' % key, recolour(mtop if key == 'willow' else jtop, top, 1.1))
        tile('bn_%s_leaves' % key, recolour(leaves, lc, 1.1))
        block(key + '_log', nm + ' Log', LOG, {'top': 'bn_%s_log_top' % key, 'bottom': 'bn_%s_log_top' % key, 'side': 'bn_%s_log' % key})
        block(key + '_leaves', nm + ' Leaves', LEAF, 'bn_%s_leaves' % key)

    if 'black_apple' not in items:
        items['black_apple'] = {'key': 'black_apple', 'name': 'Black Apple', 'maxStack': 64, 'icon': 'black_apple', 'food': {'heal': 4}}
    # (its icon: the apple's picture, near-black)
    ap = d / 'resourcepacks/DokuQuest Base/assets/minecraft/textures/item/apple.png'
    if ap.exists():
        from PIL import Image
        im = np.asarray(Image.open(ap).convert('RGBA')).astype(np.float32)
        out = d / 'textures/item'; out.mkdir(parents=True, exist_ok=True)
        save(recolour(im, 0x2C2830, 1.3)).save(out / 'black_apple.png')
        if 'black_apple' not in tiles:
            tiles['black_apple'] = tj['count']; files.append('textures/item/black_apple.png'); tj['res'].append(32); tj['count'] += 1
        sh = json.loads((d / 'itemshapes.json').read_text())
        if 'black_apple' not in sh['itemTiles']:
            sh['itemTiles'].append('black_apple'); (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('betternether_blocks: done')


if __name__ == '__main__':
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
