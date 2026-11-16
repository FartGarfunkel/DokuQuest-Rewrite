#!/usr/bin/env python3
"""Iron's Spells 'n Spellbooks' (Soulrend's copy) look for our magic, local only (the mod's art is not ours to ship):
  data/local_models/tiles/isb/*.png   its particle pictures (each frame of each particle), loaded into the tile atlas
                                      by name "isb_<file>" (assets/tileset.cpp loadLocalTiles), frames in order
  data/local_models/spell_fx.json     {"particles": {kind: [tile names, in order]}, "tiles": [every tile, in order],
                                       "models": {...}}: the spells' projectiles as voxel models (the firebolt's and
                                      fireball's boxes from their renderers, the icicle and magic missile pictures
                                      extruded the way our items are), merged by the registry like the creatures'
  python3 the spell effects import [data-dir]
The game uses them where it has them (render/magic_fx.cpp): each school's sparks in Iron's own particles, the
spells' bolts as Iron's projectiles; without the files, everything is as it was."""
import json, pathlib, shutil, sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import geo_boss_import as G
from PIL import Image

ROOT = pathlib.Path('/home/matt/soulrend_extract/mods/irons_spellbooks-1.20.1-3.15.6/assets/irons_spellbooks')

# the projectiles: a box (its renderer's addBox, the texture's size) or a picture extruded a voxel thick
BOXES = {
    'isb_firebolt': ('textures/entity/fireball/firebolt.png', 20, 10, [(-1, -1, -4, 2, 2, 8, 0, 0)], True),
    'isb_fireball': ('textures/entity/fireball/fireball_core.png', 48, 24, [(-4, -4, -4, 8, 8, 8, 0, 0)], True),
    'isb_magic_missile': ('textures/entity/magic_missile/magic_missile.png', 48, 24, [(-6, -6, -6, 12, 12, 12, 0, 0)], True),
}
SPRITES = {
    'isb_icicle': ('textures/entity/icicle_projectile.png', True),
    'isb_guiding_bolt': ('textures/entity/guiding_bolt/guiding_bolt.png', True),
}


def part_of(vox, name='body'):
    """voxels (model pixels, a glow flag) -> one of our model parts, centred on its pivot"""
    import numpy as np
    if not vox: return None
    ks = np.array(list(vox.keys())); lo = ks.min(0); hi = ks.max(0) + 1
    cen = (lo + hi) / 2.0
    cells = [[int(-(x + 1) - (-hi[0])), int(y - lo[1]), int(z - lo[2]), 1, 1, 1, rgb] + ([1] if lit else []) for (x, y, z), (rgb, lit) in vox.items()]
    n = [int(hi[0] - lo[0]), int(hi[1] - lo[1]), int(hi[2] - lo[2])]
    mn = [(-hi[0] + cen[0]) / 16, (lo[1] - cen[1]) / 16, (lo[2] - cen[2]) / 16]
    return {'name': name, 'pivot': [0, 0, 0], 'min': [round(v, 5) for v in mn],
            'max': [round(mn[0] + n[0] / 16, 5), round(mn[1] + n[1] / 16, 5), round(mn[2] + n[2] / 16, 5)], 'vox': {'n': n, 'c': cells}, 'res': 16}


def main(d='data'):
    d = pathlib.Path(d); od = d / 'local_models'; td = od / 'tiles' / 'isb'
    od.mkdir(parents=True, exist_ok=True); (od / '.gitignore').write_text('*\n!.gitignore\n')
    if td.exists(): shutil.rmtree(td)
    td.mkdir(parents=True)
    tiles, particles = [], {}
    def add_tile(src, name):
        if name in tiles: return name
        shutil.copyfile(src, td / (name[4:] + '.png')); tiles.append(name); return name
    # the particles: each kind's frames (its particles/<kind>.json), its own pictures only
    for pj in sorted((ROOT / 'particles').glob('*.json')):
        frames = []
        for t in json.loads(pj.read_text()).get('textures', []):
            ns, n = t.split(':', 1) if ':' in t else ('minecraft', t)
            if ns != 'irons_spellbooks': continue
            src = ROOT / 'textures/particle' / (n + '.png')
            if src.exists(): frames.append(add_tile(src, 'isb_' + n.replace('/', '_')))
        if frames: particles[pj.stem] = frames
    # a few sheets that are strips of frames in one picture (fiery_smoke.png: 10 frames across)
    models = {}
    for key, (rel, tw, th, boxes, glow) in BOXES.items():
        img = Image.open(ROOT / rel).convert('RGBA')
        cubes = [{'origin': [-(x + w), -(y + h), z], 'size': [w, h, dd], 'uv': [u, v]} for (x, y, z, w, h, dd, u, v) in boxes]
        vox = G.bone_voxels({'cubes': cubes}, img, None, tw, th)
        if glow: vox = {k: (c, True) for k, (c, _) in vox.items()}
        p = part_of(vox)
        if p: models[key] = {'rig': '', 'scale': 0.5 if key == 'isb_magic_missile' else 1, 'parts': [p]}
    for key, (rel, glow) in SPRITES.items():
        f = ROOT / rel
        if not f.exists(): continue
        img = Image.open(f).convert('RGBA'); W, H = img.size
        fh = min(W, H)   # (a strip: its first frame)
        vox = {}
        for y in range(fh):
            for x in range(W if H >= W else fh):
                r, g, b, a = img.getpixel((x, y))
                if a < 100: continue
                vox[(x, fh - 1 - y, 0)] = ((r << 16) | (g << 8) | b, glow)   # (standing up, facing the way it flies after the turn below)
        p = part_of(vox)
        if p: p['rest'] = [-1.5708, 0, 0.7854]; models[key] = {'rig': '', 'scale': 0.6, 'parts': [p]}   # (laid along its flight, the point ahead)
    out = {'tiles': tiles, 'particles': particles, 'models': models}
    (od / 'spell_fx.json').write_text(json.dumps(out, separators=(',', ':')))
    print('irons_fx_import: %d particle kinds, %d frames, %d projectile models' % (len(particles), len(tiles), len(models)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
