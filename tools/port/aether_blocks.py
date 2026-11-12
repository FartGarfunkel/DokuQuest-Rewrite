#!/usr/bin/env python3
"""tools/port/aether_blocks.py — The Aether's blocks for the Skylands (idempotent; data_patches.py runs it):
  python3 tools/port/aether_blocks.py <data-dir>

The Aether's art is "All Rights Reserved", so nothing here is taken from it. Each picture is the DokuQuest Base pack's
nearest picture, its light and shade kept, in the Aether block's own colour (the average colours measured from the
mod, in the research notes): holystone from stone, aether dirt from dirt (slate blue-grey), aether grass from the
pack's grass (mint on top, a mint fringe over aether dirt on the sides, never tinted by biome), skyroot from oak, ores
from the pack's coal ore with their own spots, aerclouds from wool. To see the Aether's own pictures on your computer,
make the local pack with tools/port/local_mod_art.py (never shipped).

Also puts the Aether's behaviour in the data: only the blue aercloud throws you (cold and golden ones catch you
softly), gravitite ore gives no light, quicksoil is slippery.
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
N = 32


def pack_pic(d, names):
    for n in names:
        p = d / PACK / (n + '.png')
        if p.exists():
            im = Image.open(p).convert('RGBA'); w, h = im.size
            if h > w: im = im.crop((0, 0, w, w))
            if im.size[0] != N: im = im.resize((N, N), Image.NEAREST if im.size[0] < N else Image.BOX)
            return np.asarray(im).astype(np.float32)
    raise SystemExit('aether_blocks: no pack picture among %s' % names)


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
def rgb(h): return np.array([(h >> 16) & 255, (h >> 8) & 255, h & 255], np.float32)


def recolour(a, hexcol, contrast=1.0, mask=None):
    m = a[..., 3] > 0 if mask is None else mask
    l = lum(a); mean = max(1.0, float(l[m].mean())) if m.any() else 128.0
    k = 1 + (l / mean - 1) * contrast
    out = a.copy()
    out[..., :3] = np.where(m[..., None], np.clip(rgb(hexcol)[None, None, :] * k[..., None], 0, 255), a[..., :3])
    return out


def ore(stone, coal, hexstone, hexore):
    """the pack's coal ore: its stone in the host's colour, its spots (darker than the stone round them) in the ore's"""
    l = lum(coal); spots = (l < np.quantile(l, 0.3)) & (coal[..., 3] > 0)
    out = recolour(coal, hexstone, 1.0, ~spots & (coal[..., 3] > 0))
    return recolour(out, hexore, 1.3, spots)


def save(a): return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text()); pm = json.loads((d / 'packmap.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}
    inpm = {e[0] for e in pm}

    def tile(name, a):
        rel = 'textures/block/%s.png' % name
        save(a).save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        if name not in inpm: pm.append([name, [name], [name]]); inpm.add(name)   # a pack may bring its own picture for it
        return tiles[name]

    def block(key, name, props, tex, item=True):
        faces_of = lambda t: [t['side'], t['side'], t['top'], t['bottom'], t['side'], t['side']] if isinstance(t, dict) else [t] * 6
        tid = {k: tiles[v] for k, v in tex.items()} if isinstance(tex, dict) else tiles[tex]
        b = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.0, 'tool': 'pickaxe', 'tier': 0, 'drop': key,
             'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(props); b['tex'] = tex; b['aether'] = True
        if key in at: blocks[at[key]].update(b); bj['faces'][at[key]] = faces_of(tid)
        else:
            b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces_of(tid)); at[key] = b['id']
        if item and key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    stone = pack_pic(d, ['stone']); dirt = pack_pic(d, ['dirt']); coal = pack_pic(d, ['coal_ore'])
    gtop = pack_pic(d, ['grass_block_top']); gside = pack_pic(d, ['grass_block_side']); gover = pack_pic(d, ['grass_block_side_overlay'])
    logs = pack_pic(d, ['oak_log']); logt = pack_pic(d, ['oak_log_top']); leaves = pack_pic(d, ['oak_leaves'])
    sand = pack_pic(d, ['sand']); mossy = pack_pic(d, ['mossy_cobblestone']); ice = pack_pic(d, ['packed_ice', 'diorite'])
    wool = pack_pic(d, ['white_wool', 'snow']); daisy = pack_pic(d, ['oxeye_daisy']); allium = pack_pic(d, ['allium'])

    tile('holystone', recolour(stone, 0xA7A7A7, 1.15))
    tile('mossy_holystone', recolour(mossy, 0x95A5A2, 1.1))
    tile('aether_dirt', recolour(dirt, 0x63777F, 1.1))
    tile('aether_grass_top', recolour(gtop, 0x72A785, 1.15))
    side = recolour(dirt, 0x63777F, 1.1)
    om = gover[..., 3] > 0
    side = np.where(om[..., None], recolour(gover, 0x8BC9A0, 1.1, om), side); side[..., 3] = 255
    tile('aether_grass_side', side)
    tile('quicksoil', recolour(sand, 0xD8CE82, 1.2))
    tile('icestone', recolour(ice, 0xBCBCBC, 1.1))
    tile('skyroot_log_side', recolour(logs, 0x57553E, 1.15)); tile('skyroot_log_top', recolour(logt, 0x65664B, 1.1))
    tile('golden_oak_log_side', recolour(logs, 0x665B3B, 1.15)); tile('golden_oak_log_top', recolour(logt, 0x65664B, 1.1))
    tile('skyroot_leaves', recolour(leaves, 0x89A961, 1.1)); tile('golden_oak_leaves', recolour(leaves, 0xCCAF38, 1.1))
    tile('crystal_leaves', recolour(leaves, 0x5AA8C1, 1.2))
    tile('ambrosium_ore', ore(stone, coal, 0xA7A7A7, 0xE8D85A)); tile('zanite_ore', ore(stone, coal, 0xA7A7A7, 0x8A6CC4))
    tile('gravitite_ore', ore(stone, coal, 0xA7A7A7, 0xD472C8))
    for k, c, al in (('cold_aercloud', 0xDDE8EE, 225), ('blue_aercloud', 0xC2F2F2, 225), ('golden_aercloud', 0xF2D57B, 225)):
        a = recolour(wool, c, 0.6); a[..., 3] = al; tile(k, a)
    fl = daisy.copy(); petals = (lum(daisy) > np.quantile(lum(daisy)[daisy[..., 3] > 0], 0.55)) & (daisy[..., 3] > 0)
    bricks = pack_pic(d, ['chiseled_stone_bricks', 'stone_bricks', 'stone'])
    tile('carved_stone', recolour(bricks, 0x797979, 1.15))
    sentry = recolour(bricks, 0x74797F, 1.15); l = lum(bricks); hot = (l > np.quantile(l, 0.86)) & (bricks[..., 3] > 0)
    sentry[hot, 0] = 120; sentry[hot, 1] = 200; sentry[hot, 2] = 255
    tile('sentry_stone', sentry)
    # the silver and gold dungeons: angelic stone (pale, gold-lit) and hellfire stone (dark red, ember-lit), the
    # silver dungeon's pillars, holystone bricks, and the crystal tree's fruit leaves
    sbr = pack_pic(d, ['stone_bricks']); qb = pack_pic(d, ['quartz_bricks', 'stone_bricks'])
    qp = pack_pic(d, ['quartz_pillar', 'stone']); qpt = pack_pic(d, ['quartz_pillar_top', 'stone']); cq = pack_pic(d, ['chiseled_quartz_block', 'stone'])
    def lit(a, hexcol, q=0.86):
        out = a.copy(); l = lum(a); hot = (l > np.quantile(l, q)) & (a[..., 3] > 0)
        out[hot, :3] = rgb(hexcol); return out
    tile('angelic_stone', recolour(qb, 0xBDB8AE, 1.2))
    tile('light_angelic_stone', lit(recolour(qb, 0xC9C3B4, 1.2), 0xFFE58A))
    tile('hellfire_stone', recolour(sbr, 0x6E2A22, 1.2))
    tile('light_hellfire_stone', lit(recolour(sbr, 0x7A2E24, 1.2), 0xFF9A3C))
    tile('pillar_side', recolour(qp, 0xD8D4C8, 1.15)); tile('pillar_top', recolour(qpt, 0xD8D4C8, 1.15))
    tile('pillar_cap_side', recolour(cq, 0xCFCABB, 1.2))
    tile('holystone_bricks', recolour(sbr, 0xB4B4B4, 1.15))
    cf = recolour(leaves, 0x5AA8C1, 1.2); ll = lum(leaves); fruit = (ll > np.quantile(ll[leaves[..., 3] > 0], 0.93)) & (leaves[..., 3] > 0)
    cf[fruit, :3] = rgb(0xEAF6FF); tile('crystal_fruit_leaves', cf)
    tile('white_flower', recolour(fl, 0xD8DEDA, 1.0, petals))
    pl = allium.copy(); bloom = (np.abs(allium[..., 0] - allium[..., 1]) > 25) & (allium[..., 3] > 0)
    tile('purple_flower', recolour(pl, 0x7C79B0, 1.1, bloom))

    LEAF = {'render': 2, 'opaque': False, 'hardness': 0.2, 'tool': None, 'sound': 'leaves', 'isLeaves': True, 'lightAtt': 1}
    LOG = {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5}
    CROSS = {'render': 3, 'solid': False, 'opaque': False, 'replaceable': True, 'hardness': 0, 'tool': None, 'needsSupport': True, 'sound': 'grass'}
    block('holystone', 'Holystone', {'hardness': 0.5}, 'holystone')
    block('mossy_holystone', 'Mossy Holystone', {'hardness': 0.5}, 'mossy_holystone')
    block('aether_dirt', 'Aether Dirt', {'hardness': 0.5, 'tool': 'shovel', 'sound': 'dirt'}, 'aether_dirt')
    block('aether_grass_block', 'Aether Grass Block', {'hardness': 0.6, 'tool': 'shovel', 'sound': 'grass', 'drop': 'aether_dirt'},
          {'top': 'aether_grass_top', 'side': 'aether_grass_side', 'bottom': 'aether_dirt'})
    block('quicksoil', 'Quicksoil', {'hardness': 0.5, 'tool': 'shovel', 'sound': 'sand', 'slippery': True}, 'quicksoil')
    block('icestone', 'Icestone', {'hardness': 0.5, 'sound': 'glass'}, 'icestone')
    block('skyroot_log', 'Skyroot Log', LOG, {'top': 'skyroot_log_top', 'bottom': 'skyroot_log_top', 'side': 'skyroot_log_side'})
    block('golden_oak_log', 'Golden Oak Log', LOG, {'top': 'golden_oak_log_top', 'bottom': 'golden_oak_log_top', 'side': 'golden_oak_log_side'})
    block('skyroot_leaves', 'Skyroot Leaves', LEAF, 'skyroot_leaves')
    block('golden_oak_leaves', 'Golden Oak Leaves', LEAF, 'golden_oak_leaves')
    block('crystal_leaves', 'Crystal Leaves', LEAF, 'crystal_leaves')
    block('ambrosium_ore', 'Ambrosium Ore', {'hardness': 3}, 'ambrosium_ore')
    block('zanite_ore', 'Zanite Ore', {'hardness': 3}, 'zanite_ore')
    block('gravitite_ore', 'Gravitite Ore', {'hardness': 5, 'light': 0}, 'gravitite_ore')
    CLOUD = {'render': 2, 'opaque': False, 'hardness': 0.3, 'tool': None, 'sound': 'wool', 'cloud': True, 'selfCull': True, 'translucent': True}
    block('cold_aercloud', 'Cold Aercloud', dict(CLOUD, bouncy=False), 'cold_aercloud')       # catches you softly, no fall damage
    block('blue_aercloud', 'Blue Aercloud', dict(CLOUD, bouncy=True), 'blue_aercloud')         # throws you up (sneak to stand on it)
    block('golden_aercloud', 'Golden Aercloud', dict(CLOUD, bouncy=False), 'golden_aercloud')
    block('carved_stone', 'Carved Stone', {'hardness': 0.5}, 'carved_stone')            # the bronze dungeons
    block('sentry_stone', 'Sentry Stone', {'hardness': 0.5, 'light': 11}, 'sentry_stone')
    block('angelic_stone', 'Angelic Stone', {'hardness': 0.5}, 'angelic_stone')          # the silver dungeons
    block('light_angelic_stone', 'Light Angelic Stone', {'hardness': 0.5, 'light': 11}, 'light_angelic_stone')
    block('hellfire_stone', 'Hellfire Stone', {'hardness': 0.5}, 'hellfire_stone')        # the gold dungeons
    block('light_hellfire_stone', 'Light Hellfire Stone', {'hardness': 0.5, 'light': 11}, 'light_hellfire_stone')
    block('pillar', 'Pillar', {'hardness': 0.5, 'axisBlock': True}, {'top': 'pillar_top', 'bottom': 'pillar_top', 'side': 'pillar_side'})
    block('pillar_top', 'Pillar Top', {'hardness': 0.5}, {'top': 'pillar_top', 'bottom': 'pillar_top', 'side': 'pillar_cap_side'})
    block('holystone_bricks', 'Holystone Bricks', {'hardness': 0.5}, 'holystone_bricks')
    block('crystal_fruit_leaves', 'Crystal Fruit Leaves', LEAF, 'crystal_fruit_leaves')
    block('white_flower', 'White Flower', CROSS, 'white_flower')
    block('purple_flower', 'Purple Flower', CROSS, 'purple_flower')

    # the BTA Nether's new tiles may come from a pack too (tools/port/bta_nether_blocks.py made them)
    for t in ('brimsand', 'nether_brimstone_top', 'nether_brimstone_side', 'brimthaw', 'cobbled_netherrack', 'crystalline_netherrack',
              'cobbled_gloomstone', 'ember', 'rubyglass_column', 'rubyglass_node', 'rubyglass_crystal', 'rubyglass_growth',
              'magmatic_boulder', 'sulfuric_boulder', 'bone_pile', 'soul_catcher', 'ash_layer', 'gloomstone', 'nether_coal_ore', 'basalt_coal_ore',
              'gloom_coal_ore', 'sulfuric_fire'):
        if t in tiles and t not in inpm: pm.append([t, [t], [t]]); inpm.add(t)

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    (d / 'packmap.json').write_text(json.dumps(pm, separators=(',', ':')))
    print('aether_blocks: done')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
