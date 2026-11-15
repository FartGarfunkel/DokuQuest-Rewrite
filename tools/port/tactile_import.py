#!/usr/bin/env python3
"""Tactile Blocks' station models, local only (the mod's models and pictures are not ours to ship):
  data/local_models/tactile/models/<model>.json      its block models (Blockbench boxes, as the game's pack models)
  data/local_models/tactile/textures/<name>.png      the pictures they name ("tactile:block/<name>")
  data/local_models/tactile/stations.json            {block key: {"model": name, "y": [south, east, north, west]}}:
                                                     which of our blocks each one draws, and its turn (degrees, the
                                                     blockstate's) for each way the block faces (our meta & 3 order)
  python3 tools/port/tactile_import.py [path to tactile-blocks1.0.1]
The game draws those blocks from them where it has them (assets/local_models.cpp, render/mesher.cpp); without the
files, everything is as it was. data/local_models is git-ignored."""
import json, pathlib, shutil, sys

SRC = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/matt/Documents/tactile-blocks1.0.1') / 'assets' / 'tactile'
OUT = pathlib.Path(__file__).resolve().parents[2] / 'data' / 'local_models' / 'tactile'

# our block -> (Tactile's blockstate, the model in it)
STATIONS = {
    'smithing_table': ('table_de_forge', 'table_de_forge'),
    'cartography_table': ('cartographe', 'cartographe_block'),
    'brewing_stand': ('alambic', 'alambic'),
}
ORDER = ['south', 'east', 'north', 'west']   # our meta & 3


def main():
    if not SRC.is_dir():
        sys.exit(f'no Tactile Blocks at {SRC}')
    (OUT / 'models').mkdir(parents=True, exist_ok=True)
    (OUT / 'textures').mkdir(parents=True, exist_ok=True)
    manifest = {}
    for block, (state, model) in STATIONS.items():
        bs = json.loads((SRC / 'blockstates' / f'{state}.json').read_text())
        turns = [0, 0, 0, 0]
        for k, v in bs.get('variants', {}).items():
            props = dict(p.split('=') for p in k.split(',') if '=' in p)
            if props.get('facing') in ORDER:
                turns[ORDER.index(props['facing'])] = int(v.get('y', 0))
        m = json.loads((SRC / 'models' / 'block' / f'{model}.json').read_text())
        for ref in m.get('textures', {}).values():
            if isinstance(ref, str) and ref.startswith('tactile:block/'):
                name = ref.split('/')[-1]
                shutil.copyfile(SRC / 'textures' / 'block' / f'{name}.png', OUT / 'textures' / f'{name}.png')
        (OUT / 'models' / f'{model}.json').write_text(json.dumps(m))
        manifest[block] = {'model': model, 'y': turns}
    (OUT / 'stations.json').write_text(json.dumps(manifest, indent=1))
    print(f'{len(manifest)} stations -> {OUT}')


if __name__ == '__main__':
    main()
