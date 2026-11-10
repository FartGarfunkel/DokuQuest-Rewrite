#!/usr/bin/env python3
"""tools/port/bta_extras.py — the rest of Better than Adventure 8.0.1's blocks and items that DokuQuest lacked (idempotent;
data_patches.py runs it after trommel.py):  python3 tools/port/bta_extras.py <data-dir>

Recipes are BTA's (recipes/workbench.json, furnace.json; stairs give 4 and slabs 6, as this game's do), with BTA's items
swapped for ours (nether coal -> ember coal, sugarcane -> sugar cane, dye -> ink sac...). Pictures are the DokuQuest Base
pack's nearest ones, recoloured or put together here (BTA's art isn't ours to ship).

  Stone:   polished stone, polished stone bricks (+ mossy, slab, stairs), polished limestone / gloomstone / netherrack,
           carved slabs of eight stones (the statues' plinths in BTA), cobbled + mossy cobbled basalt / granite /
           limestone (from pebbles, as BTA makes them; they smelt back to the stone) with slabs and stairs
  Blocks:  block of rubyglass, rubyglass bricks (+ slab, stairs), block of ember coal, block of sugar cane and baked,
           baked mud, steel glass, chainlink fence, slabs and stairs of gold / iron / steel / lapis bricks
  Items:   chainmail (helmet .. boots: found on armoured monsters, scrapped into chainlinks), chainlinks, wolf armour
           (leather, chainmail, iron, gold, diamond, steel), steel shears, steel fire striker, crude steel, golden /
           flaming / purple arrows, explosive charge, steel bucket, ice cream, rotary calendar, label, dough, cherries,
           cloth; and every tree's branch into two sticks
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures'


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def recolour(a, hexcol, contrast=1.0):
    """the picture's light and shade around its mean, in a colour"""
    rgb = np.array([(hexcol >> 16) & 255, (hexcol >> 8) & 255, hexcol & 255], np.float32)
    m = a[..., 3] > 0
    l = lum(a); mean = max(1.0, float(l[m].mean())) if m.any() else 128.0
    k = 1 + (l / mean - 1) * contrast
    out = a.copy(); out[..., :3] = np.clip(rgb[None, None, :] * k[..., None], 0, 255)
    return out


def mean_colour(a):
    m = a[..., 3] > 0
    c = a[m][:, :3].mean(0) if m.any() else np.array([128, 128, 128])
    return (int(c[0]) << 16) | (int(c[1]) << 8) | int(c[2])


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); R = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}

    def pic(rel):
        p = d / PACK / rel
        if not p.exists():   # the game's own built-in picture
            nm = pathlib.Path(rel).stem
            if nm in tiles and (d / files[tiles[nm]]).exists(): p = d / files[tiles[nm]]
            else: return None
        im = Image.open(p).convert('RGBA'); w, h = im.size
        if h > w: im = im.crop((0, 0, w, w))
        if im.size[0] != 32: im = im.resize((32, 32), Image.NEAREST)
        return np.asarray(im).astype(np.float32)

    def tile(name, a, kind='block'):
        rel = 'textures/%s/%s.png' % (kind, name)
        if name in tiles and files[tiles[name]] != rel: return name   # the game already has a picture by that name: keep it
        (d / rel).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA').save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return name

    def faces_of(key):   # a cube block's six faces from its tex
        t = blocks[at[key]]['tex']
        if isinstance(t, dict): return [tiles[t['side']], tiles[t['side']], tiles[t['top']], tiles[t['bottom']], tiles[t.get('front', t['side'])], tiles[t['side']]]
        return [tiles[t]] * 6

    def block(key, name, props, tex, item=True, faces=None):
        b = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe', 'tier': 0, 'drop': key,
             'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name, 'tex': tex, 'bta': True}
        b.update(props)
        if key in at: b['id'] = at[key]; blocks[at[key]].update(b)
        else: b['id'] = len(blocks); blocks.append(b); bj['faces'].append([0] * 6); at[key] = b['id']
        bj['faces'][at[key]] = faces if faces else ([tiles[tex]] * 6 if isinstance(tex, str) else
                                                    [tiles[tex['side']], tiles[tex['side']], tiles[tex['top']], tiles[tex['bottom']], tiles[tex.get('front', tex['side'])], tiles[tex['side']]])
        if item and key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    def item(key, name, props=None, icon=None, stack=64):
        it = {'key': key, 'name': name, 'maxStack': stack, 'icon': icon or key}
        if props: it.update(props)
        items[key] = it

    def shaped(out, n, shape, key, group=None):
        r = {'out': out, 'n': n, 'shape': shape, 'key': key, 'h': len(shape), 'w': max(len(s) for s in shape)}
        if group: r['group'] = group
        if not any(x.get('out') == out and x.get('shape') == shape and x.get('key') == key for x in R['recipes']): R['recipes'].append(r)

    def shapeless(out, n, ins):
        if not any(x.get('out') == out and x.get('items') == ins for x in R['recipes']):
            R['recipes'].append({'out': out, 'n': n, 'items': ins, 'w': 2 if len(ins) <= 4 else 3, 'h': 2 if len(ins) <= 4 else 3})

    def shapes_of(base, name, slab=True, stairs=True):
        """a slab and stairs of a block (the game's: shapeKind 1 / 2, the base's faces)"""
        bb = blocks[at[base]]
        common = {'render': 7, 'opaque': False, 'hardness': bb.get('hardness', 1.5), 'tool': bb.get('tool', 'pickaxe'), 'tier': bb.get('tier', 0), 'sound': bb.get('sound', 'stone'), 'base': base, 'wlog': True}
        if slab:
            block(base.replace('_bricks', '_brick') + '_slab', name + ' Slab', dict(common, shapeKind=1, family='slab'), bb['tex'], faces=faces_of(base))
            shaped(base.replace('_bricks', '_brick') + '_slab', 6, ['XXX'], {'X': base}, 'shapes')
        if stairs:
            block(base.replace('_bricks', '_brick') + '_stairs', name + ' Stairs', dict(common, shapeKind=2, family='stairs'), bb['tex'], faces=faces_of(base))
            shaped(base.replace('_bricks', '_brick') + '_stairs', 4, ['X  ', 'XX ', 'XXX'], {'X': base}, 'shapes')

    P = lambda n: pic('block/%s.png' % n)
    I = lambda n: pic('item/%s.png' % n)
    if P('stone') is None or P('cobblestone') is None: print('bta_extras: no DokuQuest Base pack - skipped'); return

    # ---- stone ----
    tile('polished_stone', P('smooth_stone') if P('smooth_stone') is not None else P('stone'))
    block('polished_stone', 'Polished Stone', {}, 'polished_stone'); shaped('polished_stone', 2, ['#', '#'], {'#': 'stone'})
    sb = P('stone_bricks'); sb[..., :3] = np.clip(sb[..., :3] * 1.08 + 6, 0, 255)
    tile('polished_stone_bricks', sb); block('polished_stone_bricks', 'Polished Stone Bricks', {}, 'polished_stone_bricks')
    shaped('polished_stone_bricks', 4, ['XX', 'XX'], {'X': 'polished_stone'}); shapes_of('polished_stone_bricks', 'Polished Stone Brick')
    tile('mossy_polished_stone_bricks', P('mossy_stone_bricks')); block('mossy_polished_stone_bricks', 'Mossy Polished Stone Bricks', {}, 'mossy_polished_stone_bricks')
    if 'vine' in items: shapeless('mossy_polished_stone_bricks', 1, ['polished_stone_bricks', 'vine'])
    pa = P('polished_andesite')
    for st in ('limestone', 'gloomstone', 'netherrack'):
        if st not in at: continue
        k = 'polished_' + st
        if k not in at:
            tile(k, recolour(pa, mean_colour(P(st) if P(st) is not None else pa), 1.0)); block(k, 'Polished ' + st.title(), {'tier': blocks[at[st]].get('tier', 0)}, k)
        shaped(k, 2, ['#', '#'], {'#': st})
    ch = P('chiseled_stone_bricks')
    for st, pol in (('stone', 'polished_stone'), ('basalt', 'polished_basalt'), ('granite', 'polished_granite'), ('limestone', 'polished_limestone'),
                    ('gloomstone', 'polished_gloomstone'), ('netherrack', 'polished_netherrack'), ('permafrost', 'polished_permafrost'), ('slate', 'polished_slate')):
        if pol not in at: continue
        k = 'carved_' + st
        tile(k, recolour(ch, mean_colour(P(pol) if P(pol) is not None else ch), 1.1))
        block(k, 'Carved ' + st.title(), {'render': 7, 'opaque': False, 'shapeKind': 1, 'family': 'slab', 'wlog': True}, k)   # BTA's carved stone is a slab: the statues' plinth
        shaped(k, 6, ['XXX'], {'X': pol})
    cob, mcob = P('cobblestone'), P('mossy_cobblestone')
    for st, partner in (('basalt', 'coal'), ('granite', 'quartz'), ('limestone', 'sand')):
        if st not in at: continue
        col = mean_colour(P(st) if P(st) is not None else cob)
        k = 'cobbled_' + st
        tile(k, recolour(cob, col, 1.15)); block(k, 'Cobbled ' + st.title(), {'hardness': 2}, k)
        if 'pebble' in items and partner in items: shaped(k, 2, ['#X', 'X#'], {'#': 'pebble', 'X': partner})
        R['smelting'][k] = st
        shapes_of(k, 'Cobbled ' + st.title())
        mk = 'mossy_cobbled_' + st
        mc = recolour(mcob, col, 1.15); moss = (mcob[..., 1] > mcob[..., 0] * 1.12)   # keep the moss green
        mc[moss] = mcob[moss]
        tile(mk, mc); block(mk, 'Mossy Cobbled ' + st.title(), {'hardness': 2}, mk)
        if 'vine' in items: shapeless(mk, 1, [k, 'vine'])

    # ---- other blocks ----
    if 'rubyglass_crystal' in at:
        tg = P('tinted_glass') if P('tinted_glass') is not None else P('glass')
        rb = recolour(tg, 0xC8182C, 1.3); rb[..., 3] = np.where(rb[..., 3] > 0, np.maximum(rb[..., 3], 150), 0)
        tile('rubyglass_block', rb)
        block('rubyglass_block', 'Block of Rubyglass', {'render': 2, 'opaque': False, 'translucent': True, 'selfCull': True, 'cullWith': 'rubyglass', 'sound': 'glass', 'light': 4}, 'rubyglass_block')
        shaped('rubyglass_block', 1, ['XXX', 'XXX', 'XXX'], {'X': 'rubyglass_crystal'}); shapeless('rubyglass_crystal', 9, ['rubyglass_block'])
        tile('rubyglass_bricks', recolour(P('stone_bricks'), 0xA8142A, 1.35)); block('rubyglass_bricks', 'Rubyglass Bricks', {'sound': 'glass'}, 'rubyglass_bricks')
        shaped('rubyglass_bricks', 4, ['XX', 'XX'], {'X': 'rubyglass_crystal'}); shapes_of('rubyglass_bricks', 'Rubyglass Brick')
    if 'ember_coal' in items:
        tile('ember_coal_block', recolour(P('coal_block'), 0x4A2418, 1.4))
        block('ember_coal_block', 'Block of Ember Coal', {'hardness': 5, 'fuel': 80}, 'ember_coal_block')
        shaped('ember_coal_block', 1, ['XXX', 'XXX', 'XXX'], {'X': 'ember_coal'}); shapeless('ember_coal', 9, ['ember_coal_block'])
    if 'sugar_cane' in items:
        hb = P('hay_block_side') if P('hay_block_side') is not None else P('bamboo_block')
        tile('sugar_cane_block', recolour(hb, 0x7CB04A, 1.2)); block('sugar_cane_block', 'Block of Sugar Cane', {'hardness': 0.5, 'tool': 'axe', 'sound': 'grass'}, 'sugar_cane_block')
        tile('baked_sugar_cane_block', recolour(hb, 0xC09A52, 1.2)); block('baked_sugar_cane_block', 'Baked Block of Sugar Cane', {'hardness': 0.8, 'tool': 'axe', 'sound': 'wood', 'fuel': 9}, 'baked_sugar_cane_block')
        shaped('sugar_cane_block', 1, ['XXX', 'XXX', 'XXX'], {'X': 'sugar_cane'}); shapeless('sugar_cane', 9, ['sugar_cane_block'])
        R['smelting']['sugar_cane_block'] = 'baked_sugar_cane_block'
    if 'mud' in at:
        tile('baked_mud', recolour(P('packed_mud'), 0x8A5A3E, 1.2)); block('baked_mud', 'Baked Mud', {'hardness': 1.2}, 'baked_mud')
        R['smelting']['mud'] = 'baked_mud'
    # crude steel first (steel glass and the steel bucket take it)
    ii = I('iron_ingot')
    tile('crude_steel', recolour(ii, 0x5E646C, 1.4), 'item'); item('crude_steel', 'Crude Steel', {'desc': 'Steel not yet worked. A steel ingot breaks down into three.'})
    if 'steel_ingot' in items: shapeless('crude_steel', 3, ['steel_ingot'])
    gl = P('glass').copy(); m = gl[..., 3] > 0
    edge = np.zeros((32, 32), bool); edge[:2, :] = edge[-2:, :] = edge[:, :2] = edge[:, -2:] = True; edge[15:17, :] = edge[:, 15:17] = True
    gl[edge] = [96, 102, 110, 255]
    tile('steel_glass', gl); block('steel_glass', 'Steel Glass', {'render': 2, 'opaque': False, 'translucent': False, 'selfCull': True, 'sound': 'metal', 'hardness': 6, 'tool': 'pickaxe', 'tier': 2, 'blastResist': 30}, 'steel_glass')
    shaped('steel_glass', 4, ['#X', 'X#'], {'#': 'glass', 'X': 'crude_steel'})
    # the chainlink fence: a diamond mesh, set like iron bars
    cf = np.zeros((32, 32, 4), np.float32); yy, xx = np.mgrid[0:32, 0:32]
    wire = (((xx + yy) % 8) < 1.5) | (((xx - yy) % 8) < 1.5)
    cf[wire] = [150, 156, 164, 255]; cf[wire & (((xx + yy) % 8) < 0.8)] = [196, 202, 210, 255]
    tile('chainlink_fence', cf)
    block('chainlink_fence', 'Chainlink Fence', {'render': 7, 'opaque': False, 'hardness': 5, 'sound': 'metal', 'shapeKind': 6, 'wlog': True}, 'chainlink_fence')
    for b in ('gold_bricks', 'iron_bricks', 'steel_bricks', 'lapis_bricks'):
        if b in at: shapes_of(b, blocks[at[b]]['name'][:-1] if blocks[at[b]]['name'].endswith('Bricks') else blocks[at[b]]['name'])

    # ---- items ----
    # chainmail: worn by armoured monsters (sim/creature.cpp), scrapped into chainlinks
    tile('chainlink', recolour(I('iron_nugget'), 0x8A9098, 1.3), 'item'); item('chainlink', 'Chainlinks', {'desc': 'Scrapped from chainmail. With iron they make chainlink fence.'})
    shaped('chainlink_fence', 8, ['X#X', 'X#X'], {'X': 'iron_ingot', '#': 'chainlink'})
    for slot, (piece, pts, dur, links) in enumerate((('helmet', 2, 165, 5), ('chestplate', 5, 240, 8), ('leggings', 4, 225, 7), ('boots', 1, 195, 4))):
        k = 'chainmail_' + piece
        tile(k, I(k) if I(k) is not None else recolour(I('iron_' + piece), 0x8A9098), 'item')
        item(k, 'Chainmail ' + piece.title(), {'armor': {'slot': slot, 'points': pts, 'durability': dur, 'fireResist': 0}}, stack=1)
        shapeless('chainlink', links, [k])
    # wolf armour: on a tamed wolf (sim/wolf_armour)
    wa = I('wolf_armor') if I('wolf_armor') is not None else I('leather_horse_armor')
    for mat, col, pts, dur, ing in (('leather', 0x8A5A32, 3, 80, 'leather'), ('chainmail', 0x8A9098, 4, 160, None), ('iron', 0xC8C8C8, 5, 240, 'iron_ingot'),
                                    ('gold', 0xE8C040, 5, 110, 'gold_ingot'), ('diamond', 0x5ADCD0, 8, 520, 'diamond'), ('steel', 0x6A7280, 7, 600, 'steel_ingot')):
        k = mat + '_wolf_armor'
        tile(k, recolour(wa, col, 1.2), 'item')
        item(k, mat.title() + ' Wolf Armour', {'wolfArmor': {'points': pts, 'durability': dur, 'mat': mat}, 'desc': 'Use it on your tamed wolf.'}, stack=1)
        if ing and ing in items: shaped(k, 1, [' XX', 'XXX', 'X X'], {'X': ing})
        if mat == 'chainmail': shapeless('chainlink', 6, [k])
    # steel tools
    if 'steel_ingot' in items:
        tile('steel_shears', recolour(I('shears'), 0x7A8290, 1.3), 'item')
        item('steel_shears', 'Steel Shears', {'shears': True, 'tool': {'type': 'shears', 'tier': 0, 'speed': 7, 'damage': 1, 'durability': 952}}, stack=1)
        shaped('steel_shears', 1, [' X', 'X '], {'X': 'steel_ingot'})
        tile('steel_fire_striker', recolour(I('flint_and_steel'), 0x6A7280, 1.2), 'item')
        item('steel_fire_striker', 'Steel Fire Striker', {'igniter': 'flint', 'tool': {'type': 'igniter', 'tier': 0, 'speed': 1, 'damage': 1, 'durability': 256}}, stack=1)
        shapeless('steel_fire_striker', 1, ['steel_ingot', 'flint'])
    # arrows (BTA: golden flies flat and fast, flaming burns and lights, purple shatters where it lands)
    ar = I('arrow')
    for k, nm, col, props in (('golden_arrow', 'Golden Arrow', 0xF0C838, {'arrow': {'damage': 2, 'gravity': 0.4, 'speed': 1.35, 'flat': True}}),
                              ('flaming_arrow', 'Flaming Arrow', 0xE8602A, {'arrow': {'damage': 6, 'fire': True, 'light': True}}),
                              ('purple_arrow', 'Purple Arrow', 0x8A3AC8, {'arrow': {'damage': 2, 'breaks': True}})):
        a = ar.copy(); tip = (np.arange(32)[None, :] - np.arange(32)[:, None]) > 6   # the head end (top right): the tint
        a2 = recolour(ar, col, 1.2); a[tip] = a2[tip]
        tile(k, a, 'item'); item(k, nm, props)
    shaped('golden_arrow', 4, [' X ', 'X#X', ' X '], {'X': 'arrow', '#': 'gold_ingot'})
    if 'sulphur' in items: shapeless('flaming_arrow', 1, ['sulphur', 'paper', 'arrow', 'sulphur'])
    tile('explosive_charge', recolour(I('fire_charge'), 0x5A4A3A, 1.3), 'item')
    item('explosive_charge', 'Explosive Charge', {'cannonAmmo': {'power': 2.2}, 'desc': 'A heavier load for the handcannon: it bursts harder.'})
    shaped('explosive_charge', 6, [' S ', '#X#', ' # '], {'S': 'string', '#': 'clay_ball', 'X': 'gunpowder'})
    # buckets: the steel bucket holds three (BTA ItemBucketSteel)
    if 'steel_ingot' in items:
        tile('steel_bucket', recolour(I('bucket'), 0x6A7280, 1.3), 'item')
        item('steel_bucket', 'Steel Bucket', {'bucket': 'empty', 'charges': 3, 'desc': 'Holds three bucketfuls.'}, stack=1)
        shaped('steel_bucket', 1, ['# #', '*#*'], {'#': 'steel_ingot', '*': 'crude_steel'})
    # BTA's ice skates (worn as boots: fast and sure-footed on ice, sim/player.cpp)
    sk = recolour(I('leather_boots'), 0x5A3A22, 1.1); sk[27:30, 4:28] = [200, 206, 214, 255]; sk[29:30, 4:28] = [120, 126, 134, 255]
    tile('ice_skates', sk, 'item')
    item('ice_skates', 'Ice Skates', {'armor': {'slot': 3, 'points': 1, 'durability': 160, 'fireResist': 0}, 'desc': 'Worn as boots: glide fast and steer on ice.'}, stack=1)
    shapeless('ice_skates', 1, ['leather_boots', 'iron_ingot', 'iron_ingot'])
    # food
    tile('dough', recolour(I('bread'), 0xE8D8A8, 0.8), 'item'); item('dough', 'Dough', {'desc': 'Bake it into bread.'})
    shaped('dough', 1, ['XXX'], {'X': 'wheat'}); R['smelting']['dough'] = 'bread'
    tile('cherries', recolour(I('sweet_berries'), 0xB01828, 1.2), 'item'); item('cherries', 'Cherries', {'food': {'heal': 2}, 'desc': 'From a flowering cherry tree.'})
    tile('ice_cream', recolour(I('milk_bucket'), 0xF0D0D8, 0.9), 'item')
    item('ice_cream', 'Bucket of Ice Cream', {'food': {'heal': 8}, 'returns': 'bucket', 'desc': 'Cherries, snow and milk in a bucket.'}, stack=1)
    shaped('ice_cream', 1, [' Y ', '#O#', ' X '], {'Y': 'cherries', '#': 'snowball', 'O': 'ink_sac' if 'ink_sac' in items else 'sugar', 'X': 'bucket'})
    tile('cloth', recolour(I('paper'), 0xE8E0D0, 0.7), 'item'); item('cloth', 'Cloth', {'desc': 'Woven cloth. The trommel turns it up from mud.'})
    if 'string' in items: shaped('cloth', 1, ['SS', 'SS'], {'S': 'string'})
    # the calendar (it shows the season and the day: render/ HUD) and the label (names a creature)
    tile('rotary_calendar', recolour(I('clock'), 0x9A7A4A, 1.2), 'item')
    item('rotary_calendar', 'Rotary Calendar', {'calendar': True, 'desc': 'Shows the season and the day when held.'}, stack=1)
    shaped('rotary_calendar', 1, [' X ', 'X#X', ' X '], {'X': '#planks', '#': 'redstone'})
    tile('label', I('name_tag'), 'item'); item('label', 'Label', {'label': True, 'desc': 'Write a name on it, then use it on a creature to name it.'}, stack=1)
    shapeless('label', 1, ['paper', 'ink_sac' if 'ink_sac' in items else 'black_dye', 'string'])

    # ---- trees: BTA's eucalyptus is the game's gum tree (named so now, with a sapling), the thorn tree gets its own log
    #      and sapling, the shrub is new (sim + gen/trees.cpp), and the Seasonal Forest's cherries flower and fruit ----
    for k, nm in (('gum_log', 'Eucalyptus Log'), ('gum_planks', 'Eucalyptus Planks'), ('gum_leaves', 'Eucalyptus Leaves')):
        if k in at: blocks[at[k]]['name'] = nm
        if k in items: items[k]['name'] = nm
    sap = P('oak_sapling')
    def sapling(key, name, tree, col):
        tile(key, recolour(sap, col, 1.2))
        block(key, name, {'render': 3, 'solid': False, 'opaque': False, 'hardness': 0, 'tool': None, 'sound': 'grass', 'needsSupport': True, 'fuel': 0.5, 'tree': tree}, key, item=False)
        items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': key, 'block': key, 'fuel': 0.5}
    if 'gum_leaves' in at:
        sapling('eucalyptus_sapling', 'Eucalyptus Sapling', 'gum', 0x8AA88A); blocks[at['gum_leaves']]['sapling'] = 'eucalyptus_sapling'
    if 'thorn_leaves' in at:
        al = P('acacia_log'); tl = recolour(al, 0x6A5A48, 1.3); tile('thorn_log', tl)
        top = P('acacia_log_top') if P('acacia_log_top') is not None else al; tile('thorn_log_top', recolour(top, 0x7A6A54, 1.2))
        block('thorn_log', 'Thorn Log', {'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'isLog': True, 'axisBlock': True, 'fuel': 1.5, 'wood': 'thorn'}, {'top': 'thorn_log_top', 'bottom': 'thorn_log_top', 'side': 'thorn_log'})
        if 'planks' in items: shapeless('planks', 4, ['thorn_log'])
        sapling('thorn_sapling', 'Thorn Sapling', 'thorn', 0x7A7A3A); blocks[at['thorn_leaves']]['sapling'] = 'thorn_sapling'
    ol = P('oak_leaves') if P('oak_leaves') is not None else P('leaves')
    if ol is not None:
        tile('shrub_leaves', recolour(ol, 0x6E8A3A, 1.25))
        block('shrub_leaves', 'Shrub Leaves', {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'lightAtt': 1, 'isLeaves': True, 'drop': 'shrub_leaves', 'sapling': 'shrub_sapling'}, 'shrub_leaves')
        sapling('shrub_sapling', 'Shrub Sapling', 'shrub', 0x6E8A3A)
    cl = P('cherry_leaves')
    if cl is not None and 'cherry_leaves' in at:
        fl = cl.copy(); yy, xx = np.mgrid[0:32, 0:32]
        bloom = ((xx * 7 + yy * 13) % 11 == 0) & (fl[..., 3] > 0)
        fl[bloom, :3] = [252, 238, 244]; fl[((xx * 5 + yy * 3) % 17 == 0) & (fl[..., 3] > 0), :3] = [176, 24, 40]   # blossom and the odd cherry
        tile('flowering_cherry_leaves', fl)
        block('flowering_cherry_leaves', 'Flowering Cherry Leaves', {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'lightAtt': 1, 'isLeaves': True,
              'drop': 'flowering_cherry_leaves', 'sapling': 'cherry_sapling', 'fruit': 'cherries'}, 'flowering_cherry_leaves')

    # ---- BTA's Drift portal (Blocks.PORTAL_DRIFT): silver, lit by bedrock set inside a glowstone frame (sim/drift.cpp) ----
    np_ = P('nether_portal')
    if np_ is not None:
        dp = recolour(np_, 0xC8D0E0, 1.4); dp[..., 3] = np.where(np_[..., 3] > 0, 190, 0)
        tile('drift_portal', dp)
        block('drift_portal', 'Drift Portal', {'render': 4, 'solid': False, 'opaque': False, 'replaceable': False, 'hardness': -1, 'tool': None, 'tier': 0,
              'drop': None, 'light': 11, 'sound': 'stone', 'portal': 3}, 'drift_portal', item=False)
        for k in ('translucent', 'selfCull', 'portalBlock'): blocks[at['drift_portal']].pop(k, None)   # (a window now, like the others)

    # ---- pebbles on the ground (BTA's overlay pebbles: place a pebble; it's also thrown, sim/bta_items.cpp), leaf piles
    #      (they fall under trees in autumn, sim/bta_items.cpp), the soul wax candle ----
    gv = P('gravel'); pb = gv.copy(); yy, xx = np.mgrid[0:32, 0:32]
    stones = np.zeros((32, 32), bool)
    for cx, cy, r in ((8, 9, 3.2), (22, 7, 2.6), (14, 21, 3.6), (26, 24, 2.4), (5, 26, 2.2)): stones |= np.hypot(xx - cx, yy - cy) < r
    pb[~stones, :3] = pb[~stones, :3] * 0.55; pb[stones, :3] = np.clip(P('cobblestone')[stones, :3] * 1.05, 0, 255)
    tile('pebbles', pb)
    # The game's own pebbles are its Loose Stones (gen/decorate.cpp scatters them on mountains, beaches and high ground,
    # in their shapes.json model): a pebble sets one down and they break back into a pebble. 'pebbles' (a flat layer for a
    # while) is kept only so any already placed still load, and looks the same.
    if 'loose_stones' in at:
        ls = blocks[at['loose_stones']]
        ls.update({'name': 'Pebbles', 'drop': 'pebble'})
        if 'loose_stones' in items: items['loose_stones'].update({'name': 'Pebbles', 'hideCreative': True})
        block('pebbles', 'Pebbles', {'render': 6, 'shape': 'loose_stones', 'solid': False, 'opaque': False, 'hardness': 0, 'tool': None, 'drop': 'pebble',
                                     'sound': 'stone', 'needsSupport': True, 'sink': True}, 'cobblestone', item=False)
        for k in ('shapeKind',): blocks[at['pebbles']].pop(k, None)
    if 'pebble' in items: items['pebble']['block'] = 'loose_stones' if 'loose_stones' in at else 'pebbles'; items['pebble']['desc'] = 'Set it down, or throw it. Four make two cobblestone; with flint, gravel.'
    if ol is not None:
        lp = recolour(ol, 0x8A6A2A, 1.3); lp[..., 3] = np.where(ol[..., 3] > 0, 255, 0)   # autumn's: russet and gold (carpets take no tint)
        tile('leaf_pile', lp)
        block('leaf_pile', 'Leaf Pile', {'render': 7, 'opaque': False, 'hardness': 0.1, 'tool': None, 'drop': 'leaf_pile', 'sound': 'leaves', 'shapeKind': 10, 'needsSupport': True, 'fuel': 0.25}, 'leaf_pile')
        blocks[at['leaf_pile']].pop('tint', None)
        shaped('leaf_pile', 6, ['LLL'], {'L': '#leaves'} if '#leaves' in R['tags'] else {'L': 'leaves'})
    if 'candle' in at:
        cd = dict(blocks[at['candle']]); cd.pop('id', None)
        ct = P('candle'); tile('soul_wax_candle', recolour(ct, 0x7AA8C0, 1.2) if ct is not None else P('stone'))
        cd.update({'key': 'soul_wax_candle', 'name': 'Soul Wax Candle', 'drop': 'soul_wax_candle', 'tex': 'soul_wax_candle', 'soulFlame': True})
        block('soul_wax_candle', 'Soul Wax Candle', cd, 'soul_wax_candle')
        if 'soul_sand' in items: shapeless('soul_wax_candle', 1, ['candle', 'soul_sand'])

    # ---- BTA's statues: a figure two blocks tall on a carved plinth, in each stone (ItemStatue; recipe: two of the stone
    #      over its carved slab). Their shapes are written to shapes.json (statue_<stone>_lower / _upper); placing the
    #      lower one puts the upper above it (sim/bta_items.cpp) ----
    sp = json.loads((d / 'shapes.json').read_text())
    for st in ('stone', 'basalt', 'granite', 'limestone', 'marble', 'gloomstone', 'netherrack', 'permafrost', 'slate'):
        if st not in at: continue
        tex = blocks[at[st]]['tex']; side = tex['side'] if isinstance(tex, dict) else tex
        car = 'carved_' + st if 'carved_' + st in at else None
        plinth = blocks[at[car]]['tex'] if car else side
        sp['shapes']['statue_%s_lower' % st] = [
            {'b': [2, 0, 2, 14, 3, 14], 'side': plinth, 'end': plinth},                                  # the plinth
            {'b': [4.5, 3, 6, 7.75, 14, 10], 'side': side, 'end': side}, {'b': [8.25, 3, 6, 11.5, 14, 10], 'side': side, 'end': side},   # legs
            {'b': [4, 14, 5.5, 12, 16, 10.5], 'side': side, 'end': side}]                              # the hips
        sp['shapes']['statue_%s_upper' % st] = [
            {'b': [4, 0, 5.5, 12, 9, 10.5], 'side': side, 'end': side},                                # the body
            {'b': [1.5, 0.5, 6.25, 4, 9, 9.75], 'side': side, 'end': side}, {'b': [12, 0.5, 6.25, 14.5, 9, 9.75], 'side': side, 'end': side},   # arms
            {'b': [4.75, 9, 4.75, 11.25, 15.5, 11.25], 'side': side, 'end': side}]                     # the head
        nm = blocks[at[st]]['name'] + ' Statue'
        base = {'render': 6, 'opaque': False, 'hardness': 1.8, 'tool': 'pickaxe', 'sound': 'stone', 'facingModel': True}
        block('statue_' + st, nm, dict(base, shape='statue_%s_lower' % st, statueLower='statue_%s_top' % st, drop='statue_' + st), side)
        block('statue_%s_top' % st, nm, dict(base, shape='statue_%s_upper' % st, statueUpper='statue_' + st, drop='statue_' + st), side, item=False)
        if car: shaped('statue_' + st, 1, ['#', '#', '_'], {'#': st, '_': car})
    (d / 'shapes.json').write_text(json.dumps(sp, separators=(',', ':')))

    # ---- bedrock as an item: BTA lights the Drift portal by setting bedrock in a glowstone frame (sim/drift.cpp). It was
    # a block with no item, so it was in no inventory; mined with a tier-4 pickaxe it now drops itself (its old drop) ----
    if 'bedrock' in at and 'bedrock' not in items:
        items['bedrock'] = {'key': 'bedrock', 'name': 'Bedrock', 'maxStack': 64, 'icon': 'block:bedrock', 'block': 'bedrock'}

    # ---- every wood's planks are planks, every cobbled stone a tool's stone: sticks, tables, wooden and stone tools, and
    #      a mixed tool's wood or stone (core/tool_parts.cpp). Palm, eucalyptus and Better End's woods weren't. ----
    tags = R.setdefault('tags', {})
    pl = tags.setdefault('#planks', [])
    for k in list(items):
        if k.endswith('_planks') and k in at and blocks[at[k]].get('render', 1) == 1 and k not in pl: pl.append(k)
    st = tags.setdefault('#stone_tool', [])
    for k in ('cobbled_emberstone', 'cobbled_permafrost', 'cobbled_netherrack', 'cobbled_gloomstone', 'cobbled_basalt', 'cobbled_granite', 'cobbled_limestone'):
        if k in items and k not in st: st.append(k)

    # ---- every tree's branch into sticks ----
    for k in list(items):
        if k.endswith('_branch') and k in at: shapeless('stick', 2, [k])
    # the trommel's cloth is real cloth now
    tp = d / 'trommel.json'
    if tp.exists():
        t = json.loads(tp.read_text())
        for rows in t.get('sift', {}).values():
            for r in rows:
                if r[1] == 'rotten_cloth': r[1] = 'cloth'
        tp.write_text(json.dumps(t, separators=(',', ':')))

    # ---- BTA's giant zombie, as the crypts' third boss (a huge ghoul; its stamp: sim/bta_items.cpp) ----
    mp, mdp = d / 'mobs.json', d / 'models.json'
    mobs, models = json.loads(mp.read_text()), json.loads(mdp.read_text())
    if 'ghoul' in models:
        g = json.loads(json.dumps(models['ghoul']))
        g['scale'] = 2.7; g['heavy'] = True
        models['giant'] = g
        mobs['giant'] = {'model': 'giant', 'label': 'the Giant', 'hp': 180, 'w': 0.8, 'h': 5.2, 'speed': 2.3, 'xp': 150, 'hostile': True, 'special': True,
                         'damage': 9, 'drops': [['rotten_flesh', 6, 12, 1], ['iron_ingot', 3, 6, 1], ['bone', 4, 8, 1], ['steel_ingot', 1, 2, 0.5]],
                         'voice': 'ghoul', 'family': 'ghoul', 'mini': True, 'boss': True, 'knockResist': 0.9, 'minion': 'ghoul', 'base': 'ghoul',
                         'tint': [0.55, 0.8, 0.45, 0.1], 'dungeonBoss': 'crypt', 'stomp': True}
        mp.write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
        mdp.write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'recipes.json').write_text(json.dumps(R, separators=(',', ':'), ensure_ascii=False))
    print('bta_extras: %d blocks, %d items, %d recipes now' % (len(blocks), len(items), len(R['recipes'])))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
