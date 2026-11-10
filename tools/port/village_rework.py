#!/usr/bin/env python3
"""The working village's data (idempotent; data_patches.py runs it last):
  python3 tools/port/village_rework.py <data-dir>
- the miller (the windmill's job: src/gen/structures_townbld.cpp, src/sim/villager_chores.cpp): who he is and his shop
  (flour from what the stones have ground; wheat bought goes to the mill)
- the town gate's barrier blocks are drawn by the gate itself (src/render/villager_layer.cpp: the portcullis rising, the
  leaves swinging): the block is invisible (it was a cube of door texture, so a shut gate showed twice and popped)"""
import json, sys, pathlib


def main(d):
    d = pathlib.Path(d)
    V = json.loads((d / 'villagers.json').read_text())
    V['jobs']['miller'] = {'title': 'Miller', 'held': 'wheat', 'shirt': '#c8b890', 'apron': '#efe6d0', 'hat': 'cap', 'work': 'hammer'}
    V['shops']['miller'] = {'buy': [['flour', 8, 1, 0]], 'sell': [['wheat', 16, 1, 0]]}
    # the working village's other hands (sim/villager_chores.cpp)
    V['jobs']['lumberjack'] = {'title': 'Lumberjack', 'held': 'iron_axe', 'shirt': '#8a3a2a', 'apron': '#5a4030', 'work': 'hammer'}
    V['jobs']['miner'] = {'title': 'Miner', 'held': 'iron_pickaxe', 'shirt': '#5a5a60', 'apron': '#3a3530', 'hat': 'cap', 'work': 'hammer'}
    V['jobs']['builder'] = {'title': 'Builder', 'held': 'iron_shovel', 'shirt': '#c08030', 'apron': '#6a5030', 'hat': 'cap', 'work': 'hammer'}
    V['jobs']['fletcher']['work'] = 'idle'   # (he hammered with his bow: the swing turned it flat, the string to the ground)
    V['jobs']['rancher'] = {'title': 'Rancher', 'held': 'wheat', 'shirt': '#7a5a3a', 'apron': '#9a8060', 'hat': 'straw', 'work': 'idle'}
    V['jobs']['fisherman'].setdefault('held', 'fishing_rod')
    V['shops']['miner'] = {'buy': [['cobblestone', 16, 1, 0], ['coal', 8, 1, 0], ['iron_ore', 2, 1, 0], ['copper_ore', 4, 1, 1]], 'sell': [['torch', 16, 1, 0], ['bread', 6, 1, 0]]}
    (d / 'villagers.json').write_text(json.dumps(V, separators=(',', ':'), ensure_ascii=False))
    # the mill's parts as items: their own models (they were the flat axle picture). Faces: +x -x +y -y +z -z.
    sh = json.loads((d / 'itemshapes.json').read_text())
    A, E, S = 'axle', 'axle_end', 'sail'
    sh['blockItems']['axle'] = {'k': 'shape', 'boxes': [[0, 6, 6, 16, 10, 10, E, E, A, A, A, A, 0]]}
    sails = [[6, 6, 6.5, 10, 10, 9.5, E, E, E, E, E, E, 0]]
    for b in ([7, 10, 7.5, 9, 16, 8.5], [10, 7, 7.5, 16, 9, 8.5], [7, 0, 7.5, 9, 6, 8.5], [0, 7, 7.5, 6, 9, 8.5]): sails.append(b + [A] * 6 + [0])
    for b in ([9, 10.5, 7.8, 12.5, 15.5, 8.2], [10.5, 3.5, 7.8, 15.5, 7, 8.2], [3.5, 0.5, 7.8, 7, 5.5, 8.2], [0.5, 9, 7.8, 5.5, 12.5, 8.2]): sails.append(b + [S] * 6 + [0])
    sh['blockItems']['windmill'] = {'k': 'shape', 'boxes': sails}
    wheel = [[6, 6, 6, 10, 10, 10, E, E, E, E, E, E, 0]]
    for b in ([7, 10, 7, 9, 14, 9], [10, 7, 7, 14, 9, 9], [7, 2, 7, 9, 6, 9], [2, 7, 7, 6, 9, 9]): wheel.append(b + [A] * 6 + [0])
    for b in ([5, 14, 4, 11, 16, 12], [14, 5, 4, 16, 11, 12], [5, 0, 4, 11, 2, 12], [0, 5, 4, 2, 11, 12]): wheel.append(b + ['planks'] * 6 + [0])
    sh['blockItems']['waterwheel'] = {'k': 'shape', 'boxes': wheel}
    (d / 'itemshapes.json').write_text(json.dumps(sh, separators=(',', ':'), ensure_ascii=False))
    bj = json.loads((d / 'blocks.json').read_text())
    for b in bj['blocks']:
        if b.get('key') == 'gate_barrier': b['render'] = 0; b.pop('selfCull', None)
    # a dirt path half a block high: the jigsaw villages' streets step up their hills on these, half a block at a time
    # (src/gen/structures_jigsaw_village.cpp halfStep), so you and the villagers walk up them instead of jumping
    tj = json.loads((d / 'tiles.json').read_text()); tiles = tj['names']
    at = {b['key']: i for i, b in enumerate(bj['blocks'])}
    tex = {'top': 'dirt_path_top', 'bottom': 'dirt', 'side': 'dirt_path_side'}
    if all(t in tiles for t in tex.values()):
        # and as stairs: a street climbing a block a cell gets one in each rising cell, a staircase you walk up
        sk = 'dirt_path_stairs'
        sb = {'render': 7, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 0.6, 'tool': 'shovel', 'tier': 0, 'drop': sk,
              'light': 0, 'lightAtt': 0, 'sound': 'grass', 'key': sk, 'name': 'Dirt Path Stairs', 'shapeKind': 2, 'family': 'stairs', 'wlog': True, 'tex': tex}
        if sk in at: sb['id'] = at[sk]; bj['blocks'][at[sk]] = sb
        else: sb['id'] = len(bj['blocks']); bj['blocks'].append(sb); bj['faces'].append([0] * 6); at[sk] = sb['id']
        bj['faces'][at[sk]] = [tiles[tex['side']], tiles[tex['side']], tiles[tex['top']], tiles[tex['bottom']], tiles[tex['side']], tiles[tex['side']]]
        nb = {'render': 7, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 0.6, 'tool': 'shovel', 'tier': 0, 'drop': 'dirt_path_slab',
              'light': 0, 'lightAtt': 0, 'sound': 'grass', 'key': 'dirt_path_slab', 'name': 'Dirt Path Slab', 'shapeKind': 1, 'family': 'slab', 'wlog': True, 'tex': tex}
        if 'dirt_path_slab' in at: nb['id'] = at['dirt_path_slab']; bj['blocks'][at['dirt_path_slab']] = nb
        else: nb['id'] = len(bj['blocks']); bj['blocks'].append(nb); bj['faces'].append([0] * 6); at['dirt_path_slab'] = nb['id']
        bj['faces'][at['dirt_path_slab']] = [tiles[tex['side']], tiles[tex['side']], tiles[tex['top']], tiles[tex['bottom']], tiles[tex['side']], tiles[tex['side']]]
        items = json.loads((d / 'items.json').read_text())
        items['dirt_path_slab'] = {'key': 'dirt_path_slab', 'name': 'Dirt Path Slab', 'maxStack': 64, 'icon': 'block:dirt_path_slab', 'block': 'dirt_path_slab'}
        items['dirt_path_stairs'] = {'key': 'dirt_path_stairs', 'name': 'Dirt Path Stairs', 'maxStack': 64, 'icon': 'block:dirt_path_stairs', 'block': 'dirt_path_stairs'}
        (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
        R = json.loads((d / 'recipes.json').read_text())
        if not any(r.get('out') == 'dirt_path_stairs' for r in R['recipes']):
            R['recipes'].append({'out': 'dirt_path_stairs', 'n': 4, 'shape': ['X  ', 'XX ', 'XXX'], 'key': {'X': 'dirt_path'}, 'group': 'shapes', 'h': 3, 'w': 3})
        if not any(r.get('out') == 'dirt_path_slab' for r in R['recipes']):
            R['recipes'].append({'out': 'dirt_path_slab', 'n': 6, 'shape': ['XXX'], 'key': {'X': 'dirt_path'}, 'group': 'shapes', 'h': 1, 'w': 3})
        (d / 'recipes.json').write_text(json.dumps(R, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    print('village rework: miller, gate barrier, dirt path slab and stairs')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
