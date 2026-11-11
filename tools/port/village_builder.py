#!/usr/bin/env python3
"""tools/port/village_builder.py — DokuQuest's own village buildings, generated here (idempotent; data_patches.py runs it):

  python3 tools/port/village_builder.py [data-dir]

Every house, workshop, farm, pen, street, crossroad and town square of the five village styles (plains, savanna, taiga,
snowy, desert) is built by the rules below and written as a Minecraft structure template (data/villages/<style>/<houses|
streets|town_centers|terminators>/<name>.nbt, listed in data/villages/index.json). src/gen/village_pieces.cpp reads them
and src/gen/structures_jigsaw_village.cpp puts a village together from them the jigsaw way. Nothing here is copied from
anyone's builds: the look of each style (what its walls, roofs and gardens are made of, how big its houses run, how its
streets join) was worked out by studying villages in general, and every building is drawn by these rules from a seed.

The conventions the village generator expects (gen/structures_jigsaw_village.cpp, gen/village_pieces.cpp):
  - a house faces west: its entrance joint (a jigsaw "minecraft:building_entrance", pool <style>/streets) stands at x 0,
    one above the floor, and the street it joins is at y 0 like the house's floor;
  - a street is 3 wide; joints "minecraft:street" (pool <style>/streets) at its ends and "minecraft:building_entrance"
    (pool <style>/houses) along both sides, one above the path;
  - a building's type and job come from its name (farm, animal_pen, stable, temple, armorer, weaponsmith, tool_smith,
    butcher, fletcher, shepherd, mason, cartographer, library, fisher, tannery, bakery, tavern) and its workstation;
    beds, chests and the square's bell are found by their blocks; a fence under a pressure plate is a table and a
    wooden stair at it a chair (they become our furniture);
  - blocks are Minecraft's names and states (village_pieces.cpp turns them into ours).
"""
import json, math, pathlib, random, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import nbt_io

H = 48   # every piece's height (room above it is cleared, as the generator does for the 48-tall pack pieces)
OPP = {'north': 'south', 'south': 'north', 'east': 'west', 'west': 'east'}
DXZ = {'north': (0, -1), 'south': (0, 1), 'west': (-1, 0), 'east': (1, 0)}


# ---------------------------------------------------------------------------------------------------- a piece
class Piece:
    def __init__(self, name, kind, style):
        self.name, self.kind, self.style = name, kind, style
        self.b = {}; self.jig = []
        self.pairs = []   # shutter pairs, put up last (template()), each pair whole or not at all

    def set(self, x, y, z, name, **p):
        if y < 0: return
        self.b[(x, y, z)] = (name, tuple(sorted((k, str(v).lower() if isinstance(v, bool) else str(v)) for k, v in p.items())))

    def setif(self, x, y, z, name, **p):   # only into an empty cell
        if (x, y, z) not in self.b: self.set(x, y, z, name, **p)

    def get(self, x, y, z): return self.b.get((x, y, z), (None,))[0]

    NO_RUG = ('stairs', 'slab', 'fence', 'wall', 'trapdoor', 'carpet', 'path', 'glass', 'leaves', 'door', 'farmland', 'snow', 'water', 'lava')

    def rug(self, x, y, z, carpet):
        """a rug laid on the floor block here (world/rug.h): a property of that block, so a table, chairs or a bed can stand
        on it in the cell above. Only on a whole floor block; False where there's none"""
        v = self.b.get((x, y, z))
        if v is None or any(t in v[0] for t in self.NO_RUG): return False
        self.b[(x, y, z)] = (v[0], tuple(sorted(dict(v[1], rug=carpet).items())))
        return True
    def clear(self, x, y, z): self.b.pop((x, y, z), None)

    def box(self, x0, y0, z0, x1, y1, z1, name, **p):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            for y in range(min(y0, y1), max(y0, y1) + 1):
                for z in range(min(z0, z1), max(z0, z1) + 1): self.set(x, y, z, name, **p)

    def joint(self, x, y, z, orient, name, target, pool, final='minecraft:structure_void'):
        self.jig.append((x, y, z, orient, name, target, pool, final))

    def template(self):
        for (a, b2, wa, wb, name, facing) in self.pairs:   # the shutters, now that everything else is built
            if all(c not in self.b for c in (a, b2)) and all(self.b.get(w, (None,))[0] not in (None, 'glass_pane') and 'door' not in self.b.get(w, ('',))[0] for w in (wa, wb)):
                for c in (a, b2): self.set(*c, name, facing=facing, half='bottom', open=True)
        self.pairs = []
        for c in [c for c in self.b if c[0] < 0 or c[2] < 0]: del self.b[c]   # (a tree's or a palm's leaves past the piece's edge)
        cells = list(self.b.keys()) + [(j[0], j[1], j[2]) for j in self.jig]
        mx = min(c[0] for c in cells); mz = min(c[2] for c in cells)
        assert mx >= 0 and mz >= 0, (self.name, mx, mz)
        sx = max(c[0] for c in cells) + 1; sz = max(c[2] for c in cells) + 1
        pal, idx, blocks = [], {}, []
        def state(name, props):
            k = (name, props)
            if k not in idx:
                e = {'Name': name if ':' in name else 'minecraft:' + name}
                if props: e['Properties'] = dict(props)
                idx[k] = len(pal); pal.append(e)
            return idx[k]
        for (x, y, z), (n, pr) in sorted(self.b.items(), key=lambda t: (t[0][1], t[0][2], t[0][0])):
            blocks.append({'pos': [x, y, z], 'state': state(n, pr)})
        for (x, y, z, o, nm, tg, pool, fin) in self.jig:
            self.b.pop((x, y, z), None)
            blocks.append({'pos': [x, y, z], 'state': state('jigsaw', (('orientation', o),)),
                           'nbt': {'id': 'minecraft:jigsaw', 'name': nm, 'target': tg, 'pool': pool, 'final_state': fin, 'joint': 'rollable'}})
        return {'DataVersion': 3465, 'size': [sx, H, sz], 'palette': pal, 'blocks': blocks, 'entities': []}


def stairs(name, facing, half='bottom'): return (name, {'facing': facing, 'half': half})


# ---------------------------------------------------------------------------------------------------- the styles
STYLES = {
    'plains': dict(found=['stone_bricks', 'cobblestone', 'stone_bricks'], frame='oak_log', frame2='stripped_oak_log',
                   fill_lo=['stone_bricks', 'cobblestone', 'white_terracotta', 'oak_planks'], fill_hi=['oak_planks', 'white_terracotta', 'spruce_planks'],
                   floor='oak_planks', roof=['oak', 'spruce', 'dark_oak'], roof_kind='gable', gable='oak_planks', wood='oak', door='oak_door',
                   trap='spruce_trapdoor', fence='oak_fence', gate='oak_fence_gate', glass='glass_pane', path=['dirt_path'] * 6 + ['coarse_dirt', 'gravel'],
                   ground='grass_block', leaves='oak_leaves', chimney='stone_bricks', chimcap='stone_brick_wall', bed=['red_bed', 'white_bed', 'blue_bed', 'yellow_bed'],
                   carpet=['red_carpet', 'brown_carpet', 'green_carpet'], plants=['poppy', 'dandelion', 'cornflower', 'oxeye_daisy', 'azure_bluet', 'red_tulip', 'allium'],
                   pave=['stone_bricks', 'cobblestone', 'polished_andesite', 'stone_bricks'], stone_stairs='stone_brick_stairs', stone_slab='stone_brick_slab',
                   wall='stone_brick_wall', crops=['wheat', 'wheat', 'carrots', 'potatoes', 'beetroots'], banner='red', log='oak_log', squares=['well', 'market', 'tree']),
    'savanna': dict(found=['smooth_stone', 'cobblestone'], frame='acacia_log', frame2='stripped_acacia_log',
                    fill_lo=['yellow_terracotta', 'red_terracotta', 'orange_terracotta', 'terracotta'], fill_hi=['yellow_terracotta', 'orange_terracotta', 'acacia_planks'],
                    floor='acacia_planks', roof=['acacia'], roof_kind='hip', gable='acacia_planks', wood='acacia', door='acacia_door',
                    trap='acacia_trapdoor', fence='acacia_fence', gate='acacia_fence_gate', glass='glass_pane', path=['dirt_path'] * 6 + ['coarse_dirt', 'red_sand'],
                    ground='grass_block', leaves='acacia_leaves', chimney='cobblestone', chimcap='cobblestone_wall', bed=['orange_bed', 'yellow_bed', 'red_bed', 'brown_bed'],
                    carpet=['orange_carpet', 'yellow_carpet', 'brown_carpet'], plants=['dandelion', 'orange_tulip', 'poppy', 'allium', 'oxeye_daisy'],
                    pave=['smooth_stone', 'cobblestone', 'smooth_stone', 'polished_andesite'], stone_stairs='cobblestone_stairs', stone_slab='smooth_stone_slab',
                    wall='cobblestone_wall', crops=['wheat', 'wheat', 'carrots', 'beetroots'], banner='orange', log='acacia_log', squares=['well', 'market', 'tree']),
    'taiga': dict(found=['cobblestone', 'mossy_cobblestone', 'cobblestone'], frame='spruce_log', frame2='stripped_spruce_log',
                  fill_lo=['spruce_planks', 'stripped_spruce_log', 'cobblestone'], fill_hi=['spruce_planks', 'stripped_spruce_log'],
                  floor='spruce_planks', roof=['spruce'], roof_kind='turf', gable='spruce_planks', wood='spruce', door='spruce_door',
                  trap='spruce_trapdoor', fence='spruce_fence', gate='spruce_fence_gate', glass='glass_pane', path=['dirt_path'] * 5 + ['coarse_dirt', 'podzol'],
                  ground='grass_block', leaves='spruce_leaves', chimney='cobblestone', chimcap='cobblestone_wall', bed=['brown_bed', 'green_bed', 'red_bed', 'gray_bed'],
                  carpet=['brown_carpet', 'green_carpet', 'red_carpet'], plants=['fern', 'fern', 'poppy', 'allium', 'sweet_berry_bush', 'dandelion'],
                  pave=['cobblestone', 'mossy_cobblestone', 'cobblestone', 'gravel'], stone_stairs='cobblestone_stairs', stone_slab='cobblestone_slab',
                  wall='cobblestone_wall', crops=['wheat', 'potatoes', 'carrots', 'wheat'], banner='green', log='spruce_log', squares=['well', 'market', 'tree']),
    'snowy': dict(found=['polished_diorite', 'cobblestone', 'polished_diorite'], frame='spruce_log', frame2='stripped_spruce_log',
                  fill_lo=['dark_oak_planks', 'spruce_planks', 'polished_diorite'], fill_hi=['dark_oak_planks', 'spruce_planks'],
                  floor='spruce_planks', roof=['dark_oak'], roof_kind='aframe', gable='spruce_planks', wood='dark_oak', door='dark_oak_door',
                  trap='spruce_trapdoor', fence='spruce_fence', gate='spruce_fence_gate', glass='glass_pane', path=['dirt_path'] * 5 + ['gravel', 'snow_block'],
                  ground='snow_block', leaves='spruce_leaves', chimney='cobblestone', chimcap='cobblestone_wall', bed=['light_blue_bed', 'white_bed', 'blue_bed', 'cyan_bed'],
                  carpet=['white_carpet', 'light_blue_carpet', 'blue_carpet'], plants=['fern', 'sweet_berry_bush', 'fern'],
                  pave=['polished_diorite', 'cobblestone', 'polished_diorite', 'snow_block'], stone_stairs='polished_diorite_stairs', stone_slab='polished_diorite_slab',
                  wall='cobblestone_wall', crops=['potatoes', 'wheat', 'beetroots'], banner='blue', log='spruce_log', squares=['well', 'market', 'tree'], boxes=False),
    'desert': dict(found=['cut_sandstone', 'smooth_sandstone'], frame='cut_sandstone', frame2='chiseled_sandstone',
                   fill_lo=['sandstone', 'smooth_sandstone', 'sandstone'], fill_hi=['sandstone', 'smooth_sandstone'],
                   floor='smooth_sandstone', roof=['sandstone'], roof_kind='flat', gable='sandstone', wood='birch', door='birch_door',
                   trap='birch_trapdoor', fence='birch_fence', gate='birch_fence_gate', glass='birch_fence', path=['smooth_sandstone', 'smooth_sandstone', 'sandstone', 'cut_sandstone', 'sand'],
                   ground='sand', leaves='jungle_leaves', chimney='cut_sandstone', chimcap='sandstone_wall', bed=['orange_bed', 'yellow_bed', 'cyan_bed', 'red_bed'],
                   carpet=['orange_carpet', 'yellow_carpet', 'cyan_carpet', 'red_carpet'], plants=['dead_bush', 'cactus_small', 'orange_tulip'],
                   pave=['smooth_sandstone', 'cut_sandstone', 'smooth_sandstone', 'sandstone'], stone_stairs='smooth_sandstone_stairs', stone_slab='cut_sandstone_slab',
                   wall='sandstone_wall', crops=['wheat', 'beetroots', 'carrots'], banner='yellow', log='jungle_log', squares=['well', 'market', 'oasis']),
}


ROOFMAT = {   # a roof's stairs, slab and full block
    'oak': ('oak_stairs', 'oak_slab', 'oak_planks'), 'spruce': ('spruce_stairs', 'spruce_slab', 'spruce_planks'),
    'dark_oak': ('dark_oak_stairs', 'dark_oak_slab', 'dark_oak_planks'), 'acacia': ('acacia_stairs', 'acacia_slab', 'acacia_planks'),
    'birch': ('birch_stairs', 'birch_slab', 'birch_planks'), 'jungle': ('jungle_stairs', 'jungle_slab', 'jungle_planks'),
    'mangrove': ('mangrove_stairs', 'mangrove_slab', 'mangrove_planks'), 'sandstone': ('sandstone_stairs', 'sandstone_slab', 'cut_sandstone'),
    'red_sandstone': ('red_sandstone_stairs', 'cut_red_sandstone_slab', 'smooth_red_sandstone'), 'thatch': ('thatch_stairs', 'thatch_slab', 'thatch'),
    'slate_tile': ('slate_tile_stairs', 'slate_tile_slab', 'stone_bricks'), 'red_mushroom': ('red_mushroom_block',) * 3, 'brown_mushroom': ('brown_mushroom_block',) * 3,
    'snow': ('snow_block',) * 3,
}


def _style(base, **kw):
    s = dict(STYLES[base]); s.update(kw); s['base'] = base; return s


for _k in list(STYLES): STYLES[_k]['base'] = _k
STYLES.update({
    # the beach's: light birch houses on stilts above the sand, thatched, open to the sea breeze
    'beach': _style('plains', found=['stripped_birch_log'], frame='stripped_birch_log', frame2='stripped_birch_log', fill_lo=['white_terracotta', 'birch_planks'],
                    fill_hi=['birch_planks', 'white_terracotta'], floor='birch_planks', roof=['thatch'], roof_kind='hip', gable='birch_planks', wood='birch',
                    door='birch_door', trap='birch_trapdoor', fence='birch_fence', gate='birch_fence_gate', path=['sand', 'sand', 'smooth_sandstone', 'dirt_path'],
                    ground='sand', leaves='jungle_leaves', chimney=None, bed=['light_blue_bed', 'white_bed', 'cyan_bed', 'yellow_bed'],
                    carpet=['light_blue_carpet', 'white_carpet', 'cyan_carpet'], plants=['dead_bush', 'fern', 'dandelion'],
                    pave=['smooth_sandstone', 'birch_planks', 'sandstone', 'cut_sandstone'], stone_stairs='smooth_sandstone_stairs', stone_slab='smooth_sandstone_slab',
                    wall='sandstone_wall', banner='light_blue', raised=2, decor='beach', log='stripped_birch_log', squares=['well', 'market', 'palms']),
    # the jungle's: jungle wood on stilts, steep thatch heaped with leaves, vines down the walls
    'jungle': _style('plains', found=['mossy_cobblestone', 'cobblestone'], frame='jungle_log', frame2='stripped_jungle_log', fill_lo=['jungle_planks', 'bamboo_block'],
                     fill_hi=['jungle_planks', 'stripped_jungle_log'], floor='jungle_planks', roof=['thatch', 'jungle'], roof_kind='gable', gable='jungle_planks',
                     wood='jungle', door='jungle_door', trap='jungle_trapdoor', fence='jungle_fence', gate='jungle_fence_gate', path=['dirt_path'] * 4 + ['coarse_dirt', 'mossy_cobblestone'],
                     leaves='jungle_leaves', chimney=None, bed=['green_bed', 'lime_bed', 'yellow_bed', 'brown_bed'], carpet=['green_carpet', 'lime_carpet', 'yellow_carpet'],
                     plants=['fern', 'fern', 'poppy', 'dandelion', 'blue_orchid'], pave=['mossy_cobblestone', 'cobblestone', 'jungle_planks', 'dirt_path'],
                     stone_stairs='mossy_cobblestone_stairs', stone_slab='cobblestone_slab', wall='mossy_cobblestone_wall', banner='lime', raised=2, decor='jungle',
                     log='jungle_log', squares=['well', 'market', 'tree']),
    # the swamp's: dark wood and mud brick on stilts, mossy roofs, vines, lily pads on the pools
    'swamp': _style('plains', found=['mossy_cobblestone', 'mud_bricks'], frame='dark_oak_log', frame2='stripped_dark_oak_log', fill_lo=['mud_bricks', 'dark_oak_planks', 'mangrove_planks'],
                    fill_hi=['dark_oak_planks', 'mangrove_planks'], floor='mangrove_planks', roof=['dark_oak', 'mangrove'], roof_kind='gable', gable='mangrove_planks',
                    wood='mangrove', door='mangrove_door', trap='mangrove_trapdoor', fence='mangrove_fence', gate='mangrove_fence_gate',
                    path=['dirt_path', 'dirt_path', 'packed_mud', 'mud_bricks', 'coarse_dirt'], leaves='mangrove_leaves', chimney='mud_bricks', chimcap='cobblestone_wall',
                    bed=['brown_bed', 'green_bed', 'gray_bed', 'black_bed'], carpet=['moss_carpet', 'brown_carpet', 'green_carpet'], plants=['blue_orchid', 'fern', 'brown_mushroom', 'red_mushroom'],
                    pave=['mud_bricks', 'packed_mud', 'mossy_cobblestone', 'dirt_path'], stone_stairs='mud_brick_stairs', stone_slab='mud_brick_slab', wall='mossy_cobblestone_wall',
                    crops=['potatoes', 'beetroots', 'wheat'], banner='green', raised=2, decor='swamp', log='dark_oak_log', squares=['well', 'market', 'tree']),
    # the mountains': stone below, spruce above, tiered roofs of slate with deep eaves
    'mountain': _style('taiga', found=['stone_bricks', 'cobblestone'], frame='spruce_log', frame2='stripped_spruce_log', fill_lo=['stone_bricks', 'cobblestone', 'andesite'],
                       fill_hi=['spruce_planks', 'stripped_spruce_log', 'white_terracotta'], roof=['slate_tile'], roof_kind='pagoda', chimney='stone_bricks', chimcap='stone_brick_wall',
                       bed=['red_bed', 'gray_bed', 'white_bed'], carpet=['red_carpet', 'gray_carpet', 'white_carpet'], plants=['allium', 'poppy', 'fern', 'cornflower'],
                       pave=['stone_bricks', 'andesite', 'cobblestone', 'polished_andesite'], stone_stairs='stone_brick_stairs', stone_slab='stone_brick_slab', wall='stone_brick_wall',
                       banner='red', squares=['well', 'market', 'tree']),
    # the alpine chalet: a stone ground floor, white plaster and dark timber above, a balcony, a wide-eaved roof
    'alpine': _style('taiga', found=['cobblestone', 'stone_bricks'], frame='dark_oak_log', frame2='stripped_dark_oak_log', fill_lo=['stone_bricks', 'cobblestone'],
                     fill_hi=['white_terracotta', 'white_terracotta', 'spruce_planks'], roof=['dark_oak', 'spruce'], roof_kind='gable', over=2, balcony=True,
                     chimney='cobblestone', bed=['red_bed', 'white_bed', 'green_bed'], carpet=['red_carpet', 'white_carpet', 'green_carpet'],
                     plants=['allium', 'cornflower', 'azure_bluet', 'oxeye_daisy', 'poppy'], pave=['cobblestone', 'stone_bricks', 'gravel', 'cobblestone'], banner='red',
                     squares=['well', 'market', 'tree']),
    # the mesa's adobe: banded terracotta, flat roofs on beams whose ends stand out of the walls
    'mesa': _style('desert', found=['terracotta', 'cut_red_sandstone'], frame='cut_red_sandstone', frame2='smooth_red_sandstone',
                   fill_lo=['orange_terracotta', 'terracotta', 'white_terracotta'], fill_hi=['orange_terracotta', 'brown_terracotta', 'terracotta'], floor='smooth_red_sandstone',
                   roof=['red_sandstone'], gable='terracotta', wood='acacia', door='acacia_door', trap='acacia_trapdoor', fence='acacia_fence', gate='acacia_fence_gate',
                   glass='acacia_fence', path=['red_sand', 'smooth_red_sandstone', 'cut_red_sandstone', 'terracotta', 'coarse_dirt'], ground='red_sand', leaves='acacia_leaves',
                   bed=['orange_bed', 'red_bed', 'brown_bed', 'yellow_bed'], carpet=['orange_carpet', 'red_carpet', 'brown_carpet'], plants=['dead_bush', 'dead_bush', 'cactus_small'],
                   pave=['smooth_red_sandstone', 'cut_red_sandstone', 'red_sandstone', 'terracotta'], stone_stairs='red_sandstone_stairs', stone_slab='cut_red_sandstone_slab',
                   wall='red_sandstone_wall', banner='orange', decor='mesa', log='acacia_log', squares=['well', 'market', 'oasis'],
                   flatm=dict(deck='smooth_red_sandstone', trim='cut_red_sandstone', pier='chiseled_red_sandstone', pslab='cut_red_sandstone_slab', pwall='red_sandstone_wall')),
    # the desert oasis: the desert's houses round water, palms and tents
    'oasis': _style('desert', decor='oasis', squares=['oasis', 'market', 'oasis']),
    # the savanna's round huts of mud under cones of thatch
    'savanna_hut': _style('savanna', found=['packed_mud', 'terracotta'], fill_lo=['packed_mud', 'terracotta', 'mud_bricks'], fill_hi=['packed_mud', 'mud_bricks'],
                          floor='packed_mud', roof=['thatch'], roof_kind='cone', gable='packed_mud', round='hut', squares=['tree', 'market', 'well']),
    # the mushroom village: homes in giant mushrooms on mycelium, shroomlights glowing
    'mushroom': _style('plains', found=['mushroom_stem'], frame='mushroom_stem', frame2='mushroom_stem', fill_lo=['mushroom_stem'], fill_hi=['mushroom_stem'],
                       floor='spruce_planks', roof=['red_mushroom'], roof_kind='cap', gable='mushroom_stem', wood='spruce', door='spruce_door', trap='spruce_trapdoor',
                       fence='spruce_fence', gate='spruce_fence_gate', path=['dirt_path', 'dirt_path', 'coarse_dirt', 'mycelium'], ground='mycelium', leaves='red_mushroom_block',
                       chimney=None, bed=['red_bed', 'white_bed', 'brown_bed'], carpet=['red_carpet', 'white_carpet', 'brown_carpet'], plants=['red_mushroom', 'brown_mushroom'],
                       pave=['mycelium', 'coarse_dirt', 'dirt_path', 'mushroom_stem'], banner='red', round='mushroom', lamp='shroomlight', log='mushroom_stem',
                       squares=['mushroom', 'market', 'mushroom']),
    # the igloo village: domes of snow with ice windows and a tunnel in
    'igloo': _style('snowy', found=['snow_block'], frame='packed_ice', frame2='snow_block', fill_lo=['snow_block'], fill_hi=['snow_block'], floor='snow_block',
                    roof=['snow'], roof_kind='stepped', gable='snow_block', wood='spruce', door='spruce_door', glass='ice', round='igloo', carpet=['white_carpet', 'light_blue_carpet'],
                    squares=['well', 'market', 'tree']),
    # Christmas: a snowy town in red, green and white, lanterns along every eave, decorated trees and presents
    'christmas': _style('snowy', fill_lo=['dark_oak_planks', 'spruce_planks', 'red_terracotta'], fill_hi=['spruce_planks', 'white_terracotta'], roof_kind='gable',
                        roof=['dark_oak'], snowcap=True, chimney='bricks', bed=['red_bed', 'green_bed', 'white_bed'], carpet=['red_carpet', 'green_carpet', 'white_carpet'],
                        pave=['polished_diorite', 'stone_bricks', 'snow_block', 'cobblestone'], banner='red', decor='christmas', squares=['xmas', 'market', 'xmas'], boxes=True),
    # Halloween: dark timber, slate, jack o'lanterns, cobwebs, gravestones, soul lanterns
    'halloween': _style('plains', found=['cobblestone', 'mossy_cobblestone'], frame='dark_oak_log', frame2='stripped_dark_oak_log',
                        fill_lo=['dark_oak_planks', 'cobblestone', 'gray_terracotta'], fill_hi=['dark_oak_planks', 'black_terracotta'], floor='dark_oak_planks',
                        roof=['slate_tile', 'dark_oak'], roof_kind='gable', gable='dark_oak_planks', wood='dark_oak', door='dark_oak_door', trap='dark_oak_trapdoor',
                        fence='dark_oak_fence', gate='dark_oak_fence_gate', path=['dirt_path', 'coarse_dirt', 'gravel', 'dirt_path', 'podzol'], leaves='dark_oak_leaves',
                        chimney='cobblestone', chimcap='cobblestone_wall', bed=['black_bed', 'purple_bed', 'orange_bed', 'gray_bed'], carpet=['purple_carpet', 'orange_carpet', 'gray_carpet'],
                        plants=['dead_bush', 'brown_mushroom', 'red_mushroom', 'fern'], pave=['cobblestone', 'mossy_cobblestone', 'gravel', 'andesite'], banner='orange',
                        decor='halloween', lamp='soul_lantern', log='dark_oak_log', squares=['pumpkins', 'market', 'pumpkins']),
})
STYLES['jungle_tree'] = dict(STYLES['jungle'], raised=0, tree=True, base='plains')



class Pal:
    """one house's materials: its style's, picked by its seed"""
    def __init__(self, style, rnd):
        S = STYLES[style]; self.S = S; self.style = style; self.r = rnd
        self.found = rnd.choice(S['found']); self.frame = S['frame']; self.frame2 = S['frame2']
        self.fill_lo = rnd.choice(S['fill_lo']); self.fill_hi = rnd.choice(S['fill_hi'])
        if self.fill_hi == self.fill_lo and style == 'plains': self.fill_hi = 'oak_planks'
        self.floor = S['floor']; self.rw = rnd.choice(S['roof']); self.gable = S['gable']; self.wood = S['wood']
        self.door = S['door']; self.trap = S['trap']; self.fence = S['fence']; self.gate = S['gate']; self.glass = S['glass']
        self.leaves = S['leaves']; self.bed = rnd.choice(S['bed']); self.carpet = rnd.choice(S['carpet'])
        self.roof_kind = S['roof_kind']
        self.flat = self.roof_kind == 'flat'; self.sandy = S['ground'] in ('sand', 'red_sand'); self.snowy = S['ground'] == 'snow_block'
        self.raised = S.get('raised', 0); self.round = S.get('round'); self.tree = S.get('tree', False); self.decor = S.get('decor')
        self.over = S.get('over', 1); self.lamp = S.get('lamp', 'lantern')
        self.shutters = rnd.random() < 0.7   # (a house has shutters on every window, or on none)

    @property
    def rstairs(self): return ROOFMAT[self.rw][0]
    @property
    def rslab(self): return ROOFMAT[self.rw][1]
    @property
    def rfull(self): return ROOFMAT[self.rw][2]
    @property
    def slab_any(self): return self.S['stone_slab'] if self.flat or self.rslab == self.rfull else self.rslab   # (a porch's or a stall's slabs)
    def plant(self): return self.r.choice(self.S['plants'])
    def pathb(self): return self.r.choice(self.S['path'])
    def pave(self): return self.r.choice(self.S['pave'])


def leaves(p, x, y, z, name): p.set(x, y, z, name, persistent=True, distance=1)


def plant_at(p, P, x, y, z):
    n = P.plant()
    if n == 'cactus_small': p.set(x, y, z, 'dead_bush'); return
    if n == 'sweet_berry_bush': p.set(x, y, z, n, age=2); return
    p.set(x, y, z, n)


# ---------------------------------------------------------------------------------------------------- house parts
class Body:
    """a rectangular house body: x0..x1 (x0 the front wall, facing the street to the west), z0..z1, floors of 4"""
    def __init__(self, x0, z0, w, d, floors=1, fh=4, y0=0):
        self.x0, self.z0, self.x1, self.z1 = x0, z0, x0 + w - 1, z0 + d - 1
        self.floors, self.fh, self.y0 = floors, fh, y0
        self.top = y0 + fh * floors
        self.upper = None
        self.zd = z0 + d // 2   # the door, mid-front

    def perimeter(self):
        for z in range(self.z0, self.z1 + 1): yield (self.x0, z, 'west'); yield (self.x1, z, 'east')
        for x in range(self.x0 + 1, self.x1): yield (x, self.z0, 'north'); yield (x, self.z1, 'south')

    def inside(self, x, z): return self.x0 < x < self.x1 and self.z0 < z < self.z1


def walls(p, P, B, rnd, step=None, windows=True, sides=('west', 'east', 'north', 'south')):
    """foundation ring and floor, the frame (posts at the corners and every few blocks, a beam round each floor's top),
    the infill between, the windows with shutters and window boxes"""
    step = step or rnd.choice([3, 4])
    for x in range(B.x0, B.x1 + 1):
        for z in range(B.z0, B.z1 + 1):
            p.set(x, B.y0, z, P.found if not B.inside(x, z) else P.floor)
    door_cells = {(B.x0, B.zd)}
    for f in range(B.floors):
        yb = B.y0 + f * B.fh
        fill = P.fill_lo if f == 0 else P.fill_hi
        for (x, z, side) in B.perimeter():
            if side not in sides: continue
            t = (z - B.z0) if side in ('west', 'east') else (x - B.x0)
            n = (B.z1 - B.z0) if side in ('west', 'east') else (B.x1 - B.x0)
            corner = (x in (B.x0, B.x1)) and (z in (B.z0, B.z1))
            post = corner or t % step == 0 or t == n
            for y in range(yb + 1, yb + B.fh):
                if P.flat:
                    p.set(x, y, z, P.frame if corner else fill)
                else:
                    p.set(x, y, z, P.frame if post else fill, **({'axis': 'y'} if post and 'log' in P.frame else {}))
            # the beam round the top of the floor
            if P.flat:
                p.set(x, yb + B.fh, z, P.S.get('flatm', FLAT)['trim'] if f == B.floors - 1 else P.fill_hi)
            else:
                ax = 'y' if corner else ('z' if side in ('west', 'east') else 'x')
                p.set(x, yb + B.fh, z, P.frame2 if 'log' in P.frame2 else P.frame, **({'axis': ax} if 'log' in P.frame2 else {}))
        if f > 0:   # the floor above
            for x in range(B.x0 + 1, B.x1):
                for z in range(B.z0 + 1, B.z1): p.set(x, yb, z, P.floor)
        if windows: window_row(p, P, B, rnd, yb, step, sides, door_cells)


def window_row(p, P, B, rnd, yb, step, sides, door_cells):
    """windows in the middle of each panel between the posts (both cells of a 3-panel, the middle one of a 4-panel; the
    flat-roofed styles' every other block); then shutters, a matching pair either side of each window (or of each run of
    windows side by side): on every window of the house or none, and only where both sides have wall to lie on"""
    runs = {}
    for (x, z, side) in B.perimeter():
        if side not in sides: continue
        if (x in (B.x0, B.x1)) and (z in (B.z0, B.z1)): continue
        t = (z - B.z0) if side in ('west', 'east') else (x - B.x0)
        n = (B.z1 - B.z0) if side in ('west', 'east') else (B.x1 - B.x0)
        if side == 'west' and yb == B.y0 and abs(z - B.zd) <= 1: continue   # the door and beside it
        if P.flat:
            if t % 2 == 1 or t in (0, n): continue
        else:
            m = t % step
            if m == 0 or t >= n: continue
            if step == 4 and m != 2: continue
        p.set(x, yb + 2, z, P.glass)
        runs.setdefault(side, []).append(t)
    if P.flat or not P.S.get('boxes', True) or not P.shutters: return
    y = yb + 2
    for side, ts in runs.items():
        ts.sort()
        groups = []
        for t in ts:
            if groups and t == groups[-1][-1] + 1: groups[-1].append(t)
            else: groups.append([t])
        ox, oz = DXZ[side]
        def cell(t):   # the wall cell at t on this side, and the cell just outside it
            if side in ('west', 'east'): wx, wz = (B.x0 if side == 'west' else B.x1), B.z0 + t
            else: wx, wz = B.x0 + t, (B.z0 if side == 'north' else B.z1)
            return (wx, wz), (wx + ox, wz + oz)
        for g in groups:
            (w0, o0), (w1, o1) = cell(g[0] - 1), cell(g[-1] + 1)
            p.pairs.append(((o0[0], y, o0[1]), (o1[0], y, o1[1]), (w0[0], y, w0[1]), (w1[0], y, w1[1]), P.trap, side))


def door(p, P, x, z, facing='east', y=0):
    """a door in the west wall at (x, y + 1, z): it opens in from the street"""
    hinge = 'left'
    p.set(x, y + 1, z, P.door, facing=facing, half='lower', hinge=hinge, open=False)
    p.set(x, y + 2, z, P.door, facing=facing, half='upper', hinge=hinge, open=False)


def entrance(p, P, B, yard_x0=0, porch=False, rnd=None):
    """the path from the piece's west edge to the door, the entrance joint at its start; a porch roof on posts"""
    door(p, P, B.x0, B.zd, y=B.y0)
    if B.y0 > 0: stilts(p, P, B); porch = False
    for x in range(yard_x0, B.x0 - (1 + B.y0 if B.y0 else 0)):
        p.set(x, 0, B.zd, P.pathb())
    p.joint(yard_x0, 1, B.zd, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % P.style)
    # a lantern by the door
    lx = B.x0 - 1
    if rnd and rnd.random() < 0.8 and B.zd + 1 <= B.z1 and not porch:
        p.set(lx, 2, B.zd + 1, 'wall_torch', facing='west')
    if porch and B.x0 - 2 >= yard_x0:
        for z in (B.zd - 1, B.zd + 1):
            p.box(B.x0 - 2, 1, z, B.x0 - 2, 3, z, P.fence)
        for z in range(B.zd - 1, B.zd + 2):
            for x in (B.x0 - 2, B.x0 - 1): p.set(x, 4, z, P.slab_any, type='bottom')
        p.set(B.x0 - 2, 3, B.zd, 'lantern', hanging=True)   # at the porch's front edge, above head height


# ---------------------------------------------------------------------------------------------------- roofs
def roof_gable(p, P, B, base, along='x', over=1, ridge_full=False):
    """stairs up from both long sides to a ridge along x, overhanging; the gable ends filled under it"""
    lo, hi = B.z0 - over, B.z1 + over
    xs = range(B.x0 - over, B.x1 + over + 1)
    k = 0
    while lo + k <= hi - k:
        y = base + k; za, zb = lo + k, hi - k
        for x in xs:
            if za == zb:
                p.set(x, y, za, P.rslab, type='bottom') if not ridge_full else p.set(x, y, za, P.rfull)
            else:
                p.set(x, y, za, P.rstairs, facing='south', half='bottom')
                p.set(x, y, zb, P.rstairs, facing='north', half='bottom')
                if za + 1 == zb:   # two rows meet: a ridge cap of slabs over the join
                    pass
        # under the eave rows inside the walls: the roof's underside (upside-down stairs) at the overhang ends
        k += 1
    top = base + k - 1
    # the gable ends
    for x in (B.x0, B.x1):
        for z in range(B.z0, B.z1 + 1):
            kz = min(z - lo, hi - z)
            for y in range(B.top + 1, base + kz):
                p.setif(x, y, z, P.gable)
    # a ridge cap where two stair rows met
    if (hi - lo + 1) % 2 == 0:
        zc = (lo + hi) // 2
        for x in xs: p.set(x, base + (hi - lo + 1) // 2, zc, P.rslab, type='bottom'); p.set(x, base + (hi - lo + 1) // 2, zc + 1, P.rslab, type='bottom')
        top = base + (hi - lo + 1) // 2
    return top


def roof_hip(p, P, B, base, over=1, tiers=True):
    """stairs up from all four sides (a tier band of planks part way up, the savanna's two-roofed look)"""
    x0, x1, z0, z1 = B.x0 - over, B.x1 + over, B.z0 - over, B.z1 + over
    y = base; k = 0
    while x0 <= x1 and z0 <= z1:
        if x1 - x0 <= 1 or z1 - z0 <= 1:
            for x in range(x0, x1 + 1):
                for z in range(z0, z1 + 1): p.set(x, y, z, P.rslab, type='bottom'); p.setif(x, y - 1, z, P.rfull)
            return y
        for x in range(x0, x1 + 1):
            p.set(x, y, z0, P.rstairs, facing='south'); p.set(x, y, z1, P.rstairs, facing='north')
        for z in range(z0 + 1, z1):
            p.set(x0, y, z, P.rstairs, facing='east'); p.set(x1, y, z, P.rstairs, facing='west')
        k += 1; x0 += 1; x1 -= 1; z0 += 1; z1 -= 1; y += 1
        if tiers and k == 2 and x1 - x0 >= 2 and z1 - z0 >= 2:   # the band between the two roofs
            for x in range(x0, x1 + 1):
                for z in (z0, z1): p.set(x, y, z, P.frame2, axis='x')
            for z in range(z0 + 1, z1):
                for x in (x0, x1): p.set(x, y, z, P.frame2, axis='z')
            y += 1; x0 -= 1; x1 += 1; z0 -= 1; z1 += 1   # the upper roof overhangs the band
    return y


def roof_turf(p, P, B, base, rnd, over=1):
    """a low roof of turf on spruce: stair eaves, then grass rising in steps to a log ridge, with ferns and flowers on
    it, crossed beams at the gable ends"""
    lo, hi = B.z0 - over, B.z1 + over
    xs = range(B.x0 - over, B.x1 + over + 1)
    k = 0
    while lo + k <= hi - k:
        y = base + k; za, zb = lo + k, hi - k
        for x in xs:
            if k == 0:
                p.set(x, y, za, P.rstairs, facing='south'); p.set(x, y, zb, P.rstairs, facing='north')
            elif za == zb:
                p.set(x, y, za, P.frame, axis='x')
            else:
                for z in (za, zb):
                    p.set(x, y, z, 'grass_block' if rnd.random() < 0.8 else 'moss_block')
                    if rnd.random() < 0.22 and p.get(x, y + 1, z) is None: plant_at(p, P, x, y + 1, z)
        k += 1
    top = base + k - 1
    zc = (lo + hi) // 2
    if (hi - lo + 1) % 2 == 0:
        for x in xs: p.set(x, top + 1, zc, P.frame, axis='x'); p.set(x, top + 1, zc + 1, P.frame, axis='x')
        top += 1
    for x in (B.x0, B.x1):   # gable ends, and the crossed beams standing up over them
        for z in range(B.z0, B.z1 + 1):
            kz = min(z - lo, hi - z)
            for y in range(B.top + 1, base + kz): p.setif(x, y, z, P.gable)
    return top


def roof_aframe(p, P, B, rnd):
    """the snowy A-frame: the roof comes down to the ground on both long sides; snow along its ridge and eaves"""
    lo, hi = B.z0 - 1, B.z1 + 1
    xs = range(B.x0 - 1, B.x1 + 2)
    k = 0
    while lo + k <= hi - k:
        y = 1 + k; za, zb = lo + k, hi - k
        for x in xs:
            if za == zb: p.set(x, y, za, P.rfull); p.set(x, y + 1, za, 'snow_block')
            else:
                p.set(x, y, za, P.rstairs, facing='south'); p.set(x, y, zb, P.rstairs, facing='north')
                if za + 1 == zb: p.set(x, y + 1, za, 'snow_block'); p.set(x, y + 1, zb, 'snow_block')
        k += 1
    top = k + 1
    for x in (B.x0, B.x1):   # the gable walls, from the floor up
        for z in range(B.z0, B.z1 + 1):
            kz = min(z - lo, hi - z)
            for y in range(1, 1 + kz): p.setif(x, y, z, P.gable if y > 1 else P.found)
    for x in (B.x0 - 1, B.x1 + 1):   # snow on the eave ends
        p.set(x, 0, lo, 'snow_block'); p.set(x, 0, hi, 'snow_block')
    return top


FLAT = dict(deck='smooth_sandstone', trim='cut_sandstone', pier='chiseled_sandstone', pslab='cut_sandstone_slab', pwall='sandstone_wall')


def roof_flat(p, P, B, rnd, terrace=True):
    """a flat roof (the desert's, the mesa's): a deck over the walls, a parapet with crenels and corner piers"""
    F = P.S.get('flatm', FLAT); y = B.top
    for x in range(B.x0, B.x1 + 1):
        for z in range(B.z0, B.z1 + 1): p.set(x, y, z, F['deck'] if B.inside(x, z) else F['trim'])
    for (x, z, side) in B.perimeter():
        corner = (x in (B.x0, B.x1)) and (z in (B.z0, B.z1))
        t = (z - B.z0) if side in ('west', 'east') else (x - B.x0)
        if corner: p.set(x, y + 1, z, F['pier']); p.set(x, y + 2, z, F['pslab'], type='bottom')
        elif t % 2 == 1: p.set(x, y + 1, z, F['pwall'])
        else: p.set(x, y + 1, z, F['pslab'], type='bottom')
    return y + 1


def roof_stepped(p, P, B, base, over=1, mat=None, cap=False):
    """a roof of whole blocks stepping in from all four sides (the igloo village's snow, a mushroom's cap)"""
    mat = mat or P.rfull
    x0, x1, z0, z1 = B.x0 - over, B.x1 + over, B.z0 - over, B.z1 + over
    y = base
    while x0 <= x1 and z0 <= z1:
        for x in range(x0, x1 + 1):
            for z in range(z0, z1 + 1):
                if x in (x0, x1) or z in (z0, z1) or y == base or x1 - x0 <= 3 or z1 - z0 <= 3: p.set(x, y, z, mat)   # (solid near the top: nothing over a hole)
        x0 += 1; x1 -= 1; z0 += 1; z1 -= 1; y += 1
    return y - 1


# ---------------------------------------------------------------------------------------------------- rooms
class Room:
    """one floor of a body: furniture along its walls, a table in the middle, lights"""
    def __init__(self, p, P, B, y, rnd, keep=(), mask=None):
        self.p, self.P, self.B, self.y, self.r = p, P, B, y, rnd
        self.free = {(x, z) for x in range(B.x0 + 1, B.x1) for z in range(B.z0 + 1, B.z1) if (mask is None or mask(x, z)) and p.get(x, y + 1, z) is None}
        if y == B.y0:   # the way in from the door
            for x in range(B.x0 + 1, min(B.x1, B.x0 + 3)): self.free.discard((x, B.zd))
        for c in keep: self.free.discard(c)

    def walls_of(self, x, z):
        out = []
        for d, (dx, dz) in DXZ.items():
            n = self.p.get(x + dx, self.y + 2, z + dz)
            if n is not None and 'door' not in n and n != 'ladder': out.append(d)
        return out

    def spots(self):
        s = [(x, z, w) for (x, z) in self.free for w in self.walls_of(x, z)]
        self.r.shuffle(s); return s

    def put(self, name, facing_in=True, **props):
        for x, z, w in self.spots():
            if (x, z) not in self.free: continue
            if facing_in: props = dict(props, facing=OPP[w])
            self.p.set(x, self.y + 1, z, name, **props); self.free.discard((x, z)); return (x, z, w)
        return None

    def bed(self):
        for x, z, w in self.spots():
            dx, dz = DXZ[w]; fx, fz = x - dx, z - dz
            if (x, z) in self.free and (fx, fz) in self.free:
                self.p.set(x, self.y + 1, z, self.P.bed, facing=w, part='head')
                self.p.set(fx, self.y + 1, fz, self.P.bed, facing=w, part='foot')
                self.free -= {(x, z), (fx, fz)}
                return True
        return False

    def table(self, chairs=2):
        """a table (a fence under a pressure plate) in the middle with chairs (stairs) at it, a rug under them all (the
        stand-ins become our table and chairs when the pieces load)"""
        B = self.B
        cx, cz = (B.x0 + B.x1) // 2 + (1 if self.y == 0 else 0), (B.z0 + B.z1) // 2
        if (cx, cz) not in self.free: cz += 1
        if (cx, cz) not in self.free: return
        for x in range(cx - 1, cx + 2):
            for z in range(cz - 1, cz + 2):
                if B.x0 < x < B.x1 and B.z0 < z < B.z1: self.p.rug(x, self.y, z, self.P.carpet)
        self.p.set(cx, self.y + 1, cz, self.P.fence); self.p.set(cx, self.y + 2, cz, self.P.wood + '_pressure_plate' if self.P.wood != 'birch' else 'birch_pressure_plate')
        self.free.discard((cx, cz))
        n = 0
        for d in ('north', 'south', 'east', 'west'):
            if n >= chairs: break
            dx, dz = DXZ[d]; x, z = cx + dx, cz + dz
            if (x, z) in self.free:
                self.p.set(x, self.y + 1, z, self.P.wood + '_stairs' if self.P.wood != 'birch' else 'birch_stairs', facing=d)   # its back away from the table
                self.free.discard((x, z)); n += 1

    def lights(self, n=2):
        B = self.B; done = 0
        for x, z, w in self.spots():
            if done >= n: break
            if (x, z) not in self.free: continue
            wx, wz = x + DXZ[w][0], z + DXZ[w][1]
            if self.p.get(wx, self.y + 3, wz) in (None, self.P.glass): continue
            self.p.set(x, self.y + 3, z, 'wall_torch', facing=OPP[w]); done += 1

    def lantern_hanging(self):
        B = self.B; cx, cz = (B.x0 + B.x1) // 2, (B.z0 + B.z1) // 2
        if self.p.get(cx, self.y + B.fh, cz) is not None: self.p.set(cx, self.y + B.fh - 1, cz, 'lantern', hanging=True)

    def fill(self, things):
        for t in things:
            if t == 'bed': self.bed()
            elif isinstance(t, tuple): self.put(t[0], **t[1])
            else: self.put(t)


def ladder(p, B, floors):
    x, z = B.x1 - 1, B.z1 - 1
    for y in range(B.y0 + 1, B.y0 + floors * B.fh + 1):
        p.set(x, y, z, 'ladder', facing='west')
    for f in range(1, floors): p.clear(x, B.y0 + f * B.fh, z)
    return (x, z)


def stilts(p, P, B):
    """the posts a raised house stands on, and the steps up to its door from the path"""
    post = P.S['log'] if 'log' in P.S['log'] else P.frame
    for (x, z, side) in B.perimeter():
        corner = (x in (B.x0, B.x1)) and (z in (B.z0, B.z1))
        t = (z - B.z0) if side in ('west', 'east') else (x - B.x0)
        if corner or t % 3 == 0:
            for y in range(0, B.y0): p.set(x, y, z, post, **({'axis': 'y'} if 'log' in post else {}))
    for k in range(B.y0):   # the steps: a stair a block, the porch deck before the door
        p.set(B.x0 - 1 - B.y0 + k, k + 1, B.zd, P.wood + '_stairs', facing='east')
        p.setif(B.x0 - 1 - B.y0 + k, k, B.zd, P.floor) if k > 0 else None
    for z in range(B.zd - 1, B.zd + 2):
        p.set(B.x0 - 1, B.y0, z, P.floor)
    for z in (B.zd - 1, B.zd + 1): p.set(B.x0 - 1, B.y0 + 1, z, P.fence)


def chimney(p, P, B, top):
    """a chimney up the back gable end, a hearth (a furnace) at its foot inside"""
    zc = (B.z0 + B.z1) // 2 + (1 if (B.z1 - B.z0) % 2 else 0)
    for y in range(0, top + 2): p.set(B.x1 + 1, y, zc, P.S['chimney'])
    p.set(B.x1 + 1, top + 2, zc, P.S['chimcap'])
    # the hearth: a fire set into the back wall under the stack, stone round it, the wall over it the mantel (the smoke
    # goes up the stack and out of its top: sim/camping.cpp campFlue)
    p.set(B.x1, B.y0, zc, P.S['chimney']); p.set(B.x1, B.y0 + 1, zc, 'campfire', lit=True, facing='west')
    p.set(B.x1, B.y0 + 2, zc, P.S['chimney'])
    return (B.x1 - 1, zc)


def yard(p, P, B, rnd, x_from=0, flowers=0.45):
    """the front garden: its ground, flowers along the front wall, a hedge or fence at the corners"""
    g = P.S['ground']
    for x in range(x_from, B.x0):
        for z in range(B.z0 - 1, B.z1 + 2):
            p.setif(x, 0, z, g)
    for z in range(B.z0, B.z1 + 1):
        if abs(z - B.zd) <= 1: continue
        x = B.x0 - 1
        if p.get(x, 1, z) is None and rnd.random() < flowers:
            if P.sandy: p.set(x, 1, z, 'dead_bush') if rnd.random() < 0.5 else leaves(p, x, 1, z, P.leaves)
            else: plant_at(p, P, x, 1, z)


def roof_for(p, P, B, rnd):
    k = P.roof_kind
    if k == 'gable':
        top = roof_gable(p, P, B, B.top, over=P.over)
        if P.S.get('snowcap'): snow_ridge(p, P, B, top)
        if P.decor == 'swamp': moss_roof(p, B, rnd, top)
        return top
    if k == 'hip': return roof_hip(p, P, B, B.top, tiers=(B.x1 - B.x0) >= 7)
    if k == 'pagoda': return roof_hip(p, P, B, B.top, over=2, tiers=True)
    if k in ('stepped', 'cap'): return roof_stepped(p, P, B, B.top, over=2 if k == 'cap' else 1)
    if k == 'cone': return roof_hip(p, P, B, B.top, tiers=False)
    if k == 'turf': return roof_turf(p, P, B, B.top, rnd)
    if k == 'flat': return roof_flat(p, P, B, rnd)
    return roof_aframe(p, P, B, rnd)


def shell(style, name, rnd, w, d, floors, x0=3, z0=2, porch=None, rect=False):
    """a house's body, roof, door, yard and chimney; returns the piece, the palette and the body (a round hut, an igloo or
    a mushroom for those styles' homes, a house up a tree for the treehouse village's; rect: always the box)"""
    P = Pal(style, rnd)
    if P.round and not rect: return round_shell(style, name, rnd, w, d, P)
    if P.tree and not rect: return tree_shell(style, name, rnd, w, d, P)
    p = Piece(name, 'houses', style)
    if P.roof_kind == 'aframe': floors = 1
    if P.raised: floors = 1
    B = Body(x0 + (P.raised + 2 if P.raised else 0), z0 + (P.over - 1), w, d, floors, y0=P.raised)
    if P.roof_kind == 'aframe':
        walls(p, P, B, rnd, sides=())   # (the floor; the roof is its walls)
        top = roof_aframe(p, P, B, rnd)
        for x in (B.x0, B.x1):   # gable windows
            p.set(x, 3, B.zd, P.glass); 
            if d >= 11: p.set(x, 2, B.zd - 2, P.glass); p.set(x, 2, B.zd + 2, P.glass)
    elif P.flat and floors > 1:
        B.floors = 1; B.top = B.fh
        walls(p, P, B, rnd); roof_flat(p, P, B, rnd)
        w2 = max(5, w // 2 + 1); d2 = d - 2
        U = Body(B.x1 - w2 + 1, B.z0 + 1, w2, d2, 1, y0=B.top); B.upper = U
        walls(p, P, U, rnd); top = roof_flat(p, P, U, rnd)
        door(p, P, U.x0, U.zd, y=U.y0)
        lx, lz = B.x0 + 1, B.z0 + 1   # the ladder up through the roof
        for y in range(1, B.top + 1): p.set(lx, y, lz, 'ladder', facing='east')
        p.clear(lx, B.top + 1, lz) if p.get(lx, B.top + 1, lz) == 'sandstone_wall' else None
        B.ladder = (lx, lz)
    else:
        walls(p, P, B, rnd)
        top = roof_for(p, P, B, rnd)
    porch = rnd.random() < 0.45 if porch is None else porch
    entrance(p, P, B, 0, porch=porch and P.roof_kind not in ('aframe',), rnd=rnd)
    yard(p, P, B, rnd)
    hearth = None
    if P.roof_kind in ('gable', 'turf', 'aframe') and P.S.get('chimney') and not P.raised and rnd.random() < 0.8: hearth = chimney(p, P, B, top)
    if P.flat: desert_dress(p, P, B, rnd)
    if P.S.get('balcony') and B.floors > 1: balcony(p, P, B)
    dress(p, P, B, rnd, top)
    return p, P, B, hearth


def desert_dress(p, P, B, rnd):
    """the desert's: a cloth awning over the door, leaves spilling over the parapet with vines down the walls; the
    mesa's adobe: the roof beams' ends (vigas) standing out of the walls under the roof instead"""
    if P.decor == 'mesa':
        for (x, z, side) in B.perimeter():
            corner = (x in (B.x0, B.x1)) and (z in (B.z0, B.z1))
            t = (z - B.z0) if side in ('west', 'east') else (x - B.x0)
            if not corner and t % 2 == 1:
                ox, oz = DXZ[side]
                p.set(x + ox, B.top - 1, z + oz, 'stripped_acacia_log', axis='x' if side in ('west', 'east') else 'z')
        return
    wool = rnd.choice(['white_wool', 'yellow_wool', 'orange_wool', 'red_wool', 'cyan_wool'])
    for z in range(B.zd - 1, B.zd + 2): p.set(B.x0 - 1, 3, z, wool)
    for (x, z, side) in B.perimeter():
        if side == 'west' and abs(z - B.zd) <= 1: continue   # (nothing hanging over the door)
        if rnd.random() < 0.12 and not ((x in (B.x0, B.x1)) and (z in (B.z0, B.z1))):
            leaves(p, x, B.top + 1, z, P.leaves)
            ox, oz = DXZ[side]
            for y in range(B.top - 1, max(1, B.top - 1 - rnd.randint(1, 3)), -1):
                if p.get(x + ox, y, z + oz) is None: p.set(x + ox, y, z + oz, 'vine', **{OPP[side]: True})


def balcony(p, P, B):
    """the alpine chalet's balcony along the front of its upper floor: a deck on the beam, a railing, a door out"""
    y = B.y0 + B.fh
    for z in range(B.z0, B.z1 + 1):   # a deck two wide on the beam, the railing along its outer edge
        for x in (B.x0 - 1, B.x0 - 2): p.set(x, y, z, P.floor)
        p.set(B.x0 - 2, y + 1, z, P.fence)
    for z in (B.z0, B.z1): p.set(B.x0 - 1, y + 1, z, P.fence)
    for yy in (y + 1, y + 2): p.clear(B.x0 - 1, yy, B.zd)   # (the way out to it clear)
    p.set(B.x0, y + 1, B.zd, P.door, facing='east', half='lower', hinge='left', open=False)
    p.set(B.x0, y + 2, B.zd, P.door, facing='east', half='upper', hinge='left', open=False)


def snow_ridge(p, P, B, top):
    """snow along a roof's ridge and on its eave ends (the snowy styles)"""
    for x in range(B.x0 - P.over, B.x1 + P.over + 1):
        for z in range(B.z0 - P.over, B.z1 + P.over + 1):
            for y in range(top + 1, top - 1, -1):
                if p.get(x, y, z) and p.get(x, y + 1, z) is None and y == top: p.set(x, y + 1, z, 'snow', layers=2)


def moss_roof(p, B, rnd, top):
    """moss growing over a swamp roof's slopes"""
    for (x, y, z), (n, pr) in list(p.b.items()):
        if n.endswith('_stairs') and y >= B.top and p.get(x, y + 1, z) is None and rnd.random() < 0.3:
            p.set(x, y + 1, z, 'moss_carpet')


def dress(p, P, B, rnd, top):
    """each style's touches outside: Christmas lights and wreaths, Halloween's lanterns and cobwebs, the swamp's lily pads
    and vines, the beach's and the jungle's greenery, the oasis's palms"""
    d = P.decor
    if d == 'christmas':
        # lanterns hung all along the eaves, a wreath over the door, a little tree with presents in the yard
        for x in range(B.x0 - P.over, B.x1 + P.over + 1, 2):
            for z in (B.z0 - P.over, B.z1 + P.over):
                if p.get(x, B.top, z) and p.get(x, B.top - 1, z) is None: p.set(x, B.top - 1, z, 'lantern', hanging=True)
        p.set(B.x0 - 1, B.y0 + 3, B.zd, 'spruce_leaves', persistent=True, distance=1)
        xmas_tree(p, P, max(1, B.x0 - 3), B.z0, rnd, small=True)
    elif d == 'halloween':
        for z in (B.zd - 1, B.zd + 1):   # jack o'lanterns on posts by the door
            if p.get(B.x0 - 1, 1, z) is None: p.set(B.x0 - 1, 1, z, P.fence); p.set(B.x0 - 1, 2, z, 'jack_o_lantern', facing='west')
        for (x, z) in ((B.x0, B.z0), (B.x1, B.z1), (B.x0, B.z1)):   # cobwebs in the eaves' corners
            if p.get(x, B.top - 1, z) is not None and p.get(x - 1, B.top - 1, z - 1) is None: p.set(x - 1, B.top - 1, z - 1, 'cobweb')
        for k in range(rnd.randint(1, 3)):   # pumpkins about the yard
            x, z = rnd.randint(0, max(0, B.x0 - 2)), rnd.randint(B.z0, B.z1)
            if p.get(x, 1, z) is None and p.get(x, 0, z) is not None and z != B.zd: p.set(x, 1, z, rnd.choice(['carved_pumpkin', 'pumpkin', 'jack_o_lantern']), facing='west')
        if rnd.random() < 0.5: grave(p, P, max(0, B.x0 - 2), B.z1 + 1)
    elif d == 'swamp':
        for (x, z, side) in B.perimeter():
            if side == 'west' and abs(z - B.zd) <= 1: continue   # (not over the door)
            if rnd.random() < 0.15:
                ox, oz = DXZ[side]
                for y in range(B.top - 1, B.top - 4, -1):
                    if p.get(x + ox, y, z + oz) is None: p.set(x + ox, y, z + oz, 'vine', **{OPP[side]: True})
    elif d in ('jungle',):
        for (x, z, side) in B.perimeter():
            if side == 'west' and abs(z - B.zd) <= 1: continue   # (not over the door)
            if rnd.random() < 0.12:
                ox, oz = DXZ[side]
                for y in range(B.top - 1, B.top - 3, -1):
                    if p.get(x + ox, y, z + oz) is None: p.set(x + ox, y, z + oz, 'vine', **{OPP[side]: True})
        for x in range(B.x0, B.x1 + 1):   # leaves heaped along the ridge
            if rnd.random() < 0.5 and p.get(x, top + 1, (B.z0 + B.z1) // 2) is None: leaves(p, x, top + 1, (B.z0 + B.z1) // 2, P.leaves)
    elif d in ('beach', 'oasis') and rnd.random() < 0.7:
        palm(p, P, max(1, B.x0 - 2 - P.raised), B.z1 + 1 if p.get(max(1, B.x0 - 2), 1, B.z1 + 1) is None else B.z0 - 1, rnd)


def xmas_tree(p, P, x, z, rnd, small=False):
    """a spruce dressed for Christmas: lights (lanterns) in it, a star (a glowstone) on top, presents under it"""
    h = 4 if small else 8
    mine = set()   # (its own boughs)
    for y in range(1, h + 1): p.set(x, y, z, 'spruce_log', axis='y')
    for y in range(2, h + 1):
        r = max(0, (h + 1 - y) // (2 if small else 2))
        r = min(r, 1 if small else 3)
        for dx in range(-r, r + 1):
            for dz in range(-r, r + 1):
                if (dx or dz) and abs(dx) + abs(dz) <= r + (0 if small else 1): leaves(p, x + dx, y, z + dz, 'spruce_leaves'); mine.add((x + dx, y, z + dz))
    for (cx, cy, cz) in list(mine):   # lights: lanterns hung under its lowest boughs
        if p.get(cx, cy - 1, cz) is None and cy > 1 and rnd.random() < 0.35:
            p.set(cx, cy - 1, cz, 'lantern', hanging=True)
    p.set(x, h + 1, z, 'glowstone')
    for (dx, dz) in ((1, 1), (-1, 1), (1, -1)) if not small else ((1, 1),):
        if p.get(x + dx, 1, z + dz) is None:
            p.set(x + dx, 1, z + dz, rnd.choice(['red_wool', 'green_wool', 'white_wool', 'light_blue_wool'])); p.set(x + dx, 2, z + dz, rnd.choice(['white_carpet', 'red_carpet', 'yellow_carpet']))


def grave(p, P, x, z):
    """a gravestone: a headstone (a wall block) and its mound"""
    p.set(x, 0, z, 'coarse_dirt'); p.set(x, 1, z, 'cobblestone_wall'); p.set(x + 1, 0, z, 'coarse_dirt')


# ---------------------------------------------------------------------------------------------------- round homes
def round_shell(style, name, rnd, w, d, P):
    """a round home: a hut of mud under a cone of thatch (the savanna's), an igloo of snow (the snowy's), or a house in a
    giant mushroom (the mushroom village's). Its door faces west, the path to it from the piece's edge."""
    p = Piece(name, 'houses', style)
    R = max(3, min(w, d) // 2)
    kind = P.round
    cx, cz = R + 3 + (1 if kind == 'igloo' else 0), R + 2 + (1 if kind == 'mushroom' else 0)
    def dist(x, z): return math.hypot(x - cx, z - cz)
    inside = lambda x, z: dist(x, z) < R - 0.5
    wallc = lambda x, z: R - 0.5 <= dist(x, z) < R + 0.5
    B = Body(cx - R, cz - R, 2 * R + 1, 2 * R + 1, 1); B.zd = cz
    for x in range(cx - R - 1, cx + R + 2):
        for z in range(cz - R - 1, cz + R + 2):
            if inside(x, z): p.set(x, 0, z, P.floor)
            elif wallc(x, z): p.set(x, 0, z, P.found)
    if kind == 'igloo':
        for x in range(cx - R - 1, cx + R + 2):   # a dome of snow
            for z in range(cz - R - 1, cz + R + 2):
                for y in range(1, R + 2):
                    r3 = math.sqrt((x - cx) ** 2 + (z - cz) ** 2 + (y - 0.5) ** 2)
                    if R - 0.5 <= r3 < R + 0.5: p.set(x, y, z, 'snow_block')
        for (x, z) in ((cx + R, cz), (cx, cz - R), (cx, cz + R)):   # windows of ice
            p.set(x, 2, z, 'ice')
        for x in (cx - R - 1, cx - R - 2):   # the tunnel in, its door at the end
            for z in (cz - 1, cz + 1): p.box(x, 1, z, x, 2, z, 'snow_block')
            p.set(x, 3, cz, 'snow_block'); p.set(x, 0, cz, P.floor)
        p.clear(cx - R, 1, cz); p.clear(cx - R, 2, cz)
        door(p, P, cx - R - 2, cz)
        for x in range(0, cx - R - 2): p.set(x, 0, cz, P.pathb())
        top = R + 1
    else:
        H = 3 if kind == 'hut' else 4
        for x in range(cx - R, cx + R + 1):
            for z in range(cz - R, cz + R + 1):
                if wallc(x, z):
                    for y in range(1, H + 1): p.set(x, y, z, P.fill_lo)
        for (x, z) in ((cx + R, cz), (cx, cz - R), (cx, cz + R)): p.set(x, 2, z, P.glass)
        door(p, P, cx - R, cz)
        for x in range(0, cx - R): p.set(x, 0, cz, P.pathb())
        if kind == 'hut':   # a cone of thatch
            for k in range(0, R + 3):
                rr = R + 1 - k; y = H + 1 + k
                if rr <= 0: p.set(cx, y, cz, 'thatch'); p.setif(cx, y - 1, cz, 'thatch'); top = y; break
                for x in range(cx - rr - 1, cx + rr + 2):
                    for z in range(cz - rr - 1, cz + rr + 2):
                        dd = math.hypot(x - cx, z - cz)
                        edge = dd < rr + 0.5 and any(math.hypot(x + a - cx, z + b - cz) >= rr + 0.5 for a in (-1, 0, 1) for b in (-1, 0, 1))
                        if edge:   # (the disc's rim, unbroken side to side: a ring of single cells only met at their corners)
                            fx, fz = cx - x, cz - z
                            f = ('east' if fx > 0 else 'west') if abs(fx) >= abs(fz) else ('south' if fz > 0 else 'north')
                            p.set(x, y, z, P.rstairs, facing=f)
                        elif dd < rr - 0.5 and k > 0: pass
                top = y
        else:   # the mushroom's cap: wide, low, round, its underside showing
            cap = 'red_mushroom_block' if rnd.random() < 0.7 else 'brown_mushroom_block'
            for k, rr in enumerate((R + 2, R + 2, R + 1, R - 1)):
                y = H + 1 + k
                for x in range(cx - rr - 1, cx + rr + 2):
                    for z in range(cz - rr - 1, cz + rr + 2):
                        dd = math.hypot(x - cx, z - cz)
                        if dd < rr + 0.5 and (k > 0 or dd >= rr - 1.5): p.set(x, y, z, cap)
            p.set(cx, H + 1, cz, 'shroomlight')   # the light in the cap's underside
            top = H + 4
    R0 = Room(p, P, B, 0, rnd, mask=lambda x, z: dist(x, z) < R - 1.2)
    p.joint(0, 1, cz, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    yard(p, P, Body(cx - R, cz - R, 2 * R + 1, 2 * R + 1), rnd, flowers=0.3)
    B.round_room = R0
    return p, P, B, None


def tree_shell(style, name, rnd, w, d, P):
    """a house up a jungle tree: a great trunk, a deck round it high up, the house on the deck with the trunk through it,
    the canopy over the roof; a stair winds up round the trunk from the path (the villagers walk up it)"""
    p = Piece(name, 'houses', style)
    py = 8   # the deck's height
    w, d = max(w, 7), max(d, 7)
    x0, z0 = 5, 3
    B = Body(x0, z0, w, d, 1, y0=py)
    tx, tz = (B.x0 + B.x1) // 2, (B.z0 + B.z1) // 2   # the trunk, 2 x 2
    trunk = {(tx, tz), (tx + 1, tz), (tx, tz + 1), (tx + 1, tz + 1)}
    for (x, z) in trunk:
        for y in range(0, py + 12): p.set(x, y, z, 'jungle_log', axis='y')
    for x in range(B.x0 - 1, B.x1 + 2):   # the deck, a railing round it
        for z in range(B.z0 - 1, B.z1 + 2):
            if (x, z) in trunk: continue
            p.set(x, py, z, P.floor)
            if (x in (B.x0 - 1, B.x1 + 1) or z in (B.z0 - 1, B.z1 + 1)) and not (x == B.x0 - 1 and abs(z - B.zd) <= 1): p.set(x, py + 1, z, P.fence)
    walls(p, P, B, rnd)
    for (x, z) in trunk:
        for y in range(py, py + 12): p.set(x, y, z, 'jungle_log', axis='y')
    top = roof_gable(p, P, B, B.top)
    for (x, z) in trunk: p.set(x, top + 1, z, 'jungle_log', axis='y')
    for x in range(tx - 4, tx + 6):   # the canopy over the roof
        for z in range(tz - 4, tz + 6):
            for y in range(top + 1, top + 4):
                if math.hypot(x - tx - 0.5, z - tz - 0.5) + (y - top - 1) * 1.3 < 5 and p.get(x, y, z) is None: leaves(p, x, y, z, 'jungle_leaves')
    door(p, P, B.x0, B.zd, y=py)
    # the stair: a spiral round a post in front of the house, a step a block, to a walk out to the deck
    sx, sz = B.x0 - 4, B.zd
    for y in range(0, py): p.set(sx, y, sz, 'jungle_log', axis='y')
    ring = [(sx, sz + 1), (sx + 1, sz + 1), (sx + 1, sz), (sx + 1, sz - 1), (sx, sz - 1), (sx - 1, sz - 1), (sx - 1, sz), (sx - 1, sz + 1)]
    def towards(a, b):
        dx, dz = b[0] - a[0], b[1] - a[1]
        return ('east' if dx > 0 else 'west') if abs(dx) >= abs(dz) else ('south' if dz > 0 else 'north')
    for k in range(py):
        (x, z), nxt = ring[k % 8], ring[(k + 1) % 8]
        p.set(x, k + 1, z, P.wood + '_stairs', facing=towards((x, z), nxt))
        if k > 0: p.set(x, k, z, P.floor)
    lx, lz = ring[(py - 1) % 8]
    for x in range(min(lx + 1, sx), B.x0 - 1): p.set(x, py, lz, P.floor)
    for x in range(0, sx - 1): p.set(x, 0, sz, P.pathb())
    p.set(sx - 1, 0, sz, P.pathb()); p.set(sx - 1, 0, sz + 1, P.pathb())
    p.joint(0, 1, sz, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    for (x, z) in ((B.x0 - 1, B.z0 - 1), (B.x1 + 1, B.z1 + 1), (B.x0 - 1, B.z1 + 1), (B.x1 + 1, B.z0 - 1)):   # vines down from the deck
        for yy in range(py - 1, py - 5, -1): p.set(x, yy, z, 'vine', north=True)
    B.trunk = trunk
    return p, P, B, None


def dims(rnd, size, style):
    if size == 'small': w, d = rnd.choice([7, 7, 9]), rnd.choice([7, 9])
    elif size == 'medium': w, d = rnd.choice([9, 9, 11]), rnd.choice([9, 11])
    else: w, d = rnd.choice([11, 13]), rnd.choice([11, 13])
    if STYLES[style]['roof_kind'] == 'aframe': d += 2   # (an A-frame needs the width for headroom)
    floors = 1 if size == 'small' else (2 if size == 'big' else rnd.choice([1, 2]))
    if STYLES[style]['roof_kind'] == 'flat' and size != 'small': floors = 2
    return w, d, floors


def furnish(p, P, B, rnd, hearth, beds, extra=(), job=None):
    if getattr(B, 'round_room', None):   # a round home: one room, everything in it
        R = B.round_room
        if job: R.put(job[0], **job[1])
        R.fill(list(extra)); R.put('chest', type='single')
        for _ in range(beds): R.bed()
        R.put('crafting_table', facing_in=False); R.lights(2)
        return
    keep = [hearth] if hearth else []
    keep += list(getattr(B, 'trunk', ()))
    lad = ladder(p, B, B.floors) if B.floors > 1 else getattr(B, 'ladder', None)
    if lad: keep.append(lad)
    rooms = [Room(p, P, B, B.y0 + f * B.fh, rnd, keep) for f in range(B.floors)]
    if B.upper: rooms.append(Room(p, P, B.upper, B.upper.y0, rnd))
    down = rooms[0]; up = rooms[-1]
    if job: down.put(job[0], **job[1])
    down.fill(list(extra))
    down.put('chest', type='single'); down.put('crafting_table', facing_in=False)
    cupboard(p, P, down, rnd, rnd.sample(KITCHEN, 4), rnd.sample(KSHELF, 3))   # the kitchen
    if len(rooms) == 1:
        for _ in range(beds): down.bed()
        down.table(chairs=2)
    else:
        down.table(chairs=3)
        for _ in range(beds): up.bed()
        up.put('chest', type='single'); up.table(chairs=1)
    for r in rooms:
        r.lights(2 if (B.x1 - B.x0) > 7 else 1)
    up = rooms[-1]   # a shelf by the beds, up out of the way
    UB = Body(up.B.x0, up.B.z0, up.B.x1 - up.B.x0 + 1, up.B.z1 - up.B.z0 + 1, 1, up.B.fh, up.y); UB.zd = up.B.zd
    wall_shelf(p, P, UB, rnd, rnd.sample(BEDSIDE, 3), cells=wall_cells(p, P, UB, UB.y0 + 3, rnd))


# ---------------------------------------------------------------------------------------------------- buildings
_ITEMS = None


def item_ok(k):
    global _ITEMS
    if _ITEMS is None:
        try: _ITEMS = set(json.loads((pathlib.Path(__file__).resolve().parents[2] / 'data' / 'items.json').read_text()).keys())
        except Exception: _ITEMS = set()
    return not _ITEMS or k in _ITEMS


GOODS = {   # what a trade shows: on its walls (frames), on its display table, over its door (the first: its sign)
    'armorer_house': ['iron_chestplate', 'iron_helmet', 'steel_chestplate', 'iron_leggings', 'iron_boots', 'chainmail_chestplate', 'steel_helmet'],
    'weaponsmith': ['iron_sword', 'steel_sword', 'iron_axe', 'stone_sword', 'diamond_sword', 'gold_sword'],
    'tool_smith': ['iron_pickaxe', 'iron_shovel', 'iron_axe', 'iron_hoe', 'shears', 'iron_ingot'],
    'butcher_shop': ['cooked_beef', 'beef', 'porkchop', 'cooked_porkchop', 'cooked_chicken', 'mutton'],
    'fletcher_house': ['bow', 'arrow', 'feather', 'flint', 'arrow', 'stick'],
    'shepherds_house': ['shears', 'string', 'wool', 'lead'],
    'masons_house': ['brick', 'stone_bricks', 'chiseled_stone_bricks', 'iron_pickaxe'],
    'cartographer_house': ['empty_map', 'compass', 'paper', 'spyglass', 'clock'],
    'fisher_cottage': ['fishing_rod', 'cod', 'salmon', 'tropical_fish'],
    'tannery': ['leather', 'leather_chestplate', 'saddle', 'lead', 'leather_helmet'],
    'bakery': ['bread', 'pumpkin_pie', 'cake', 'cookie', 'bread'],
    'library': ['book', 'enchanted_book', 'book', 'paper', 'candle'],
    'alchemist': ['potion_heal', 'potion_regen', 'potion_mana', 'potion_swift', 'potion_strength', 'glass_bottle'],
    'general_store': ['candle', 'torch', 'string', 'rope', 'coal', 'bowl', 'lead', 'paper'],
    'jeweller': ['emerald', 'diamond', 'gold_ingot', 'ruby', 'sapphire', 'amethyst_shard'],
    'tavern': ['ale_mug', 'bread', 'ale_mug', 'cooked_beef'],
    'sawmill': ['iron_axe', 'stick'], 'builders_yard': ['iron_shovel', 'brick'], 'barracks': ['iron_sword', 'iron_helmet', 'steel_sword'],
}


def racked(g): return g.endswith(('_sword', '_axe', '_pickaxe', '_shovel', '_hoe')) or g in ('bow', 'fishing_rod', 'crossbow', 'trident', 'spyglass')
def hung(g): return g.endswith(('_chestplate', '_helmet', '_leggings', '_boots')) or g in ('empty_map', 'filled_map', 'clock', 'painting')


def wall_mats(P):
    """what this house's walls are made of (a frame, a shelf or a rack hangs only on these)"""
    S = P.S; F = S.get('flatm', FLAT)
    m = {P.fill_lo, P.fill_hi, P.frame, P.frame2, P.found, P.gable, S.get('chimney'), F['trim']} | set(S['fill_lo']) | set(S['fill_hi']) | set(S['found'])
    if 'log' in P.frame: m.add(P.frame)
    return {x for x in m if x}


def is_wall(P, b): return b is not None and b in wall_mats(P)


NOT_WALL = ('_door', '_trapdoor', 'ladder', 'torch', 'lantern', '_fence', 'item_frame', '_shelf', 'weapon_rack', 'pane', '_stairs', '_slab', 'leaves', 'vine', 'glass', 'ice', 'campfire', 'bed')


def wall_cells(p, P, B, y, rnd):
    """the cells along the inside of the walls at height y: (x, z, d), d the side the wall is on (a real wall block there,
    not a window, a door, a ladder or a fire); not the way in from the door, not before the hearth"""
    out = []
    for x in range(B.x0 + 1, B.x1):
        for z in range(B.z0 + 1, B.z1):
            if p.get(x, y, z) is not None or p.get(x, B.y0, z) is None: continue
            if x == B.x0 + 1 and abs(z - B.zd) <= 1: continue
            if p.get(x + 1, B.y0 + 1, z) == 'campfire' or p.get(x - 1, B.y0 + 1, z) == 'campfire': continue
            for d, (dx, dz) in DXZ.items():
                if is_wall(P, p.get(x + dx, y, z + dz)): out.append((x, z, d)); break
    rnd.shuffle(out)
    return out


def wall_shelf(p, P, B, rnd, items, cells=None):
    """a wall shelf of the house's wood with its three things: right over a cupboard, a chest or a barrel (a shelf above
    the counter), else up out of everyone's way under the ceiling"""
    for (x, z, d) in (cells if cells is not None else wall_cells(p, P, B, B.y0 + 3, rnd)):
        under = p.get(x, B.y0 + 1, z) or ''
        y = B.y0 + 2 if any(t in under for t in ('_cabinet', 'chest', 'barrel', '_table', 'crafting_table', 'furnace', 'smoker', 'composter')) and p.get(x, B.y0 + 2, z) is None else B.y0 + 3
        if p.get(x, y, z) is not None or p.get(x, y + (1 if y == B.y0 + 2 else -1), z) in ('wall_torch',): continue
        if not is_wall(P, p.get(x + DXZ[d][0], y, z + DXZ[d][1])): continue
        if y == B.y0 + 3 and p.get(x, B.y0 + 2, z) is not None: continue
        p.set(x, y, z, furn(P, 'shelf'), facing=OPP[d], items=','.join(items[:3]))
        return (x, y, z)
    return None


def cupboard(p, P, R, rnd, stock, shelf_items=None):
    """a cupboard (our cabinet) against a wall, stocked; a wall shelf right over it with a few more things"""
    spot = R.put(furn(P, 'cabinet'), items=','.join(stock))
    if spot and shelf_items:
        x, z, d = spot
        wall_shelf(p, P, R.B, rnd, shelf_items, cells=[(x, z, d)])
    return spot


KITCHEN = ['bread*3', 'apple*2', 'carrot*2', 'potato*3', 'bowl', 'cooked_chicken', 'honey_bottle', 'egg*2', 'beetroot*2', 'cookie*2']
KSHELF = ['bowl', 'bread', 'apple', 'honey_bottle', 'candle', 'flower_pot', 'glass_bottle', 'cookie']
BEDSIDE = ['candle', 'book', 'flower_pot', 'clock', 'paper', 'candle']


def show_goods(p, P, B, rnd, key, frames=3, table=True, shelves=3):
    """a shop's goods where they belong: blades, tools and bows on weapon racks on the walls, armour and maps hung in frames,
    everything else (food, potions, gems, books, cloth) on wall shelves; a display table with goods lying on it; the shop's
    sign over the door outside (its own thing in a frame)"""
    goods = [g for g in GOODS.get(key, []) if item_ok(g)]
    if not goods: return
    rk = [g for g in goods if racked(g)]; hg = [g for g in goods if hung(g)]; sh = [g for g in goods if not racked(g) and not hung(g)]
    y = B.y0 + 2
    if rk:   # weapon racks, three a rack
        n = 0
        for (x, z, d) in wall_cells(p, P, B, y, rnd):
            if n >= 2 or p.get(x, y, z) is not None: continue
            p.set(x, y, z, 'wall_weapon_rack', facing=OPP[d], items=','.join(rnd.sample(rk * 3, 3))); n += 1
    if hg:
        n = 0
        for (x, z, d) in wall_cells(p, P, B, y, rnd):
            if n >= frames or p.get(x, y, z) is not None: continue
            p.set(x, y, z, 'item_frame', facing=OPP[d], items=hg[n % len(hg)]); n += 1
    pool = sh or goods
    for _ in range(shelves if sh else 1):
        if not wall_shelf(p, P, B, rnd, [rnd.choice(pool) for _ in range(3)]): break
    if table:   # a display table with goods laid on it
        for x in range(B.x0 + 2, B.x1):
            done = False
            for z in range(B.z0 + 1, B.z1):
                if p.get(x, B.y0 + 1, z) is None and p.get(x, B.y0 + 2, z) is None and abs(z - B.zd) > 1 and p.get(x, B.y0, z) is not None:
                    p.set(x, B.y0 + 1, z, furn(P, 'table')); p.set(x, B.y0 + 2, z, 'item_pile', items=','.join(rnd.choice(goods) for _ in range(3)))
                    done = True; break
            if done: break
    # the sign over the door: what's made here, in a frame
    sx, sy = B.x0 - 1, B.y0 + 3
    if p.get(sx, sy, B.zd) is None and p.get(B.x0, sy, B.zd) is not None and 'door' not in p.get(B.x0, sy, B.zd):
        p.set(sx, sy, B.zd, 'item_frame', facing='west', items=goods[0])


JOBS = {   # name keyword -> its workstation and what else it keeps
    'armorer_house': ('blast_furnace', ['anvil', 'bellows', ('barrel', {'facing': 'up'})]),
    'weaponsmith': ('grindstone', ['anvil', 'bellows', ('barrel', {'facing': 'up'})]),
    'tool_smith': ('smithing_table', ['anvil', 'bellows', 'furnace']),
    'butcher_shop': ('smoker', [('barrel', {'facing': 'up'}), 'sealed_barrel', 'furnace']),   # (the sealable barrel: hides soaking)
    'fletcher_house': ('fletching_table', [('barrel', {'facing': 'up'})]),
    'shepherds_house': ('loom', ['white_wool', 'white_wool']),
    'masons_house': ('stonecutter', ['furnace']),
    'cartographer_house': ('cartography_table', ['bookshelf']),
    'fisher_cottage': ('barrel', ['cauldron', ('barrel', {'facing': 'up'})]),
    'tannery': ('cauldron', ['brown_wool', 'sealed_barrel', 'sealed_barrel', ('barrel', {'facing': 'up'})]),
    'bakery': ('smoker', ['furnace', 'quern', 'hay_block', ('barrel', {'facing': 'up'})]),
}


def house(style, name, seed, size):
    rnd = random.Random(seed)
    w, d, floors = dims(rnd, size, style)
    p, P, B, hearth = shell(style, name, rnd, w, d, floors)
    furnish(p, P, B, rnd, hearth, beds={'small': 1, 'medium': 2, 'big': 3}[size])
    return p


def workshop(style, name, key, seed):
    rnd = random.Random(seed)
    w, d, floors = dims(rnd, rnd.choice(['small', 'medium']), style)
    p, P, B, hearth = shell(style, name, rnd, w, d, floors)
    ws, extra = JOBS[key]
    job = (ws, {'facing': 'west'} if ws not in ('cauldron', 'composter', 'barrel') else ({'facing': 'up'} if ws == 'barrel' else {}))
    furnish(p, P, B, rnd, hearth, beds=1 + (B.floors > 1), extra=extra, job=job)
    if key == 'armorer_house':   # what's made here, fitted on stands about the shop (the armourer works them on the round)
        R = getattr(B, 'round_room', None) or Room(p, P, B, B.y0, rnd, [hearth] if hearth else [])
        for gear in rnd.sample(ARMOUR_SETS, 2): R.put('armor_stand', items=gear)
    if not getattr(B, 'round_room', None): show_goods(p, P, B, rnd, key)
    side_work(p, P, B, rnd, key)
    return p


ARMOUR_SETS = [   # helmet, chest, legs, boots, hand (an armour stand's slots)
    'iron_helmet,iron_chestplate,iron_leggings,iron_boots,', 'chainmail_helmet,chainmail_chestplate,chainmail_leggings,chainmail_boots,',
    'leather_helmet,leather_chestplate,leather_leggings,leather_boots,', 'steel_helmet,steel_chestplate,iron_leggings,iron_boots,',
    'iron_helmet,silver_chestplate,chainmail_leggings,leather_boots,']


def side_work(p, P, B, rnd, key):
    """what a trade keeps outside, along the house's south side"""
    z = B.z1 + 2; g = P.S['ground']
    xs = range(B.x0, B.x1 + 1)
    for x in xs: p.setif(x, 0, z, g); p.setif(x, 0, z - 1, g)
    if key in ('armorer_house', 'weaponsmith', 'tool_smith'):   # an open forge: posts, a slab roof, an anvil and a quench tub
        for x in (B.x0 + 1, B.x0 + 4):
            p.box(x, 1, z + 1, x, 3, z + 1, P.fence)
        for x in range(B.x0, B.x0 + 6):
            for zz in range(z - 1, z + 2): p.set(x, 4, zz, P.S['stone_slab'], type='bottom')
        p.set(B.x0 + 2, 1, z, 'anvil', facing='north'); p.set(B.x0 + 3, 1, z, 'water_cauldron', level=3)
        # a TFC forge: charcoal in a pit walled with stone, the bellows beside it (sim/forge.h)
        p.set(B.x0 + 5, 1, z, 'forge'); p.set(B.x0 + 4, 1, z, 'bellows')
        p.set(B.x0 + 6, 1, z, 'cobblestone'); p.set(B.x0 + 5, 1, z - 1, 'cobblestone'); p.set(B.x0 + 5, 1, z + 1, 'cobblestone'); p.set(B.x0 + 5, 0, z, 'cobblestone')
    elif key == 'fletcher_house':   # targets
        for x in range(B.x0 + 1, B.x1, 3): p.set(x, 1, z + 1, 'hay_block', axis='y'); p.set(x, 2, z + 1, 'target')
    elif key == 'masons_house':   # stone set out to be cut
        for x in range(B.x0 + 1, B.x0 + 5):
            p.set(x, 1, z, rnd.choice(['stone', 'cobblestone', P.S['pave'][0]]))
            if rnd.random() < 0.5: p.set(x, 2, z, P.S['stone_slab'], type='bottom')
    elif key == 'shepherds_house':   # a fold for the sheep
        sheep_fold(p, P, B.x0, z, 6, 4)
    elif key in ('fisher_cottage',):   # barrels and a rack of nets
        for x in range(B.x0 + 1, B.x0 + 4): p.set(x, 1, z, 'barrel', facing='up')
    elif key == 'tannery':   # hides drying on racks
        for x in range(B.x0 + 1, B.x0 + 5, 2):
            p.set(x, 1, z, P.fence); p.set(x, 2, z, P.fence); p.set(x + 1, 2, z, rnd.choice(['brown_carpet', 'orange_carpet'])) 
            p.set(x + 1, 1, z, 'water_cauldron', level=2)
    elif key == 'bakery':
        p.set(B.x0 + 1, 1, z, 'hay_block', axis='y'); p.set(B.x0 + 2, 1, z, 'hay_block', axis='x'); p.set(B.x0 + 1, 2, z, 'hay_block', axis='y')


def sheep_fold(p, P, x0, z0, w, d):
    for x in range(x0, x0 + w):
        for z in range(z0, z0 + d):
            edge = x in (x0, x0 + w - 1) or z in (z0, z0 + d - 1)
            p.set(x, 0, z, P.S['ground'])
            if edge: p.set(x, 1, z, P.fence)
    p.set(x0 + w // 2, 1, z0, P.gate, facing='north')
    for (x, z) in ((x0 + 2, z0 + 1), (x0 + w - 3, z0 + d - 2)):
        p.joint(x, 1, z, 'up_north', 'minecraft:bottom', 'minecraft:bottom', 'minecraft:village/common/sheep', 'minecraft:air')


def library(style, name, seed):
    rnd = random.Random(seed)
    aframe = STYLES[style]['roof_kind'] == 'aframe'
    w, d = rnd.choice([11, 13]), rnd.choice([11, 13]) + (2 if aframe else 0)
    p, P, B, hearth = shell(style, name, rnd, w, d, 2 if not aframe else 1, porch=True, rect=True)
    keep = [hearth] if hearth else []
    lad = ladder(p, B, B.floors) if B.floors > 1 else None
    if lad: keep.append(lad)
    for f in range(B.floors):
        R = Room(p, P, B, B.y0 + f * B.fh, rnd, keep)
        for x, z, wl in list(R.spots()):   # shelves along the walls, two high
            if (x, z) in R.free and rnd.random() < 0.7:
                for yy in (R.y + 1, R.y + 2):
                    p.set(x, yy, z, furn(P, 'bookcase'), facing=OPP[wl], items=','.join(rnd.choice(['book', 'book', 'book', 'enchanted_book', 'book', '']) for _ in range(6)))
                R.free.discard((x, z))
        if f == 0: R.put('lectern', facing_in=False, facing='west')
        R.table(chairs=3); R.lights(2)
        if f == B.floors - 1: R.bed()
    if B.floors == 1:
        R = Room(p, P, B, B.y0, rnd, keep); R.put('lectern', facing_in=False, facing='west')
    show_goods(p, P, B, rnd, 'library', frames=2, table=False)
    return p


def temple(style, name, seed):
    """a chapel: a long tall nave, pews, an altar with the brewing stand, a tower over the door"""
    rnd = random.Random(seed)
    p = Piece(name, 'houses', style); P = Pal(style, rnd)
    w, d = rnd.choice([13, 15]), 9
    B = Body(4, 2, w, d, 1, fh=6)
    if P.roof_kind == 'aframe':
        B = Body(4, 2, w, 11, 1, fh=4); walls(p, P, B, rnd, sides=()); top = roof_aframe(p, P, B, rnd)
    else:
        walls(p, P, B, rnd, step=3); top = roof_for(p, P, B, rnd)
    for (x, z, side) in B.perimeter():   # stained glass
        if p.get(x, 2, z) == P.glass and P.glass == 'glass_pane': p.set(x, 2, z, 'yellow_stained_glass_pane'); p.setif(x, 3, z, 'white_stained_glass_pane')
    entrance(p, P, B, 0, porch=False, rnd=rnd)
    yard(p, P, B, rnd, flowers=0.7)
    zc = B.zd
    for x in range(B.x0 + 2, B.x1 - 2, 2):   # pews
        for z in range(B.z0 + 1, B.z1):
            if abs(z - zc) <= 0: continue
            p.set(x, 1, z, furn(P, 'bench'), facing='east')
    p.set(B.x1 - 1, 1, zc, furn(P, 'table')); p.set(B.x1 - 1, 2, zc, 'brewing_stand')   # the altar: the stand on a table
    p.set(B.x1 - 1, 1, zc - 1, P.S['stone_slab'], type='double'); p.set(B.x1 - 1, 1, zc + 1, P.S['stone_slab'], type='double')
    p.set(B.x1 - 1, 2, zc - 1, 'candle', lit=True, candles=3) ; p.set(B.x1 - 1, 2, zc + 1, 'candle', lit=True, candles=2)
    for x in range(B.x0 + 1, B.x1 - 1):   # a rug down the aisle to the altar, another colour before it
        p.rug(x, 0, zc, P.carpet if x == B.x1 - 2 else 'red_carpet')
    R = Room(p, P, B, 0, rnd); R.lights(4)
    # the tower over the front: four posts up past the roof, a little roof on them
    if P.roof_kind != 'flat':
        tx0, tz0 = B.x0, zc - 1; ty = top + 1
        for (x, z) in ((tx0, tz0), (tx0 + 2, tz0), (tx0, tz0 + 2), (tx0 + 2, tz0 + 2)):
            for y in range(B.top, ty + 3): p.set(x, y, z, P.frame, axis='y') if 'log' in P.frame else p.set(x, y, z, P.frame)
        for x in range(tx0 - 1, tx0 + 4):
            for z in range(tz0 - 1, tz0 + 4): p.set(x, ty + 3, z, P.rslab, type='bottom')
        p.set(tx0 + 1, ty + 4, tz0 + 1, P.rfull); p.set(tx0 + 1, ty + 5, tz0 + 1, P.fence)
    else:
        F = P.S.get('flatm', FLAT)
        for y in range(B.top + 1, B.top + 5):
            for (x, z) in ((B.x0, zc - 1), (B.x0, zc + 1), (B.x0 + 2, zc - 1), (B.x0 + 2, zc + 1)): p.set(x, y, z, F['trim'])
        for x in range(B.x0 - 1, B.x0 + 4):
            for z in range(zc - 2, zc + 3): p.set(x, B.top + 5, z, F['pslab'], type='bottom')
    return p


def tavern(style, name, seed):
    """the inn: a big hall with a bar of barrels behind a counter, long tables and benches; rooms above"""
    rnd = random.Random(seed)
    aframe = STYLES[style]['roof_kind'] == 'aframe'
    w, d = 13, 13 + (2 if aframe else 0)
    p, P, B, hearth = shell(style, name, rnd, w, d, 2 if not aframe else 1, porch=True, rect=True)
    keep = [hearth] if hearth else []
    lad = ladder(p, B, B.floors) if B.floors > 1 else None
    if lad: keep.append(lad)
    R = Room(p, P, B, B.y0, rnd, keep)
    for z in range(B.z0 + 1, B.z1):   # the bar along the back wall: barrels, a counter of tables before them
        if (B.x1 - 1, z) in R.free:
            p.set(B.x1 - 1, R.y + 1, z, 'barrel', facing='west'); R.free.discard((B.x1 - 1, z))
            if is_wall(P, p.get(B.x1, R.y + 2, z)) and p.get(B.x1 - 1, R.y + 2, z) is None:   # the back bar: a shelf of mugs over each barrel
                p.set(B.x1 - 1, R.y + 2, z, furn(P, 'shelf'), facing='west', items=','.join(rnd.choice(['ale_mug', 'ale_mug', 'ale_mug', 'honey_bottle', 'glass_bottle']) for _ in range(3)))
        if (B.x1 - 3, z) in R.free and B.z0 + 2 < z < B.z1 - 2:
            p.set(B.x1 - 3, R.y + 1, z, furn(P, 'table'), facing='west'); R.free.discard((B.x1 - 3, z))
            if rnd.random() < 0.5: p.set(B.x1 - 3, R.y + 2, z, 'item_pile', items=','.join(rnd.choice(['ale_mug', 'ale_mug', 'bread', '']) for _ in range(3)))
    for z in (B.z0 + 2, B.z1 - 2):   # long tables with benches
        for x in range(B.x0 + 2, B.x0 + 5):
            if (x, z) in R.free:
                p.set(x, R.y + 1, z, furn(P, 'table')); R.free.discard((x, z))
                if rnd.random() < 0.6: p.set(x, R.y + 2, z, 'item_pile', items=','.join(rnd.choice(['ale_mug', 'vegetable_stew', 'bread', 'cooked_chicken', '']) for _ in range(3)))
            for dz, f in ((-1, 'south'), (1, 'north')):   # benches either side, facing the table
                if (x, z + dz) in R.free: p.set(x, R.y + 1, z + dz, furn(P, 'bench'), facing=f); R.free.discard((x, z + dz))
    R.lights(4)
    if B.floors > 1:
        U = Room(p, P, B, B.y0 + B.fh, rnd, keep)
        for _ in range(4): U.bed()
        U.lights(2)
    else:
        for _ in range(2): R.bed()
    show_goods(p, P, B, rnd, 'tavern', frames=3, table=False)
    return p


def farm(style, name, seed, big=False):
    """crops in rows between water channels, a log border, a composter by the gate, a scarecrow"""
    rnd = random.Random(seed)
    p = Piece(name, 'houses', style); P = Pal(style, rnd)
    w, d = (13, 13) if big else (9, rnd.choice([7, 9]))
    x0, z0 = 1, 0
    crops = P.S['crops']; crop = rnd.choice(crops)
    border = P.frame if 'log' in P.frame else 'cut_sandstone'
    for x in range(x0, x0 + w):
        for z in range(z0, z0 + d):
            edge = x in (x0, x0 + w - 1) or z in (z0, z0 + d - 1)
            if edge:
                ax = 'z' if x in (x0, x0 + w - 1) else 'x'
                p.set(x, 0, z, border, **({'axis': ax} if 'log' in border else {}))
            elif (z - z0) % 4 == 0 and z != z0 + d - 1:
                p.set(x, 0, z, 'water', level=0)
            else:
                p.set(x, 0, z, 'farmland', moisture=7)
                if big and (x - x0) == w // 2: crop = crops[(z - z0) // 4 % len(crops)]
                p.set(x, 1, z, crop, age=rnd.choice([3, 5, 7, 7]) if crop != 'beetroots' else rnd.choice([1, 2, 3]))
    zc = z0 + d // 2
    if (zc - z0) % 4 == 0: zc += 1
    p.set(x0, 0, zc, P.pathb()); p.clear(x0, 1, zc)   # the way in
    p.joint(0, 1, zc, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    p.set(0, 0, zc, P.pathb())
    p.set(x0, 1, zc - 1, 'composter', level=rnd.randint(0, 6))
    # a scarecrow
    sx, sz = x0 + w // 2, z0 + 1 if (1 % 4) else z0 + 2
    if p.get(sx, 0, sz) == 'farmland':
        p.set(sx, 1, sz, P.fence); p.set(sx, 2, sz, 'hay_block', axis='y'); p.set(sx, 3, sz, 'carved_pumpkin', facing='west')
    if big:   # a tool shed in the corner
        p.set(x0 + w - 2, 1, z0 + d - 2, 'barrel', facing='up')
    return p


def animal_pen(style, name, seed, sheep=False):
    rnd = random.Random(seed)
    p = Piece(name, 'houses', style); P = Pal(style, rnd)
    w, d = rnd.choice([9, 11]), rnd.choice([9, 11])
    x0, z0 = 1, 0
    zc = z0 + d // 2
    for x in range(x0, x0 + w):
        for z in range(z0, z0 + d):
            p.set(x, 0, z, P.S['ground'])
            if x in (x0, x0 + w - 1) or z in (z0, z0 + d - 1): p.set(x, 1, z, P.fence)
    p.set(x0, 1, zc, P.gate, facing='east')
    p.set(0, 0, zc, P.pathb())
    p.joint(0, 1, zc, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    # a shelter in the back corner, hay, a trough
    sx0, sz0 = x0 + w - 4, z0 + 1
    for (x, z) in ((sx0, sz0), (x0 + w - 2, sz0), (sx0, sz0 + 2), (x0 + w - 2, sz0 + 2)):
        p.box(x, 1, z, x, 2, z, P.fence)
    for x in range(sx0 - 1, x0 + w):
        for z in range(sz0 - 1, sz0 + 4): p.set(x, 3, z, P.rslab, type='bottom')
    p.set(sx0 + 1, 1, sz0, 'hay_block', axis='y'); p.set(sx0 + 2, 1, sz0, 'hay_block', axis='x'); p.set(sx0 + 1, 2, sz0, 'hay_block', axis='y')
    p.set(x0 + 1, 1, z0 + d - 2, 'water_cauldron', level=3)
    pool = 'minecraft:village/common/sheep' if sheep else 'minecraft:village/common/animals'
    for (x, z) in ((x0 + 2, z0 + 2), (x0 + 3, z0 + d - 3), (x0 + w // 2, zc)):
        p.joint(x, 1, z, 'up_north', 'minecraft:bottom', 'minecraft:bottom', pool, 'minecraft:air')
    return p


def stable(style, name, seed):
    """stalls under one roof, open to the yard, hay in each"""
    rnd = random.Random(seed)
    p = Piece(name, 'houses', style); P = Pal(style, rnd)
    w, d = 7, 11
    B = Body(3, 2, w, d, 1)
    for x in range(B.x0, B.x1 + 1):
        for z in range(B.z0, B.z1 + 1): p.set(x, 0, z, P.found if not B.inside(x, z) else 'coarse_dirt' if not P.sandy else P.S['ground'])
    for (x, z, side) in B.perimeter():   # a back wall and ends; posts along the open front
        if side == 'west':
            if (z - B.z0) % 3 == 0: p.box(x, 1, z, x, 3, z, P.frame, **({'axis': 'y'} if 'log' in P.frame else {}))
        else: p.box(x, 1, z, x, 3, z, P.fill_lo)
        p.set(x, 4, z, P.frame2, **({'axis': 'x' if side in ('north', 'south') else 'z'} if 'log' in P.frame2 else {}))
    for z in range(B.z0 + 3, B.z1, 3):   # stall dividers
        for x in range(B.x0 + 2, B.x1): p.set(x, 1, z, P.fence)
    for z in range(B.z0 + 1, B.z1, 3): p.set(B.x1 - 1, 1, z + 1 if z + 1 < B.z1 else z, 'hay_block', axis='y')
    if P.roof_kind == 'aframe': P.roof_kind = 'gable'
    roof_for(p, P, B, rnd) if P.roof_kind != 'flat' else roof_flat(p, P, B, rnd)
    for x in range(0, B.x0): p.set(x, 0, B.zd, P.pathb())
    p.joint(0, 1, B.zd, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    return p



def windmill(style, name, seed):
    """the mill: a three-storey tower in the style's materials, its great sails on a hub out of the back wall, the shaft
    (axles) running in to the millstone on the top floor; the miller's bed below, grain in sacks (hay) between. The
    ground round the back is kept clear for the sails to turn (they reach seven blocks from the hub)."""
    rnd = random.Random(seed)
    p = Piece(name, 'houses', style); P = Pal(style, rnd)
    w = d = 7
    B = Body(3, 4, w, d, 3)
    walls(p, P, B, rnd, step=3)
    if P.roof_kind == 'aframe': P.roof_kind = 'gable'
    top = roof_for(p, P, B, rnd)
    entrance(p, P, B, 0, porch=False, rnd=rnd)
    yard(p, P, B, rnd)
    zc = (B.z0 + B.z1) // 2; hy = 10
    # the sails' room: clear ground behind and beside the tower (a yard of the style's ground)
    g = P.S['ground']
    for x in range(B.x0, B.x1 + 3):
        for z in range(zc - 7, zc + 8): p.setif(x, 0, z, g)
    # the hub, the shaft, the millstone on its plinth
    p.set(B.x1 + 1, hy, zc, 'windmill', axis='x', big=True)
    for x in (B.x1, B.x1 - 1): p.set(x, hy, zc, 'axle', axis='x')
    p.set(B.x1 - 2, hy, zc, 'millstone'); p.set(B.x1 - 2, hy - 1, zc, P.floor)
    for zz in range(zc - 3, zc + 4):
        for yy in range(hy - 3, hy + 4):
            if (p.get(B.x1 + 1, yy, zz) or '').endswith('_trapdoor'): p.clear(B.x1 + 1, yy, zz)
    # inside: a ladder up all three floors, the miller's bed and chest below, sacks of grain on the middle floor
    lad = ladder(p, B, B.floors)
    R0 = Room(p, P, B, 0, rnd, [lad]); R0.bed(); R0.put('chest', type='single'); R0.put('barrel', facing_in=False, facing='up'); R0.lights(1)
    R1 = Room(p, P, B, B.fh, rnd, [lad])
    for _ in range(3): R1.put('hay_block', facing_in=False, axis='y')
    R1.put('barrel', facing_in=False, facing='up'); R1.lights(1)
    R2 = Room(p, P, B, 2 * B.fh, rnd, [lad, (B.x1 - 2, zc), (B.x1 - 1, zc)]); R2.lights(1)
    return p


# ---------------------------------------------------------------------------------------------------- the other trades
def open_shed(p, P, x0, z0, w, d, h=3):
    """a roof on posts (a lean-to, a work shed), its floor the style's floor"""
    for x in range(x0, x0 + w):
        for z in range(z0, z0 + d):
            p.set(x, 0, z, P.floor if not P.sandy else P.S['pave'][0])
            p.set(x, h + 1, z, P.slab_any, type='bottom')
    for (x, z) in ((x0, z0), (x0 + w - 1, z0), (x0, z0 + d - 1), (x0 + w - 1, z0 + d - 1)):
        p.box(x, 1, z, x, h, z, P.fence)


def sawmill(style, name, seed):
    """the lumberjack's: a shed with the saw (a stonecutter) and a chopping block, logs stacked out in the yard, planks
    under cover, an axe's corner; a bed in the little hut at the back"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 7, 7, 1, rect=True)
    R = Room(p, P, B, B.y0, rnd); R.bed(); R.put('chest', type='single'); R.lights(1)
    show_goods(p, P, B, rnd, 'sawmill', frames=2, table=False)
    log = P.S['log'] if 'log' in P.S['log'] else 'oak_log'
    z = B.z1 + 2
    open_shed(p, P, B.x0, z, 6, 4)
    p.set(B.x0 + 2, 1, z + 1, 'stonecutter', facing='north')   # the saw
    p.set(B.x0 + 4, 1, z + 1, log, axis='y')                   # the chopping block
    for x in range(B.x0, B.x0 + 6):   # logs stacked along the far side
        p.set(x, 1, z + 3, log, axis='x'); 
        if x % 2: p.set(x, 2, z + 3, log, axis='x')
    p.set(B.x0 + 1, 1, z + 2, P.floor); p.set(B.x0 + 1, 2, z + 2, P.floor)   # sawn planks
    return p


def builders_yard(style, name, seed):
    """the builder's: a yard of stone and timber stacked ready, scaffolding up a half-built wall, a workbench, a hut"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 7, 7, 1, rect=True)
    R = Room(p, P, B, B.y0, rnd); R.bed(); R.put('chest', type='single'); R.put('crafting_table', facing_in=False); R.lights(1)
    show_goods(p, P, B, rnd, 'builders_yard', frames=2, table=False)
    z = B.z1 + 2
    for x in range(B.x0, B.x0 + 7):
        for zz in range(z, z + 4): p.set(x, 0, zz, P.S['ground'] if not P.snowy else 'gravel')
    p.set(B.x0 + 1, 1, z, 'crafting_table'); p.set(B.x0 + 2, 1, z, 'stonecutter', facing='north')
    for x in range(B.x0 + 4, B.x0 + 7):   # a wall going up, scaffolding against it
        p.box(x, 1, z + 3, x, 1 + (x - B.x0 - 3), z + 3, P.found)
        p.box(x, 1, z + 2, x, 3, z + 2, 'scaffolding')
    for x in range(B.x0, B.x0 + 3):   # materials stacked
        p.set(x, 1, z + 2, rnd.choice([P.found, P.floor, 'cobblestone', P.S['pave'][0]]))
        if rnd.random() < 0.5: p.set(x, 2, z + 2, P.slab_any, type='bottom')
    return p


def barracks(style, name, seed):
    """the guards': a long hall of bunks, an armoury chest and a rack, a training yard with dummies (hay on posts with
    pumpkins) and a target; a watch platform up a ladder at its end"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 7, 11, 1, rect=True, porch=False)
    R = Room(p, P, B, B.y0, rnd)
    for _ in range(4): R.bed()
    R.put('chest', type='single'); R.put('barrel', facing_in=False, facing='up'); R.put('anvil'); R.lights(2)
    show_goods(p, P, B, rnd, 'barracks', frames=4, table=False)
    z = B.z1 + 2
    for x in range(B.x0, B.x1 + 1):
        for zz in range(z, z + 4): p.set(x, 0, zz, 'coarse_dirt' if not P.sandy else P.S['ground'])
    for x in range(B.x0 + 1, B.x1, 3):   # training dummies
        p.set(x, 1, z + 2, P.fence); p.set(x, 2, z + 2, 'hay_block', axis='y'); p.set(x, 3, z + 2, 'carved_pumpkin', facing='north')
    p.set(B.x1, 1, z + 3, 'hay_block', axis='y'); p.set(B.x1, 2, z + 3, 'target')
    return p


def jeweller(style, name, seed):
    """the jeweller's shop: the cutting wheel (a grindstone), a bench, a display of what's been made under glass"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 7, 9, 1, porch=True)
    if getattr(B, 'round_room', None):
        R = B.round_room; R.put('grindstone'); R.put('smithing_table', facing_in=False); R.bed(); R.lights(1); return p
    R = Room(p, P, B, B.y0, rnd, list(getattr(B, 'trunk', ())))
    R.put('grindstone'); R.put('smithing_table', facing_in=False); R.put('chest', type='single'); R.bed()
    show_goods(p, P, B, rnd, 'jeweller', frames=4, table=False)
    for k, x in enumerate(range(B.x0 + 2, B.x0 + 4)):   # a glass case of gems on its counter
        z = B.zd + 2
        if (x, z) in R.free:
            p.set(x, B.y0 + 1, z, rnd.choice(['emerald_block', 'gold_block', 'lapis_block', 'amethyst_block'])); p.set(x, B.y0 + 2, z, 'glass'); R.free.discard((x, z))
    R.lights(2)
    return p


def alchemist(style, name, seed):
    """the alchemist's: the brewing stand and a cauldron, shelves of potions, herbs drying, a bed at the back"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 9, 9, 1, porch=True, rect=True)
    R = Room(p, P, B, B.y0, rnd)
    spot = R.put(furn(P, 'table'), facing_in=False)   # the brewing stand on a table, the cauldron beside it on the floor
    if spot: p.set(spot[0], R.y + 2, spot[1], 'brewing_stand')
    R.put('water_cauldron', facing_in=False, level=3); R.bed(); R.put('chest', type='single')
    cupboard(p, P, R, rnd, ['glass_bottle*4', 'sugar*2', 'red_mushroom*2', 'glow_berries*2', 'nether_wart_item*3', 'glowstone_dust'], [g for g in GOODS['alchemist'] if item_ok(g)])
    pots = [g for g in GOODS['alchemist'] if item_ok(g)]
    for _ in range(2): R.put(furn(P, 'standing_shelf'), items=','.join(rnd.choice(pots) for _ in range(6)))
    R.lights(2)
    show_goods(p, P, B, rnd, 'alchemist', frames=3)
    return p


def general_store(style, name, seed):
    """the general store: barrels and crates of the everyday, shelves of it, a counter (tables with goods on them)"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p, P, B, hearth = shell(style, name, rnd, 9, 11, 1, porch=True, rect=True)
    R = Room(p, P, B, B.y0, rnd)
    goods = [g for g in GOODS['general_store'] if item_ok(g)]
    for _ in range(3): R.put(furn(P, 'standing_shelf'), items=','.join(rnd.choice(goods) for _ in range(6)))
    for _ in range(3): R.put('barrel', facing_in=False, facing='up')
    R.put('chest', type='single'); R.bed(); R.lights(2)
    show_goods(p, P, B, rnd, 'general_store', frames=3)
    return p


WARES = {   # what each trader sells, laid out on the counter and on the shelf behind (real items: you can tell the stall)
    'merchant': ['candle', 'string', 'bowl', 'paper', 'book', 'leather', 'lead', 'compass', 'clock', 'honey_bottle'],
    'baker': ['bread', 'bread', 'pumpkin_pie', 'cake', 'cookie', 'apple', 'bread', 'cookie'],
    'butcher': ['cooked_beef', 'cooked_porkchop', 'cooked_chicken', 'beef', 'porkchop', 'mutton', 'chicken', 'cooked_mutton'],
    'fisherman': ['cod', 'salmon', 'cooked_cod', 'cooked_salmon', 'tropical_fish', 'cod', 'salmon'],
    'farmer': ['carrot', 'potato', 'beetroot', 'wheat', 'melon_slice', 'apple', 'egg', 'pumpkin'],
    'jeweller': ['emerald', 'diamond', 'gold_ingot', 'ruby', 'sapphire', 'amethyst_shard', 'gold_nugget', 'lapis_lazuli'],
}
STALLS = {'merchant': 'barrel', 'baker': 'smoker', 'butcher': 'smoker', 'fisherman': 'barrel', 'farmer': 'composter', 'jeweller': 'grindstone'}   # the trader's station
_KEYS = None


def known(k):
    """is k a block of ours (or one village_pieces.cpp turns into ours)?"""
    global _KEYS
    if _KEYS is None:
        try:
            d = json.loads((pathlib.Path(__file__).resolve().parents[2] / 'data' / 'blocks.json').read_text())
            _KEYS = {b.get('key') for b in d['blocks']} | set(d.get('aliases', {}).keys())
        except Exception: _KEYS = set()
    return not _KEYS or k in _KEYS


def furn(P, kind):
    """our furniture of the style's wood: 'table', 'chair', 'standing_shelf', 'shelf'"""
    k = '%s_%s' % (P.wood, kind)
    return k if known(k) else 'oak_' + kind


def shelf_of(P):
    k = P.wood + '_standing_shelf'
    return k if known(k) else 'oak_standing_shelf'


def stall_frame(p, P, x0, z0, wares, rnd, w=5, station=None, cols=None):
    """a stall: four posts and a striped cloth roof; at the front a counter of tables (fences under piles of the wares,
    which become our tables with the goods lying on them); behind, the trader's station and a standing shelf stocked
    with more; the trader comes in at the side"""
    cols = cols or rnd.choice([('red_wool', 'white_wool'), ('blue_wool', 'white_wool'), ('yellow_wool', 'orange_wool'), ('green_wool', 'white_wool'), ('purple_wool', 'yellow_wool')])
    if P.decor == 'christmas': cols = ('red_wool', 'white_wool')
    if P.decor == 'halloween': cols = ('orange_wool', 'black_wool')
    goods = WARES[wares]
    for x in range(x0, x0 + 4):
        for z in range(z0, z0 + w): p.set(x, 0, z, P.S['pave'][0] if not P.sandy else P.S['pave'][1])
    for (x, z) in ((x0, z0), (x0, z0 + w - 1), (x0 + 3, z0), (x0 + 3, z0 + w - 1)): p.box(x, 1, z, x, 2, z, P.fence)
    for x in range(x0, x0 + 4):
        for z in range(z0, z0 + w): p.set(x, 3, z, cols[(z - z0) % 2])
    for z in range(z0 + 1, z0 + w - 1):   # the counter: our tables with the wares lying on them
        p.set(x0 + 1, 1, z, furn(P, 'table'))
        g = [rnd.choice(goods) for _ in range(rnd.randint(2, 4))]
        p.set(x0 + 1, 2, z, 'item_pile', items=','.join(g))
    if station:
        p.set(x0 + 3, 1, z0 + 1, station, **({'facing': 'west'} if station in ('smoker', 'grindstone') else {'facing': 'up'} if station == 'barrel' else {}))
    for z in range(z0 + (2 if station else 1), z0 + w - 1):   # the shelf behind, facing the counter
        p.set(x0 + 3, 1, z, shelf_of(P), facing='west', items=','.join(rnd.choice(goods) for _ in range(6)))
    p.set(x0 + 2, 1, z0 + w // 2, furn(P, 'chair'), facing='west')   # the trader's chair behind the counter, facing the customers


def market_stall(style, name, seed, wares):
    """a trader's stall on the street (the merchant, the baker, the butcher, the fisherman, the farmer, the jeweller)"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'houses', style)
    stall_frame(p, P, 0, 0, wares, rnd, station=STALLS[wares])
    p.joint(0, 1, 2, 'west_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/streets' % style)
    return p

# ---------------------------------------------------------------------------------------------------- streets
def street_joint(p, x, z, facing, style):
    p.joint(x, 1, z, facing + '_up', 'minecraft:street', 'minecraft:street', 'minecraft:village/%s/streets' % style)


def house_joint(p, x, z, facing, style):
    p.joint(x, 1, z, facing + '_up', 'minecraft:building_entrance', 'minecraft:building_entrance', 'minecraft:village/%s/houses' % style)


def straight(style, name, seed, L):
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'streets', style)
    for z in range(L):
        for x in range(3): p.set(x, 0, z, P.pathb())
    street_joint(p, 1, 0, 'north', style); street_joint(p, 1, L - 1, 'south', style)
    for z in range(1, L - 1):
        house_joint(p, 0, z, 'west', style); house_joint(p, 2, z, 'east', style)
    return p


def corner(style, name, seed, east=True):
    """a street turning: in from the north, out to the east (or the west)"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'streets', style)
    K = 7
    cells = {(x, z) for x in range(3) for z in range(5)} | {(x, z) for x in range(K) for z in range(2, 5)}
    if not east: cells = {(K - 1 - x, z) for (x, z) in cells}
    for (x, z) in cells: p.set(x, 0, z, P.pathb())
    nx = 1 if east else K - 2
    street_joint(p, nx, 0, 'north', style)
    street_joint(p, K - 1 if east else 0, 3, 'east' if east else 'west', style)
    for x in range(K):   # houses along the outer side of the bend
        house_joint(p, x, 4, 'south', style)
    for z in range(1, 4):
        house_joint(p, 0 if east else K - 1, z, 'west' if east else 'east', style)
    decorate(p, P, rnd, [(x, z) for x in range(K) for z in range(2) if (x, z) not in cells])
    return p


def crossroad(style, name, seed, arms=('north', 'south', 'east', 'west'), S=9):
    """streets meeting, the corners between them planted: a lamp, a bench, flowers, a little tree, a well"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'streets', style)
    c = S // 2
    road = set()
    for t in range(S):
        for w in (c - 1, c, c + 1):
            if t <= c + 1 or 'south' in arms:
                if t >= c - 1 or 'north' in arms: road.add((w, t))
            if t <= c + 1 or 'east' in arms:
                if t >= c - 1 or 'west' in arms: road.add((t, w))
    for (x, z) in road: p.set(x, 0, z, P.pathb())
    for a in arms:
        street_joint(p, *{'north': (c, 0), 'south': (c, S - 1), 'west': (0, c), 'east': (S - 1, c)}[a], a, style)
    rest = [(x, z) for x in range(S) for z in range(S) if (x, z) not in road]
    decorate(p, P, rnd, rest, big=True)
    return p


def decorate(p, P, rnd, cells, big=False):
    """the ground off the street: grass (sand, snow), and in each corner a thing or two"""
    g = P.S['ground']
    for (x, z) in cells: p.set(x, 0, z, g)
    cs = set(cells)
    corners = []
    for (x, z) in cells:   # group the free ground into its corners (flood fill)
        if any((x, z) in c for c in corners): continue
        grp = set(); st = [(x, z)]
        while st:
            q = st.pop()
            if q in grp or q not in cs: continue
            grp.add(q); st += [(q[0] + 1, q[1]), (q[0] - 1, q[1]), (q[0], q[1] + 1), (q[0], q[1] - 1)]
        corners.append(grp)
    things = ['lamp', 'flowers', 'flowers', 'barrels', 'hay', 'bush']
    for grp in corners:
        g2 = sorted(grp)
        if len(g2) < 2: continue
        kind = rnd.choice(things)
        x0 = min(q[0] for q in g2); z0 = min(q[1] for q in g2); x1 = max(q[0] for q in g2); z1 = max(q[1] for q in g2)
        cx, cz = (x0 + x1) // 2, (z0 + z1) // 2
        if (cx, cz) not in grp: cx, cz = g2[len(g2) // 2]
        if kind == 'lamp' or len(g2) < 6:
            lamp(p, P, cx, cz)
        elif kind == 'flowers':
            for q in g2:
                if rnd.random() < 0.6: (plant_at(p, P, q[0], 1, q[1]) if not P.sandy else p.set(q[0], 1, q[1], 'dead_bush'))
            lamp(p, P, g2[0][0], g2[0][1])
        elif kind == 'bush':
            for q in g2:
                if rnd.random() < 0.3: leaves(p, q[0], 1, q[1], P.leaves)
            lamp(p, P, g2[-1][0], g2[-1][1])
        elif kind == 'hay':
            p.set(cx, 1, cz, 'hay_block', axis='y')
            if (cx, cz + 1) in grp and not P.sandy: p.set(cx, 1, cz + 1, 'pumpkin')
        elif kind == 'barrels':
            for q in g2[:2]: p.set(q[0], 1, q[1], 'barrel', facing='up')
        else:
            lamp(p, P, cx, cz)


def bench(p, P, cells, back):
    """our bench (one per cell), its back toward `back`: the sitters face the other way"""
    for (x, z) in cells: p.set(x, 1, z, furn(P, 'bench'), facing=OPP[back])


def lamp(p, P, x, z):
    post = P.fence if not P.flat else P.S['wall']
    p.set(x, 1, z, P.S['wall']); p.set(x, 2, z, post); p.set(x, 3, z, post)
    p.set(x, 4, z, P.lamp, **({'hanging': False} if 'lantern' in P.lamp else {}))


def tree(p, P, x, z, rnd):
    log = P.S['log']
    h = rnd.randint(3, 4)
    for y in range(1, h + 1): p.set(x, y, z, log, axis='y')
    for dx in (-1, 0, 1):
        for dz in (-1, 0, 1):
            for y in (h, h + 1):
                if (dx or dz or y > h) and not (abs(dx) + abs(dz) == 2 and y == h + 1): leaves(p, x + dx, y, z + dz, P.leaves)
    leaves(p, x, h + 2, z, P.leaves)


def mini_well(p, P, x0, z0):
    st = P.S['pave'][0]
    for dx in range(3):
        for dz in range(3):
            if dx == 1 and dz == 1: p.set(x0 + 1, 0, z0 + 1, 'water', level=0)
            else: p.set(x0 + dx, 1, z0 + dz, st)
    for (dx, dz) in ((0, 0), (2, 0), (0, 2), (2, 2)): p.set(x0 + dx, 2, z0 + dz, P.fence)   # a post at each corner
    for dx in range(3):
        for dz in range(3): p.set(x0 + dx, 3, z0 + dz, P.rslab, type='bottom')


def terminator(style, name, seed):
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'terminators', style)
    for x in range(3):
        for z in range(3): p.set(x, 0, z, P.pathb())
    p.joint(0, 1, 1, 'west_up', 'minecraft:street', 'minecraft:street', 'minecraft:empty')
    if rnd.random() < 0.5: lamp(p, P, 2, 1)
    else: p.set(2, 1, 1, 'barrel', facing='up'); p.set(2, 1, 0, 'hay_block', axis='y')
    return p


# ---------------------------------------------------------------------------------------------------- town squares
def square(style, name, seed, kind):
    """the town centre: a paved square with the bell (a well under a roof, a market, a great tree, an oasis pool), lamps
    at its corners, benches and beds of flowers; streets leave it from the middle of each side"""
    rnd = random.Random(seed); P = Pal(style, rnd)
    p = Piece(name, 'town_centers', style)
    S = rnd.choice([15, 17]) if kind != 'market' else 19
    c = S // 2
    for x in range(S):
        for z in range(S):
            edge = x in (0, S - 1) or z in (0, S - 1)
            p.set(x, 0, z, P.pave() if not edge or abs(x - c) <= 1 or abs(z - c) <= 1 else P.S['ground'])
    for a, (x, z) in (('north', (c, 0)), ('south', (c, S - 1)), ('west', (0, c)), ('east', (S - 1, c))):
        street_joint(p, x, z, a, style)
    for (x, z) in ((2, 2), (S - 3, 2), (2, S - 3), (S - 3, S - 3)): lamp(p, P, x, z)
    for (x, z, f) in ((c - 4, 2, 'north'), (c + 3, 2, 'north'), (c - 4, S - 3, 'south'), (c + 3, S - 3, 'south')):   # benches facing in
        bench(p, P, [(x, z), (x + 1, z)], f)
    for (x0, z0) in ((1, c - 4), (S - 3, c - 4), (1, c + 3), (S - 3, c + 3)):   # flower beds
        for dx in range(2):
            for dz in range(2):
                p.set(x0 + dx, 0, z0 + dz, P.S['ground'] if not P.snowy else 'grass_block')
                if P.sandy: leaves(p, x0 + dx, 1, z0 + dz, P.leaves) if rnd.random() < 0.5 else p.set(x0 + dx, 1, z0 + dz, 'dead_bush')
                elif P.snowy: p.set(x0 + dx, 1, z0 + dz, 'sweet_berry_bush', age=3) if rnd.random() < 0.5 else p.set(x0 + dx, 1, z0 + dz, 'fern')
                else: plant_at(p, P, x0 + dx, 1, z0 + dz)
    if kind == 'well':
        x0 = z0 = c - 2
        st = P.S['pave'][0]
        for dx in range(5):
            for dz in range(5):
                inner = 1 <= dx <= 3 and 1 <= dz <= 3
                if inner: p.set(x0 + dx, 0, z0 + dz, 'water', level=0)
                elif not (dx in (0, 4) and dz in (0, 4)): p.set(x0 + dx, 1, z0 + dz, P.S['stone_slab'], type='bottom')
                else:
                    p.set(x0 + dx, 1, z0 + dz, st)
                    for y in (2, 3): p.set(x0 + dx, y, z0 + dz, P.fence)
        for dx in range(-1, 6):
            for dz in range(-1, 6):
                d = max(abs(dx - 2), abs(dz - 2))
                p.set(x0 + dx, 4 + (1 if d <= 1 else 0), z0 + dz, P.slab_any, type='bottom')
        p.set(c, 5, c, P.rfull); p.set(c, 4, c, 'bell', attachment='ceiling', facing='north')
    elif kind == 'market':
        p.set(c, 1, c, P.S['pave'][0]); p.set(c, 2, c, P.S['wall']); p.set(c, 3, c, 'bell', attachment='floor', facing='north')
        kinds = list(WARES); rnd.shuffle(kinds)
        for i, (sx, sz) in enumerate(((c - 7, c - 7), (c + 3, c - 7), (c - 7, c + 3), (c + 3, c + 3))):   # four stalls round the bell
            stall_frame(p, P, sx, sz, kinds[i % len(kinds)], rnd, w=5)
    elif kind == 'tree':
        for dx in range(-2, 3):
            for dz in range(-2, 3):
                ring = max(abs(dx), abs(dz)) == 2
                p.set(c + dx, 1, c + dz, P.S['stone_slab'] if ring else P.S['ground'] if not P.snowy else 'grass_block', **({'type': 'top'} if ring else {}))
        big_tree(p, P, c, c, rnd)
        p.set(c + 3, 1, c, P.S['wall']); p.set(c + 3, 2, c, P.fence); p.set(c + 3, 3, c, 'bell', attachment='floor', facing='west')
    elif kind == 'xmas':   # Christmas: the town's great tree, lit, with presents round it
        xmas_tree(p, P, c, c, rnd)
        p.set(c + 4, 1, c, P.S['wall']); p.set(c + 4, 2, c, P.fence); p.set(c + 4, 3, c, 'bell', attachment='floor', facing='west')
        for (dx, dz) in ((-4, -4), (4, 4), (-4, 4)):
            p.set(c + dx, 1, c + dz, 'snow_block'); p.set(c + dx, 2, c + dz, 'snow_block'); p.set(c + dx, 3, c + dz, 'carved_pumpkin', facing='south')   # a snowman
    elif kind == 'pumpkins':   # Halloween: a pumpkin patch, a dead tree, a witch's cauldron, the bell on a post
        for dx in range(-3, 4):
            for dz in range(-3, 4):
                if abs(dx) + abs(dz) <= 4 and (dx or dz):
                    p.set(c + dx, 0, c + dz, 'coarse_dirt')
                    if rnd.random() < 0.45: p.set(c + dx, 1, c + dz, rnd.choice(['pumpkin', 'carved_pumpkin', 'jack_o_lantern']), facing=rnd.choice(['north', 'south', 'east', 'west']))
        for y in range(1, 6): p.set(c, y, c, 'dark_oak_log', axis='y')
        for (dx, dz, y) in ((1, 0, 4), (2, 0, 4), (-1, 0, 3), (0, 1, 5), (0, -1, 4)): p.set(c + dx, y, c + dz, 'dark_oak_fence')
        p.set(c + 5, 1, c + 1, 'water_cauldron', level=3); p.set(c + 5, 0, c + 1, 'campfire', lit=True) if False else None
        p.set(c - 5, 1, c, P.S['wall']); p.set(c - 5, 2, c, P.fence); p.set(c - 5, 3, c, 'bell', attachment='floor', facing='east')
        for k in range(4): grave(p, P, c - 6 + k * 3, c + 6)
    elif kind == 'mushroom':   # a giant mushroom over the square, the bell under its cap
        for y in range(1, 7): p.set(c, y, c, 'mushroom_stem')
        for dx in range(-4, 5):
            for dz in range(-4, 5):
                d = math.hypot(dx, dz)
                if d < 4.5: p.set(c + dx, 7, c + dz, 'red_mushroom_block')
                if d < 3.2: p.set(c + dx, 8, c + dz, 'red_mushroom_block')
        p.set(c + 1, 6, c, 'bell', attachment='ceiling', facing='north') if False else p.set(c + 2, 6, c, 'bell', attachment='ceiling', facing='north')
        p.set(c - 2, 6, c, 'shroomlight')
    elif kind == 'palms':   # the beach's: a sandy square with palms and a well
        mini_well(p, P, c - 1, c - 1)
        for (dx, dz) in ((-4, -3), (4, 3), (-3, 4)): palm(p, P, c + dx, c + dz, rnd)
        p.set(c + 4, 1, c - 4, P.S['wall']); p.set(c + 4, 2, c - 4, P.fence); p.set(c + 4, 3, c - 4, 'bell', attachment='floor', facing='north')
    elif kind == 'oasis':   # the desert's: a pool with palms
        for dx in range(-3, 4):
            for dz in range(-3, 4):
                r = abs(dx) + abs(dz)
                if r <= 3: p.set(c + dx, 0, c + dz, 'water', level=0)
                elif r == 4: p.set(c + dx, 1, c + dz, 'cut_sandstone_slab', type='bottom')
        for (dx, dz) in ((-3, -2), (3, 2)): palm(p, P, c + dx, c + dz, rnd)
        p.set(c, 1, c + 5, 'chiseled_sandstone'); p.set(c, 2, c + 5, 'sandstone_wall'); p.set(c, 3, c + 5, 'bell', attachment='floor', facing='north')
    return p


def big_tree(p, P, x, z, rnd):
    log = P.S['log']
    h = 6
    for y in range(2, 2 + h): p.set(x, y, z, log, axis='y')
    for y in range(h - 1, h + 3):
        r = 3 if y < h + 1 else 2 if y < h + 2 else 1
        for dx in range(-r, r + 1):
            for dz in range(-r, r + 1):
                if dx * dx + dz * dz <= r * r + 1 and (dx or dz or y > h): leaves(p, x + dx, y + 1, z + dz, P.leaves)


def palm(p, P, x, z, rnd):
    for y in range(1, 6): p.set(x, y, z, 'jungle_log', axis='y')
    for (dx, dz) in ((1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0), (0, 2), (0, -2)): leaves(p, x + dx, 6 - (abs(dx) + abs(dz) > 1), z + dz, P.leaves)
    leaves(p, x, 6, z, P.leaves)


# ---------------------------------------------------------------------------------------------------- all of it
def pieces(style):
    out = []; s = lambda n: hash_seed(style, n)
    for i in range(1, 7): out.append(house(style, '%s_small_house_%d' % (style, i), s('small%d' % i), 'small'))
    for i in range(1, 4): out.append(house(style, '%s_medium_house_%d' % (style, i), s('medium%d' % i), 'medium'))
    for i in range(1, 3): out.append(house(style, '%s_big_house_%d' % (style, i), s('big%d' % i), 'big'))
    for key in JOBS: out.append(workshop(style, '%s_%s_1' % (style, key), key, s(key)))
    out.append(library(style, '%s_library_1' % style, s('library')))
    out.append(temple(style, '%s_temple_1' % style, s('temple')))
    out.append(tavern(style, '%s_tavern_1' % style, s('tavern')))
    out.append(farm(style, '%s_small_farm_1' % style, s('farm1')))
    out.append(farm(style, '%s_small_farm_2' % style, s('farm2')))
    out.append(farm(style, '%s_large_farm_1' % style, s('farm3'), big=True))
    out.append(animal_pen(style, '%s_animal_pen_1' % style, s('pen1')))
    out.append(animal_pen(style, '%s_animal_pen_2' % style, s('pen2'), sheep=True))
    out.append(stable(style, '%s_stable_1' % style, s('stable')))
    out.append(windmill(style, '%s_windmill_1' % style, s('windmill')))
    for i, L in enumerate([8, 10, 12, 16, 20, 24], 1): out.append(straight(style, 'straight_%02d' % i, s('st%d' % i), L))
    out.append(corner(style, 'corner_01', s('c1'), east=True)); out.append(corner(style, 'corner_02', s('c2'), east=False))
    out.append(crossroad(style, 'crossroad_01', s('x1'))); out.append(crossroad(style, 'crossroad_02', s('x2'), S=11))
    out.append(crossroad(style, 'split_01', s('t1'), arms=('north', 'south', 'east'))); out.append(crossroad(style, 'split_02', s('t2'), arms=('north', 'south', 'west')))
    kinds = STYLES[style]['squares']
    for i, k in enumerate(kinds, 1): out.append(square(style, 'meeting_point_%d' % i, s('sq%d' % i), k))
    for i in (1, 2): out.append(terminator(style, 'terminator_%02d' % i, s('term%d' % i)))
    # every other trade its own building, and the traders' stalls
    out.append(sawmill(style, '%s_sawmill_1' % style, s('sawmill')))
    out.append(builders_yard(style, '%s_builders_yard_1' % style, s('builders')))
    out.append(barracks(style, '%s_barracks_1' % style, s('barracks')))
    out.append(jeweller(style, '%s_jeweller_1' % style, s('jeweller')))
    out.append(alchemist(style, '%s_alchemist_1' % style, s('alchemist')))
    out.append(general_store(style, '%s_general_store_1' % style, s('store')))
    for w in STALLS: out.append(market_stall(style, '%s_market_stall_%s_1' % (style, w), s('stall' + w), w))
    return out


def hash_seed(*parts):
    h = 2166136261
    for ch in '/'.join(parts).encode(): h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return h


def main(d):
    d = pathlib.Path(d); out = d / 'villages'
    files = []
    for style in STYLES:
        sd = out / style
        if sd.is_dir():
            for f in sd.rglob('*.nbt'): f.unlink()
        for pc in pieces(style):
            rel = '%s/%s/%s.nbt' % (style, pc.kind, pc.name)
            (out / rel).parent.mkdir(parents=True, exist_ok=True)
            nbt_io.write(out / rel, pc.template())
            files.append(rel)
    (out / 'index.json').write_text(json.dumps({'source': "DokuQuest's own (tools/port/village_builder.py)", 'pieces': files}, indent=0))
    # the styles beyond the game's five, for gen/structures_townbld.cpp vstyle(): each a base style's look with its own
    # materials (its walls, gates and lamp posts, the hall's and the windmill's woods, its streets)
    extra = []
    for k, S in STYLES.items():
        if k in ('plains', 'savanna', 'taiga', 'snowy', 'desert'): continue
        wood = S['wood']
        extra.append({'key': k, 'base': S['base'], 'set': {
            'planks': ROOFMAT.get(wood, ('', '', S['floor']))[2] if wood in ROOFMAT else S['floor'], 'log': S['log'], 'floor': S['floor'], 'stairs': wood + '_stairs',
            'slab': wood + '_slab', 'fence': S['fence'], 'gate': S['gate'], 'door': S['door'], 'trapdoor': S['trap'], 'wood': wood, 'fwood': wood,
            'stone': S['pave'][0], 'stoneSlab': S['stone_slab'], 'stoneStairs': S['stone_stairs'], 'lampPost': S['fence'], 'leaves': S['leaves'],
            'base': S['found'][0], 'base2': S['found'][0], 'base3': S['found'][-1], 'plaster': S['fill_hi'][0], 'frame': S['frame'] if 'log' in S['frame'] else S['log'],
            'path': S['path'], 'bed': S['bed'], 'banner': [S['banner'] + '_wool', 'white_wool'],
            'roofs': [[ROOFMAT[r][0], ROOFMAT[r][1]] for r in S['roof'] if ROOFMAT[r][0] != ROOFMAT[r][1]] or [['dark_oak_stairs', 'dark_oak_slab']]}})
    (out / 'styles.json').write_text(json.dumps({'source': "DokuQuest's own (tools/port/village_builder.py)", 'styles': extra}, indent=0))
    print('village_builder: %d pieces into %s' % (len(files), out))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
