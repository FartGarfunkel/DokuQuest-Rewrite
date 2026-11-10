#!/usr/bin/env python3
"""The Wither's voice (Minecraft's has one; ours was silent but for borrowed ghast noises): a rasping, breathy growl
from three throats at once (its three heads, a little apart in pitch and time), a sharp rasp when hurt, a long
falling wail when it dies, the cry when it bursts out of its T (sim/wither.cpp plays it for everyone, wherever they
are), the hiss of a skull leaving a head and the crunch of the blocks it breaks out of. Synth recipes in sfx.json
(vx.wither*, fx.wither_*); mobs.json wither gets voice "wither". Idempotent; data_patches.py runs it.
   python3 tools/port/wither_sounds.py data"""
import json, pathlib, sys


def throat(at, ln, f, v, k=0.8, breath=0.6, rough=0.5, sub=0.3, att=0.04, rel=0.5, am=None, drive=None):
    o = {'f': f, 'v': v, 'k': k, 'breath': breath, 'rough': rough, 'sub': sub, 'att': att, 'rel': rel}
    if am: o['am'] = am
    if drive: o['drive'] = drive
    return ['throat', at, ln, o]


def trio(at, ln, base, shape, v, **kw):
    """Three heads: the same cry from three throats, a little apart in pitch and onset."""
    return [throat(at + i * 0.045, ln - i * 0.04, [[t, round(base * m * r, 2)] for t, m in shape], v, **kw)
            for i, r in enumerate((1.0, 1.27, 0.83))]


def main(d):
    d = pathlib.Path(d)
    sfx = json.loads((d / 'sfx.json').read_text()); R = sfx['recipes']
    groan = [[0, 1.0], [0.3, 1.18], [0.75, 0.95], [1, 0.78]]
    R['vx.wither'] = {'len': 2.1, 'gain': 0.62, 'pv': None, 'takes': [
        trio(0, 1.55, 92, groan, 'axo', breath=0.8, rough=0.6, am=[[6.5, 0.35]]) + [['noise', 0, 1.6, 'bp', 850, 0.9, 0.22, {'att': 0.12}], ['room', 0.35, 1.2]],
        trio(0, 1.7, 82, groan, 'oxa', breath=0.8, rough=0.65, am=[[5.5, 0.4]]) + [['noise', 0, 1.7, 'bp', 700, 0.9, 0.22, {'att': 0.15}], ['room', 0.35, 1.2]],
        trio(0, 1.4, 104, [[0, 1.0], [0.5, 1.1], [1, 0.85]], 'exa', breath=0.85, rough=0.55, am=[[8, 0.3]]) + [['noise', 0, 1.45, 'bp', 1000, 0.9, 0.2, {'att': 0.1}], ['room', 0.35, 1.2]],
    ]}
    R['vx.wither_hurt'] = {'len': 0.8, 'gain': 0.7, 'pv': None, 'takes': [
        trio(0, 0.45, 150, [[0, 1.25], [0.25, 1.0], [1, 0.7]], 'ax', breath=0.6, rough=0.7, att=0.008, rel=0.3, drive=1.6) + [['noise', 0, 0.35, 'hp', 1500, 0.7, 0.25, {'att': 0.003}], ['room', 0.25]],
        trio(0, 0.5, 135, [[0, 1.3], [0.3, 1.0], [1, 0.72]], 'ex', breath=0.6, rough=0.7, att=0.008, rel=0.3, drive=1.6) + [['noise', 0, 0.35, 'hp', 1300, 0.7, 0.25, {'att': 0.003}], ['room', 0.25]],
    ]}
    R['vx.wither_death'] = {'len': 4.2, 'gain': 0.75, 'pv': None, 'takes': [
        trio(0, 3.6, 130, [[0, 1.2], [0.15, 1.35], [0.5, 0.9], [1, 0.35]], 'axou', breath=0.75, rough=0.6, att=0.02, rel=1.2, am=[[5, 0.45]], drive=1.4)
        + [['noise', 0, 3.8, 'lp', 600, 0.7, 0.35, {'brown': True, 'att': 0.05}], ['thump', 0.05, 60, 30, 1.2, 0.5], ['room', 0.55, 1.6]],
    ]}
    R['fx.wither_spawn'] = {'len': 5.0, 'gain': 1.0, 'pv': None, 'takes': [
        [['thump', 0, 70, 26, 1.4, 1.0], ['noise', 0, 2.6, 'lp', 280, 0.7, 0.9, {'brown': True, 'att': 0.004, 'curve': 0.6}],
         ['noise', 0, 0.6, 'lp', 1500, 0.7, 0.5, {'att': 0.002}]]
        + trio(0.08, 3.6, 78, [[0, 0.8], [0.12, 1.25], [0.45, 1.05], [1, 0.55]], 'axon', breath=0.7, rough=0.75, att=0.06, rel=1.4, am=[[4.5, 0.3]], drive=2.0)
        + [['room', 0.6, 1.8]],
    ]}
    R['fx.wither_shoot'] = {'len': 0.7, 'gain': 0.55, 'pv': None, 'takes': [
        [['noise', 0, 0.45, 'bp', 1400, 0.8, 0.6, {'att': 0.004, 'curve': 1.6}], ['thump', 0, 150, 70, 0.18, 0.4],
         throat(0, 0.35, [[0, 260], [1, 170]], 'ax', breath=0.9, rough=0.6, att=0.004, rel=0.2), ['room', 0.25]],
        [['noise', 0, 0.4, 'bp', 1200, 0.8, 0.6, {'att': 0.004, 'curve': 1.6}], ['thump', 0, 140, 65, 0.18, 0.4],
         throat(0, 0.32, [[0, 240], [1, 160]], 'ex', breath=0.9, rough=0.6, att=0.004, rel=0.2), ['room', 0.25]],
    ]}
    R['fx.wither_break'] = {'len': 1.2, 'gain': 0.8, 'pv': None, 'takes': [
        [['noise', 0, 0.9, 'lp', 900, 0.7, 0.8, {'att': 0.002, 'curve': 0.8}], ['thump', 0, 90, 40, 0.5, 0.8],
         ['grains', 0.02, 0.7, 40, 300, 3000, 1.4, 0.35, {'front': 2, 'fade': True, 'dMax': 0.025}], ['room', 0.3]],
    ]}
    (d / 'sfx.json').write_text(json.dumps(sfx, separators=(',', ':'), ensure_ascii=False))
    mobs = json.loads((d / 'mobs.json').read_text())
    if 'wither' in mobs: mobs['wither']['voice'] = 'wither'
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    print('wither sounds: 6 recipes')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
