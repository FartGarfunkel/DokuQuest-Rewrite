#!/usr/bin/env python3
"""The Wither Storm as Minecraft: Story Mode has it: no longer a Wither drawn five times over, but the behemoth it grows
into: a black obsidian mass with a nether-brick maw, three heads with glowing violet eyes and mandibles (the middle one
huge, quartz-white teeth), the command block glowing in its chest, and long tentacles hanging under it that sway as it
drifts (rig "wither_storm": sim/wither.cpp animates the tentacles, the jaws and the heads; it grows with what it eats).
Idempotent; data_patches.py runs it after the Wither."""
import json, math, pathlib

OBS = {'all': 'obsidian', 'top': 'crying_obsidian'}
DARK = 'blackstone'
MAW = 'nether_bricks'
EYE = 'purple_concrete'
TOOTH = 'bone_block_side'
HEART = {'all': 'command_block_side', 'front': 'command_block_front', 'back': 'command_block_front'}


def P(name, parent, at, box, tex, glow=False):
    p = {'name': name, 'parent': parent, 'at': at, 'box': box, 'tex': tex,
         'pivot': [at[0] / 16, at[1] / 16, at[2] / 16], 'min': [b / 16 for b in box[:3]], 'max': [b / 16 for b in box[3:]]}
    if glow: p['glow'] = 1
    return p


def head(name, parent, at, s, out):
    """A head of the Storm: a block of obsidian, two violet eyes, mandibles either side of a nether-brick maw with teeth."""
    w, h, d = 6 * s, 5 * s, 7 * s
    out.append(P(name, parent, at, [-w, -h * 0.4, -d * 2, w, h, 0], OBS))
    out.append(P(name + 'Maw', name, [0, -h * 0.4, -d * 1.2], [-w * 0.7, -h * 0.5, -d * 0.8, w * 0.7, 0.01, d * 0.6], MAW))
    out.append(P(name + 'Teeth', name, [0, -h * 0.4, -d * 1.9], [-w * 0.65, -h * 0.25, -0.6 * s, w * 0.65, 0, 0], TOOTH))
    for side, sx in (('L', -1), ('R', 1)):
        out.append(P(name + 'Eye' + side, name, [sx * w * 0.45, h * 0.35, -d * 2 - 0.05], [-1.3 * s, -0.8 * s, -0.5, 1.3 * s, 0.8 * s, 0], EYE, glow=True))
        out.append(P(name + 'Jaw' + side, name, [sx * w * 0.95, -h * 0.35, -d * 1.4], [-1.3 * s, -h * 0.9, -d * 0.8, 1.3 * s, 0, d * 0.6], OBS))
        out.append(P(name + 'Fang' + side, name + 'Jaw' + side, [0, -h * 0.9, -d * 0.8], [-0.7 * s, -1.6 * s, -0.6 * s, 0.7 * s, 0, 0.6 * s], TOOTH))


def model():
    parts = []
    # the mass: a hulk of obsidian, lumps grown onto it, its maw underneath, spines along the back
    parts.append(P('core', None, [0, 36, 0], [-13, -10, -9, 13, 12, 9], OBS))
    parts.append(P('massL', 'core', [-12, -2, 2], [-6, -7, -7, 4, 8, 7], DARK))
    parts.append(P('massR', 'core', [12, -2, 2], [-4, -7, -7, 6, 8, 7], DARK))
    parts.append(P('hump', 'core', [0, 12, 3], [-9, 0, -6, 9, 6, 6], OBS))
    parts.append(P('belly', 'core', [0, -10, 0], [-10, -5, -7, 10, 0.01, 7], MAW))
    for i, (x, z) in enumerate([(-7, 6), (0, 8), (7, 6), (-4, 1), (4, 1)]):
        parts.append(P('spine%d' % i, 'hump', [x, 6, z - 3], [-1.2, 0, -1.2, 1.2, 4 + (i % 3) * 2, 1.2], DARK))
    # the command block in its chest, glowing
    parts.append(P('heart', 'core', [0, 0, -9.05], [-3.5, -3.5, -1.6, 3.5, 3.5, 0.4], HEART, glow=True))
    # three heads: the middle one huge
    head('head', 'core', [0, 11, -7], 1.6, parts)   # the middle one is the big one
    head('headL', 'core', [-15, 6, -4], 0.8, parts)
    head('headR', 'core', [15, 6, -4], 0.8, parts)
    # tentacles under it: six, four segments each, thinning
    spots = [(-9, -5), (0, -6), (9, -5), (-8, 5), (0, 6), (8, 5)]
    for i, (x, z) in enumerate(spots):
        parent = 'core'
        at = [x, -12, z]
        for j in range(4):
            r = 2.4 - j * 0.45
            L = 11 - j
            name = 'tent%d_%d' % (i, j)
            parts.append(P(name, parent, at, [-r, -L, -r, r, 0, r], OBS if j % 2 == 0 else DARK))
            parent, at = name, [0, -L, 0]
        parts.append(P('tent%d_tip' % i, parent, at, [-0.8, -3, -0.8, 0.8, 0, 0.8], 'crying_obsidian'))
    return {'rig': 'wither_storm', 'scale': 2.4, 'parts': parts}


def main(d):
    d = pathlib.Path(d)
    models = json.loads((d / 'models.json').read_text())
    mobs = json.loads((d / 'mobs.json').read_text())
    models['wither_storm'] = model()
    if 'wither_storm' in mobs:
        m = mobs['wither_storm']
        m.pop('tint', None)               # its own colours now
        m['w'], m['h'] = 5.5, 11.0        # (grows: sim/wither.cpp scales it with what it has eaten)
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    print('wither storm: %d parts' % len(models['wither_storm']['parts']))


if __name__ == '__main__':
    main(pathlib.Path(__file__).resolve().parents[2] / 'data')
