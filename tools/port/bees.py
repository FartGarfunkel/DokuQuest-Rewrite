#!/usr/bin/env python3
# tools/port/bees.py — bees the original's size (game/husbandry.js: MOB_TYPES.bee w 0.12, h 0.14, the rig at 0.32): small,
# several to a nest or hive (sim/creature_spawn.cpp colonies), wearing the pack's bee skins (plain, angry, nectar) when
# a pack has them (render/creature_models.cpp). Hooked into data_patches.py.
import json, pathlib, sys


def main(d):
    d = pathlib.Path(d)
    mobs = json.loads((d / 'mobs.json').read_text()); models = json.loads((d / 'models.json').read_text())
    M = models.get('models', models)
    if 'bee' in mobs: mobs['bee'].update({'w': 0.12, 'h': 0.14})
    if 'bee' in M: M['bee']['scale'] = 0.32
    (d / 'mobs.json').write_text(json.dumps(mobs, separators=(',', ':'), ensure_ascii=False))
    (d / 'models.json').write_text(json.dumps(models, separators=(',', ':'), ensure_ascii=False))
    print('bees ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
