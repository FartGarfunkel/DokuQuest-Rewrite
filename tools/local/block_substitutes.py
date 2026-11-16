#!/usr/bin/env python3
"""Other mods' blocks in Soulrend's structures (Create, Quark, Supplementaries, Farmer's Delight, The Graveyard, Alex's Mobs...)
and newer Minecraft names we lack -> the nearest of ours: data/block_substitutes.json, read by gen/village_pieces.cpp mapState
before its name-guessing fallback. Each entry lists choices, the first one we have wins; "air" leaves the cell empty
(placeholders, machines' moving parts, things that would only be clutter). Run after adding blocks to re-check:
  python3 tools/port/block_substitutes.py [data-dir]"""
import json, pathlib, re, sys

COLORS = ['white', 'orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black']
T = {}
def put(names, *choices):
    for n in ([names] if isinstance(names, str) else names): T[n] = list(choices)

# ---- Create: its stones (cut, polished, layered...), the copycats, the machines
STONE = {'deepslate': ['polished_slate', 'slate_tiles', 'polished_deepslate', 'cobbled_slate'], 'scoria': ['blackstone', 'polished_blackstone', 'basalt'],
         'scorchia': ['polished_blackstone', 'blackstone'], 'ochrum': ['yellow_terracotta', 'smooth_sandstone'], 'veridium': ['green_terracotta', 'prismarine'],
         'crimsite': ['red_terracotta', 'red_nether_bricks'], 'asurine': ['blue_terracotta', 'light_blue_terracotta'], 'dripstone': ['dripstone_block', 'granite'],
         'limestone': ['calcite', 'diorite'], 'tuff': ['tuff', 'andesite'], 'granite': ['polished_granite'], 'diorite': ['polished_diorite'], 'andesite': ['polished_andesite'],
         'calcite': ['calcite', 'diorite']}
for st, ch in STONE.items():
    for f in ('cut_%s', 'polished_cut_%s', '%s', 'layered_%s', 'cut_%s_bricks', 'small_%s_bricks', '%s_pillar'):
        put('create:' + f % st, *(ch + ['stone_bricks']))
    for shp in ('slab', 'stairs', 'wall'):
        put('create:cut_%s_%s' % (st, shp), *[c + '_' + shp for c in ch] + ['stone_brick_' + shp])
put(['create:copycat_step', 'create:copycat_panel'], 'stone_brick_slab', 'stone_slab')
put(['create:water_wheel_structure', 'create:honey', 'create:clipboard', 'create:placard', 'create:redstone_contact', 'create:pulse_extender',
     'create:powered_toggle_latch', 'create:nixie_tube', 'create:display_board', 'create:copper_backtank', 'create:controls', 'create:contraption_controls',
     'create:speedometer', 'create:stressometer', 'create:steam_whistle', 'create:nozzle', 'create:portable_fluid_interface', 'create:item_drain'], 'air')
put(['create:water_wheel', 'create:large_water_wheel', 'create:flywheel'], 'stripped_spruce_log', 'spruce_log')
put(['create:encased_chain_drive', 'create:gearshift', 'create:sequenced_gearshift', 'create:clutch', 'create:rotation_speed_controller',
     'create:mechanical_bearing', 'create:windmill_bearing', 'create:encased_fan', 'create:mechanical_pump', 'create:mechanical_drill', 'create:mechanical_press',
     'create:mechanical_mixer', 'create:mechanical_saw', 'create:mechanical_arm', 'create:steam_engine', 'create:linear_chassis', 'create:radial_chassis',
     'create:gantry_carriage', 'create:crushing_wheel', 'create:hose_pulley', 'create:pulley_magnet', 'create:schematic_table', 'create:fluid_valve'],
    'gearbox', 'stripped_spruce_wood', 'spruce_planks')
put(['create:chute', 'create:andesite_funnel', 'create:andesite_tunnel', 'quark:chute'], 'hopper')
put(['create:fluid_tank', 'create:item_vault', 'create:spout', 'create:basin', 'create:depot'], 'cauldron')
put('create:piston_extension_pole', 'oak_fence')
put('create:track', 'rail')
put(['create:copper_valve_handle', 'create:gray_valve_handle', 'create:black_valve_handle'], 'lever')
put('create:andesite_alloy_block', 'polished_andesite')
put(['create:zinc_block', 'create:raw_zinc_block'], 'smooth_stone')
put('create:brass_block', 'gold_block')
for a, b in (('waxed_exposed_copper_tiles', 'exposed_cut_copper'), ('waxed_weathered_copper_tiles', 'weathered_cut_copper'), ('oxidized_copper_tiles', 'oxidized_cut_copper'),
             ('copper_tiles', 'cut_copper'), ('exposed_copper_tiles', 'exposed_cut_copper'), ('weathered_copper_tiles', 'weathered_cut_copper')):
    put('create:' + a, b, 'cut_copper')
for c in COLORS:
    put('create:%s_seat' % c, '%s_carpet' % c, 'carpet')
    put('create:%s_toolbox' % c, 'air')
    put('create:%s_sail' % c, '%s_wool' % c, 'wool')
    put('supplementaries:awning_%s' % c, '%s_wool' % c, 'wool')
    put(['supplementaries:present_%s' % c, 'supplementaries:trapped_present_%s' % c], '%s_wool' % c, 'wool')
    put('supplementaries:ceiling_banner_%s' % c, 'air')
    put('supplementaries:flag_%s' % c, 'air')
    put('quark:%s_stool' % c, 'oak_slab')
    put('farmersdelight:%s_canvas_wall_sign' % c, 'air')
    for corp in ('%s_corundum', 'waxed_%s_corundum'):
        put('quark:' + corp % c, '%s_stained_glass' % c, 'glass')
    put('quark:%s_corundum_cluster' % c, 'amethyst_cluster')
for c in ('indigo', 'violet'):
    put(['quark:%s_corundum' % c, 'quark:waxed_%s_corundum' % c], 'purple_stained_glass', 'glass')
    put('quark:%s_corundum_cluster' % c, 'amethyst_cluster')
# the working mills (ours: sim/mechanics): a windmill bearing is our windmill (its sails our own, drawn on it), the shafts our
# axles, the gearboxes and cogs our gearbox, the millstone ours, the water wheel, saw and crushing wheel our machines; the
# assembled sails' cloth goes (our windmill draws its own)
put('create:windmill_bearing', 'windmill')
put('create:millstone', 'millstone')
put(['create:gearbox', 'create:vertical_gearbox', 'create:cogwheel', 'create:large_cogwheel'], 'gearbox')
put(['create:shaft', 'create:andesite_encased_shaft', 'create:brass_encased_shaft'], 'axle')
put(['create:water_wheel', 'create:large_water_wheel'], 'waterwheel')
put('create:mechanical_saw', 'sawmill')
put('create:crushing_wheel', 'crusher')
put(['create:white_sail', 'create:sail_frame'] + ['create:%s_sail' % c for c in ('orange', 'magenta', 'light_blue', 'yellow', 'lime', 'pink', 'gray', 'light_gray', 'cyan', 'purple', 'blue', 'brown', 'green', 'red', 'black')], 'air')
put(['create:brown_toolbox'], 'air')

# ---- Supplementaries
put(['supplementaries:notice_board', 'supplementaries:goblet', 'supplementaries:wind_vane', 'supplementaries:globe', 'supplementaries:globe_sepia',
     'supplementaries:lunch_basket', 'supplementaries:lumisene', 'supplementaries:crystal_display', 'supplementaries:blaze_rod', 'supplementaries:quark/hanging_sign_blossom',
     'supplementaries:cog_block'], 'air')
put('supplementaries:cage', 'iron_bars')
put(['supplementaries:iron_gate', 'supplementaries:gold_gate'], 'iron_bars')
put(['supplementaries:sconce', 'supplementaries:sconce_wall'], 'torch')
put(['supplementaries:sconce_soul', 'supplementaries:sconce_wall_soul'], 'soul_torch', 'torch')
put('supplementaries:planter', 'terracotta', 'brown_terracotta')
put(['supplementaries:present', 'supplementaries:trapped_present'], 'red_wool', 'wool')
put('supplementaries:awning', 'white_wool', 'wool')
put('supplementaries:stone_tile', 'polished_andesite', 'stone_bricks')
put('supplementaries:blackstone_tile', 'polished_blackstone_bricks', 'polished_blackstone')
put('supplementaries:checker_block', 'chiseled_quartz_block', 'quartz_block')
put(['supplementaries:flax_block', 'supplementaries:fodder'], 'hay_bale', 'hay_block')
put(['supplementaries:flax', 'supplementaries:wild_flax'], 'tall_grass', 'fern')
put('supplementaries:potted_flax', 'flower_pot')
put('supplementaries:fire_pit', 'campfire')
put(['supplementaries:daub', 'supplementaries:daub_frame', 'supplementaries:daub_brace', 'supplementaries:daub_cross_brace'], 'white_terracotta', 'calcite')
put(['supplementaries:timber_frame', 'supplementaries:timber_brace', 'supplementaries:timber_cross_brace'], 'dark_oak_planks')
put('supplementaries:fine_wood', 'planks', 'oak_planks')
put('supplementaries:speaker_block', 'note_block')
put('supplementaries:pulley_block', 'stripped_oak_log', 'log')
put('supplementaries:spring_launcher', 'piston')
put('supplementaries:flint_block', 'coal_block')
put('supplementaries:feather_block', 'white_wool', 'wool')
put('supplementaries:safe', 'iron_block')
put('supplementaries:soap_block', 'white_concrete')
put(['supplementaries:relayer', 'supplementaries:redstone_illuminator'], 'redstone_lamp')

# ---- Quark
put('quark:azalea_wood', 'oak_wood', 'log')
put(['quark:blossom_wood', 'quark:blossom_log'], 'cherry_wood', 'cherry_log')
put('quark:stripped_blossom_wood', 'stripped_cherry_wood', 'cherry_planks')
put('quark:blossom_planks', 'cherry_planks')
put('quark:blossom_fence', 'cherry_fence', 'oak_fence')
put('quark:blossom_fence_gate', 'cherry_fence_gate', 'oak_fence_gate')
put('quark:blossom_wall_sign', 'cherry_sign', 'oak_sign')
put('quark:azalea_fence', 'oak_fence')
put('quark:azalea_fence_gate', 'oak_fence_gate')
put('quark:ancient_fence', 'dark_oak_fence')
for n, c in (('oak', 'moss_carpet'), ('spruce', 'moss_carpet'), ('birch', 'lime_carpet'), ('jungle', 'moss_carpet'), ('dark_oak', 'green_carpet'), ('azalea', 'moss_carpet'),
             ('flowering_azalea', 'moss_carpet'), ('yellow_blossom', 'yellow_carpet'), ('orange_blossom', 'orange_carpet'), ('red_blossom', 'red_carpet'),
             ('blue_blossom', 'light_blue_carpet'), ('lavender_blossom', 'purple_carpet'), ('acacia', 'moss_carpet'), ('mangrove', 'moss_carpet'), ('cherry', 'pink_carpet')):
    put('quark:%s_leaf_carpet' % n, c, 'moss_carpet', 'green_carpet')
put(['quark:paper_wall', 'quark:paper_wall_big', 'quark:paper_wall_sakura'], 'white_stained_glass_pane', 'glass_pane')
put('quark:iron_rod', 'chain', 'iron_bars')
put('quark:iron_plate', 'iron_block')
put('quark:rusty_iron_plate', 'exposed_copper', 'terracotta')
put('quark:grate', 'iron_trapdoor', 'iron_bars')
put('quark:glow_lichen_growth', 'glow_lichen', 'air')
put('quark:stick_block', 'bamboo_block', 'hay_bale')
put(['quark:monster_box', 'quark:ender_watcher'], 'air')
put(['quark:apple_crate', 'quark:potato_crate', 'quark:carrot_crate', 'quark:golden_apple_crate', 'quark:beetroot_crate', 'quark:crate'], 'barrel')
put('quark:bonded_leather', 'brown_wool', 'wool')
put('quark:bonded_rabbit_hide', 'white_wool', 'wool')
put('quark:charcoal_block', 'coal_block')
put('quark:mud_brick_lattice', 'mud_brick_wall', 'oak_fence')
put(['quark:bamboo_mat', 'quark:bamboo_mat_carpet'], 'yellow_carpet', 'carpet')
put('quark:cactus_block', 'green_terracotta')
put('quark:gravisand', 'sand')
put('quark:jasper', 'granite')
put('quark:polished_jasper', 'polished_granite', 'granite')
put('quark:midori_block', 'lime_terracotta', 'green_terracotta')
put('quark:myalite_crystal', 'amethyst_block', 'purple_stained_glass')
put('quark:redstone_randomizer', 'repeater')
for p in ('rose', 'wheat', 'cocoa_bean', 'chorus_weeds', 'chorus_twist', 'chorus'):
    put('quark:potted_' + p, 'flower_pot')

# ---- Farmer's Delight
put(['farmersdelight:cutting_board', 'farmersdelight:skillet', 'farmersdelight:safety_net', 'farmersdelight:tomatoes', 'farmersdelight:cabbages',
     'farmersdelight:roast_chicken_block', 'farmersdelight:honey_glazed_ham_block', 'farmersdelight:stuffed_pumpkin_block', 'farmersdelight:shepherds_pie_block',
     'farmersdelight:rice_roll_medley_block', 'farmersdelight:apple_pie', 'farmersdelight:brown_canvas_wall_sign'], 'air')
put('farmersdelight:cooking_pot', 'cauldron')
put('farmersdelight:stove', 'smoker', 'furnace')
put('farmersdelight:rich_soil', 'coarse_dirt', 'dirt')
put('farmersdelight:canvas_rug', 'white_carpet', 'carpet')
put('farmersdelight:full_tatami_mat', 'hay_bale')
put('farmersdelight:half_tatami_mat', 'yellow_carpet', 'carpet')
put(['farmersdelight:straw_bale', 'farmersdelight:rice_bale', 'farmersdelight:rice_bag'], 'hay_bale')
put(['farmersdelight:potato_crate', 'farmersdelight:carrot_crate', 'farmersdelight:tomato_crate', 'farmersdelight:cabbage_crate', 'farmersdelight:onion_crate',
     'farmersdelight:basket'], 'barrel')

# ---- The Graveyard: its laid-out dead are our fallen adventurers (render/fallen_layer.cpp draws the skeleton)
put(['graveyard:lying_skeleton', 'graveyard:leaning_skeleton', 'graveyard:laterally_lying_skeleton'], 'remains_traveller')
put(['graveyard:lying_wither_skeleton', 'graveyard:leaning_wither_skeleton'], 'remains_miner', 'remains_traveller')
put(['graveyard:torso_pile', 'graveyard:bone_remains', 'graveyard:ossuary'], 'bone_block')
put(['graveyard:skeleton_hand', 'graveyard:wither_skeleton_hand', 'graveyard:creeper_skeleton', 'graveyard:hanged_skeleton', 'graveyard:upper_bone_staff',
     'graveyard:middle_bone_staff', 'graveyard:lower_bone_staff'], 'air')

# ---- the rest
put('alexsmobs:bison_carpet', 'brown_carpet', 'carpet')
put('alexsmobs:bison_fur_block', 'brown_wool', 'wool')
put(['alexsmobs:hummingbird_feeder', 'alexsmobs:crocodile_egg', 'alexsmobs:gustmaker', 'byg:wailing_bell_blossom', 'hexerei:herb_drying_rack',
     'hexerei:herb_drying_rack_full'], 'air')
put('biomesoplenty:hellbark_fence', 'crimson_fence', 'dark_oak_fence')
put(['biomesoplenty:rose_quartz_cluster', 'byg:subzero_crystal_cluster'], 'amethyst_cluster')
put('biomesoplenty:hanging_cobweb_strand', 'cobweb')
put('biomesoplenty:rose', 'flower_red', 'poppy')
put('biomesoplenty:potted_rose', 'flower_pot')
put('byg:subzero_crystal_block', 'blue_ice', 'packed_ice')
put('byg:snowdrops', 'oxeye_daisy', 'flower_red')
put('byg:emeraldite_ore', 'emerald_ore')
put('hexerei:coffer', 'chest')
put('iceandfire:gold_pile', 'yellow_carpet', 'carpet')
put('ars_nouveau:scribes_table', 'lectern')
put('forbidden_arcanus:arcane_crystal_block', 'amethyst_block')
put('redeco:red_fluffy_carpet', 'red_carpet', 'carpet')
# newer Minecraft names we lack
put('muddy_mangrove_roots', 'mud')
put('wither_rose', 'dead_bush')
put('lodestone', 'chiseled_stone_bricks')
for w in ('oak', 'spruce', 'birch', 'jungle', 'acacia', 'dark_oak', 'mangrove', 'cherry', 'bamboo', 'crimson', 'warped'):
    put(['%s_hanging_sign' % w, '%s_wall_hanging_sign' % w], '%s_sign' % w, 'oak_sign')
put('bubble_column', 'water')
for c in ('tube', 'brain', 'bubble', 'fire', 'horn'):
    put('%s_coral_wall_fan' % c, '%s_coral_fan' % c)
    put('dead_%s_coral_wall_fan' % c, 'dead_coral_block', 'dead_bush')
put('netherite_block', 'obsidian')
put('powder_snow', 'snow_block')
for c in COLORS: put('%s_shulker_box' % c, 'barrel')

# building materials (stone, wood, tiles, bricks...): tools/local/block_match.py matches these by their texture instead
MATERIAL = re.compile(r'^create:(cut_|polished_cut_|layered_|small_)|^create:(scoria|scorchia|ochrum|veridium|crimsite|asurine|limestone|tuff|calcite|dripstone)'
                      r'|copper_tiles|zinc_block|brass_block|andesite_alloy_block|jasper|blossom_wood|blossom_log|blossom_planks|azalea_wood|midori|myalite|'
                      r'stick_block|charcoal_block|stone_tile|blackstone_tile|checker_block|daub|timber_|fine_wood|flint_block|soap_block|cactus_block|'
                      r'mud_brick_lattice|gravisand|_corundum$|bison_fur_block|subzero_crystal_block|rose_quartz_block|feather_block|bonded_')
def is_material(n): return bool(MATERIAL.search(n))


def main(d='data'):
    d = pathlib.Path(d)
    have = {b['key'] for b in json.loads((d / 'blocks.json').read_text())['blocks']}
    out, bad = {}, []
    for k, ch in sorted(T.items()):
        pick = next((c for c in ch if c == 'air' or c in have), None)
        if pick is None: bad.append((k, ch)); continue
        out[k] = pick
    (d / 'block_substitutes.json').write_text(json.dumps(out, indent=0, sort_keys=True))
    print('block_substitutes: %d mapped, %d with no choice we have' % (len(out), len(bad)))
    for k, ch in bad: print('  ', k, ch)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'data')
