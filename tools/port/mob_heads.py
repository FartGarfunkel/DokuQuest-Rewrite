#!/usr/bin/env python3
"""Mob heads (idempotent; data_patches.py runs it):
  python3 tools/port/mob_heads.py <data-dir>
- one block mob_head: an 8x8x8 px box you bump into and can pick, drawn by render/head_layer.cpp from the creature's
  own head (the block entity says whose: {"type": "head", "mob": ..., "rot": 0..15, "item": the head item})
- one item per creature that has a head in Minecraft (ours stand in: ghoul = zombie, fuseling = creeper, zombie pigman
  = piglin): <mob>_head (<mob>_skull for the skeletons), worn in the helmet slot, placed by sim/usables.cpp; its
  picture is the head's own face tile"""
import json, sys, pathlib

MC = {'skeleton', 'wither_skeleton', 'ghoul', 'fuseling', 'zombie_pigman', 'ender_dragon'}   # Minecraft's heads, for now
SKULLS = {'skeleton', 'wither_skeleton'}


def title(k): return ' '.join(w.capitalize() for w in k.split('_'))


def main(d):
    d = pathlib.Path(d)
    bj = json.loads((d / 'blocks.json').read_text())
    sj = json.loads((d / 'shapes.json').read_text())
    models = json.loads((d / 'models.json').read_text())
    mobs = json.loads((d / 'mobs.json').read_text())
    items = json.loads((d / 'items.json').read_text())
    tiles = json.loads((d / 'tiles.json').read_text())['names']
    blocks = bj['blocks']
    if not any(b.get('key') == 'mob_head' for b in blocks):
        bid = len(blocks)
        blocks.append({'id': bid, 'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 1, 'tool': None,
                       'tier': 0, 'drop': 'none', 'light': 0, 'lightAtt': 0, 'sound': 'stone', 'key': 'mob_head', 'name': 'Mob Head',
                       'tex': 'bone_block' if 'bone_block' in tiles else 'stone', 'shape': 'mob_head'})
        t = tiles.get('bone_block', tiles['stone'])
        bj['faces'].append([t] * 6)
        (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    if 'mob_head' not in sj['shapes']:   # collides and picks, draws nothing (the head layer draws the head)
        sj['shapes']['mob_head'] = [{'b': [4, 0, 4, 12, 8, 12], 'side': 'stone', 'only': 0}]
        (d / 'shapes.json').write_text(json.dumps(sj, separators=(',', ':'), ensure_ascii=False))
    for k in [k for k, v in items.items() if isinstance(v, dict) and v.get('mobHead') and v['mobHead'] not in MC]: del items[k]   # an earlier, wider set
    made = 0
    for key, m in mobs.items():
        if key not in MC: continue
        md = models.get(m.get('model', key))
        if not md: continue
        hp = [p for p in md['parts'] if p['name'] == 'head' or p.get('limb') == 'head' or p.get('role') in ('head', 'phead')]
        if not hp: continue
        tx = hp[0].get('tex')
        face = tx.get('front', tx.get('all')) if isinstance(tx, dict) else tx
        if face not in tiles: continue
        ik = key + ('_skull' if key in SKULLS else '_head')
        items[ik] = {'key': ik, 'name': title(key) + (' Skull' if key in SKULLS else ' Head'), 'maxStack': 64, 'icon': face,
                     'mobHead': key, 'armor': {'slot': 0, 'points': 0}, 'desc': 'Place it, or wear it on your head.'}
        made += 1
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    print('mob heads:', made)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
