#!/usr/bin/env python3
"""The swing's whoosh (Soulrend's Epic Fight Nightfall: whoosh_light_*, whoosh_heavy_*), local only: each recording analysed
into our synthesis profiles (tools/port/sound_analyze.py: nothing of the recordings kept) -> data/local_models/
swing_sounds.json + .bin (git-ignored; audio/sfxlib.cpp loads every data/local_models/*_sounds.json). Its events:
  fx.swing_light   a sword's, a dagger's, a light blade's cut through the air
  fx.swing_heavy   an axe's, a hammer's, a club's, a great blade's
  python3 tools/port/swing_sounds.py [data-dir]
"""
import json, pathlib, sys, zlib

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import sound_analyze as SA

ROOT = pathlib.Path('/home/matt/soulrend_extract/mods/EpicFight Nightfall-3.4.2-all/assets/efn/sounds')
SETS = {'fx.swing_light': ['whoosh_light_%d' % i for i in range(1, 5)], 'fx.swing_heavy': ['whoosh_heavy_%d' % i for i in range(1, 5)]}


def main(d='data'):
    d = pathlib.Path(d); od = d / 'local_models'; od.mkdir(parents=True, exist_ok=True)
    nb = bytearray(); index = {}; events = {}
    for ev, names in SETS.items():
        got = []
        for n in names:
            f = ROOT / (n + '.ogg')
            if not f.exists(): continue
            x = SA.decode(f)
            if len(x) < 64: continue
            b = SA.encode(SA.analyze(x))
            key = 'swing/' + n
            index[key] = [len(nb), len(b)]; nb += b
            got.append([key, 1.0, 0.8])
        if got: events[ev] = got
    main_meta = json.loads((d / 'sounds.json').read_text())
    meta = {k: main_meta[k] for k in ('rate', 'bands', 'nfft', 'fine', 'db0', 'edges') if k in main_meta}
    meta.update({'about': "the swing's whoosh as synthesis profiles (local: tools/port/swing_sounds.py)", 'raw': len(nb), 'sounds': index, 'events': events, 'voices': {}})
    (od / 'swing_sounds.bin').write_bytes(zlib.compress(bytes(nb), 9))
    (od / 'swing_sounds.json').write_text(json.dumps(meta, separators=(',', ':')))
    print('swing_sounds: %d profiles, %d events' % (len(index), len(events)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
