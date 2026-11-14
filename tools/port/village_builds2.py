"""tools/port/village_builds2.py — the villages' market and town pieces from the user's pictures (village_builds.all_pieces
adds them): the bakery stalls (the cream-roofed one with its oven smoking, the flowery one, the baker's corner), the
alchemist's potion stall, the garden stall hung with vines, the great striped market stalls (blue and white with its cart,
blue and gold with its dyes and spices), the town fountain, the ruin's waterfall into a pool, the wishing well under its
mossy roof, the weaponsmith's armoury corner, the cargo crane with its crate, the bench by a lamp post, the clay-pot lamp,
and a ruined portal arch of the Depths (cold: its frame only, never lit). Same conventions as village_builds.py: a
building faces west, its entrance joint at x 0, one up; a street decoration (terminator) laid out facing north through
Turned; the ground at y 0; goods set down on our tables, barrels and shelves (item piles), never in frames."""
import random

import village_builder as vb
from village_builder import Piece, Pal, leaves, furn, shelf_of, plant_at
from village_builds import Turned, street_end, chain_lantern, pile, bush, entrance


def stripes(p, x0, z0, x1, z1, y, cols, along='z'):
    """a cloth roof in stripes (wool), striped along z (or x)"""
    for x in range(x0, x1 + 1):
        for z in range(z0, z1 + 1): p.set(x, y, z, cols[((z - z0) if along == 'z' else (x - x0)) % len(cols)])


def posts(p, P, pts, h, name=None):
    for (x, z) in pts:
        for y in range(1, h + 1): p.set(x, y, z, name or P.S['log'], axis='y')


def floor(p, P, x0, z0, x1, z1, rnd, mix=None):
    for x in range(x0, x1 + 1):
        for z in range(z0, z1 + 1): p.set(x, 0, z, rnd.choice(mix) if mix else P.floor)


def stall_base(name, style, seed):
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'houses', style)
    return rnd, P, p


def stall_joint(p, style, z=2):
    p.joint(0, 1, z, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)


# ------------------------------------------------------------------------------------------------ the bakers' stalls
def bakery_stall(style, name, seed):
    """the cream-roofed bakery stall: plank posts, a roof of cream and white cloth with greenery and lanterns at its
    corners; the counter of loaves, cookies and pies, a cake on it; behind, the oven (a smoker and a furnace) with its
    smoke going up through the roof; wheat sheaves and flowers about; cloth banners hung from the beam"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 5, 5, rnd)
    posts(p, P, ((1, 0), (1, 5), (5, 0), (5, 5)), 3)
    stripes(p, 0, 0, 6, 5, 4, ['white_wool', 'brown_wool' if not vb.known('beige_wool') else 'beige_wool'])
    for (x, z) in ((0, 0), (0, 5), (6, 0), (6, 5)): p.set(x, 5, z, 'lantern')
    for (x, z) in ((3, 0), (4, 5), (6, 2)): leaves(p, x, 5, z, 'flowering_azalea_leaves' if vb.known('flowering_azalea_leaves') else P.leaves)
    for z in (1, 4): p.set(1, 3, z, rnd.choice(['white_wall_banner', 'brown_wall_banner']), facing='west')
    goods = ['bread', 'cookie', 'pumpkin_pie', 'bread']
    for z in range(1, 5):
        if z == 2: p.set(2, 1, z, 'cake'); continue
        p.set(2, 1, z, furn(P, 'table')); pile(p, 2, 2, z, [rnd.choice(goods) + '*%d' % rnd.randint(2, 5) for _ in range(2)])
    p.set(5, 1, 2, 'smoker', facing='west', lit=True); p.set(5, 1, 3, 'furnace', facing='west', lit=True)
    p.set(5, 2, 2, P.S['stone_slab'], type='bottom'); p.set(5, 4, 3, 'campfire', lit=True)   # (the oven's smoke up through the roof)
    p.set(4, 1, 1, 'hay_block', axis='y'); p.set(4, 1, 4, 'barrel', facing='up'); pile(p, 4, 2, 4, ['wheat*12'])
    p.set(3, 1, 3, furn(P, 'chair'), facing='west')
    for (x, z) in ((0, 0), (0, 5)): p.set(x, 1, z, 'potted_' + rnd.choice(['poppy', 'dandelion', 'cornflower', 'blue_orchid']))
    chain_lantern(p, 3, 3, 2)
    stall_joint(p, style)
    return p


def bakery_corner(style, name, seed):
    """the baker's corner: a back wall of panelled planks with the bread and wheat on its shelf, the oven range (a smoker,
    a furnace, a cauldron of water), the counter with a cake and loaves, a barrel and a chest; a stepped plank roof with a
    lantern on a chain; a red and white banner on the post"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 5, 5, rnd, [P.floor, P.floor, P.S['pave'][0]])
    for z in range(0, 6):
        for y in range(1, 4): p.set(5, y, z, P.floor if y != 2 else P.S['log'], axis='z')
    for x in range(1, 5):
        for y in range(1, 4): p.set(x, y, 5, P.floor if y != 2 else P.S['log'], axis='x')
    posts(p, P, ((1, 0),), 3)
    p.set(5, 1, 1, 'smoker', facing='west', lit=True); p.set(5, 1, 2, 'furnace', facing='west', lit=True)
    p.set(5, 1, 3, 'water_cauldron', level=3); p.set(5, 1, 4, 'crafting_table')
    p.set(4, 2, 5, shelf_of(P), facing='north', items='bread*4,wheat*6,flour*3' if vb.item_ok('flour') else 'bread*4,wheat*6,egg*3')
    p.set(3, 2, 5, 'potted_' + rnd.choice(['oxeye_daisy', 'lily_of_the_valley', 'dandelion']))
    p.set(2, 1, 1, 'cake'); p.set(2, 1, 2, furn(P, 'table')); pile(p, 2, 2, 2, ['bread*4', 'cookie*6'])
    p.set(2, 1, 3, 'barrel', facing='up'); pile(p, 2, 2, 3, ['wheat*16'])
    p.set(1, 1, 4, 'chest', facing='east')
    for x in range(0, 6):   # the roof stepping down to the street
        for z in range(0, 6):
            y = 4 + max(0, min(2, (x - 1) // 2))
            p.set(x, y, z, P.rslab, type='bottom')
    chain_lantern(p, 3, 3, 3)
    p.set(1, 3, 0, 'red_wall_banner', facing='north')
    stall_joint(p, style)
    return p


# ------------------------------------------------------------------------------------------------ the potion stall
def potion_stall(style, name, seed):
    """the alchemist's stall: teal posts of warped stem under a roof of purple and magenta cloth; the brewing stand and a
    cauldron behind, a bookshelf, a chest with bottles on it; the counter of potions; mushrooms and glowing things about,
    a soul lantern hanging"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 5, 5, rnd, [P.floor, 'dark_prismarine', P.floor])
    posts(p, P, ((1, 0), (1, 5), (5, 0), (5, 5)), 3, 'warped_stem')
    stripes(p, 0, 0, 6, 5, 4, ['purple_wool', 'magenta_wool', 'purple_wool'])
    for z in (0, 5): p.set(1, 3, z, 'purple_wall_banner', facing='west')
    for z in range(1, 5):
        p.set(2, 1, z, furn(P, 'table'))
        pile(p, 2, 2, z, ['potion*%d' % rnd.randint(1, 3), rnd.choice(['glass_bottle', 'nether_wart_item' if vb.item_ok('nether_wart_item') else 'glass_bottle', 'glowstone_dust', 'redstone'])])
    p.set(5, 1, 1, 'brewing_stand'); p.set(5, 1, 2, 'water_cauldron', level=3); p.set(5, 1, 3, 'bookshelf'); p.set(5, 2, 3, 'bookshelf')
    p.set(5, 1, 4, 'chest', facing='west'); pile(p, 5, 2, 4, ['potion*2', 'glass_bottle*3'])
    p.set(3, 1, 4, furn(P, 'chair'), facing='west')
    p.set(0, 1, 0, 'barrel', facing='up'); p.set(0, 2, 0, 'red_mushroom'); p.set(0, 1, 5, 'barrel', facing='up'); p.set(0, 2, 5, 'brown_mushroom')
    p.set(6, 1, 0, 'amethyst_cluster', facing='up'); p.set(6, 1, 5, 'crimson_roots' if vb.known('crimson_roots') else 'fern')
    chain_lantern(p, 3, 3, 2)
    p.set(3, 2, 2, 'air'); p.clear(3, 2, 2)
    p.set(3, 3, 2, 'chain', axis='y'); p.set(3, 2, 2, 'soul_lantern', hanging=True)
    stall_joint(p, style)
    return p


# ------------------------------------------------------------------------------------------------ the garden stall
def garden_stall(style, name, seed):
    """the garden stall: a roof of moss and green cloth with vines hanging from its beams and lanterns among them; mossy
    stone feet to the posts; potted ferns and saplings along the counter, crates of produce, a water trough at its end"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 5, 5, rnd)
    for (x, z) in ((1, 0), (1, 5), (5, 0), (5, 5)):
        p.set(x, 1, z, 'mossy_cobblestone'); p.set(x, 2, z, P.S['log'], axis='y'); p.set(x, 3, z, P.S['log'], axis='y')
    stripes(p, 0, 0, 6, 5, 4, ['moss_block' if vb.known('moss_block') else 'green_wool', 'lime_wool', 'green_wool'])
    for (x, z) in ((0, 1), (0, 4), (6, 2), (2, 0), (4, 5)):
        for y in (3, 2): p.set(x, y, z, 'vine', **{'east' if x == 0 else 'west' if x == 6 else 'south' if z == 0 else 'north': True})
    for z in range(1, 5):
        p.set(2, 1, z, furn(P, 'table') if z % 2 else 'barrel', **({} if z % 2 else {'facing': 'up'}))
        p.set(2, 2, z, 'potted_' + rnd.choice(['fern', 'oak_sapling', 'birch_sapling', 'red_tulip', 'azure_bluet']) if z % 2 else 'item_pile', **({} if z % 2 else {'items': ','.join(rnd.choice(['carrot', 'potato', 'beetroot', 'apple']) + '*6' for _ in range(2))}))
    p.set(5, 1, 2, 'water_cauldron', level=3); p.set(5, 1, 3, 'composter', level=4)
    p.set(5, 1, 1, shelf_of(P), facing='west', items='wheat_seeds*8,carrot*4,beetroot_seeds*6,bone_meal*4')
    for z in (0, 5): bush(p, P, 0, z, rnd)
    for (x, z) in ((3, 1), (3, 4)): chain_lantern(p, x, 3, z)
    stall_joint(p, style)
    return p


# ------------------------------------------------------------------------------------------------ the great market stalls
def great_stall(style, name, seed, cols, cart=False, wares=None):
    """a big market stall: dark log posts, a stepped plank roof with a peak of striped cloth, banners on the posts and
    lanterns on chains; the counter across its front with goods on it, a shelf and a bookcase behind, chests and barrels
    about, crates of produce at the side (and a cart with them)"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 6, 6, rnd, [P.floor, P.S['pave'][0]])
    posts(p, P, ((1, 0), (1, 6), (6, 0), (6, 6)), 4, 'dark_oak_log' if vb.known('dark_oak_log') else None)
    for x in range(0, 8):   # the stepped roof: plank slabs out at the eaves, the cloth peak along the middle
        for z in range(0, 7):
            edge = min(z, 6 - z)
            y = 5 + min(edge, 2)
            p.set(x, y, z, cols[x % len(cols)] if edge >= 2 else P.rslab, **({} if edge >= 2 else {'type': 'bottom'}))
    for z in (0, 6):
        p.set(1, 3, z, cols[0].replace('_wool', '_wall_banner'), facing='west'); p.set(6, 3, z, cols[0].replace('_wool', '_wall_banner'), facing='east')
    wares = wares or ['bread', 'apple', 'cod', 'leather', 'candle', 'book']
    for z in range(1, 6):
        p.set(2, 1, z, furn(P, 'table')); pile(p, 2, 2, z, [rnd.choice(wares) + '*%d' % rnd.randint(1, 4) for _ in range(2)])
    p.set(6, 1, 2, shelf_of(P), facing='west', items=','.join(rnd.choice(wares) for _ in range(6)))
    p.set(6, 1, 3, 'bookshelf'); p.set(6, 2, 3, 'bookshelf'); p.set(6, 1, 4, 'chest', facing='west'); p.set(6, 1, 5, 'barrel', facing='west')
    p.set(4, 1, 3, furn(P, 'chair'), facing='west')
    for (x, z) in ((3, 1), (3, 5), (5, 3)): chain_lantern(p, x, 4, z)
    for (x, z) in ((7, 1), (7, 2), (0, 7), (1, 7)):   # crates of produce at its side
        p.set(x, 1, z, 'barrel', facing='up'); pile(p, x, 2, z, [rnd.choice(['apple', 'carrot', 'melon_slice', 'beetroot', 'potato']) + '*8'])
    if cart:
        for x in (3, 5): p.set(x, 1, 8, 'oak_trapdoor', facing='north', half='bottom', open=True)
        for x in (3, 4, 5): p.set(x, 1, 7, P.floor if x != 4 else 'barrel', **({'facing': 'up'} if x == 4 else {}))
        pile(p, 3, 2, 7, ['apple*8']); pile(p, 5, 2, 7, ['carrot*8'])
    stall_joint(p, style, 3)
    return p


# ------------------------------------------------------------------------------------------------ the town's corners
def fountain(style, name, seed):
    """the town fountain: a round basin of stone bricks with lily pads on the water, a pillar in its middle pouring from
    its top every way into it, lanterns on the basin's rim"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 9):
        for z in range(0, 9): p.set(x, 0, z, P.S['pave'][0] if (x + z) % 3 else rnd.choice(P.S['pave']))
    c = 4
    for x in range(1, 8):
        for z in range(1, 8):
            d = max(abs(x - c), abs(z - c))
            if d == 3: p.set(x, 1, z, 'stone_bricks' if (x + z) % 2 else 'mossy_stone_bricks'); p.set(x, 2, z, 'stone_brick_slab', type='bottom')
            elif d < 3: p.set(x, 0, z, 'stone_bricks'); p.set(x, 1, z, 'water')
    for y in range(1, 4): p.set(c, y, c, 'chiseled_stone_bricks' if y == 3 else 'stone_bricks')
    p.set(c, 4, c, 'water')   # (it spills from the top over every side into the basin)
    for (x, z) in ((2, 3), (6, 5), (3, 6)): p.set(x, 2, z, 'lily_pad')
    for (x, z) in ((1, 1), (7, 7), (1, 7), (7, 1)): p.set(x, 3, z, 'lantern')
    street_end(piece, c, P.pathb())
    return piece


def waterfall_pool(style, name, seed):
    """a ruin's waterfall: an old wall of mossy and cracked stone, water pouring from a gap high in it into a pool at its
    foot; ferns, moss and a log bench by the water"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 7):
        for z in range(0, 7): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    ruin = ['mossy_stone_bricks', 'cracked_stone_bricks', 'stone_bricks', 'mossy_cobblestone']
    for x in range(0, 7):
        h = 5 - abs(x - 3) // 2 + rnd.randint(-1, 0)
        for y in range(1, h + 1): p.set(x, y, 6, rnd.choice(ruin))
    p.set(3, 4, 6, 'water'); p.set(3, 4, 5, 'stone_brick_slab', type='bottom')   # (the spout: water pours over the lip)
    for x in range(2, 5):
        for z in range(3, 6): p.set(x, 0, z, 'stone_bricks'); p.set(x, 1, z, 'water' if z < 5 or x != 3 else 'water')
    for x in (1, 5):
        for z in range(3, 6): p.set(x, 1, z, rnd.choice(ruin))
    for x in range(1, 6): p.set(x, 1, 2, 'mossy_stone_bricks' if x % 2 else 'stone_brick_slab', **({} if x % 2 else {'type': 'bottom'}))
    for (x, z) in ((0, 4), (6, 3), (6, 5), (0, 2)): p.set(x, 1, z, rnd.choice(['fern', 'large_fern' if False else 'fern', 'moss_carpet']))
    p.set(2, 2, 2, 'lily_pad') if False else None
    for x in (4, 5): p.set(x, 1, 0, furn(P, 'bench'), facing='south')
    street_end(piece, 3, P.pathb())
    return piece


def wishing_well(style, name, seed):
    """the wishing well: a stone ring round the water, two log posts with a mossy stepped roof over (leaves and vines on it,
    flowers on top), a chain hanging into the water; stone steps up to it, a chest and barrels by it, lanterns hung from
    the roof's corners"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 7):
        for z in range(0, 7): p.set(x, 0, z, rnd.choice(P.S['pave'] + ['mossy_cobblestone']))
    c = 3
    for x in range(1, 6):
        for z in range(1, 6):
            if max(abs(x - c), abs(z - c)) == 2: p.set(x, 1, z, rnd.choice(['stone_bricks', 'mossy_stone_bricks', 'cobblestone']))
            elif max(abs(x - c), abs(z - c)) == 1: p.set(x, 1, z, 'water'); p.set(x, 0, z, 'water'); p.set(x, -1, z, 'stone_bricks')
    p.set(c, 1, c, 'water'); p.set(c, 0, c, 'water')
    for (x, z) in ((1, 1), (5, 5), (1, 5), (5, 1)):
        for y in range(2, 5): p.set(x, y, z, P.S['log'], axis='y')
    for x in range(0, 7):   # the roof: stairs stepping up from each side to a ridge, leaves over it
        for z in range(0, 7):
            d = min(x, 6 - x, z, 6 - z)
            p.set(x, 5 + min(d, 2), z, P.rslab if d >= 2 else P.rstairs, **({'type': 'bottom'} if d >= 2 else {'facing': 'east' if x == d else 'west' if 6 - x == d else 'south' if z == d else 'north', 'half': 'bottom'}))
    for (x, z) in ((2, 0), (4, 6), (0, 3), (6, 2), (3, 3)): leaves(p, x, 6 + (1 if (x, z) == (3, 3) else 0), z, P.leaves)
    p.set(3, 8, 3, 'poppy') if False else None
    for y in (4, 3, 2): p.set(c, y, c, 'chain', axis='y')
    for (x, z) in ((0, 0), (6, 6)): p.set(x, 4, z, 'lantern', hanging=True)
    p.set(6, 1, 0, 'chest', facing='south'); p.set(0, 1, 6, 'barrel', facing='up'); p.set(0, 2, 6, 'potted_' + rnd.choice(['poppy', 'cornflower']))
    for (x, z) in ((6, 4), (0, 1)): plant_at(p, P, x, 1, z)
    street_end(piece, c, P.pathb())
    return piece


def armory(style, name, seed):
    """the weaponsmith's armoury corner: an open shop with a plank roof on log posts; armour on stands, swords and axes
    on a rack and in a barrel, the grindstone, the anvil, a chest of more; a hanging lantern"""
    rnd, P, p = stall_base(name, style, seed)
    floor(p, P, 0, 0, 6, 6, rnd, [P.S['pave'][0], 'cobblestone', P.floor])
    posts(p, P, ((1, 0), (1, 6), (6, 0), (6, 6)), 3)
    for z in range(0, 7):
        for y in (1, 2, 3): p.set(6, y, z, P.floor)
    for x in range(0, 8):
        for z in range(0, 7): p.set(x, 4, z, P.rslab, type='bottom')
    for z in (1, 5): p.set(5, 1, z, 'armor_stand', items=rnd.choice(vb.ARMOUR_SETS))
    p.set(6, 2, 3, 'weapon_rack', facing='west', items='iron_sword,iron_axe,bow')
    p.set(5, 1, 3, 'grindstone', face='floor', facing='west'); p.set(3, 1, 3, 'anvil', facing='north')
    p.set(5, 1, 2, 'barrel', facing='up'); pile(p, 5, 2, 2, ['iron_sword', 'stone_sword', 'iron_axe'])
    p.set(5, 1, 4, 'chest', facing='west')
    p.set(2, 1, 1, furn(P, 'table')); pile(p, 2, 2, 1, ['shield', 'iron_helmet'])
    chain_lantern(p, 3, 3, 3)
    stall_joint(p, style, 3)
    return p


def crane(style, name, seed):
    """the cargo crane: a log mast on a stone foot in grass and flowers, a beam out from its top with a chain to a crate
    (a barrel and a chest on planks) at the end; vines and leaves grown over it"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 7):
        for z in range(0, 7): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    m = (2, 4)
    p.set(m[0], 1, m[1], 'mossy_cobblestone'); p.set(m[0] + 1, 1, m[1], 'cobblestone')
    for y in range(2, 9): p.set(m[0], y, m[1], P.S['log'], axis='y')
    for x in range(m[0] - 2, m[0] + 5): p.set(x, 8, m[1], P.S['log'], axis='x')   # the boom
    p.set(m[0] - 2, 7, m[1], 'mossy_stone_bricks')   # (its counterweight)
    for y in range(3, 8): p.set(m[0] + 4, y, m[1], 'chain', axis='y')
    p.set(m[0] + 4, 2, m[1], 'barrel', facing='up'); p.set(m[0] + 4, 1, m[1], 'chest', facing='west')
    for (x, y) in ((m[0] + 1, 8), (m[0] + 3, 8)): leaves(p, x, y + 1, m[1], P.leaves)
    for y in range(3, 8):
        if rnd.random() < 0.6: p.set(m[0] + 1, y, m[1], 'vine', west=True)
    for (x, z) in ((1, 3), (3, 5), (1, 5), (4, 3)): plant_at(p, P, x, 1, z)
    street_end(piece, 3, P.pathb())
    return piece


def bench_lamp(style, name, seed):
    """a bench by a lamp post: our bench, a barrel at its end with a pot of flowers, flowering bushes behind, a lamp post on
    a stone foot with its lantern hung from a crook"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 6):
        for z in range(0, 5): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    for x in (2, 3): p.set(x, 1, 3, furn(P, 'bench'), facing='north')
    p.set(1, 1, 3, 'barrel', facing='up'); p.set(1, 2, 3, 'potted_' + rnd.choice(['poppy', 'red_tulip', 'blue_orchid']))
    for x in range(0, 5): leaves(p, x, 1, 4, 'flowering_azalea_leaves' if vb.known('flowering_azalea_leaves') else P.leaves)
    for x in (0, 1, 3): leaves(p, x, 2, 4, P.leaves)
    p.set(4, 1, 3, 'stone_bricks'); p.set(4, 2, 3, 'stone_brick_wall')
    for y in range(3, 7): p.set(4, y, 3, P.fence)
    p.set(3, 6, 3, P.fence); p.set(3, 5, 3, 'chain', axis='y'); p.set(3, 4, 3, 'lantern', hanging=True)
    for (x, z) in ((2, 1), (3, 0)): p.set(x, 0, z, rnd.choice(['dirt_path', P.S['pave'][0]]))
    street_end(piece, 2, P.pathb())
    return piece


def clay_lamp(style, name, seed):
    """the clay-pot lamp: a planter of grass and roses, a pale stone foot, a log post with a stone collar, crossed slab beams
    at its top with four lanterns hung from them; a big clay pot of flowers beside"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 5):
        for z in range(0, 5): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    for x in range(1, 4):
        for z in range(1, 4): p.set(x, 1, z, P.floor if (x in (1, 3) or z in (1, 3)) else 'grass_block')
    c = 2
    p.set(c, 1, c, 'smooth_stone' if vb.known('smooth_stone') else 'stone'); p.set(c, 2, c, 'smooth_stone' if vb.known('smooth_stone') else 'stone')
    for y in range(3, 6): p.set(c, y, c, 'birch_log' if vb.known('birch_log') else P.S['log'], axis='y')
    p.set(c, 6, c, 'smooth_stone' if vb.known('smooth_stone') else 'stone')
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        p.set(c + dx, 7, c + dz, P.rslab, type='bottom'); p.set(c + dx, 6, c + dz, 'lantern', hanging=True)
    p.set(c, 7, c, P.rfull)
    p.set(3, 2, 3, 'terracotta'); p.set(3, 3, 3, 'potted_' + rnd.choice(['cornflower', 'blue_orchid']))
    p.set(1, 2, 1, 'rose_bush' if vb.known('rose_bush') else 'poppy'); p.set(1, 2, 3, P.plant())
    street_end(piece, c, P.pathb())
    return piece


def ruined_arch(style, name, seed):
    """a ruined portal arch of the Depths, cold: a frame of obsidian (some of it crying) set in blackstone and nether
    bricks gone to ruin, netherrack and magma at its foot, crimson growth on it, soul fire in braziers either side; a
    landmark, never a door (no portal is ever lit in it)"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style); p = Turned(piece)
    for x in range(0, 7):
        for z in range(0, 6): p.set(x, 0, z, rnd.choice(['netherrack', 'blackstone', 'netherrack', 'magma_block', 'nether_bricks']))
    for x in range(1, 6):   # the frame: 3 wide inside, 4 tall
        for y in range(1, 7):
            edge = x in (1, 5) or y in (1, 6)
            if not edge: continue
            p.set(x, y, 3, rnd.choice(['obsidian', 'obsidian', 'crying_obsidian']) if rnd.random() > 0.15 else 'air')
    for x in range(0, 7):   # its ruined surround
        for y in range(1, 8):
            if x in (0, 6) and y < 7 - rnd.randint(0, 3): p.set(x, y, 3, rnd.choice(['blackstone', 'nether_bricks', 'gilded_blackstone' if rnd.random() < 0.1 else 'blackstone']))
    for x in (0, 6): p.set(x, 1, 1, 'nether_bricks'); p.set(x, 2, 1, 'soul_campfire', lit=True)
    for (x, z) in ((2, 1), (4, 0), (1, 4), (5, 5)): p.set(x, 1, z, rnd.choice(['crimson_roots', 'crimson_fungus' if vb.known('crimson_fungus') else 'crimson_roots']))
    p.set(3, 1, 1, 'nether_wart_block')
    for (x, z) in ((0, 0), (6, 5)): p.set(x, 1, z, 'chest' if False else 'nether_bricks')
    street_end(piece, 3, P.pathb())
    return piece


def all_pieces(style, s):
    return [bakery_stall(style, '%s_market_stall_baker_2' % style, s('bakery2')),
            bakery_corner(style, '%s_market_stall_baker_3' % style, s('bakery3')),
            potion_stall(style, '%s_market_stall_alchemist_1' % style, s('potion')),
            garden_stall(style, '%s_market_stall_farmer_2' % style, s('garden')),
            great_stall(style, '%s_market_stall_merchant_2' % style, s('great1'), ['blue_wool', 'white_wool'], cart=True),
            great_stall(style, '%s_market_stall_merchant_3' % style, s('great2'), ['blue_wool', 'yellow_wool'],
                        wares=['potion', 'redstone', 'lapis_lazuli', 'glowstone_dust', 'carrot', 'apple', 'bread']),
            armory(style, '%s_weaponsmith_armory_1' % style, s('armory')),
            fountain(style, 'fountain_01', s('fount1')),
            waterfall_pool(style, 'fountain_02', s('fount2')),
            wishing_well(style, 'wishing_well_01', s('well')),
            crane(style, 'crane_01', s('crane')),
            bench_lamp(style, 'park_bench_03', s('park3')),
            clay_lamp(style, 'lamp_post_02', s('lamp2')),
            ruined_arch(style, 'ruined_portal_01', s('arch'))]
