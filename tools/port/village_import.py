#!/usr/bin/env python3
"""The village buildings (idempotent; data_patches.py runs it):
  python3 tools/port/village_import.py <data-dir> [<Better Village folder>]

Our villages are built from Better Village's buildings (jtorleon studios, the Forge/Fabric mod): its houses, streets,
town centres and street ends for plains, savanna, snowy, desert and taiga, put together Minecraft's jigsaw way by
src/gen/structures_jigsaw_village.cpp. This copies its structure pieces
(data/minecraft/structures/village/<style>/<houses|streets|town_centers|terminators>/*.nbt) into data/villages/ and
writes data/villages/index.json listing them; src/gen/village_pieces.cpp reads them and turns each Minecraft block
state into ours. The mod's Java code only moves villages further apart (its spacing/separation config): ours are one
per 352-block region already.

Better Village is "All rights reserved": data/villages/ is local game data made from the player's own copy. Don't
commit or release it without the author's leave."""
import json, pathlib, shutil, sys

KINDS = ('houses', 'streets', 'town_centers', 'terminators')


def main(d, src=None):
    d = pathlib.Path(d)
    src = pathlib.Path(src) if src else pathlib.Path(__file__).resolve().parents[3] / 'bettervillage'
    root = src / 'data/minecraft/structures/village'
    if not root.is_dir():
        print('village_import: no Better Village at', src, '- the classic villages stay'); return
    out = d / 'villages'
    files = []
    for p in sorted(root.rglob('*.nbt')):
        rel = p.relative_to(root)
        if len(rel.parts) != 3 or rel.parts[1] not in KINDS or 'zombie' in rel.parts[1]: continue
        to = out / rel
        to.parent.mkdir(parents=True, exist_ok=True)
        if not to.exists() or to.stat().st_size != p.stat().st_size: shutil.copyfile(p, to)
        files.append(rel.as_posix())
    (out / 'index.json').write_text(json.dumps({'source': 'Better Village 3.3.1 (jtorleon studios)', 'pieces': files}, indent=0))
    print('village_import: %d pieces into %s' % (len(files), out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data', sys.argv[2] if len(sys.argv) > 2 else None)
