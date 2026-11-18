#!/usr/bin/env python3
"""tools/port/cave_drips.py — drips off a stalactite (render/env_layers.cpp, sim/worldsys.cpp cave drips): a hanging
pointed dripstone ticks now and then (it fills a cauldron under it with what lies over the stone it hangs from: water,
or lava), and a cauldron of lava glows (core/registry.cpp metaLight "cauldron": meta & 4 holds lava).
  python3 tools/port/cave_drips.py [data-dir]"""
import json, pathlib, sys

def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text())
    for b in bj['blocks']:
        if b.get('key') == 'pointed_dripstone': b['randomTick'] = True
        if b.get('key') == 'cauldron': b['lightMeta'] = 'cauldron'
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'data')
