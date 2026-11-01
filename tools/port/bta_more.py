#!/usr/bin/env python3
# tools/port/bta_more.py — the rest of the BTA list (our own designs, our own pictures recoloured from the base pack or
# the code-painted tiles; idempotent, run by data_patches.py after bta_blocks.py):
#   painted doors, trapdoors, buttons and pressure plates (16 colours; the paintbrush makes them, signs and chests take
#   a colour in their block entity), igneous emberstone (keeps a fire burning on top, dries wet sponges), the gold mesh
#   (an item filter), the Emberglass conduit (carries a redstone signal at full strength, glowing), the redstone pumpkin
#   (a powered block), the matcher (signals when the container behind it holds its chosen item), the firefly lantern,
#   the cacao tree (leaves and sapling; cocoa grows on its trunk), the summoning wand (creative), rotten cloth (ghouls
#   drop it: string, or a bed with wool).
import json, pathlib, sys, importlib.util

_s = importlib.util.spec_from_file_location('bta_blocks', pathlib.Path(__file__).with_name('bta_blocks.py'))
_b = importlib.util.module_from_spec(_s); _s.loader.exec_module(_b)
src_pic, recolour, hsv_map = _b.src_pic, _b.recolour, _b.hsv_map

DYE = {'white': (240, 240, 240), 'orange': (230, 128, 40), 'magenta': (190, 70, 180), 'light_blue': (110, 170, 220), 'yellow': (240, 210, 60),
       'lime': (130, 200, 50), 'pink': (235, 140, 170), 'gray': (90, 92, 96), 'light_gray': (160, 160, 156), 'cyan': (40, 140, 150),
       'purple': (120, 50, 170), 'blue': (50, 70, 170), 'brown': (120, 80, 50), 'green': (80, 110, 40), 'red': (170, 45, 40), 'black': (35, 32, 38)}


def title(k): return ' '.join(w.capitalize() for w in k.split('_'))


def paint(im, rgb):
    """the picture's light and shade over a dye colour (the grain and the window holes stay)"""
    im = im.copy(); px = im.load(); w, h = im.size
    lum = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 8: lum.append((r * 299 + g * 587 + b * 114) / 1000)
    mean = (sum(lum) / len(lum)) if lum else 1
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a <= 8: continue
            k = ((r * 299 + g * 587 + b * 114) / 1000) / (mean or 1)
            px[x, y] = (min(255, int(rgb[0] * k)), min(255, int(rgb[1] * k)), min(255, int(rgb[2] * k)), a)
    return im


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); sj = json.loads((d / 'shapes.json').read_text())
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text()); mobs = json.loads((d / 'mobs.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key'] for b in blocks}; B = {b['key']: b for b in blocks}
    sh = json.loads((d / 'itemshapes.json').read_text())

    def tile(name, im, kind='block'):
        rel = 'textures/%s/%s.png' % (kind, name)
        if name not in tiles:
            im.save(d / rel)
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    def item_pic(name, im):
        t = tile(name, im, 'item')
        if name not in sh['itemTiles']: sh['itemTiles'].append(name)
        return t

    def block(key, name, props, faces, item=True):
        if key in have: return B[key]
        b = {'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe',
             'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(props); blocks.append(b); bj['faces'].append(faces); have.add(key); B[key] = b
        if item: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
        return b

    def recipe(out, n, shape=None, key=None, items_=None, extra=False):
        if not extra and any(r.get('out') == out for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r['items'] = items_
        if extra and any(json.dumps(x, sort_keys=True) == json.dumps(r, sort_keys=True) for x in rj['recipes']): return
        rj['recipes'].append(r)

    def pic(name): return src_pic(d, tiles, files, name)

    # ---- painted doors, trapdoors, buttons and pressure plates (the paintbrush and a dye make them) ----
    for col, rgb in DYE.items():
        nm = title(col)
        lo, up, tr, ic = col + '_painted_door_lower', col + '_painted_door_upper', col + '_painted_trapdoor', col + '_painted_door_item'
        if lo not in tiles:
            tile(lo, paint(pic('oak_door_lower'), rgb)); tile(up, paint(pic('oak_door_upper'), rgb)); tile(tr, paint(pic('oak_trapdoor'), rgb))
            item_pic(ic, paint(pic('oak_door_item'), rgb))
        door = dict(B['oak_door']); door.pop('id', None); door.pop('name', None)
        door.update({'key': col + '_painted_door', 'drop': col + '_painted_door', 'tex': {'side': lo, 'top': lo}, 'texUpper': up,
                     'upperTile': tiles[up], 'itemIcon': ic, 'painted': col, 'paintOf': 'oak_door'})
        block(col + '_painted_door', nm + ' Painted Door', door, [tiles[lo]] * 6)
        items[col + '_painted_door'].update({'icon': ic, 'maxStack': 1, 'fuel': 1})
        trap = dict(B['oak_trapdoor']); trap.pop('id', None); trap.pop('name', None)
        trap.update({'key': col + '_painted_trapdoor', 'drop': col + '_painted_trapdoor', 'tex': tr, 'painted': col, 'paintOf': 'oak_trapdoor'})
        block(col + '_painted_trapdoor', nm + ' Painted Trapdoor', trap, [tiles[tr]] * 6)
        # a painted chest: the chest's own pictures over the dye (it keeps everything a chest does; same colours pair up)
        inv = {v: k for k, v in tiles.items()}
        cf = bj['faces'][B['chest']['id']]
        pf = []
        for f in cf:
            src = inv.get(f)
            tk = col + '_painted_' + src
            if tk not in tiles: tile(tk, paint(pic(src), rgb))
            pf.append(tiles[tk])
        ch = dict(B['chest']); ch.pop('id', None); ch.pop('name', None)
        ctex = ch.get('tex') if isinstance(ch.get('tex'), dict) else {}
        ch.update({'key': col + '_painted_chest', 'drop': col + '_painted_chest', 'painted': col, 'paintOf': 'chest',
                   'tex': {k: col + '_painted_' + v for k, v in ctex.items()}})
        block(col + '_painted_chest', nm + ' Painted Chest', ch, pf)
        items[col + '_painted_chest'].update({'maxStack': 1})
        pl = col + '_painted_planks'
        for base, word in (('oak_button', 'Button'), ('oak_pressure_plate', 'Pressure Plate')):
            k = col + '_painted_' + base[4:]
            b2 = dict(B[base]); b2.pop('id', None); b2.pop('name', None)
            b2.update({'key': k, 'drop': k, 'base': pl, 'tex': pl, 'painted': col, 'paintOf': base})
            block(k, nm + ' Painted ' + word, b2, [tiles[pl]] * 6)

    # ---- igneous emberstone: an ever-burning stone (fire on top never goes out; wet sponges beside it dry) ----
    if 'igneous_emberstone' not in tiles:
        im = pic('emberstone'); px = im.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = px[x, y]
                if (x * 7 + y * 11) % 13 < 3 and ((x // 4 + y // 3) % 4 == 0 or (x + y) % 9 == 0):
                    px[x, y] = (255, 120 + (x * y) % 70, 30, a)
                else: px[x, y] = (min(255, r + 18), g, max(0, b - 8), a)
        tile('igneous_emberstone', im)
    block('igneous_emberstone', 'Igneous Emberstone', {'hardness': 0.6, 'tier': 1, 'light': 5, 'hot': True, 'igneous': True,
                                                        'randomTick': True, 'tex': 'igneous_emberstone'}, [tiles['igneous_emberstone']] * 6)
    recipe('igneous_emberstone', 1, items_=['emberstone', 'magma_cream'])
    # smooth emberstone (BTA's smooth netherrack): polished emberstone fired in a furnace
    if 'smooth_emberstone' not in tiles:
        sm = pic('polished_emberstone') if 'polished_emberstone' in tiles else pic('emberstone'); sp = sm.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = sp[x, y]; q = (x * 3 + y * 5) % 7 - 3   # the grain softened to a gentle mottle
                sp[x, y] = (max(0, min(255, (r * 2 + 150) // 3 + q)), max(0, min(255, (g * 2 + 60) // 3 + q)), max(0, min(255, (b * 2 + 55) // 3 + q)), a)
        tile('smooth_emberstone', sm)
    block('smooth_emberstone', 'Smooth Emberstone', {'hardness': 0.8, 'tier': 1, 'tex': 'smooth_emberstone'}, [tiles['smooth_emberstone']] * 6)
    rj.setdefault('smelting', {}).setdefault('polished_emberstone', 'smooth_emberstone')

    # ---- the gold mesh: like the iron mesh, but only the item it is set to falls through (use an item on it) ----
    if 'gold_mesh' not in tiles: tile('gold_mesh', recolour(pic('iron_mesh'), hsv_map(hue=0.13, sat=1.6, val=1.15, add=10)))
    block('gold_mesh', 'Gold Mesh', {'render': 2, 'opaque': False, 'hardness': 3, 'sound': 'metal', 'mesh': True, 'meshFilter': True,
                                     'tex': 'gold_mesh'}, [tiles['gold_mesh']] * 6)
    recipe('gold_mesh', 1, [' G ', 'GMG', ' G '], {'G': 'gold_ingot', 'M': 'iron_mesh'})

    # ---- the Emberglass conduit: a run of them carries a redstone signal at full strength, glowing while it does ----
    if 'emberglass_conduit' not in tiles:
        base = pic('emberglass_block'); dark = recolour(base, hsv_map(sat=0.7, val=0.55)); dp = dark.load()
        for y in range(32):
            for x in range(32):
                if x < 2 or x > 29 or y < 2 or y > 29: dp[x, y] = (58, 40, 34, 255)
        tile('emberglass_conduit', dark)
        lit = recolour(base, hsv_map(sat=1.2, val=1.25, add=20)); lp = lit.load()
        for y in range(32):
            for x in range(32):
                if x < 2 or x > 29 or y < 2 or y > 29: lp[x, y] = (90, 52, 36, 255)
        tile('emberglass_conduit_lit', lit)
    block('emberglass_conduit', 'Emberglass Conduit', {'hardness': 1, 'sound': 'glass', 'rs': 'conduit', 'conduitLit': 'emberglass_conduit_lit',
                                                       'tex': 'emberglass_conduit'}, [tiles['emberglass_conduit']] * 6)
    block('emberglass_conduit_lit', 'Emberglass Conduit', {'hardness': 1, 'sound': 'glass', 'rs': 'conduit', 'lit': True, 'light': 10,
                                                           'drop': 'emberglass_conduit', 'conduitDark': 'emberglass_conduit',
                                                           'tex': 'emberglass_conduit_lit'}, [tiles['emberglass_conduit_lit']] * 6, item=False)
    recipe('emberglass_conduit', 3, ['ERE'], {'E': 'emberglass_cluster', 'R': 'redstone'})

    # ---- the redstone pumpkin: a carved pumpkin with a redstone heart; it powers what is round it like a redstone block ----
    if 'redstone_pumpkin' not in tiles:
        face = pic('carved_pumpkin'); fp = face.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = fp[x, y]
                if r + g + b < 160: fp[x, y] = (200 + (x * 3 + y) % 50, 20, 24, a)   # the cut-out face shows the red glow
        tile('redstone_pumpkin', face)
    rp = dict(B['jack_o_lantern']); rp.pop('id', None); rp.pop('name', None)
    rp.update({'key': 'redstone_pumpkin', 'drop': 'redstone_pumpkin', 'light': 7, 'rsBlock': True,
               'tex': {'top': 'pumpkin_top', 'bottom': 'pumpkin_top', 'side': 'pumpkin_side', 'front': 'redstone_pumpkin'}})
    jf = bj['faces'][B['jack_o_lantern']['id']]
    block('redstone_pumpkin', 'Redstone Pumpkin', rp, [tiles['redstone_pumpkin'] if f == tiles.get('jack_o_lantern') else f for f in jf])
    recipe('redstone_pumpkin', 1, items_=['carved_pumpkin', 'redstone_block'])

    # ---- the matcher: a crystal comparator; it signals ahead while the container behind it holds the item it is set to ----
    if 'matcher_item' not in tiles:
        item_pic('matcher_item', recolour(pic('comparator_item'), hsv_map(hue=0.78, sat=1.2, val=1.05)))
    mt = dict(B['comparator']); mt.pop('id', None); mt.pop('name', None)
    mt.update({'key': 'matcher', 'drop': 'matcher', 'rs': 'matcher', 'tex': 'amethyst_block', 'itemIcon': 'matcher_item', 'matcher': True})
    block('matcher', 'Matcher', mt, [tiles['amethyst_block']] * 6)
    items['matcher'].update({'icon': 'matcher_item'})
    recipe('matcher', 1, [' T ', 'TAT', 'SSS'], {'T': 'redstone_torch', 'A': 'amethyst_shard', 'S': 'stone'})

    # ---- the firefly lantern ----
    if 'firefly_lantern' not in tiles:
        im = pic('lantern'); px = im.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                r, g, b, a = px[x, y]
                if a > 8 and r > 170 and g > 120: px[x, y] = (min(255, r * 75 // 100 + 40), min(255, g + 30), max(0, b * 40 // 100), a)
        tile('firefly_lantern', im)
    fl = dict(B['lantern']); fl.pop('id', None); fl.pop('name', None)
    fl.update({'key': 'firefly_lantern', 'drop': 'firefly_lantern', 'light': 12, 'tex': 'firefly_lantern', 'fireflies': True})
    block('firefly_lantern', 'Firefly Lantern', fl, [tiles['firefly_lantern']] * 6)
    recipe('firefly_lantern', 1, [' N ', 'NJN', ' N '], {'N': 'iron_nugget', 'J': 'firefly_jar'})

    # ---- the cacao tree: jungle wood, its own leaves, cocoa pods on the trunk; saplings from its leaves ----
    if 'cacao_leaves' not in tiles:
        lv = pic('jungle_leaves'); lp = lv.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = lp[x, y]
                if a > 8 and (x * 5 + y * 7) % 23 == 0: lp[x, y] = (150, 80, 30, a)   # little pods among the leaves
        tile('cacao_leaves', lv)
        tile('cacao_sapling', recolour(pic('jungle_sapling'), hsv_map(hue=0.08, sat=0.9, val=0.95)))
    cl = dict(B['jungle_leaves']); cl.pop('id', None); cl.pop('name', None)
    cl.update({'key': 'cacao_leaves', 'drop': 'cacao_leaves', 'tex': 'cacao_leaves', 'sapling': 'cacao_sapling'})
    block('cacao_leaves', 'Cacao Leaves', cl, [tiles['cacao_leaves']] * 6)
    cs = dict(B['jungle_sapling']); cs.pop('id', None); cs.pop('name', None)
    cs.update({'key': 'cacao_sapling', 'drop': 'cacao_sapling', 'tex': 'cacao_sapling', 'tree': 'cacao'})
    block('cacao_sapling', 'Cacao Sapling', cs, [tiles['cacao_sapling']] * 6)
    recipe('cacao_sapling', 1, items_=['jungle_sapling', 'cocoa_beans'])

    # ---- the Sulphur Springs' steam vent (BTA): a crusted hole that breathes out scalding steam now and then ----
    if 'steam_vent' not in tiles:
        sv = pic('sulphur_block') if 'sulphur_block' in tiles else pic('netherrack'); vp = sv.load()
        for y in range(32):
            for x in range(32):
                d2 = (x - 15.5) ** 2 + (y - 15.5) ** 2
                if d2 < 30: vp[x, y] = (24, 20, 18, 255)
                elif d2 < 60: r, g, b, a = vp[x, y]; vp[x, y] = (r * 6 // 10, g * 6 // 10, b * 5 // 10, a)
        tile('steam_vent', sv)
    side = 'sulphur_block' if 'sulphur_block' in tiles else 'netherrack'
    block('steam_vent', 'Steam Vent', {'hardness': 1.2, 'tier': 0, 'steamVent': True, 'tex': {'top': 'steam_vent', 'bottom': side, 'side': side}},
          [tiles[side], tiles[side], tiles['steam_vent'], tiles[side], tiles[side], tiles[side]])
    # emberglass regrows: a block of it now and then grows a fresh cluster on top (in the Depths)
    if 'emberglass_block' in B: B['emberglass_block'].update({'regrows': 'emberglass_cluster', 'randomTick': True})

    # ---- the Seasonal Forest's leaves: four forms that follow the year (sim/worldsys.cpp turns them on random ticks):
    #      0 spring blossom (pink), 1 summer green (biome tinted), 2 autumn (orange and red), 3 winter (bare twigs) ----
    if 'seasonal_leaves' not in tiles:
        tile('seasonal_leaves', pic('leaves') if 'leaves' in tiles else pic('oak_leaves'))
        tile('seasonal_blossom', pic('cherry_leaves'))
        au = pic('leaves') if 'leaves' in tiles else pic('oak_leaves'); ap = au.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = ap[x, y]
                if a > 8:
                    l = (r * 299 + g * 587 + b * 114) / 1000 / 110
                    hot = (x * 7 + y * 3) % 5 < 2
                    ap[x, y] = (min(255, int((215 if hot else 190) * l)), min(255, int((70 if hot else 110) * l)), min(255, int(30 * l)), a)
        tile('seasonal_leaves_autumn', au)
        wi = pic('leaves') if 'leaves' in tiles else pic('oak_leaves'); wp = wi.load()
        for y in range(32):
            for x in range(32):
                r, g, b, a = wp[x, y]
                twig = (x + y * 2) % 9 == 0 or (x * 3 - y) % 11 == 0
                wp[x, y] = (96, 74, 56, 255) if (a > 8 and twig) else (0, 0, 0, 0)
        tile('seasonal_leaves_winter', wi)
    base_l = B.get('leaves') or B.get('oak_leaves')
    for k, nm, tx, form, tint in (('seasonal_blossom', 'Blossoming Leaves', 'seasonal_blossom', 0, None), ('seasonal_leaves', 'Seasonal Leaves', 'seasonal_leaves', 1, 'foliage'),
                                  ('seasonal_leaves_autumn', 'Autumn Leaves', 'seasonal_leaves_autumn', 2, None), ('seasonal_leaves_winter', 'Bare Branches', 'seasonal_leaves_winter', 3, None)):
        lb = dict(base_l); lb.pop('id', None); lb.pop('name', None); lb.pop('tint', None)
        lb.update({'key': k, 'drop': k, 'tex': tx, 'isLeaves': True, 'sapling': 'cherry_sapling', 'seasonalForm': form, 'randomTick': True})
        if tint: lb['tint'] = tint
        block(k, nm, lb, [tiles[tx]] * 6)

    # ---- the summoning wand (creative): use it to call up the creature it is set to; sneak + use picks the next ----
    if 'summoning_wand' not in tiles:
        item_pic('summoning_wand', recolour(pic('stick'), hsv_map(hue=0.75, sat=1.5, val=1.1, add=10)))
    if 'summoning_wand' not in items:
        items['summoning_wand'] = {'key': 'summoning_wand', 'name': 'Summoning Wand', 'maxStack': 1, 'icon': 'summoning_wand', 'summonWand': True,
                                   'creativeOnly': True, 'rarity': 'epic', 'desc': 'Use: call up a creature. Sneak and use: choose another.'}

    # ---- rotten cloth: ghouls and their kin drop it; string, or a bed with wool ----
    if 'rotten_cloth' not in tiles:
        item_pic('rotten_cloth', recolour(pic('paper'), hsv_map(hue=0.2, sat=0.5, val=0.62)))
    if 'rotten_cloth' not in items:
        items['rotten_cloth'] = {'key': 'rotten_cloth', 'name': 'Rotten Cloth', 'maxStack': 64, 'icon': 'rotten_cloth', 'desc': 'Torn from the walking dead.'}
    for k, m in mobs.items():
        dr = m.get('drops') if isinstance(m, dict) else None
        if isinstance(dr, list) and any(x[0] == 'rotten_flesh' for x in dr if isinstance(x, list)) and not any(x[0] == 'rotten_cloth' for x in dr if isinstance(x, list)):
            dr.append(['rotten_cloth', 0, 1, 0.3])
    recipe('string', 2, items_=['rotten_cloth'], extra=True)
    recipe('bed', 1, ['CCC', 'WWW', 'PPP'], {'C': 'rotten_cloth', 'W': '#wool' if '#wool' in rj.get('tags', {}) else 'wool', 'P': '#planks'}, extra=True)

    # ---- campfire logs: bark all round, the ring on the log ends (stretched over them), the grain along each log ----
    for cf in ('campfire', 'soul_campfire'):
        for b in sj['shapes'].get(cf, []):
            if b.get('side') != 'campfire_log': continue
            x0, y0, z0, x1, y1, z1 = b['b']
            b['end'] = 'campfire_log'; b['fit'] = True
            if z1 - z0 > x1 - x0: b['z'] = 'campfire_log_end'; b['grain'] = 'z'; b.pop('x', None)
            else: b['x'] = 'campfire_log_end'; b['grain'] = 'x'; b.pop('z', None)

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, separators=(',', ':'), ensure_ascii=False))
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    print('bta more ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
