#!/usr/bin/env python3
"""tools/port/betterend_blocks.py — Better End's blocks for the End band (idempotent; data_patches.py runs it when the
Better End jar is unpacked next to the project):

  python3 tools/port/betterend_blocks.py <data-dir> [<Better End folder>]

Better End 3.2.5 (github.com/quiqueck/BetterEnd, MIT; see docs/CREDITS.md) has its own terrain, turf, trees, mushrooms
and plants. Each block here takes Better End's own picture (16 px) and is made to sit in the DokuQuest Base pack:
doubled to the pack's 32 px, then given the fine grain of the pack's nearest picture (end stone for stones, moss for
turf tops, the oak log for logs, the oak leaves for leaves). Better End's shapes and colours stay; the grain is the
pack's. Plants keep their own pixels (their outline is the picture).

Blocks Better End and DokuQuest both have (end_moss, amber_block) take Better End's pictures. Tile names are be_<file>.
The list of what is added is written to tools/port/betterend_manifest.json.
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'
N = 32

# ---- the blocks ------------------------------------------------------------------------------------------------
# kind: cube | turf (top, side) | col (log: top, side) | leaf | trans | cross | hang (a hanging plant) | glass
# props are merged into the block (light, hardness...). Textures are Better End file names (no .png).
STONE = {'hardness': 2.5, 'tool': 'pickaxe', 'sound': 'stone'}
TURF = {'hardness': 3, 'tool': 'pickaxe', 'sound': 'grass', 'drop': 'end_stone'}
LOG = {'hardness': 2, 'tool': 'axe', 'sound': 'log', 'isLog': True, 'axisBlock': True, 'fuel': 1.5}
CAP = {'hardness': 0.4, 'tool': 'axe', 'sound': 'wood'}
BLOCKS = [
    # terrain and turf (spec §5a)
    ('endstone_dust', 'End Stone Dust', 'cube', ['endstone_dust'], {'hardness': 0.5, 'tool': 'shovel', 'sound': 'sand', 'falls': True}),
    ('end_moss', 'End Moss', 'turf', ['end_moss_top', 'end_moss_side'], TURF),
    ('end_mycelium', 'End Mycelium', 'turf', ['end_mycelium_top', 'end_mycelium_side'], TURF),
    ('chorus_nylium', 'Chorus Nylium', 'turf', ['chorus_nylium_top', 'chorus_nylium_side'], TURF),
    ('crystal_moss', 'Crystal Moss', 'turf', ['crystal_moss_top', 'crystal_moss_side'], TURF),
    ('shadow_grass', 'Shadow Grass', 'turf', ['shadow_grass_top', 'shadow_grass_side'], TURF),
    ('pink_moss', 'Pink Moss', 'turf', ['pink_moss_top', 'pink_moss_side'], TURF),
    ('amber_moss', 'Amber Moss', 'turf', ['amber_moss_top', 'amber_moss_side_1'], TURF),
    ('jungle_moss', 'Jungle Moss', 'turf', ['jungle_moss_top', 'jungle_moss_side'], TURF),
    ('sangnum', 'Sangnum', 'turf', ['sangnum_top', 'sangnum_side'], TURF),
    ('rutiscus', 'Rutiscus', 'turf', ['rutiscus_top_1', 'rutiscus_side_1'], TURF),
    ('cave_moss', 'Cave Moss', 'turf', ['cave_moss_top', 'cave_moss_side'], TURF),
    ('pallidium_full', 'Pallidium (Full Cover)', 'turf', ['pallidium_full_top', 'pallidium_full_side'], dict(TURF, drop='umbralith', bottom='be_umbralith')),
    ('pallidium_heavy', 'Pallidium (Heavy Cover)', 'turf', ['pallidium_heavy_top', 'pallidium_heavy_side'], dict(TURF, drop='umbralith', bottom='be_umbralith')),
    ('pallidium_thin', 'Pallidium (Thin Cover)', 'turf', ['pallidium_thin_top', 'pallidium_thin_side'], dict(TURF, drop='umbralith', bottom='be_umbralith')),
    ('pallidium_tiny', 'Pallidium (Tiny Cover)', 'turf', ['pallidium_tiny_top', 'pallidium_tiny_side'], dict(TURF, drop='umbralith', bottom='be_umbralith')),
    ('flavolite', 'Flavolite', 'cube', ['flavolite'], STONE),
    ('violecite', 'Violecite', 'cube', ['violecite'], STONE),
    ('sulphuric_rock', 'Sulphuric Rock', 'cube', ['sulphuric_rock'], STONE),
    ('brimstone', 'Brimstone', 'cube', ['brimstone'], dict(STONE, hardness=1.5)),
    ('umbralith', 'Umbralith', 'cube', ['umbralith'], STONE),
    ('virid_jadestone', 'Virid Jadestone', 'cube', ['virid_jadestone'], STONE),
    ('azure_jadestone', 'Azure Jadestone', 'cube', ['azure_jadestone'], STONE),
    ('sandy_jadestone', 'Sandy Jadestone', 'cube', ['sandy_jadestone'], STONE),
    ('dragon_bone_block', 'Dragon Bone Block', 'col', ['dragon_bone_block_top', 'dragon_bone_block_side'], dict(STONE, isLog=False, axisBlock=True)),
    ('dense_snow', 'Dense Snow', 'cube', ['dense_snow'], {'hardness': 0.5, 'tool': 'shovel', 'sound': 'snow'}),
    ('emerald_ice', 'Emerald Ice', 'glass', ['emerald_ice'], {'hardness': 0.5, 'tool': 'pickaxe', 'sound': 'glass', 'slippery': True}),
    ('dense_emerald_ice', 'Dense Emerald Ice', 'cube', ['dense_emerald_ice'], {'hardness': 0.8, 'tool': 'pickaxe', 'sound': 'glass', 'slippery': True}),
    ('ancient_emerald_ice', 'Ancient Emerald Ice', 'cube', ['ancient_emerald_ice'], {'hardness': 1, 'tool': 'pickaxe', 'sound': 'glass', 'slippery': True}),
    # ores and crystals (§5b)
    ('amber_ore', 'Amber Ore', 'cube', ['amber_ore'], dict(STONE, hardness=3)),
    ('ender_ore', 'Ender Ore', 'cube', ['ender_ore'], dict(STONE, hardness=3)),
    ('thallasium_ore', 'Thallasium Ore', 'cube', ['thallasium_ore'], dict(STONE, hardness=3)),
    ('amber_block', 'Amber Block', 'cube', ['amber_block'], dict(STONE, hardness=1.5)),
    ('aurora_crystal', 'Aurora Crystal', 'glass', ['aurora_crystal'], {'hardness': 1, 'tool': 'pickaxe', 'sound': 'glass', 'light': 15}),
    ('smaragdant_crystal', 'Smaragdant Crystal', 'col', ['smaragdant_crystal_top', 'smaragdant_crystal_side'], {'hardness': 1, 'tool': 'pickaxe', 'sound': 'glass', 'light': 15}),
    ('smaragdant_crystal_shard', 'Smaragdant Crystal Shard', 'cross', ['smaragdant_crystal_shard'], {'hardness': 0.3, 'sound': 'glass', 'light': 15}),
    # trees and mushrooms (§5c)
    ('mossy_glowshroom_log', 'Mossy Glowshroom Log', 'col', ['mossy_glowshroom_log_top', 'mossy_glowshroom_log_side'], LOG),
    ('mossy_glowshroom_cap', 'Mossy Glowshroom Cap', 'cube', ['mossy_glowshroom_cap'], CAP),
    ('mossy_glowshroom_hymenophore', 'Mossy Glowshroom Hymenophore', 'cube', ['mossy_glowshroom_hymenophore_1'], dict(CAP, light=15)),
    ('mossy_glowshroom_fur', 'Mossy Glowshroom Fur', 'hang', ['mossy_glowshroom_fur'], {'light': 15}),
    ('pythadendron_log', 'Pythadendron Log', 'col', ['pythadendron_log_top', 'pythadendron_log_side'], LOG),
    ('pythadendron_leaves', 'Pythadendron Leaves', 'leaf', ['pythadendron_leaves'], {}),
    ('lacugrove_log', 'Lacugrove Log', 'col', ['lacugrove_log_top', 'lacugrove_log_side'], LOG),
    ('lacugrove_leaves', 'Lacugrove Leaves', 'leaf', ['lacugrove_leaves'], {}),
    ('dragon_tree_log', 'Dragon Tree Log', 'col', ['dragon_tree_log_top', 'dragon_tree_log_side'], LOG),
    ('dragon_tree_leaves', 'Dragon Tree Leaves', 'leaf', ['dragon_tree_leaves'], {}),
    ('tenanea_log', 'Tenanea Log', 'col', ['tenanea_log_top', 'tenanea_log_side'], LOG),
    ('tenanea_leaves', 'Tenanea Leaves', 'leaf', ['tenanea_leaves'], {}),
    ('tenanea_flowers', 'Tenanea Flowers', 'hang', ['tenanea_flowers'], {'light': 15, 'tintRgb': [244, 156, 222]}),
    ('helix_tree_log', 'Helix Tree Log', 'col', ['helix_tree_log_top', 'helix_tree_log_side'], LOG),
    ('helix_tree_leaves', 'Helix Tree Leaves', 'cube', ['helix_tree_leaves'], {'hardness': 0.2, 'sound': 'leaves', 'isLeaves': True}),
    ('umbrella_tree_log', 'Umbrella Tree Log', 'col', ['umbrella_tree_log_top', 'umbrella_tree_log_side'], LOG),
    ('umbrella_tree_membrane', 'Umbrella Tree Membrane', 'trans', ['umbrella_tree_membrane_3'], {'hardness': 0.3, 'sound': 'slime', 'bouncy': True}),
    ('umbrella_tree_cluster', 'Umbrella Tree Cluster', 'cube', ['umbrella_tree_cluster'], {'hardness': 0.4, 'sound': 'wood', 'light': 15}),
    ('jellyshroom_log', 'Jellyshroom Log', 'col', ['jellyshroom_log_top', 'jellyshroom_log_side'], LOG),
    ('jellyshroom_cap_purple', 'Purple Jellyshroom Cap', 'trans', ['jellyshroom_cap'], {'hardness': 0.3, 'sound': 'slime', 'bouncy': True, 'tintRgb': [170, 76, 214]}),
    ('lucernia_log', 'Lucernia Log', 'col', ['lucernia_log_top', 'lucernia_log_side'], LOG),
    ('lucernia_leaves', 'Lucernia Leaves', 'leaf', ['lucernia_leaves_1'], {}),
    ('filalux', 'Filalux', 'hang', ['filalux_bottom'], {'light': 15}),
    ('amaranita_hyphae', 'Amaranita Hyphae', 'col', ['amaranita_hyphae_top', 'amaranita_hyphae_side'], LOG),
    ('amaranita_cap', 'Amaranita Cap', 'cube', ['amaranita_cap'], CAP),
    ('amaranita_lantern', 'Amaranita Lantern', 'cube', ['amaranita_lantern'], dict(CAP, light=15)),
    ('amaranita_fur', 'Amaranita Fur', 'hang', ['amaranita_fur'], {'light': 15}),
    # plants (§5d): light from the spec
    ('umbrella_moss', 'Umbrella Moss', 'cross', ['umbrella_moss_small'], {'light': 11}),
    ('twisted_umbrella_moss', 'Twisted Umbrella Moss', 'cross', ['twisted_umbrella_moss_small'], {'light': 11}),
    ('creeping_moss', 'Creeping Moss', 'cross', ['creeping_moss_leaves'], {'light': 11}),
    ('chorus_grass', 'Chorus Grass', 'cross', ['chorus_grass_01'], {}),
    ('crystal_grass', 'Crystal Grass', 'cross', ['crystal_grass_3'], {}),
    ('shadow_plant', 'Shadow Plant', 'cross', ['shadow_plant'], {}),
    ('bushy_grass', 'Bushy Grass', 'cross', ['bushy_grass_1'], {}),
    ('amber_grass', 'Amber Grass', 'cross', ['amber_grass'], {}),
    ('jungle_grass_be', 'Jungle Grass', 'cross', ['jungle_grass'], {}),
    ('blooming_cooksonia', 'Blooming Cooksonia', 'cross', ['blooming_cooksonia'], {'light': 6}),
    ('salteago', 'Salteago', 'cross', ['salteago'], {}),
    ('vaiolush_fern', 'Vaiolush Fern', 'cross', ['vaiolush_fern'], {}),
    ('fracturn', 'Fracturn', 'cross', ['fracturn'], {'light': 6}),
    ('clawfern', 'Clawfern', 'cross', ['clawfern'], {}),
    ('globulagus', 'Globulagus', 'cross', ['globulagus'], {'light': 6}),
    ('orango', 'Orango', 'cross', ['orango'], {}),
    ('aeridium', 'Aeridium', 'cross', ['aeridium'], {'light': 6}),
    ('lutebus', 'Lutebus', 'cross', ['lutebus'], {}),
    ('lamellarium', 'Lamellarium', 'cross', ['lamellarium'], {}),
    ('inflexia', 'Inflexia', 'cross', ['inflexia'], {}),
    ('needlegrass', 'Needlegrass', 'cross', ['needlegrass_01'], {}),
    ('murkweed', 'Murkweed', 'cross', ['murkweed'], {}),
    ('cave_grass', 'Cave Grass', 'cross', ['cave_grass_1'], {}),
    ('charnia_cyan', 'Cyan Charnia', 'cross', ['charnia_cyan'], {}),
    ('charnia_purple', 'Purple Charnia', 'cross', ['charnia_purple'], {}),
    ('charnia_red', 'Red Charnia', 'cross', ['charnia_red'], {}),
    ('charnia_orange', 'Orange Charnia', 'cross', ['charnia_orange'], {}),
    ('charnia_green', 'Green Charnia', 'cross', ['charnia_green'], {}),
    ('end_lily', 'End Lily', 'cross', ['end_lily_flower'], {'light': 13}),
    ('glowing_pillar_luminophor', 'Glowing Pillar Luminophor', 'cube', ['glowing_pillar_luminophor'], {'hardness': 0.4, 'sound': 'wood', 'light': 15}),
    ('dense_vine', 'Dense Vine', 'hang', ['dense_vine'], {'light': 15}),
    ('twisted_vine', 'Twisted Vine', 'hang', ['twisted_vine'], {}),
    ('jungle_vine', 'Jungle Vine', 'hang', ['jungle_vine'], {}),
    ('blue_vine_lantern', 'Blue Vine Lantern', 'cube', ['blue_vine_lantern'], {'hardness': 0.4, 'sound': 'wood', 'light': 15}),
    ('blue_vine', 'Blue Vine', 'cross', ['blue_vine'], {}),
    ('neon_cactus', 'Neon Cactus', 'cube', ['neon_cactus_block_side'], {'hardness': 0.4, 'sound': 'wood', 'light': 15, 'tex2': 'neon_cactus_block_top'}),
    ('tail_moss', 'Tail Moss', 'cross', ['tail_moss'], {}),
    ('cyan_moss', 'Cyan Moss', 'cross', ['cyan_moss'], {}),
    ('twisted_moss', 'Twisted Moss', 'cross', ['twisted_moss'], {}),
    ('end_stone_stalactite', 'End Stone Stalactite', 'cross', ['end_stone_stalactite_3'], {'hardness': 0.5, 'sound': 'stone', 'hangs': True}),
]
# the pack picture each kind takes its grain from (the first that exists)
DONORS = {
    'stone': ['end_stone', 'stone'], 'turf_top': ['moss_block', 'grass_block_top'], 'turf_side': ['end_stone'],
    'log_top': ['oak_log_top'], 'log_side': ['oak_log'], 'leaf': ['oak_leaves'], 'cap': ['red_mushroom_block', 'nether_wart_block'],
    'ice': ['packed_ice', 'ice'], 'snow': ['snow'], 'sand': ['sand'], 'glass': ['amethyst_block'],
}


def load_be(be, name):
    p = be / 'assets/betterend/textures/block' / (name + '.png')
    if not p.exists(): return None
    im = Image.open(p).convert('RGBA')
    w, h = im.size
    if h > w: im = im.crop((0, 0, w, w))   # an animation strip: its first frame
    return im


def load_pack(d, names):
    for n in names:
        p = d / PACK / (n + '.png')
        if p.exists():
            im = Image.open(p).convert('RGBA')
            w, h = im.size
            if h > w: im = im.crop((0, 0, w, w))
            if im.size[0] != N: im = im.resize((N, N), Image.BOX if im.size[0] > N else Image.NEAREST)
            return np.asarray(im).astype(np.float32)
    return None


def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def box_blur(a, r=2):
    # the local mean over a (2r+1)^2 window, wrapping (block pictures tile)
    out = np.zeros_like(a); n = 0
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out += np.roll(np.roll(a, dy, 0), dx, 1); n += 1
    return out / n


def restyle(im, donor, k=0.5):
    """Better End's picture at 32 px with the pack picture's fine grain (its light and shade at the scale of a few
    pixels, around its own local mean) laid over Better End's colours."""
    if im.size[0] != N: im = im.resize((N, N), Image.NEAREST if im.size[0] < N else Image.BOX)
    a = np.asarray(im).astype(np.float32)
    if donor is None: return Image.fromarray(a.astype(np.uint8), 'RGBA')
    dl = lum(donor)
    grain = (dl - box_blur(dl, 2)) / max(1.0, float(dl.mean()))
    m = a[..., 3] > 0
    rgb = a[..., :3] * (1 + k * grain[..., None])
    a[..., :3] = np.where(m[..., None], np.clip(rgb, 0, 255), a[..., :3])
    return Image.fromarray(a.astype(np.uint8), 'RGBA')


def tint(im, rgb):
    a = np.asarray(im).astype(np.float32)
    l = lum(a) / 255.0
    for c in range(3): a[..., c] = np.clip(l * rgb[c] * 1.25, 0, 255)
    return Image.fromarray(a.astype(np.uint8), 'RGBA')


def main(d, be):
    d = pathlib.Path(d); be = pathlib.Path(be)
    if not (be / 'assets/betterend/textures/block').exists():
        print('betterend_blocks: no Better End textures at', be, '- skipped'); return
    bj = json.loads((d / 'blocks.json').read_text())
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; at = {b['key']: i for i, b in enumerate(blocks)}
    made = []

    def tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles:
            tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    # the art pass (tools/port/restyle.py via local_mod_art.fit): solid blocks become the pack's picture of the same kind
    # of thing in Better End's colours; turf sides keep the pack's grass-side lip; see-through things keep their shape
    import importlib.util as _ilu
    _lm = _ilu.spec_from_file_location('local_mod_art', pathlib.Path(__file__).with_name('local_mod_art.py')); LM = _ilu.module_from_spec(_lm); _lm.loader.exec_module(LM)
    RAMP = {'stone', 'turf_top', 'log_top', 'log_side', 'leaf', 'cap', 'ice', 'snow', 'sand'}
    def pic(src, donor_kind, k=0.5, rgb=None):
        im = load_be(be, src)
        if im is None: return None
        if rgb: im = tint(im, rgb)
        dn = load_pack(d, DONORS[donor_kind]) if donor_kind else None
        if dn is not None and donor_kind in RAMP and im.getextrema()[3][0] >= 128:
            return tile('be_' + src, LM.fit(im, Image.fromarray(dn.astype(np.uint8), 'RGBA'), 'ramp'))
        if donor_kind == 'turf_side':
            gs = load_pack(d, ['grass_block_side'])
            if gs is not None: return tile('be_' + src, LM.fit(im, Image.fromarray(gs.astype(np.uint8), 'RGBA'), 'grass'))
        return tile('be_' + src, restyle(im, dn, k))

    for key, name, kind, tex, props in BLOCKS:
        props = dict(props)
        rgb = props.pop('tintRgb', None); tex2 = props.pop('tex2', None); bottom = props.pop('bottom', None)
        if kind == 'turf':
            t_top = pic(tex[0], 'turf_top', 0.45); t_side = pic(tex[1], 'turf_side', 0.4)
            if t_top is None or t_side is None: print('  missing', key); continue
            t_bot = tiles.get(bottom) if bottom else tiles.get('end_stone')
            if bottom and t_bot is None: t_bot = pic(bottom[3:], 'stone')
            faces = [t_side, t_side, t_top, t_bot, t_side, t_side]
            texj = {'top': 'be_' + tex[0], 'side': 'be_' + tex[1], 'bottom': bottom or 'end_stone'}
            base = {'render': 1, 'solid': True, 'opaque': True}
        elif kind == 'col':
            t_top = pic(tex[0], 'log_top' if props.get('isLog') else 'stone'); t_side = pic(tex[1], 'log_side' if props.get('isLog') else 'stone')
            if t_top is None or t_side is None: print('  missing', key); continue
            faces = [t_side, t_side, t_top, t_top, t_side, t_side]
            texj = {'top': 'be_' + tex[0], 'bottom': 'be_' + tex[0], 'side': 'be_' + tex[1]}
            base = {'render': 1, 'solid': True, 'opaque': True}
        else:
            donor = {'cube': 'cap' if props.get('sound') == 'wood' else 'ice' if 'ice' in key else 'snow' if 'snow' in key else 'sand' if 'dust' in key else 'stone',
                     'leaf': 'leaf', 'trans': None, 'glass': 'glass', 'cross': None, 'hang': None}[kind]
            if kind == 'cube' and props.get('isLeaves'): donor = 'leaf'
            t = pic(tex[0], donor, 0.35 if kind in ('glass', 'leaf') else 0.5, rgb)
            if t is None: print('  missing', key); continue
            faces = [t] * 6
            texj = 'be_' + tex[0]
            if tex2:
                t2 = pic(tex2, donor)
                if t2 is not None: faces = [t, t, t2, t2, t, t]; texj = {'top': 'be_' + tex2, 'bottom': 'be_' + tex2, 'side': 'be_' + tex[0]}
            base = {'cube': {'render': 1, 'solid': True, 'opaque': True},
                    'leaf': {'render': 2, 'solid': True, 'opaque': False, 'isLeaves': True, 'lightAtt': 1, 'hardness': 0.2, 'sound': 'leaves'},
                    'trans': {'render': 2, 'solid': True, 'opaque': False, 'translucent': True, 'selfCull': True},
                    'glass': {'render': 2, 'solid': True, 'opaque': False, 'translucent': True, 'selfCull': True},
                    'cross': {'render': 3, 'solid': False, 'opaque': False, 'replaceable': True, 'hardness': 0, 'needsSupport': True, 'sound': 'grass'},
                    'hang': {'render': 3, 'solid': False, 'opaque': False, 'replaceable': True, 'hardness': 0, 'needsSupport': True, 'hangs': True,
                             'stackPlant': True, 'climb': True, 'sound': 'leaves'}}[kind]
        b = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': None, 'tier': 0, 'drop': key,
             'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(base); b.update(props); b['tex'] = texj; b['betterEnd'] = True
        if key in at:   # a block both games have: Better End's look, our id and behaviour otherwise
            old = blocks[at[key]]
            for k2 in ('tex', 'name', 'render', 'solid', 'opaque', 'translucent', 'selfCull', 'light', 'betterEnd'):
                if k2 in b: old[k2] = b[k2]
            bj['faces'][at[key]] = faces
        else:
            b['id'] = len(blocks); blocks.append(b); bj['faces'].append(faces); at[key] = b['id']
        if key not in items: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}
        made.append(key)

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':')))
    pathlib.Path(__file__).with_name('betterend_manifest.json').write_text(json.dumps({'blocks': made}, indent=1))
    print('betterend_blocks: %d blocks' % len(made))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data', sys.argv[2] if len(sys.argv) > 2 else str(pathlib.Path(__file__).resolve().parents[3] / 'Better END'))
