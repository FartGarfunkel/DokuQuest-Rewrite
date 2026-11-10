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
    'zanite': (['zanite_gemstone'], 'Zanite', 'zanite', ''),
    'gravitite': (['gravitite_plate'], 'Gravitite', 'gravitite', ''),
    'thallasium': (['thallasium_ingot'], 'Thallasium', 'thallasium', ''),
}
# the game's other ores and gems, which have no tool or armour set of their own: their own stats (a pickaxe's tier, speed,
# damage and wear; armour as a share of iron's points and wear), so they make tools, swords and armour too, alone or mixed
# (core/tool_parts.cpp). key: (items, name, prefix, element, stats)
EXTRA = {
    'tin': (['tin_ingot'], 'Tin', 'tin', '', {'tier': 2, 'speed': 5, 'damage': 3, 'durability': 150, 'armor': 0.6, 'armorDur': 0.6}),
    'lapis': (['lapis_lazuli'], 'Lapis', 'lapis', '', {'tier': 2, 'speed': 6, 'damage': 3, 'durability': 160, 'armor': 0.65, 'armorDur': 0.7}),
    'amber': (['amber_gem'], 'Amber', 'amber', '', {'tier': 2, 'speed': 6.5, 'damage': 3, 'durability': 220, 'armor': 0.7, 'armorDur': 0.8}),
    'quartz': (['quartz'], 'Quartz', 'quartz', '', {'tier': 3, 'speed': 7, 'damage': 4, 'durability': 200, 'armor': 0.8, 'armorDur': 0.8}),
    'amethyst': (['amethyst_shard'], 'Amethyst', 'amethyst', '', {'tier': 3, 'speed': 9, 'damage': 4, 'durability': 170, 'armor': 0.75, 'armorDur': 0.7}),
    'olivine': (['olivine'], 'Olivine', 'olivine', '', {'tier': 3, 'speed': 6.5, 'damage': 4, 'durability': 320, 'armor': 0.9, 'armorDur': 0.95}),
    'aether_crystal': (['aether_crystal'], 'Aether Crystal', 'aether_crystal', '', {'tier': 3, 'speed': 8, 'damage': 4, 'durability': 450, 'armor': 0.9, 'armorDur': 1.0}),
    'ender': (['ender_shard'], 'Ender', 'ender', '', {'tier': 4, 'speed': 8.5, 'damage': 5, 'durability': 900, 'armor': 1.05, 'armorDur': 1.2}),
    'mese': (['mese_crystal'], 'Mese', 'mese', '', {'tier': 4, 'speed': 9, 'damage': 5.5, 'durability': 1100, 'armor': 1.15, 'armorDur': 1.4}),
    'crude_steel': (['crude_steel'], 'Crude Steel', 'crude_steel', '', {'tier': 3, 'speed': 6.5, 'damage': 4, 'durability': 600, 'armor': 1.08, 'armorDur': 1.4}),
    'frost': (['frost_shard'], 'Frost', 'frost', 'frost', {'tier': 3, 'speed': 7, 'damage': 4, 'durability': 400, 'armor': 0.9, 'armorDur': 1.0}),
    'red_crystal': (['red_crystal'], 'Red Crystal', 'red_crystal', '', {'tier': 3, 'speed': 7.5, 'damage': 4.5, 'durability': 380, 'armor': 0.95, 'armorDur': 1.1}),
    'rubyglass': (['rubyglass_crystal'], 'Rubyglass', 'rubyglass', 'fire', {'tier': 3, 'speed': 9, 'damage': 4.5, 'durability': 360, 'armor': 0.95, 'armorDur': 1.0, 'fireResist': 0.1}),
    'charged_crystal': (['charged_crystal'], 'Charged Crystal', 'charged_crystal', '', {'tier': 4, 'speed': 10, 'damage': 5, 'durability': 700, 'armor': 1.1, 'armorDur': 1.5}),
    'aurora': (['aurora_crystal'], 'Aurora', 'aurora', '', {'tier': 4, 'speed': 9, 'damage': 5, 'durability': 1000, 'armor': 1.15, 'armorDur': 1.8}),
    'smaragdant': (['smaragdant_crystal', 'smaragdant_crystal_shard'], 'Smaragdant', 'smaragdant', '', {'tier': 4, 'speed': 8, 'damage': 5, 'durability': 900, 'armor': 1.2, 'armorDur': 1.9}),
    'echo': (['echo_shard'], 'Echo', 'echo', '', {'tier': 5, 'speed': 10, 'damage': 6, 'durability': 1800, 'armor': 1.45, 'armorDur': 2.5}),
    'rift': (['rift_crystal'], 'Rift', 'rift', '', {'tier': 5, 'speed': 10.5, 'damage': 6, 'durability': 2000, 'armor': 1.45, 'armorDur': 2.6}),
    'ambrosium': (['ambrosium_shard'], 'Ambrosium', 'ambrosium', '', {'tier': 1, 'speed': 10, 'damage': 2, 'durability': 80, 'armor': 0.55, 'armorDur': 0.5}),
    'croc': (['croc_scale'], 'Crocodile Scale', 'croc', '', {'tier': 2, 'speed': 4, 'damage': 3, 'durability': 160, 'armor': 0.8, 'armorDur': 1.1}),
    # dragon scale: its own armour (dragonscale_*); its tools from these, burning like the dragon's breath
    'dragonscale': (['dragon_scale'], 'Dragonscale', 'dragonscale', 'fire', {'tier': 5, 'speed': 9, 'damage': 6.5, 'durability': 2400}),
    # chainmail: its own pieces stay its own recipes; links in a tool head make a heavy, plain tool
    'chainmail': (['chainlink'], 'Chainmail', 'chainmail', '', {'tier': 2, 'speed': 4.5, 'damage': 3, 'durability': 200}),
}
# the materials that have tools but no armour (or armour but no tools) of their own: the other half, so anything mixes
# with anything (armour as a share of iron's points and wear; tools as a pickaxe's tier, speed, damage and wear)
OTHER_HALF = {
    'wood': {'armor': 0.3, 'armorDur': 0.35}, 'stone': {'armor': 0.45, 'armorDur': 0.55}, 'flint': {'armor': 0.45, 'armorDur': 0.5},
    'ruby': {'armor': 1.3, 'armorDur': 2.0}, 'sapphire': {'armor': 1.3, 'armorDur': 2.1}, 'obsidian': {'armor': 1.25, 'armorDur': 2.5},
    'emberglass': {'armor': 0.9, 'armorDur': 1.0, 'fireResist': 0.15},
    'leather': {'tier': 0, 'speed': 1.5, 'damage': 1.5, 'durability': 40},
}
FALLBACK = {'leather': (160, 101, 64), 'wood': (162, 130, 78), 'stone': (128, 128, 128), 'flint': (78, 78, 88), 'copper': (214, 124, 82), 'bronze': (196, 140, 70),
            'iron': (214, 214, 218), 'silver': (228, 234, 244), 'gold': (250, 210, 70), 'steel': (150, 160, 178), 'diamond': (96, 232, 222),
            'emerald': (60, 200, 110), 'ruby': (222, 40, 64), 'sapphire': (52, 92, 222), 'mithril': (168, 222, 244), 'adamantite': (180, 60, 120),
            'cinderite': (230, 96, 40), 'obsidian': (52, 36, 80), 'emberglass': (240, 96, 40),
            'zanite': (170, 118, 236), 'gravitite': (232, 120, 214), 'thallasium': (92, 204, 194),
            'tin': (200, 206, 212), 'lapis': (44, 76, 196), 'amber': (232, 150, 40), 'quartz': (236, 230, 222), 'amethyst': (164, 104, 222),
            'olivine': (132, 172, 64), 'aether_crystal': (150, 220, 255), 'ender': (40, 150, 128), 'mese': (240, 228, 40), 'chainmail': (150, 152, 160),
            'crude_steel': (140, 140, 146), 'frost': (170, 230, 250), 'red_crystal': (220, 50, 60), 'rubyglass': (230, 60, 80), 'charged_crystal': (255, 140, 80),
            'aurora': (150, 120, 240), 'smaragdant': (60, 220, 140), 'echo': (20, 70, 90), 'rift': (160, 60, 220), 'ambrosium': (250, 230, 120),
            'croc': (100, 120, 70), 'dragonscale': (150, 40, 50)}
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


def item_colour(d, tiles, files, item):
    """the mean colour of an item's own picture (the pack's, else the game's), leaving out the dark outline"""
    pic = None
    p = d / ITEM_PACK / (item + '.png')
    if p.exists(): pic = Image.open(p).convert('RGBA')
    elif item in tiles: pic = Image.open(d / files[tiles[item]]).convert('RGBA')
    if pic is None: return None
    pic = pic.crop((0, 0, pic.width, pic.width)).resize((16, 16), Image.NEAREST)
    px = pic.load(); acc = [0, 0, 0]; n = 0
    for y in range(16):
        for x in range(16):
            r, g, b, a = px[x, y]
            if a < 128: continue
            if colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)[2] < 0.2: continue
            acc[0] += r; acc[1] += g; acc[2] += b; n += 1
    if n < 4: return None
    col = [c / n for c in acc]
    if max(col) < 150: col = [c * 150 / max(1, max(col)) for c in col]   # (a dark picture would make a black tool)
    return tuple(int(c) for c in col)


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    tiles, files = tj['names'], tj['files']
    out = {}
    for k, (its, name, prefix, el) in MATS.items():
        if not any((prefix + '_' + t) in items for t in TYPES + ['helmet']): continue
        col = head_colour(d, tiles, files, prefix) or FALLBACK[k]
        out[k] = {'items': its, 'name': name, 'prefix': prefix, 'rgb': list(col), 'element': el}
        if k in OTHER_HALF: out[k]['stats'] = OTHER_HALF[k]
    for k, (its, name, prefix, el, stats) in EXTRA.items():
        if not all(i in items for i in its): continue
        col = item_colour(d, tiles, files, its[0]) or FALLBACK[k]
        out[k] = {'items': its, 'name': name, 'prefix': prefix, 'rgb': list(col), 'element': el, 'stats': stats}
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
