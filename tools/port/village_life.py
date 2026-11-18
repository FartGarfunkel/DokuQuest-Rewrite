#!/usr/bin/env python3
"""tools/port/village_life.py — what a village gives back (sim/village_life.h): the Shrine of Renewal (a carved stone
basin on a pillar of runes, in each village's square: it takes back the talent tree and the stat points you spent,
for coins) with its pictures drawn here and its model in data/shapes.json; and each trade's wants
(data/villagers.json "tradeWants": what a job's villager asks for, which skill it teaches). Idempotent; data_patches.py
runs it.

  python3 tools/port/village_life.py [data-dir]
"""
import json, pathlib, sys
from PIL import Image, ImageDraw

# what each trade wants: [item, [least, most], why, the skill level it waits for]; their skill (the trade's own)
TRADE_WANTS = {
    'farmer': ['farming', [['wheat', [12, 24], 'The market wants grain and my field came up thin.', 1], ['carrot', [8, 16], 'Carrots for the stall.', 1],
                           ['potato', [8, 16], 'Potatoes, a sack of them.', 1], ['beetroot', [8, 16], 'Beetroot for the dyer and the pot.', 10], ['pumpkin', [3, 6], 'Pumpkins for the harvest fair.', 20],
                           ['melon_slice', [12, 24], 'Melon for the hot days.', 30]]],
    'fisherman': ['fishing', [['cod', [4, 10], 'The nets came up empty. Bring me fish.', 1], ['salmon', [3, 8], 'Salmon, for the smoker.', 10],
                              ['tropical_fish', [1, 3], 'A bright fish from warm water, for the fair.', 30], ['pufferfish', [1, 2], 'The healer wants a pufferfish. Mind the spines.', 40]]],
    'shepherd': ['taming', [['wool', [8, 16], 'The flock was shorn thin this year. Wool for the looms.', 1], ['wheat', [10, 20], 'Feed for the ewes.', 1], ['lead', [1, 2], 'A lead or two, for the strays.', 15]]],
    'rancher': ['taming', [['egg', [6, 12], 'The hens went quiet. Eggs, please.', 1], ['leather', [4, 8], 'Hides from the herd, tanned.', 10], ['milk_bucket', [1, 2], 'A pail of milk for the dairy.', 5]]],
    'leatherworker': ['taming', [['leather', [6, 12], 'Hides for the tannery. Mine are all used up.', 1], ['prepared_hide', [3, 6], 'Hides scraped and ready, for gloves.', 10], ['raw_hide', [4, 8], 'Raw hides to soak.', 1]]],
    'fletcher': ['archery', [['feather', [8, 16], 'Feathers, for fletching.', 1], ['arrow', [16, 32], 'The watch is short of arrows.', 5], ['bow', [1, 2], 'A good bow, for the new guard.', 20]]],
    'librarian': ['magic', [['book', [3, 6], 'Books, for the shelves. Any you can spare.', 1], ['paper', [12, 24], 'Paper to copy the old lore onto.', 1],
                            ['writable_book', [1, 2], 'A book to write in. The village chronicle runs out of pages.', 10], ['enchanted_book', [1, 1], 'A book with a charm in it, for the reading room.', 30],
                            ['lapis_lazuli', [6, 12], 'Lapis, for the illuminations.', 20]]],
    'cartographer': ['exploring', [['paper', [8, 16], 'Paper for the new maps.', 1], ['compass', [1, 1], 'A true compass. Mine spins.', 5], ['filled_map', [1, 2], 'A map of somewhere I have not drawn. Fill one and bring it.', 15]]],
    'cleric': ['alchemy', [['potion_heal', [1, 3], 'Healing draughts for the sickroom.', 1], ['potion_regen', [1, 2], 'Something to mend the old ones.', 15], ['glass_bottle', [6, 12], 'Clean bottles for the remedies.', 1],
                           ['nether_wart_item', [4, 8], 'Wart from the deep places, for the brews.', 20], ['potion_fireres', [1, 2], 'Fire draughts for the smiths.', 30]]],
    'armorer': ['smithing', [['iron_ingot', [6, 12], 'Iron for new mail.', 1], ['iron_helmet', [1, 1], 'A helmet for the new guard.', 10], ['iron_chestplate', [1, 1], 'A breastplate. The old one is rust.', 20],
                             ['iron_boots', [1, 2], 'Boots of iron for the watch.', 15]]],
    'weaponsmith': ['smithing', [['iron_ingot', [6, 10], 'Iron for blades.', 1], ['iron_sword', [1, 2], 'Swords for the guards.', 10], ['iron_axe', [1, 2], 'Axes for the woodsmen and the wall.', 15]]],
    'toolsmith': ['smithing', [['iron_ingot', [4, 8], 'Iron for new tools.', 1], ['iron_pickaxe', [1, 1], 'A pickaxe for the mine.', 10], ['iron_shovel', [1, 2], 'Shovels for the builders.', 10], ['iron_hoe', [1, 2], 'Hoes for the fields.', 5]]],
    'butcher': ['cooking', [['cooked_beef', [6, 12], 'Roast beef for the feast table.', 1], ['cooked_porkchop', [6, 12], 'Pork, well cooked.', 1], ['cooked_chicken', [6, 12], 'Roast chicken for the inn.', 5], ['cooked_mutton', [6, 12], 'Mutton, done right.', 10]]],
    'baker': ['cooking', [['bread', [8, 16], 'Loaves for the market, and the ovens are behind.', 1], ['cookie', [12, 24], 'Cookies for the children.', 10], ['pumpkin_pie', [3, 6], 'Pies for the fair.', 15], ['cake', [1, 2], 'A cake for a wedding.', 25]]],
    'innkeeper': ['cooking', [['mushroom_stew', [2, 4], 'Stew for the travellers.', 1], ['beetroot_soup', [2, 4], 'Soup for a cold night.', 5], ['baked_potato', [8, 16], 'Baked potatoes for the common room.', 1],
                              ['cooked_cod', [4, 8], 'Fish for the supper.', 10], ['cooked_salmon', [4, 8], 'Salmon for the guests.', 15]]],
    'miller': ['farming', [['wheat', [16, 32], 'The stones are idle. Grain, please.', 1], ['sugar_cane', [8, 16], 'Cane for the sweet stuff.', 10]]],
    'mason': ['mining', [['stone_bricks', [16, 32], 'Dressed stone for the wall.', 1], ['bricks', [12, 24], 'Bricks for the new chimney.', 5], ['cobblestone', [32, 64], 'Rubble for the footings.', 1], ['smooth_stone', [12, 24], 'Smooth stone for the hearths.', 15]]],
    'jeweller': ['mining', [['gold_ingot', [3, 6], 'Gold, for a ring.', 5], ['lapis_lazuli', [6, 12], 'Lapis, for the settings.', 10], ['amethyst_shard', [4, 8], 'Amethyst, for a brooch.', 15], ['diamond', [1, 2], 'A diamond. Just the one. A wedding.', 35],
                           ['emerald', [2, 4], 'Emeralds, cut or rough.', 25]]],
    'miner': ['mining', [['coal', [16, 32], 'Coal for the village. The seam ran out.', 1], ['iron_ore', [8, 16], 'Iron ore, for the smiths.', 10], ['copper_ingot', [6, 12], 'Copper for the pipes.', 5], ['redstone', [8, 16], 'Redstone dust, for the mill works.', 20]]],
    'lumberjack': ['woodcutting', [['log', [16, 32], 'Logs for the winter stores.', 1], ['spruce_log', [12, 24], 'Spruce, straight and tall, for the beams.', 10], ['birch_log', [12, 24], 'Birch, for the furniture maker.', 10],
                                   ['dark_oak_log', [12, 24], 'Dark oak for the hall\'s new doors.', 25]]],
    'builder': ['excavation', [['sand', [16, 32], 'Sand for the glass and the mortar.', 1], ['gravel', [16, 32], 'Gravel for the paths.', 1], ['clay_ball', [12, 24], 'Clay for the bricks.', 5], ['glass', [8, 16], 'Glass for the windows.', 10]]],
    'merchant': ['exploring', [['cactus', [4, 8], 'Cactus from the dry lands sells well here.', 1], ['ice', [6, 12], 'Ice from the cold north, for the cellar.', 10], ['cocoa_beans', [6, 12], 'Cocoa from the jungles. The town loves it.', 20],
                               ['glow_berries', [4, 8], 'Glow berries from under the world.', 30]]],
}


def main(d):
    d = pathlib.Path(d)
    items = json.loads((d / 'items.json').read_text()); tj = json.loads((d / 'tiles.json').read_text())
    bj = json.loads((d / 'blocks.json').read_text()); shp = json.loads((d / 'shapes.json').read_text())
    vj = json.loads((d / 'villagers.json').read_text(encoding='utf-8'))
    tiles, files = tj['names'], tj['files']
    tj['res'] += [32] * max(0, tj['count'] - len(tj['res']))
    (d / 'textures/block').mkdir(parents=True, exist_ok=True)

    def tile(name, im):
        rel = 'textures/block/%s.png' % name
        im.save(d / rel)
        if name not in tiles: tiles[name] = tj['count']; files.append(rel); tj['res'].append(32); tj['count'] += 1
        return tiles[name]

    # pale carved stone: dressed blocks with a chiselled border, a little speckle
    PALE, MID, DARK, GOLD, GLOW = (196, 192, 182, 255), (164, 160, 150, 255), (104, 100, 94, 255), (226, 184, 72, 255), (150, 226, 214, 255)
    import random
    rng = random.Random(7051)

    def stone():
        im = Image.new('RGBA', (32, 32), PALE); g = ImageDraw.Draw(im)
        for _ in range(70):
            x, y = rng.randrange(32), rng.randrange(32); c = rng.choice([MID, (208, 204, 196, 255), (184, 180, 170, 255)])
            g.point((x, y), fill=c)
        g.rectangle([0, 0, 31, 31], outline=DARK); g.rectangle([1, 1, 30, 30], outline=MID)
        return im
    st = stone()
    # the pillar's runes: a ring broken and made whole (renewal), in gold, a glow at its heart
    ru = stone(); g = ImageDraw.Draw(ru)
    g.ellipse([8, 6, 23, 21], outline=GOLD, width=2)
    g.rectangle([15, 4, 16, 8], fill=PALE)   # the ring's break at the top...
    g.polygon([(17, 3), (21, 6), (17, 9)], fill=GOLD)   # ...an arrow carrying on round it
    g.ellipse([13, 11, 18, 16], fill=GLOW, outline=(80, 160, 150, 255))
    for x in (6, 12, 19, 25): g.rectangle([x, 25, x + 1, 28], fill=GOLD)
    # the basin's top: a still pool that glows, in a stone lip
    tp = stone(); g = ImageDraw.Draw(tp)
    g.rectangle([4, 4, 27, 27], fill=MID); g.rectangle([6, 6, 25, 25], fill=(70, 150, 150, 255))
    g.ellipse([9, 9, 22, 22], fill=GLOW); g.ellipse([13, 12, 17, 16], fill=(230, 255, 250, 255))
    t = {'shrine_stone': tile('shrine_stone', st), 'shrine_rune': tile('shrine_rune', ru), 'shrine_top': tile('shrine_top', tp)}

    # the model: a stepped base, a pillar of runes, the basin on top with its pool (1/16-block boxes)
    shp['shapes']['respec_shrine'] = [
        {'b': [0, 0, 0, 16, 2, 16], 'side': 'shrine_stone', 'end': 'shrine_stone'},
        {'b': [2, 2, 2, 14, 4, 14], 'side': 'shrine_stone', 'end': 'shrine_stone'},
        {'b': [4, 4, 4, 12, 11, 12], 'side': 'shrine_rune', 'end': 'shrine_stone'},
        {'b': [2, 11, 2, 14, 13, 14], 'side': 'shrine_stone', 'end': 'shrine_stone'},
        {'b': [1, 13, 1, 15, 15, 15], 'side': 'shrine_stone', 'end': 'shrine_top'},
    ]
    blocks, faces = bj['blocks'], bj['faces']
    while len(faces) < len(blocks): faces.append(None)   # (faces are read by the block's place in the list)
    if not any(b['key'] == 'respec_shrine' for b in blocks):
        blocks.append({'id': len(blocks), 'render': 6, 'solid': True, 'opaque': False, 'replaceable': False, 'hardness': 3, 'tool': 'pickaxe', 'tier': 0,
                       'drop': 'respec_shrine', 'light': 6, 'lightAtt': 0, 'sound': 'stone', 'key': 'respec_shrine', 'name': 'Shrine of Renewal',
                       'tex': 'shrine_rune', 'shape': 'respec_shrine', 'respecShrine': True})
        faces.append([t['shrine_rune']] * 6)
    items['respec_shrine'] = {'key': 'respec_shrine', 'name': 'Shrine of Renewal', 'maxStack': 64, 'icon': 'block:respec_shrine', 'block': 'respec_shrine',
                              'desc': "Every village keeps one in its square. Use it to take back the talents you've learnt and the stat points you've spent, and choose again, for gold coins: the more you've spent, the more it costs; a village that thinks well of you asks less."}

    # each trade's wants (sim/village_life.cpp tradeRequestFor)
    vj['tradeWants'] = {job: {'skill': s, 'wants': w} for job, (s, w) in TRADE_WANTS.items()}

    (d / 'shapes.json').write_text(json.dumps(shp, indent=1, ensure_ascii=False))
    (d / 'items.json').write_text(json.dumps(items, separators=(',', ':'), ensure_ascii=True))
    (d / 'tiles.json').write_text(json.dumps(tj, separators=(',', ':'), ensure_ascii=False))
    (d / 'blocks.json').write_text(json.dumps(bj, separators=(',', ':'), ensure_ascii=False))
    (d / 'villagers.json').write_text(json.dumps(vj, separators=(',', ':'), ensure_ascii=True), encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
