#!/usr/bin/env python3
"""tools/port/world_life.py — the living world (sim/world_life.cpp):
- "carriable": the natural blocks the tall stare-creature (enderman) may lift and carry about: soil, sand, gravel, clay,
  single flowers, small mushrooms, pumpkins, melons, cactus and TNT (never what a player put down: sim/skills.h).
- "randomTick" on the small mushrooms (they spread in the dark and wither in bright light) and the Overworld's vine
  (it creeps along walls and hangs further down).
  python3 tools/port/world_life.py [data-dir]"""
import json, pathlib, sys

CARRIABLE = {'grass', 'dirt', 'coarse_dirt', 'podzol', 'mycelium', 'rooted_dirt', 'sand', 'red_sand', 'gravel', 'clay',
             'brown_mushroom', 'red_mushroom', 'pumpkin', 'melon', 'cactus', 'tnt', 'flower_red', 'flower_yellow'}
NOT_CARRIABLE = {'spore_blossom'}   # (it hangs from a ceiling)
TICKS = {'brown_mushroom', 'red_mushroom', 'vine'}

def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text())
    for b in bj['blocks']:
        k = b.get('key')
        if k not in NOT_CARRIABLE and (k in CARRIABLE or (b.get('flower') is True and not b.get('double') and b.get('render') == 3)):
            b['carriable'] = True
        else: b.pop('carriable', None)
        if k in TICKS: b['randomTick'] = True
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[2] / 'data')
