#!/usr/bin/env python3
"""tools/port/bta_nether_blocks.py — the blocks of Better than Adventure 8.0's Nether that DokuQuest lacked (idempotent;
data_patches.py runs it):  python3 tools/port/bta_nether_blocks.py <data-dir>

BTA's art is not ours to ship (it has no licence), so each picture here is the DokuQuest Base pack's nearest picture,
its light and shade kept and its colours moved to BTA's block's average colour (the colours measured from BTA's
textures; see the research notes). To see BTA's own pictures on your computer, make the local pack with
tools/port/local_mod_art.py (never shipped).

  brimsand (BTA's black volcanic sand; it does not fall), nether_brimstone (BTA's near-black brimstone; Better End has
  its own yellow brimstone), brimthaw (glowing olive, light 10), cobbled_netherrack (BTA's world filler),
  crystalline_netherrack (cobbled, with glowing red veins), cobbled_gloomstone, ember (light 5, buried under the Old
  World's ash), ash_layer (a 1/8 layer of ash), rubyglass_column / rubyglass_node (the giant crystals), rubyglass_crystal
  (the crust on the crystal seas: no light, slippery), rubyglass_growth, magmatic_boulder, sulfuric_boulder, bone_pile,
  soul_catcher.
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
            im = Image.open(p).convert('RGBA')
            w, h = im.size
            if h > w: im = im.crop((0, 0, w, w))
            if im.size[0] != N: im = im.resize((N, N), Image.NEAREST if im.size[0] < N else Image.BOX)
            return np.asarray(im).astype(np.float32)
    raise SystemExit('bta_nether_blocks: no pack picture among %s' % names)


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def recolour(a, hexcol, contrast=1.0):
    """the donor's light and shade (each pixel's brightness over the picture's mean), in the target colour"""
    rgb = np.array([(hexcol >> 16) & 255, (hexcol >> 8) & 255, hexcol & 255], np.float32)
    m = a[..., 3] > 0
    l = lum(a); mean = max(1.0, float(l[m].mean())) if m.any() else 128.0
    k = 1 + (l / mean - 1) * contrast
    out = a.copy()
    out[..., :3] = np.where(m[..., None], np.clip(rgb[None, None, :] * k[..., None], 0, 255), a[..., :3])
    return out


def veins(a, hexcol, frac=0.14):
    """the brightest pixels become glowing veins of a colour"""
    rgb = np.array([(hexcol >> 16) & 255, (hexcol >> 8) & 255, hexcol & 255], np.float32)
    l = lum(a); thr = np.quantile(l[a[..., 3] > 0], 1 - frac)
    out = a.copy(); sel = (l >= thr) & (a[..., 3] > 0)
    out[sel, :3] = rgb
    return out


def boulder(a, seed):
    """a rock's outline (BTA's boulders are a cross of rock sprites): a lumpy oval in the lower part of the picture"""
    rng = np.random.default_rng(seed); yy, xx = np.mgrid[0:N, 0:N]
    r = ((xx - 15.5) / 12.5) ** 2 + ((yy - 21.5) / 9.5) ** 2 + rng.normal(0, 0.06, (N, N))
    out = a.copy(); out[..., 3] = np.where(r <= 1, 255, 0)
    edge = (r > 0.75) & (r <= 1); out[edge, :3] *= 0.7
    return out


def save(a): return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')


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
        b.update(props); b['tex'] = tex; b['bta'] = True
        if key in at: blocks[at[key]].update(b); bj['faces'][at[key]] = faces_of(tid)
        else:
            b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces_of(tid)); at[key] = b['id']
        if item and key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    sand = pack_pic(d, ['sand']); cob = pack_pic(d, ['cobblestone']); cdeep = pack_pic(d, ['cobbled_deepslate', 'cobblestone'])
    bas_t = pack_pic(d, ['basalt_top']); bas_s = pack_pic(d, ['basalt_side']); magma = pack_pic(d, ['magma'])
    ame = pack_pic(d, ['amethyst_block']); glass = pack_pic(d, ['glass']); clus = pack_pic(d, ['amethyst_cluster'])
    bush = pack_pic(d, ['dead_bush']); grav = pack_pic(d, ['gravel']); snow = pack_pic(d, ['snow'])

    tile('brimsand', recolour(sand, 0x2E2B24, 1.3))
    tile('nether_brimstone_top', recolour(bas_t, 0x2F2E2C, 1.2)); tile('nether_brimstone_side', recolour(bas_s, 0x34322E, 1.2))
    tile('brimthaw', veins(recolour(magma, 0x4A4618, 1.1), 0xC8C030, 0.18))
    tile('cobbled_netherrack', recolour(cob, 0x6E3533, 1.1))
    tile('crystalline_netherrack', veins(recolour(cob, 0x6A2A2C, 1.1), 0xE0283A, 0.12))
    tile('cobbled_gloomstone', recolour(cdeep, 0x312742, 1.1))
    tile('ember', veins(recolour(magma, 0x3A271A, 1.2), 0xFF7A1A, 0.1))
    tile('rubyglass_column', veins(recolour(ame, 0x86091A, 1.2), 0xFF3B4A, 0.08))
    tile('rubyglass_node', veins(recolour(ame, 0xA21234, 1.2), 0xFF6A7A, 0.12))
    # the crust: glass's frame in bright red, its clear middle a deep translucent red
    g = recolour(glass, 0xB01020, 1.0); clear = glass[..., 3] < 40
    g[clear, 0] = 116; g[clear, 1] = 0; g[clear, 2] = 8; g[..., 3] = np.where(clear, 150, 215)
    tile('rubyglass_crystal', g)
    tile('rubyglass_growth', recolour(clus, 0x8A091A, 1.2))
    tile('magmatic_boulder', boulder(veins(recolour(grav, 0x774229, 1.2), 0xFF6A20, 0.1), 1))
    tile('sulfuric_boulder', boulder(veins(recolour(grav, 0x787722, 1.2), 0xE8E040, 0.1), 2))
    tile('bone_pile', recolour(bush, 0x938A80, 1.1))
    tile('soul_catcher', recolour(bush, 0x46362C, 1.2))
    tile('ash_layer', recolour(snow, 0x454046, 1.2))

    block('brimsand', 'Brimsand', {'hardness': 0.7, 'tool': 'shovel', 'sound': 'sand'}, 'brimsand')
    block('nether_brimstone', 'Brimstone', {'hardness': 1.0}, {'top': 'nether_brimstone_top', 'bottom': 'nether_brimstone_top', 'side': 'nether_brimstone_side'})
    block('brimthaw', 'Brimthaw', {'hardness': 1.25, 'light': 10, 'hot': True}, 'brimthaw')
    block('cobbled_netherrack', 'Cobbled Netherrack', {'hardness': 0.4, 'tier': 1}, 'cobbled_netherrack')
    block('crystalline_netherrack', 'Crystalline Netherrack', {'hardness': 0.4, 'tier': 1, 'drop': 'cobbled_netherrack'}, 'crystalline_netherrack')
    block('cobbled_gloomstone', 'Cobbled Gloomstone', {'hardness': 2.0, 'tier': 1}, 'cobbled_gloomstone')
    # BTA's sulfuric fire: the pack's flames burning sulphur-yellow (#e0d620), forever on the brimstone of the Sulfur Fields
    tile('sulfuric_fire', recolour(pack_pic(d, ['fire_0', 'fire']), 0xE0D620, 1.25))
    block('sulfuric_fire', 'Sulfuric Fire', {'render': 3, 'solid': False, 'opaque': False, 'replaceable': True, 'hardness': 0, 'tool': None,
          'drop': None, 'light': 15, 'sound': 'wool', 'fire': True}, 'sulfuric_fire', item=False)
    block('ember', 'Ember', {'hardness': 1.0, 'light': 5, 'hot': True}, 'ember')
    block('ash_layer', 'Ash Layer', {'render': 7, 'opaque': False, 'hardness': 0.1, 'tool': 'shovel', 'sound': 'sand', 'shapeKind': 12, 'drop': 'ash'}, 'ash_layer')
    # (the giant crystals clear red glass, as the user wants them, one body with the crystal: no faces between them)
    block('rubyglass_column', 'Rubyglass Column', {'render': 2, 'opaque': False, 'hardness': 2.0, 'sound': 'glass', 'translucent': True, 'selfCull': True, 'cullWith': 'rubyglass'}, 'rubyglass_column')
    block('rubyglass_node', 'Rubyglass Node', {'render': 2, 'opaque': False, 'hardness': 3.0, 'sound': 'glass', 'translucent': True, 'selfCull': True, 'cullWith': 'rubyglass'}, 'rubyglass_node')
    block('rubyglass_crystal', 'Rubyglass Crystal', {'render': 2, 'opaque': False, 'hardness': 1.5, 'sound': 'glass', 'translucent': True, 'selfCull': True, 'cullWith': 'rubyglass',
                                                     'slippery': True, 'drop': None}, 'rubyglass_crystal')
    cross = {'render': 3, 'solid': False, 'opaque': False, 'replaceable': False, 'hardness': 1.0, 'needsSupport': True}
    block('rubyglass_growth', 'Rubyglass Growth', dict(cross, sound='glass'), 'rubyglass_growth')
    block('magmatic_boulder', 'Magmatic Boulder', dict(cross, hot=True), 'magmatic_boulder')
    block('sulfuric_boulder', 'Sulfuric Boulder', dict(cross, hot=True), 'sulfuric_boulder')
    block('bone_pile', 'Bone Pile', dict(cross, hardness=0.5, sound='stone'), 'bone_pile')
    block('soul_catcher', 'Soul Catcher', dict(cross, hardness=0.5, sound='grass'), 'soul_catcher')

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    print('bta_nether_blocks: done')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
