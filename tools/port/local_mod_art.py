#!/usr/bin/env python3
"""tools/port/local_mod_art.py — a resource pack for YOUR computer only, with the real pictures of Better than Adventure
8.0's Nether blocks and The Aether's blocks, fitted to the DokuQuest Base pack (doubled to 32 px, then given the fine
grain of the base pack's nearest picture) — now through tools/port/restyle.py's art pass: each picture is the base pack's
picture of the same kind of thing in the mod picture's own colours (ores, grass sides and shaped things handled apart).

BTA's art has no licence and The Aether's is "All Rights Reserved", so these pictures must never be committed, shipped
in a release or shared: this tool reads them from your own copies of the mods and writes the pack into your game
folder (outside the project). The game's own pictures for these blocks (tools/port/bta_nether_blocks.py,
tools/port/aether_blocks.py) are what everyone else sees.

  python3 tools/port/local_mod_art.py [--bta DIR] [--aether DIR] [--data data] [--out PACKDIR]

Defaults: ../BTA/jarmods/bta-v8.0.1, ../Aether (next to the project), and
~/.local/share/DokuQuest/resourcepacks/Mod Art (local). Then pick "Mod Art (local)" in Options -> Asset Packs.
"""
import argparse, json, os, pathlib
import numpy as np
from PIL import Image

N = 32
PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/block'

# our tile -> (the mod's picture under its block textures, the base pack's picture of the same kind of thing, method)
# methods (tools/port/restyle.py, the art pass the rest of the game's own pictures went through):
#   ramp  - the pack picture's light, shade and shapes in the mod picture's colours, darkest to lightest (stone, sand, dirt...)
#   ore   - the pack's ore: its stone in the mod's stone colours, its spots in the mod's ore colours
#   grass - the pack's grass side: its dirt in the mod's dirt colours, its grass lip in the mod's grass colours
#   shape - the mod picture's own shape (plants, crystals, clouds, fire), doubled, with the pack's grain and outline light
BTA = {
    'brimsand': ('brimsand', 'soul_sand', 'ramp'), 'nether_brimstone_top': ('brimstone/top', 'basalt_top', 'ramp'),
    'nether_brimstone_side': ('brimstone/side', 'basalt_side', 'ramp'), 'brimthaw': ('brimthaw', 'magma', 'ramp'),
    'cobbled_netherrack': ('cobbled_netherrack/normal', 'cobblestone', 'ramp'),
    'crystalline_netherrack': ('cobbled_netherrack/crystalline', 'cobblestone', 'shape'), 'cobbled_gloomstone': ('cobbled_gloomstone', 'cobbled_deepslate', 'ramp'),
    'gloomstone': ('gloomstone', 'deepslate', 'ramp'), 'ember': ('ember', 'magma', 'shape'), 'rubyglass_column': ('rubyglass/column', 'amethyst_block', 'ramp'),
    'rubyglass_node': ('rubyglass/node', 'amethyst_block', 'shape'), 'rubyglass_crystal': ('rubyglass/crystal/0', 'tinted_glass', 'shape'),
    'rubyglass_growth': ('rubyglass/growth/bud', 'amethyst_cluster', 'shape'), 'magmatic_boulder': ('boulder_magmatic', None, 'shape'),
    'sulfuric_boulder': ('boulder_sulfuric', None, 'shape'), 'bone_pile': ('bone_pile', None, 'shape'), 'soul_catcher': ('soul_catcher', None, 'shape'),
    'ash_layer': ('block_ash', 'snow', 'ramp'), 'sulfuric_fire': ('fire_sulfuric', 'fire_0', 'shape'),
    'nether_coal_ore': ('ore/nethercoal/netherrack', 'coal_ore', 'ore'), 'basalt_coal_ore': ('ore/nethercoal/basalt', 'coal_ore', 'ore'),
    'gloom_coal_ore': ('ore/nethercoal/gloomstone', 'coal_ore', 'ore'),
}
AETHER = {
    'holystone': ('natural/holystone', 'stone', 'ramp'), 'mossy_holystone': ('natural/mossy_holystone', 'mossy_cobblestone', 'ramp'),
    'aether_dirt': ('natural/aether_dirt', 'dirt', 'ramp'), 'aether_grass_top': ('natural/aether_grass_block_top', 'grass_block_top', 'ramp'),
    'aether_grass_side': ('natural/aether_grass_block_side', 'grass_block_side', 'grass'), 'quicksoil': ('natural/quicksoil', 'sand', 'ramp'),
    'icestone': ('natural/icestone', 'packed_ice', 'ramp'), 'skyroot_log_side': ('natural/skyroot_log', 'oak_log', 'ramp'),
    'skyroot_log_top': ('natural/skyroot_log_top', 'oak_log_top', 'ramp'), 'golden_oak_log_side': ('natural/golden_oak_log', 'oak_log', 'ramp'),
    'golden_oak_log_top': ('natural/skyroot_log_top', 'oak_log_top', 'ramp'), 'skyroot_leaves': ('natural/skyroot_leaves', 'oak_leaves', 'ramp'),
    'golden_oak_leaves': ('natural/golden_oak_leaves', 'oak_leaves', 'ramp'), 'crystal_leaves': ('natural/crystal_leaves', 'oak_leaves', 'ramp'),
    'crystal_fruit_leaves': ('natural/crystal_fruit_leaves', 'oak_leaves', 'shape'),
    'ambrosium_ore': ('natural/ambrosium_ore', 'coal_ore', 'ore'), 'zanite_ore': ('natural/zanite_ore', 'coal_ore', 'ore'),
    'gravitite_ore': ('natural/gravitite_ore', 'coal_ore', 'ore'), 'cold_aercloud': ('natural/cold_aercloud', 'white_wool', 'ramp'),
    'blue_aercloud': ('natural/blue_aercloud', 'white_wool', 'ramp'), 'golden_aercloud': ('natural/golden_aercloud', 'white_wool', 'ramp'),
    'white_flower': ('natural/white_flower', 'oxeye_daisy', 'shape'), 'purple_flower': ('natural/purple_flower', 'allium', 'shape'),
    'carved_stone': ('dungeon/carved_stone', 'chiseled_stone_bricks', 'ramp'), 'sentry_stone': ('dungeon/sentry_stone', 'chiseled_stone_bricks', 'shape'),
    'angelic_stone': ('dungeon/angelic_stone', 'quartz_bricks', 'ramp'), 'light_angelic_stone': ('dungeon/light_angelic_stone', 'quartz_bricks', 'shape'),
    'hellfire_stone': ('dungeon/hellfire_stone', 'stone_bricks', 'ramp'), 'light_hellfire_stone': ('dungeon/light_hellfire_stone', 'stone_bricks', 'shape'),
    'pillar_side': ('dungeon/pillar_side', 'quartz_pillar', 'ramp'), 'pillar_top': ('dungeon/pillar_top', 'quartz_pillar_top', 'ramp'),
    'pillar_cap_side': ('dungeon/pillar_carved', 'chiseled_quartz_block', 'ramp'),
}

def lum(a): return a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114


def load(path):
    if not path.exists(): return None
    im = Image.open(path).convert('RGBA'); w, h = im.size
    if h > w: im = im.crop((0, 0, w, w))   # an animation strip: its first frame
    return im


import importlib.util as _ilu
_rs = _ilu.spec_from_file_location('restyle', pathlib.Path(__file__).with_name('restyle.py')); RS = _ilu.module_from_spec(_rs); _rs.loader.exec_module(RS)


def arr32(im, smooth=False):
    if im.size[0] != N: im = im.resize((N, N), (Image.BILINEAR if smooth else Image.NEAREST) if im.size[0] < N else Image.BOX)
    return np.asarray(im.convert('RGBA')).astype(np.float32)


def minor_mask(a):
    """the smaller of two colour groups among the opaque pixels (an ore's spots, a grass lip): a two-means split"""
    m = a[..., 3] >= 128
    px = a[m][:, :3]
    if px.shape[0] < 4: return np.zeros(m.shape, bool)
    c0, c1 = px[np.argmin(lum(px[None])[0])], px[np.argmax(lum(px[None])[0])]
    for _ in range(8):
        d0 = ((px - c0) ** 2).sum(1); d1 = ((px - c1) ** 2).sum(1); g = d1 < d0
        if g.all() or (~g).all(): break
        c0, c1 = px[~g].mean(0), px[g].mean(0)
    full = np.zeros(m.shape, bool); full[m] = g
    return full if g.sum() <= (~g).sum() else (m & ~full)


def part(a, mask):
    out = a.copy(); out[..., 3] = np.where(mask, 255, 0); return out


def fit(im, donor, method):
    t = arr32(im)
    if donor is None or method == 'shape':
        up = arr32(im)
        if donor is not None: up = RS.grain(arr32(donor), up)
        return Image.fromarray(np.clip(RS.outline_style(up, (40, 30, 30)) if up[..., 3].min() < 128 else up, 0, 255).astype(np.uint8), 'RGBA')
    d = arr32(donor)
    if method == 'ramp':
        out = RS.ramp_recolour(d, t)
    elif method in ('ore', 'grass'):
        tm, dm = minor_mask(t), minor_mask(d)
        if method == 'grass':   # the lip is the part toward the top, whichever group is smaller
            ys = np.mgrid[0:N, 0:N][0]
            if ys[tm].mean() > ys[~tm & (t[..., 3] >= 128)].mean(): tm = (t[..., 3] >= 128) & ~tm
            if ys[dm].mean() > ys[~dm & (d[..., 3] >= 128)].mean(): dm = (d[..., 3] >= 128) & ~dm
        host = RS.ramp_recolour(part(d, (d[..., 3] >= 128) & ~dm), part(t, (t[..., 3] >= 128) & ~tm))
        spot = RS.ramp_recolour(part(d, dm), part(t, tm))
        out = np.where(dm[..., None], spot, host); out[..., 3] = np.where(d[..., 3] >= 128, 255, 0)
    else:
        out = RS.ramp_recolour(d, t)
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), 'RGBA')


def main():
    here = pathlib.Path(__file__).resolve().parents[2]
    ap = argparse.ArgumentParser()
    ap.add_argument('--bta', default=str(here.parent / 'BTA' / 'jarmods' / 'bta-v8.0.1'))
    ap.add_argument('--aether', default=str(here.parent / 'Aether'))
    ap.add_argument('--data', default=str(here / 'data'))
    home = os.environ.get('XDG_DATA_HOME') or os.path.join(os.path.expanduser('~'), '.local', 'share')
    ap.add_argument('--out', default=os.path.join(home, 'DokuQuest', 'resourcepacks', 'Mod Art (local)'))
    o = ap.parse_args()
    out = pathlib.Path(o.out); blk = out / 'assets' / 'minecraft' / 'textures' / 'block'
    blk.mkdir(parents=True, exist_ok=True)
    (out / 'pack.mcmeta').write_text(json.dumps({'pack': {'pack_format': 12, 'description': 'BTA and Aether pictures from your own copies - this computer only, do not share'}}))
    (out / 'README.txt').write_text('Made by tools/port/local_mod_art.py from your own copies of Better than Adventure and The Aether.\n'
                                    'Their art is not licensed for sharing: keep this pack on this computer.\n')
    data = pathlib.Path(o.data)
    def donor(name):
        if not name: return None
        p = data / PACK / (name + '.png')
        return load(p)
    made = 0
    for table, root in ((BTA, pathlib.Path(o.bta) / 'assets/minecraft/textures/block'), (AETHER, pathlib.Path(o.aether) / 'assets/aether/textures/block')):
        if not root.exists(): print('local_mod_art: not found, skipped:', root); continue
        for tile, (src, dn, how) in table.items():
            im = load(root / (src + '.png'))
            if im is None: print('  missing', root / (src + '.png')); continue
            dimg = donor(dn)
            if dn and dimg is None: print('  no pack picture', dn, 'for', tile)
            fit(im, dimg, how).save(blk / (tile + '.png')); made += 1
    print('local_mod_art: %d pictures in %s' % (made, out))


if __name__ == '__main__':
    main()
