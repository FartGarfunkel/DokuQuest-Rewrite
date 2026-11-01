#!/usr/bin/env python3
"""Tools of mixed materials (the idea is Cubyz's; our own design and code, nothing taken from it). Idempotent; data_patches.py
runs it after forging.py:
  python3 tools/port/tool_parts.py <data-dir>
- data/tool_materials.json: every material a tool head can be made of: the items that count as it, its name, its colour
  (the head of its own pickaxe's picture, so it matches the pack), the tool set whose stats it brings (<prefix>_pickaxe...),
  and an element (fire for emberglass)
- the mixed tools: mixed_pickaxe, mixed_axe, mixed_shovel, mixed_sword, mixed_hoe. The crafting table makes one when the
  head of a tool's pattern holds more than one material (core/tool_parts.cpp); its stats, name and colours are on the stack."""
import json, sys, pathlib, colorsys
from PIL import Image

TYPES = ['pickaxe', 'axe', 'shovel', 'sword', 'hoe']
# key: (items, name, tool prefix, element)
MATS = {
    'wood': (['#planks'], 'Wooden', 'wood', ''),
    'stone': (['#stone_tool'], 'Stone', 'stone', ''),
    'flint': (['flint'], 'Flint', 'flint', ''),
    'copper': (['copper_ingot'], 'Copper', 'copper', ''),
    'bronze': (['bronze_ingot'], 'Bronze', 'bronze', ''),
    'iron': (['iron_ingot'], 'Iron', 'iron', ''),
    'silver': (['silver_ingot'], 'Silver', 'silver', ''),
    'gold': (['gold_ingot'], 'Golden', 'gold', ''),
    'steel': (['steel_ingot'], 'Steel', 'steel', ''),
    'diamond': (['diamond'], 'Diamond', 'diamond', ''),
    'emerald': (['emerald'], 'Emerald', 'emerald', ''),
    'ruby': (['ruby'], 'Ruby', 'ruby', ''),
    'sapphire': (['sapphire'], 'Sapphire', 'sapphire', ''),
    'mithril': (['mithril_ingot'], 'Mithril', 'mithril', ''),
    'adamantite': (['adamantite_ingot'], 'Adamantite', 'adamantite', ''),
    'cinderite': (['cinderite_shard'], 'Cinderite', 'cinderite', 'fire'),
    'obsidian': (['obsidian'], 'Obsidian', 'obsidian', ''),
    'emberglass': (['emberglass_cluster'], 'Emberglass', 'emberglass', 'fire'),
    'leather': (['leather'], 'Leather', 'leather', ''),
}
FALLBACK = {'leather': (160, 101, 64), 'wood': (162, 130, 78), 'stone': (128, 128, 128), 'flint': (78, 78, 88), 'copper': (214, 124, 82), 'bronze': (196, 140, 70),
            'iron': (214, 214, 218), 'silver': (228, 234, 244), 'gold': (250, 210, 70), 'steel': (150, 160, 178), 'diamond': (96, 232, 222),
            'emerald': (60, 200, 110), 'ruby': (222, 40, 64), 'sapphire': (52, 92, 222), 'mithril': (168, 222, 244), 'adamantite': (180, 60, 120),
            'cinderite': (230, 96, 40), 'obsidian': (52, 36, 80), 'emberglass': (240, 96, 40)}
ITEM_PACK = 'resourcepacks/DokuQuest Base/assets/minecraft/textures/item'


def head_colour(d, tiles, files, prefix):
    """the mean colour of the pickaxe picture's head (the top rows, leaving out the stick's browns)"""
    pic = None
    for name in (prefix + '_pickaxe', prefix + 'en_pickaxe'):
        p = d / ITEM_PACK / (name + '.png')
        if p.exists(): pic = Image.open(p).convert('RGBA'); break
        if name in tiles: pic = Image.open(d / files[tiles[name]]).convert('RGBA'); break
    if pic is None: return None
    pic = pic.crop((0, 0, pic.width, pic.width)).resize((16, 16), Image.NEAREST)
    px = pic.load(); acc = [0, 0, 0]; n = 0
    for y in range(0, 7):
        for x in range(16):
            r, g, b, a = px[x, y]
            if a < 128: continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            if v < 0.18: continue                            # the dark outline
            if 0.04 < h < 0.13 and 0.3 < s < 0.75 and v < 0.6: continue   # the stick
            acc[0] += r; acc[1] += g; acc[2] += b; n += 1
    if n < 4: return None
    return tuple(int(c / n) for c in acc)


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    tiles, files = tj['names'], tj['files']
    out = {}
    for k, (its, name, prefix, el) in MATS.items():
        if not any((prefix + '_' + t) in items for t in TYPES + ['helmet']): continue
        col = head_colour(d, tiles, files, prefix) or FALLBACK[k]
        out[k] = {'items': its, 'name': name, 'prefix': prefix, 'rgb': list(col), 'element': el}
    (d / 'tool_materials.json').write_text(json.dumps({'_comment': 'tools/port/tool_parts.py: materials a mixed tool head can be made of', 'materials': out}, indent=1))
    for t in TYPES:
        k = 'mixed_' + t
        base = items.get('iron_' + t, {})
        it = {'key': k, 'name': t.capitalize(), 'maxStack': 1, 'icon': base.get('icon', 'iron_' + t), 'mixedTool': t,
              'tool': dict(base.get('tool', {'type': t, 'tier': 3, 'speed': 6, 'damage': 4, 'durability': 251})), 'hideCreative': True,
              'desc': 'Made of more than one material.'}
        items[k] = it
    for piece, slot in (('helmet', 0), ('chestplate', 1), ('leggings', 2), ('boots', 3)):   # mixed armour (core/tool_parts.h)
        base = items.get('iron_' + piece, {})
        items['mixed_' + piece] = {'key': 'mixed_' + piece, 'name': piece.capitalize(), 'maxStack': 1, 'icon': base.get('icon', 'iron_' + piece),
                                   'mixedArmor': piece, 'armor': dict(base.get('armor', {'slot': slot, 'points': 2, 'durability': 165})),
                                   'hideCreative': True, 'desc': 'Made of more than one material.'}
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    # the Tool Forge is where a gem is set in a tool now (a two-slot work station, sim/stations.cpp "forge")
    bj = json.loads((d / 'blocks.json').read_text())
    for b in bj['blocks']:
        if b['key'] == 'tool_forge': b['station'] = 'forge'
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    if 'tool_forge' in items: items['tool_forge']['desc'] = 'Set a gem in a tool: a diamond, an emerald, a ruby, a sapphire, amethyst, quartz or a crystal.'
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    print('tool parts:', len(out), 'materials')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
