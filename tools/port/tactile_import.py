#!/usr/bin/env python3
"""Tactile Blocks, local only (the mod's models and pictures are not ours to ship; docs/TACTILE_PORT.md):
  data/local_models/tactile/models/<model>.json      every block model of the mod, its parts too (doors, lids, the wheel)
  data/local_models/tactile/textures/<name>.png      every picture they name ("tactile:block/<name>")
  data/local_models/tactile/stations.json            {our block key: {"state": blockstate, "model": [4], "y": [4]}}:
                                                     the model and its turn (degrees, clockwise from above) for each way
                                                     our block faces (meta & 3: south, east, north, west), lit or not
  python3 tools/port/tactile_import.py [path to tactile-blocks1.0.1]
The game draws those blocks from them where it has them (assets/local_models.cpp, render/mesher.cpp, render/tactile_*);
without the files, everything is as it was. data/local_models is git-ignored."""
import json, pathlib, shutil, sys

SRC = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/matt/Documents/tactile-blocks1.0.1') / 'assets' / 'tactile'
OUT = pathlib.Path(__file__).resolve().parents[2] / 'data' / 'local_models' / 'tactile'

# our block -> (Tactile's blockstate, its other properties for our block: lit, ...)
STATIONS = {
    'smithing_table': ('table_de_forge', {}),
    'cartography_table': ('cartographe', {}),
    'brewing_stand': ('alambic', {}),
    'furnace': ('four', {'lit': 'false'}), 'lit_furnace': ('four', {'lit': 'true'}),
    'smoker': ('fumoir', {'lit': 'false'}), 'lit_smoker': ('fumoir', {'lit': 'true'}),
    'blast_furnace': ('haut_fourneau', {'lit': 'false'}), 'lit_blast_furnace': ('haut_fourneau', {'lit': 'true'}),
    'grindstone': ('meule', {}),
    'chest': ('coffre', {}),
    **{f'{c}_painted_chest': ('coffre', {}) for c in ['white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black']},
    'party_chest': ('coffre', {}),
    'ender_chest': ('coffre', {}),   # (every chest the same model, in its own picture's colours)
    'anvil': ('enclume', {}), 'chipped_anvil': ('enclume', {}), 'damaged_anvil': ('enclume', {}),
    'enchanting_table': ('table_enchantement', {}),
    'jukebox': ('jukebox', {}),
    'bookshelf': ('bibliotheque', {}),
}
ORDER = ['south', 'east', 'north', 'west']   # our meta & 3


def pick(variants, facing, want):
    best = None
    for k, v in variants.items():
        props = dict(p.split('=') for p in k.split(',') if '=' in p)
        if props.get('facing', facing) != facing: continue
        score = sum(1 for a, b in want.items() if props.get(a) == b) - sum(1 for a, b in want.items() if a in props and props[a] != b) * 10
        if best is None or score > best[0]:
            best = (score, v[0] if isinstance(v, list) else v)
    return best[1] if best else None


def main():
    if not SRC.is_dir():
        sys.exit(f'no Tactile Blocks at {SRC}')
    for sub in ('models', 'textures'):
        (OUT / sub).mkdir(parents=True, exist_ok=True)
    for f in (SRC / 'models' / 'block').glob('*.json'):
        shutil.copyfile(f, OUT / 'models' / f.name)
    for f in (SRC / 'textures' / 'block').glob('*.png'):
        shutil.copyfile(f, OUT / 'textures' / f.name)
    manifest = {}
    for block, (state, want) in STATIONS.items():
        p = SRC / 'blockstates' / f'{state}.json'
        if not p.exists():
            continue
        variants = json.loads(p.read_text()).get('variants', {})
        models, turns = [], []
        for facing in ORDER:
            v = pick(variants, facing, want) or {}
            models.append(v.get('model', '').split('/')[-1])
            turns.append(int(v.get('y', 0)))
        if not models[0]:
            continue
        manifest[block] = {'state': state, 'model': models, 'y': turns}
    (OUT / 'stations.json').write_text(json.dumps(manifest, indent=1))
    print(f'{len(manifest)} blocks -> {OUT}')


if __name__ == '__main__':
    main()
