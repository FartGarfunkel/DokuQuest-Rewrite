"""tools/port/village_builds.py — more of the villages' own pieces (village_builder.py pieces() adds them): the smith's open
forge, the barn with its loft, yard and cart, the stalls' produce and lanterns, the garden bridge over a brook, the hay
wagon, the park benches under their pergolas (green and autumn), the lamp posts with their lanterns hung from beams,
and the library's study. Built by rules like the rest (the same conventions: a building faces west, its entrance joint at
x 0, one up; the ground at y 0); what's shown is set down on our tables, shelves, barrels and crates (item piles), never
in item frames."""
import random

import village_builder as vb
from village_builder import Piece, Pal, OPP, leaves, furn, shelf_of, plant_at, lamp


def chain_lantern(p, x, y, z, links=1):
    """a lantern hanging on a chain from the block above y + links"""
    for k in range(links): p.set(x, y + 1 + k, z, 'chain', axis='y')
    p.set(x, y, z, 'lantern', hanging=True)


def pile(p, x, y, z, items):
    p.set(x, y, z, 'item_pile', items=','.join(items))


def bush(p, P, x, z, rnd, h=1, flowering=False):
    lv = 'flowering_azalea_leaves' if flowering else P.leaves
    for y in range(1, h + 1): leaves(p, x, y, z, lv)


def entrance(p, style, z, path):
    p.set(0, 0, z, path)
    p.joint(0, 1, z, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)


# -------------------------------------------------------------------------------------------- the smith's open forge
def forge_shop(style, name, seed):
    """a smith's forge open to the street: a cobbled floor under a plank roof on log posts; at the back a brick hearth with
    the fire behind iron bars, its chimney, the bellows; the anvil on its stump, the quench tub, the grindstone, a stand
    with the work, a barrel of tools, the shelf of ingots, a chest; a lantern on a chain at the front"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'houses', style)
    X0, X1, Z0, Z1 = 1, 8, 0, 8
    zc = (Z0 + Z1) // 2
    log, planks = P.S['log'], P.floor
    pave = P.S['pave']
    for x in range(X0, X1 + 1):
        for z in range(Z0, Z1 + 1):
            p.set(x, 0, z, rnd.choice(pave + ['gravel']) if 0 < z < Z1 and x > X0 else pave[0])
    for (x, z) in ((X0, Z0), (X0, Z1), (X1, Z0), (X1, Z1), (5, Z0), (5, Z1)):   # the posts
        for y in range(1, 4): p.set(x, y, z, log, axis='y')
    for x in range(6, X1):   # the back half walled at the sides
        for y in range(1, 4): p.set(x, y, Z0, planks); p.set(x, y, Z1, planks)
    for z in range(Z0 + 1, Z1):   # the back wall: brick round the hearth, planks either side
        for y in range(1, 4): p.set(X1, y, z, 'bricks' if 2 <= z <= 6 else planks)
    # the hearth: brick cheeks, the fire in its pit (our forge: charcoal burns there) behind iron bars, a stone hood over it
    p.set(7, 1, zc - 1, 'bricks'); p.set(7, 1, zc + 1, 'bricks')
    p.set(7, 1, zc, 'forge')
    p.set(7, 2, zc - 1, 'bricks'); p.set(7, 2, zc + 1, 'bricks'); p.set(7, 2, zc, 'iron_bars')
    for z in range(zc - 1, zc + 2): p.set(7, 3, z, P.S['stone_slab'], type='top')
    p.set(6, 1, zc + 2, 'bellows')
    p.set(7, 1, zc + 2, 'blast_furnace', facing='west')   # (the smith's own furnace: the workstation)
    for y in range(4, 8): p.set(X1, y, zc, (P.S.get('chimney') or 'stone_bricks'))   # the chimney
    p.set(X1, 8, zc, (P.S.get('chimcap') or 'stone_brick_wall'))
    # the anvil on a stump, the quench tub, the grindstone
    p.set(4, 1, zc, 'anvil', facing='north')
    p.set(4, 1, zc + 2, 'water_cauldron', level=3)
    p.set(6, 1, Z0 + 1, 'grindstone', face='floor', facing='west')
    p.set(2, 1, Z0 + 1, 'armor_stand', items=rnd.choice(vb.ARMOUR_SETS))
    p.set(3, 1, Z0 + 1, 'barrel', facing='up'); pile(p, 3, 2, Z0 + 1, ['iron_shovel', 'iron_hoe', 'stick'])
    p.set(X1 - 1, 1, Z1 - 1, 'chest', facing='west')
    p.set(X1 - 1, 2, Z0 + 1, furn(P, 'shelf'), facing='south', items='iron_ingot*3,iron_nugget*6,coal*4')
    p.set(2, 1, Z1 - 1, 'smithing_table'); p.set(2, 2, Z1 - 1, 'potted_' + rnd.choice(['red_tulip', 'dandelion', 'cornflower']))
    p.set(3, 1, Z1 - 1, 'coal_block'); pile(p, 3, 2, Z1 - 1, ['charcoal*8'])
    # the roof: plank slabs on beams, a lantern hanging at the front
    for x in range(X0 - 1, X1 + 2):
        for z in range(Z0 - 1, Z1 + 2): p.set(x, 4, z, vb.ROOFMAT.get(P.wood, (None, P.rslab))[1] if P.wood in vb.ROOFMAT else P.rslab, type='bottom')
    for z in range(Z0, Z1 + 1): p.set(X0, 4, z, log, axis='z')
    p.set(X1, 4, zc, (P.S.get('chimney') or 'stone_bricks'))
    chain_lantern(p, X0 + 1, 2, Z0 + 1, links=1)
    chain_lantern(p, X0 + 1, 2, Z1 - 1, links=1)
    for z in (Z0 - 1, Z1 + 1):   # green at its corners
        if not P.sandy and not P.snowy: bush(p, P, X0, z, rnd); bush(p, P, X0 + 1, z, rnd)
    entrance(p, style, zc, P.pathb())
    return p


# -------------------------------------------------------------------------------------------- the barn
def barn(style, name, seed):
    """a red barn: plank-trimmed corners, a broken-pitch roof, its great doors open on stalls of hay, the loft door above
    with hay spilling, bales stacked by the door, a cart, and a fenced yard with a crop patch and the cows"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'houses', style)
    wall = 'red_terracotta' if not P.snowy else 'red_wool'
    trim = 'white_terracotta' if not P.sandy else 'smooth_sandstone'
    X0, X1, Z0, Z1 = 2, 10, 0, 8
    zc = (Z0 + Z1) // 2
    for x in range(X0, X1 + 1):
        for z in range(Z0, Z1 + 1): p.set(x, 0, z, 'cobblestone' if x in (X0, X1) or z in (Z0, Z1) else 'coarse_dirt')
    H = 5
    for x in range(X0, X1 + 1):
        for z in range(Z0, Z1 + 1):
            if not (x in (X0, X1) or z in (Z0, Z1)): continue
            corner = x in (X0, X1) and z in (Z0, Z1)
            for y in range(1, H + 1): p.set(x, y, z, trim if corner else wall)
    for z in range(Z0, Z1 + 1): p.set(X0, H, z, trim); p.set(X1, H, z, trim)
    for z in range(zc - 1, zc + 2):   # the great door, open
        for y in range(1, 4): p.clear(X0, y, z)
    p.set(X0, 4, zc - 1, trim); p.set(X0, 4, zc + 1, trim)
    p.set(X0 - 1, 1, zc - 2, P.gate, facing='west', open=True); p.set(X0 - 1, 1, zc + 2, P.gate, facing='west', open=True)
    # the broken-pitch (gambrel) roof along x: steep at the eaves, flatter over the loft
    prof = [(-1, H), (0, H + 1), (1, H + 2), (2, H + 3), (3, H + 3), (4, H + 4)]
    rs, rsl, rf = P.rstairs, P.rslab, P.rfull
    for x in range(X0 - 1, X1 + 2):
        for (dz, y) in prof:
            for (z, f) in ((Z0 + dz, 'south'), (Z1 - dz, 'north')):
                if dz == 4: p.set(x, y, z, rsl, type='bottom'); continue
                p.set(x, y, z, rs, facing=f, half='bottom')
                if dz in (1, 2) and x in (X0 - 1, X1 + 1): p.set(x, y - 1, z, rf)   # (the eave's ends closed)
    for x in (X0, X1):   # the gable ends, walled
        for z in range(Z0, Z1 + 1):
            top = max(y for (dz, y) in prof if Z0 + dz == z or Z1 - dz == z) if any(Z0 + dz == z or Z1 - dz == z for (dz, _) in prof) else H
            for y in range(H + 1, top): p.setif(x, y, z, wall)
    # the loft door, open, hay spilling out; a lantern on a chain over it
    p.clear(X0, H + 1, zc); p.clear(X0, H + 2, zc)
    p.set(X0 - 1, H + 1, zc - 1, P.trap, facing='north', open=True, half='bottom'); p.set(X0 - 1, H + 1, zc + 1, P.trap, facing='south', open=True, half='bottom')
    p.set(X0 + 1, H + 1, zc, 'hay_block', axis='y')
    for z in range(Z0 + 1, Z1):
        for x in range(X0 + 1, X1): p.set(x, H, z, P.floor)   # the loft floor
    for z in range(zc - 1, zc + 2): p.clear(X0 + 1, H, z)
    p.set(X0 + 2, H + 1, zc - 1, 'hay_block', axis='x'); p.set(X0 + 3, H + 1, zc + 1, 'hay_block', axis='y')
    p.set(X0 - 1, H + 3, zc, 'lantern', hanging=True)
    # inside: stalls of hay, a trough, a ladder to the loft
    for x in range(X0 + 2, X1, 3):
        for z in (Z0 + 1, Z1 - 1): p.set(x, 1, z, P.fence)
    p.set(X1 - 1, 1, Z0 + 1, 'hay_block', axis='y'); p.set(X1 - 1, 1, Z1 - 1, 'feeding_trough', items='wheat*%d' % rnd.randint(14, 30))
    for y in range(1, H): p.set(X1 - 1, y, zc, 'ladder', facing='west')
    p.set(X0 + 3, 4, zc, 'lantern', hanging=True)
    # outside the door: bales stacked, a barrel, the cart
    p.set(0, 1, Z0 + 1, 'hay_block', axis='x'); p.set(0, 1, Z0 + 2, 'hay_block', axis='z'); p.set(0, 2, Z0 + 1, 'hay_block', axis='y')
    p.set(1, 1, Z1 - 1, 'barrel', facing='up')
    # the silo: brick and stone in bands, a copper cap
    sx, sz = X1 + 2, Z0 + 1
    for y in range(1, H + 4): p.set(sx, y, sz, 'bricks' if (y // 2) % 2 else P.S['pave'][0])
    p.set(sx, H + 4, sz, 'cut_copper' if vb.known('cut_copper') else 'bricks')
    # the yard: a fence round a crop patch and the cows' pen, along the barn's south side
    yz0, yz1 = Z1 + 2, Z1 + 7
    for x in range(X0, X1 + 3):
        for z in range(yz0, yz1 + 1):
            p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
            if x in (X0, X1 + 2) or z in (yz0, yz1): p.set(x, 1, z, P.fence)
    p.set(X0, 1, yz0 + 2, P.gate, facing='east')
    for x in range(X0 + 1, X0 + 4):
        for z in range(yz0 + 1, yz1):
            p.set(x, 0, z, 'farmland', moisture=7); p.set(x, 1, z, P.S['crops'][0] if P.S['crops'] else 'wheat', age=rnd.randint(4, 7))
    for (x, z) in ((X0, yz0), (X1 + 2, yz1)): p.set(x, 2, z, 'lantern')
    for (x, z) in ((X1 - 2, yz0 + 2), (X1, yz1 - 2)):
        p.joint(x, 1, z, 'up_north', 'minecraft:bottom', 'minecraft:bottom', 'minecraft:village/common/animals', 'minecraft:air')
    p.set(X1 + 1, 1, yz0 + 1, 'feeding_trough', items='wheat*%d' % rnd.randint(14, 30))
    entrance(p, style, zc, P.pathb())
    for z in range(zc - 1, zc + 2): p.set(1, 0, z, P.pathb())
    return p


# -------------------------------------------------------------------------------------------- the hay wagon, the bridge
def hay_wagon(p, P, x0, z0, rnd):
    """a wagon along x: a plank bed on trapdoor wheels, its sides of slabs, bales and a sheaf in it, the shaft forward"""
    for x in range(x0, x0 + 4):
        p.set(x, 1, z0, P.floor); p.set(x, 1, z0 + 1, P.floor)
    for x in (x0, x0 + 3):   # the wheels, standing open trapdoors
        p.set(x, 1, z0 - 1, P.trap, facing='north', open=True, half='bottom'); p.set(x, 1, z0 + 2, P.trap, facing='south', open=True, half='bottom')
    p.set(x0, 2, z0, 'hay_block', axis='x'); p.set(x0 + 1, 2, z0, 'hay_block', axis='x'); p.set(x0 + 1, 2, z0 + 1, 'hay_block', axis='z')
    p.set(x0, 3, z0, 'hay_block', axis='y')
    pile(p, x0 + 2, 2, z0 + 1, ['wheat*12'])
    p.set(x0 + 3, 2, z0, P.fence); p.set(x0 + 3, 2, z0 + 1, P.fence)
    p.set(x0 + 4, 1, z0, P.fence); p.set(x0 + 4, 1, z0 + 1, P.fence)   # the shaft


def garden_bridge(style, name, seed):
    """a little brook crossed by a plank bridge with rails; stone steps up to it, a lantern post at its head; lily pads,
    flowers and bushes along the banks; and a hay wagon left by the far bank"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'houses', style)
    W, D = 15, 9
    for x in range(1, W):
        for z in range(0, D): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    # the brook runs across the piece (along z) at x 5..7
    # (a stream of its own: it rises from a mossy spring at one end and runs into a reed pool at the other, closed in by its
    #  banks, so none of it spills out over the land and the fields beside)
    for x in range(4, 9):
        for z in range(0, D): p.set(x, -1, z, 'stone')   # (its bed)
    for x in range(5, 8):
        for z in range(1, D - 1):
            p.set(x, 0, z, 'water', level=0)
            if rnd.random() < 0.18 and x == 6 and z not in (3, 4, 5): p.set(x, 1, z, 'lily_pad')
    for x in range(4, 9):
        p.set(x, 0, 0, rnd.choice(['mossy_cobblestone', 'mossy_stone_bricks', 'cobblestone']))
        p.set(x, 0, D - 1, rnd.choice(['mossy_cobblestone', 'gravel', 'clay' if vb.known('clay') else 'gravel']))
    for z in range(1, D - 1):
        for x in (4, 8):
            if z not in (3, 4, 5): p.set(x, 0, z, rnd.choice(['mossy_cobblestone', 'gravel', 'coarse_dirt']))
    p.set(6, 1, 0, 'mossy_cobblestone'); p.set(5, 1, 0, 'mossy_stone_bricks'); p.set(7, 1, 0, 'fern')   # the spring's rocks
    for x in (5, 7): p.set(x, 1, D - 1, 'sugar_cane' if vb.known('sugar_cane') else 'fern')
    # the bridge: across x 4..8 at z 3..5, a step up each end, rails along its sides
    for x in range(4, 9):
        for z in range(3, 6): p.set(x, 1, z, P.floor)
        p.set(x, 2, 3, P.fence); p.set(x, 2, 5, P.fence)
    for z in range(3, 6):
        p.set(3, 1, z, P.S['stone_stairs'], facing='east'); p.set(9, 1, z, P.S['stone_stairs'], facing='west')
        p.set(4, 0, z, P.S['pave'][0]); p.set(8, 0, z, P.S['pave'][0])
    for x in (4, 8):
        p.set(x, 2, 3, P.S['wall']); p.set(x, 2, 5, P.S['wall'])
    lamp(p, P, 3, 2)
    # banks: flowers and bushes
    for (x, z) in ((2, 0), (2, 7), (10, 1), (10, 7), (9, 8), (1, 6)):
        if P.sandy: p.set(x, 1, z, 'dead_bush'); continue
        if rnd.random() < 0.5: bush(p, P, x, z, rnd, flowering=rnd.random() < 0.4)
        else: plant_at(p, P, x, 1, z)
    for (x, z) in ((2, 1), (3, 7), (10, 0), (9, 7)):
        if not P.sandy: p.set(x, 1, z, P.plant())
    hay_wagon(p, P, 10, 6, rnd)   # (left on the far bank)
    for z in range(3, 6): p.set(1, 0, z, P.pathb()); p.set(2, 0, z, P.pathb())
    entrance(p, style, 4, P.pathb())
    return p


# -------------------------------------------------------------------------------------------- the stalls, enriched
def stall_dress(p, P, x0, z0, rnd, w=5, produce=None):
    """a stall's lanterns hung from its cloth, potted plants at the counter's ends, crates of produce beside it"""
    for z in (z0 + 1, z0 + w - 2): p.set(x0 + 2, 2, z, 'lantern', hanging=True)
    for z in (z0, z0 + w - 1):
        if p.get(x0 + 1, 1, z) is None: p.set(x0 + 1, 1, z, 'potted_' + rnd.choice(['fern', 'red_tulip', 'oxeye_daisy', 'cornflower']))
    if produce:
        for k, dz in enumerate((w, w + 1)):
            z = z0 + dz
            if p.get(x0 + 1, 1, z) is None:
                p.set(x0 + 1, 1, z, 'barrel', facing='up'); pile(p, x0 + 1, 2, z, [produce[k % len(produce)] + '*8'])


# -------------------------------------------------------------------------------------------- the town's own corners
class Turned:
    """a piece drawn with x and z swapped (a layout worked out facing north, set down facing the street to the west)"""
    SWAP = {'north': 'west', 'west': 'north', 'south': 'east', 'east': 'south'}
    def __init__(self, p): self.p = p
    def _props(self, kw):
        if 'facing' in kw: kw['facing'] = self.SWAP.get(kw['facing'], kw['facing'])
        if 'axis' in kw: kw['axis'] = {'x': 'z', 'z': 'x'}.get(kw['axis'], kw['axis'])
        return kw
    def set(self, x, y, z, name, **kw): self.p.set(z, y, x, name, **self._props(kw))
    def setif(self, x, y, z, name, **kw): self.p.setif(z, y, x, name, **self._props(kw))
    def get(self, x, y, z): return self.p.get(z, y, x)
    def clear(self, x, y, z): self.p.clear(z, y, x)


def street_end(p, z, path):
    """where the street comes in from the west (a terminator's joint)"""
    p.set(0, 0, z, path)
    p.joint(0, 1, z, 'west_up', 'minecraft:street', 'minecraft:street', 'minecraft:empty')

def pergola_bench(style, name, seed, autumn=False):
    """a bench under a pergola: two log posts and beams over, leaves (flowering, or autumn's orange and gold) climbing it;
    a lantern on a chain from the middle beam, another on a crook post; a barrel each side, one with a pot of flowers on
    it; a path of stepping stones in the grass before it; pumpkins and fallen leaves in autumn"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    piece = Piece(name, 'terminators', style)
    p = Turned(piece)   # (laid out with the bench at the back facing north; set down facing the street)
    for x in range(0, 7):
        for z in range(0, 7): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    lv = ('orange_leaves' if vb.known('orange_leaves') else 'acacia_leaves') if autumn else ('flowering_azalea_leaves' if vb.known('flowering_azalea_leaves') else P.leaves)
    lv2 = ('yellow_leaves' if vb.known('yellow_leaves') else 'birch_leaves') if autumn else P.leaves
    log = P.S['log']
    bz = 5   # the bench along x at z 5, its back to the south, facing north to the path
    for x in (1, 5):
        for y in range(1, 5): p.set(x, y, bz, log, axis='y')
    for x in range(0, 7): p.set(x, 5, bz, log, axis='x')
    for x in range(1, 6, 2): p.set(x, 5, bz - 1, P.fence)
    for x in range(2, 5): p.set(x, 1, bz, furn(P, 'bench'), facing='north')
    p.set(1, 1, bz - 1, 'barrel', facing='up'); p.set(1, 2, bz - 1, 'potted_' + rnd.choice(['blue_orchid', 'poppy', 'allium', 'cornflower']))
    if autumn: p.set(5, 1, bz - 1, 'pumpkin', facing='north')
    else: p.set(5, 1, bz - 1, 'composter')
    p.set(3, 4, bz, 'lantern', hanging=True)
    for (x, y) in ((0, 4), (0, 5), (6, 5), (6, 4), (6, 3), (1, 6), (2, 6), (5, 6)): leaves(p, x, y, bz, rnd.choice([lv, lv2]))
    for y in range(1, 4):
        if rnd.random() < 0.7: leaves(p, 0, y, bz, lv)
    leaves(p, 6, 1, bz, lv2); leaves(p, 6, 2, bz, lv)
    # a crook post with a lantern by the bench's end
    for y in range(1, 4): p.set(6, y, bz - 2, P.S['wall'] if y == 1 else P.fence)
    p.set(6, 4, bz - 2, P.fence); p.set(5, 4, bz - 2, P.fence); p.set(5, 3, bz - 2, 'lantern', hanging=True)
    # stepping stones in the grass, a mossy edge
    for (x, z) in ((3, 3), (2, 2), (3, 1), (4, 2), (3, 0)): p.set(x, 0, z, rnd.choice(['dirt_path', P.S['pave'][0], 'coarse_dirt']))
    for (x, z) in ((2, 3), (4, 3)): p.set(x, 1, z, P.S['stone_slab'], type='bottom')
    if autumn:
        for (x, z) in ((0, 2), (6, 0), (1, 0)): p.set(x, 1, z, rnd.choice(['pumpkin', 'carved_pumpkin']), facing='north')
        for (x, z) in ((2, 4), (4, 4), (5, 3), (1, 2)): p.set(x, 1, z, 'leaf_litter' if vb.known('leaf_litter') else 'orange_carpet')
    else:
        for (x, z) in ((0, 1), (6, 1)): plant_at(p, P, x, 1, z)
    street_end(piece, 3, P.pathb())
    return piece


def lamp_post(style, name, seed):
    """the street's great lamp: a stone foot in a planter of grass and flowers, a log post, slab beams across its top with
    lanterns hung on chains from them; a clay pot of flowers beside"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'terminators', style)
    for x in range(0, 5):
        for z in range(0, 5): p.set(x, 0, z, P.S['ground'] if not P.snowy else 'grass_block')
    c = 2
    for x in range(1, 4):   # the planter: a ring of planks round grass
        for z in range(1, 4):
            ring = x != c or z != c
            p.set(x, 1, z, P.floor if ring and (x in (1, 3) or z in (1, 3)) else 'grass_block')
    p.set(c, 1, c, P.S['pave'][0])
    p.set(c, 2, c, P.S['wall'])
    for y in range(3, 7): p.set(c, y, c, P.S['log'] if y < 6 else P.frame2 if 'log' in P.frame2 else P.S['log'], axis='y')
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        p.set(c + dx, 7, c + dz, P.rslab, type='bottom')
        p.set(c + dx, 6, c + dz, 'lantern', hanging=True)
    p.set(c, 7, c, P.rfull)
    for (x, z) in ((1, 1), (3, 3)):
        if not P.sandy: p.set(x, 2, z, P.plant())
    p.set(3, 2, 1, 'potted_' + rnd.choice(['blue_orchid', 'cornflower', 'allium']))
    street_end(p, c, P.pathb())
    return p


# -------------------------------------------------------------------------------------------- the library's study
def study(p, P, R, rnd):
    """the scholar's corner of a library: the enchanting table on a rug, a desk with an open map, a book, a candle and a
    bottle; a shelf of potions over the bookcases; vines hanging from a beam"""
    spot = R.put('enchanting_table')
    if spot:
        x, z, _ = spot
        p.rug(x, R.y, z, rnd.choice(['purple_carpet', 'blue_carpet', 'red_carpet']))
    d = R.put(furn(P, 'table'))
    if d:
        x, z, _ = d
        pile(p, x, R.y + 2, z, ['filled_map' if vb.item_ok('filled_map') else 'paper', 'book', 'candle'])
    vb.wall_shelf(p, P, R.B, rnd, ['potion', 'glass_bottle', 'experience_bottle' if vb.item_ok('experience_bottle') else 'glass_bottle'])
    vb.wall_shelf(p, P, R.B, rnd, ['candle', 'book', 'potion'])


def all_pieces(style, s):
    out = [forge_shop(style, '%s_armorer_forge_1' % style, s('forge')),
           barn(style, '%s_barn_animal_pen_1' % style, s('barn')),
           garden_bridge(style, '%s_garden_bridge_1' % style, s('bridge')),
           pergola_bench(style, 'park_bench_01', s('park1')),
           pergola_bench(style, 'park_bench_02', s('park2'), autumn=True),
           lamp_post(style, 'lamp_post_01', s('lamp1'))]
    import village_builds2   # (the market and town pieces from the pictures: tools/port/village_builds2.py)
    out += village_builds2.all_pieces(style, s)
    return out
