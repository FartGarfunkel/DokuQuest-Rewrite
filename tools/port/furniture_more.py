#!/usr/bin/env python3
"""Dungeon furniture, our own Doku designs (idempotent; data_patches.py runs it):
  python3 tools/port/furniture_more.py <data-dir>
Pictures are existing pack tiles (planks, stripped oak, stone bricks, red wool, candle, iron, torch, fire).
- oak_desk: an oak table with a backboard and two drawer fronts
- oak_coffin: a closed wooden box, one cell, not a chest
- altar: a stone-brick dais, a cloth runner, two candles
- sconce: an iron wall bracket holding a torch"""
import json, sys, pathlib


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text()); sj = json.loads((d / 'shapes.json').read_text())
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    rj = json.loads((d / 'recipes.json').read_text())
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    blocks = bj['blocks']; have = {b['key'] for b in blocks}

    def tex(*names):
        for n in names:
            if n in tiles: return n
        return names[0]

    def block(key, name, props, faces, item=True):
        if key in have: return
        b = {'id': len(blocks), 'render': 1, 'solid': True, 'opaque': True, 'replaceable': False, 'hardness': 1.5, 'tool': 'pickaxe',
             'tier': 0, 'drop': key, 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': key, 'name': name}
        b.update(props); blocks.append(b); bj['faces'].append(faces); have.add(key)
        if item: items[key] = {'key': key, 'name': name, 'maxStack': 64, 'icon': 'block:' + key, 'block': key}

    def recipe(out, n, shape=None, key=None, items_=None, group=None):
        if any(r.get('out') == out for r in rj['recipes']): return
        r = {'out': out, 'n': n}
        if shape: r.update({'shape': shape, 'key': key, 'h': len(shape), 'w': len(shape[0])})
        else: r['items'] = items_
        if group: r['group'] = group
        rj['recipes'].append(r)

    P, L = tex('planks'), tex('stripped_oak_log_side', 'log_side')
    SB, WOOL = tex('stone_bricks'), tex('red_wool', 'wool')
    CAND, FLAME = tex('candle'), tex('candle_flame', 'fire')
    IRON, TORCH, FIRE = tex('iron_block'), tex('torch'), tex('fire', 'torch_flame')
    SLAB = 'oak_slab' if 'oak_slab' in items else 'planks'

    # oak_desk: the oak table, plus a backboard on the north and two drawer fronts on the south
    if 'oak_desk' not in sj['shapes']:
        desk = list(sj['shapes']['oak_table']) if 'oak_table' in sj['shapes'] else [
            {'b': [0, 13, 0, 16, 16, 16], 'side': P, 'end': P},
            {'b': [1, 0, 1, 3.5, 13, 3.5], 'side': L, 'end': L},
            {'b': [12.5, 0, 1, 15, 13, 3.5], 'side': L, 'end': L},
            {'b': [1, 0, 12.5, 3.5, 13, 15], 'side': L, 'end': L},
            {'b': [12.5, 0, 12.5, 15, 13, 15], 'side': L, 'end': L},
            {'b': [3, 4, 3, 13, 5, 13], 'side': L, 'end': L}]
        desk += [{'b': [0, 8, 0, 16, 16, 1.5], 'side': P, 'end': P},
                 {'b': [1.5, 8, 14.5, 7.5, 12.5, 16], 'side': P, 'end': P},
                 {'b': [8.5, 8, 14.5, 14.5, 12.5, 16], 'side': P, 'end': P},
                 {'b': [4, 9.8, 15.5, 5.2, 10.8, 16], 'side': L, 'end': L},
                 {'b': [11, 9.8, 15.5, 12.2, 10.8, 16], 'side': L, 'end': L}]
        sj['shapes']['oak_desk'] = desk
    block('oak_desk', 'Oak Desk', {'render': 6, 'opaque': False, 'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'fuel': 1.5,
                                  'shape': 'oak_desk', 'tex': P, 'facingModel': True, 'furniture': 'desk', 'wood': 'oak'}, [tiles[P]] * 6)
    recipe('oak_desk', 1, ['PPP', 'PSP', 'S S'], {'P': 'planks', 'S': 'stick'}, group='furniture')

    # oak_coffin: one cell, a closed box with a lid seam; not a container
    if 'oak_coffin' not in sj['shapes']:
        sj['shapes']['oak_coffin'] = [
            {'b': [2, 0, 1.5, 14, 7.5, 14.5], 'side': P, 'end': P},
            {'b': [1.5, 7.5, 1, 14.5, 10, 15], 'side': P, 'end': P},
            {'b': [1.5, 7.3, 1, 14.5, 8.2, 15], 'side': L, 'end': L},
            {'b': [3, 10, 1, 13, 11.2, 4], 'side': P, 'end': P}]
    block('oak_coffin', 'Oak Coffin', {'render': 6, 'opaque': False, 'hardness': 2, 'tool': 'axe', 'sound': 'wood', 'fuel': 1.5,
                                      'shape': 'oak_coffin', 'tex': P, 'facingModel': True, 'furniture': 'coffin', 'wood': 'oak'}, [tiles[P]] * 6)
    recipe('oak_coffin', 1, ['PPP', 'PSP'], {'P': 'planks', 'S': SLAB}, group='furniture')

    # altar: dais, cloth, two bone candlesticks with fire (candle sprites are hollow on a model box)
    WAX, FIRET = tex('honeycomb_block', 'yellow_terracotta', 'bone_block_side', 'smooth_stone'), tex('fire')
    GOLD = tex('gold_block', 'raw_gold_block', SB)
    sj['shapes']['altar'] = [
        {'b': [0, 0, 0, 16, 4, 16], 'side': SB, 'end': SB},
        {'b': [4, 4, 2, 12, 5, 14], 'side': WOOL, 'end': WOOL},
        {'b': [6.5, 5, 6.5, 9.5, 6.2, 9.5], 'side': GOLD, 'end': GOLD},
        {'b': [3.5, 5, 3, 5.5, 11, 5], 'side': WAX, 'end': WAX},
        {'b': [10.5, 5, 3, 12.5, 11, 5], 'side': WAX, 'end': WAX},
        {'b': [4.2, 10.5, 2.2, 4.8, 16, 5.8], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8},
        {'b': [3.2, 10.5, 3.7, 5.8, 16, 4.3], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8},
        {'b': [11.2, 10.5, 2.2, 11.8, 16, 5.8], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8},
        {'b': [10.2, 10.5, 3.7, 12.8, 16, 4.3], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8}]
    block('altar', 'Altar', {'render': 6, 'opaque': False, 'hardness': 2, 'tool': 'pickaxe', 'sound': 'stone', 'light': 8,
                             'shape': 'altar', 'tex': SB, 'facingModel': True}, [tiles[SB]] * 6)
    recipe('altar', 1, ['C C', 'SSS'], {'C': 'candle', 'S': 'stone_bricks'})

    # sconce: iron plate on the north wall, an arm, a cup, a torch flame (nocol). facingModel meta 0-3.
    if 'sconce' not in sj['shapes']:
        sj['shapes']['sconce'] = [
            {'b': [5.5, 4, 0, 10.5, 12, 1.2], 'side': IRON, 'end': IRON},
            {'b': [7, 7, 1.2, 9, 9, 6.5], 'side': IRON, 'end': IRON},
            {'b': [6, 6, 5.5, 10, 8, 9.5], 'side': IRON, 'end': IRON},
            {'b': [7.2, 7.5, 6.5, 8.8, 13, 8.1], 'side': TORCH, 'end': TORCH, 'nocol': True},
            {'b': [6.5, 12, 5.8, 9.5, 15.5, 8.8], 'side': FIRE, 'end': FIRE, 'nocol': True}]
    block('sconce', 'Sconce', {'render': 6, 'solid': False, 'opaque': False, 'hardness': 1, 'sound': 'metal', 'light': 12, 'hot': True,
                              'shape': 'sconce', 'tex': IRON, 'facingModel': True}, [tiles[IRON]] * 6)
    recipe('sconce', 1, ['N', 'T'], {'N': 'iron_nugget', 'T': 'torch'})

    # brazier: a cross of fire in the bowl, not a solid fire cube
    if 'brazier' in sj['shapes']:
        boxes = [b for b in sj['shapes']['brazier'] if b.get('side') != 'fire' and b.get('end') != 'fire']
        boxes += [
            {'b': [7.2, 9, 3, 8.8, 16, 13], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8},
            {'b': [3, 9, 7.2, 13, 16, 8.8], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8},
            {'b': [6, 9, 6, 10, 16, 10], 'side': FIRET, 'end': FIRET, 'nocol': True, 'tv': 8}]
        sj['shapes']['brazier'] = boxes

    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'shapes.json').write_text(json.dumps(sj, indent=1, ensure_ascii=False) + '\n')
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'recipes.json').write_text(json.dumps(rj, separators=(',', ':'), ensure_ascii=False))
    print('furniture more ok')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
