#!/usr/bin/env python3
"""tools/port/dynamic_trees.py — the data for growing trees (MIT-licensed
source, docs/THIRD_PARTY_NOTICES.md; sim/dyntree.cpp grows them). Idempotent; data_patches.py runs it:
  python3 tools/port/dynamic_trees.py <data-dir>

- every branch block (oak_branch ...) learns its log and its leaves ('log', 'leaves'): a felled tree gives logs by the wood
  in it, and a growing tree leafs out in its own leaves;
- rooty_dirt: the soil under a growing tree (rooty soil). Its meta is the soil's life (0..15); it ticks and
  sends the tree its growth; it drops dirt. A sapling planted in a terrain-13 world takes root in it.
"""
import json, pathlib


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); items = json.loads((d / 'items.json').read_text())
    tj = json.loads((d / 'tiles.json').read_text())
    blocks = bj['blocks']; by = {b['key']: b for b in blocks}
    logs = {}
    for b in blocks:
        if b.get('isLog') and not b.get('branch') and isinstance(b.get('tex'), dict): logs.setdefault(b['tex'].get('side'), b['key'])
    n = 0
    for b in blocks:
        if not b.get('branch'): continue
        wood = b['key'][:-len('_branch')]
        lg = logs.get(b['tex'] if isinstance(b['tex'], str) else None)
        lv = wood + '_leaves' if wood + '_leaves' in by else 'leaves'
        if lg: b['log'] = lg
        b['leaves'] = lv; b['dynTree'] = True
        n += 1
    # saplings: the branch they grow into (sapling 'tree' oak -> oak_branch)
    for b in blocks:
        t = b.get('tree')
        if isinstance(t, str) and t + '_branch' in by and b.get('render') == 3: b['branchBlock'] = t + '_branch'
    # rooty soil
    tile = 'rooted_dirt' if 'rooted_dirt' in tj['names'] else 'dirt'
    rd = {'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 0.8, 'tool': 'shovel', 'tier': 0, 'drop': 'dirt',
          'light': 0, 'lightAtt': 0, 'sound': 'dirt', 'key': 'rooty_dirt', 'name': 'Rooty Dirt', 'tex': tile, 'randomTick': True, 'rootySoil': True}
    tid = tj['names'][tile]
    if 'rooty_dirt' in by:
        i = by['rooty_dirt']['id']; rd['id'] = i; blocks[i] = rd; bj['faces'][i] = [tid] * 6
    else:
        rd['id'] = len(blocks); blocks.append(rd); bj['faces'].append([tid] * 6)
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    print('dynamic_trees: %d branch woods, rooty soil' % n)


if __name__ == '__main__':
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
