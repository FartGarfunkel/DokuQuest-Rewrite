#!/usr/bin/env python3
"""Soulrend's passive skill trees (Passive Skill Tree, its pack datapack: warrior, ranger, rogue, mage; 985 nodes) into
data/local_models/skilltree.json (git-ignored: the pack's design), read by sim/skilltree.cpp:
  python3 tools/port/soulrend_tree.py [pack overrides dir] [data-dir]

Each node: where it sits, its size (minor, notable, keystone), its links (two-way and one-way), whether it starts a tree,
its tags (a tree's limits count them), and its bonuses as our attributes (ATTR below; the rest kept, shown, not applied)."""
import json, pathlib, sys

PACK = pathlib.Path('/home/matt/Documents/Soulrend/overrides')
TREES = ['warrior', 'ranger', 'rogue', 'mage']
# their attributes -> ours (sim/skilltree.h treeAttr names)
ATTR = {
    'minecraft:generic.max_health': 'health', 'minecraft:generic.attack_damage': 'damage', 'minecraft:generic.armor': 'armor',
    'minecraft:generic.armor_toughness': 'armor', 'minecraft:generic.movement_speed': 'speed', 'minecraft:generic.attack_speed': 'attack_speed',
    'minecraft:generic.knockback_resistance': 'knock_resist', 'minecraft:generic.attack_knockback': 'knockback', 'minecraft:generic.luck': 'luck',
    'irons_spellbooks:max_mana': 'mana', 'irons_spellbooks:mana_regen': 'mana_regen', 'irons_spellbooks:spell_power': 'spell_power',
    'irons_spellbooks:spell_resist': 'spell_resist', 'irons_spellbooks:cooldown_reduction': 'cooldown', 'irons_spellbooks:cast_time_reduction': 'cast_time',
    'irons_spellbooks:summon_damage': 'summon_damage',
    'irons_spellbooks:fire_spell_power': 'spell_power.fire', 'irons_spellbooks:ice_spell_power': 'spell_power.frost',
    'irons_spellbooks:lightning_spell_power': 'spell_power.storm', 'irons_spellbooks:holy_spell_power': 'spell_power.divine',
    'irons_spellbooks:nature_spell_power': 'spell_power.nature', 'irons_spellbooks:blood_spell_power': 'spell_power.death',
    'irons_spellbooks:ender_spell_power': 'spell_power.arcane', 'irons_spellbooks:evocation_spell_power': 'spell_power.alter',
    'irons_spellbooks:eldritch_spell_power': 'spell_power.illusion', 'traveloptics:aqua_spell_power': 'spell_power.frost',
    'gtbcs_geomancy_plus:geo_spell_power': 'spell_power.nature', 'familiarslib:sound_spell_power': 'spell_power.arcane',
    'attributeslib:crit_chance': 'crit_chance', 'attributeslib:crit_damage': 'crit_damage', 'attributeslib:dodge_chance': 'dodge',
    'attributeslib:arrow_damage': 'arrow_damage', 'attributeslib:arrow_velocity': 'arrow_speed', 'attributeslib:draw_speed': 'draw_speed',
    'attributeslib:armor_pierce': 'armor_pierce', 'attributeslib:armor_shred': 'armor_pierce', 'attributeslib:overheal': 'overheal',
    'epicfight:staminar': 'stamina', 'epicfight:stamina_regen': 'stamina_regen', 'epicfight:impact': 'posture_damage',
    'epicfight:armor_negation': 'armor_pierce', 'epicfight:stun_armor': 'stun_armor', 'epicfight:weight': 'weight',
    'additional_attributes:fishing_luck': 'fishing', 'forge:step_height_addition': 'step',
}
SCALE = {'health': 0.5, 'damage': 0.5, 'armor': 0.5}
NAMES = {'health': 'max health', 'damage': 'damage', 'armor': 'armour', 'speed': 'speed', 'attack_speed': 'attack speed',
         'knock_resist': 'knockback resistance', 'knockback': 'knockback', 'luck': 'luck', 'mana': 'max mana', 'mana_regen': 'mana regeneration',
         'spell_power': 'spell power', 'spell_resist': 'spell resistance', 'cooldown': 'spell cooldowns', 'cast_time': 'cast time',
         'summon_damage': 'summons\' damage', 'crit_chance': 'critical chance', 'crit_damage': 'critical damage', 'dodge': 'dodge chance',
         'arrow_damage': 'arrow damage', 'arrow_speed': 'arrow speed', 'draw_speed': 'draw speed', 'armor_pierce': 'armour piercing',
         'overheal': 'overheal', 'stamina': 'max stamina', 'stamina_regen': 'stamina regeneration', 'posture_damage': 'posture damage',
         'stun_armor': 'stagger resistance', 'weight': 'weight', 'fishing': 'fishing luck', 'step': 'step height'}


def describe(b):
    a = b['attr']; n = NAMES.get(a, a)
    if a.startswith('spell_power.'): n = a.split('.', 1)[1] + ' spell power'
    v = b['v']
    return ('+%g%% ' % round(v * 100, 2) if b['op'] else '+%g ' % round(v, 2)) + n


def main(pack=PACK, d='data'):
    pack, d = pathlib.Path(pack), pathlib.Path(d)
    src = pack / 'config/paxi/datapacks/soulrend_skilltree/data/skilltree'
    lang = json.loads((pack / 'config/paxi/resourcepacks/soulrend_lang/assets/skilltree/lang/en_us.json').read_text())
    out = {'trees': {}}
    for t in TREES:
        tf = src / 'skill_trees' / (t + '.json')
        if not tf.exists(): continue
        tj = json.loads(tf.read_text())
        nodes = []
        for sid in tj['skillIds']:
            f = src / 'skills' / (sid.split(':', 1)[1] + '.json')
            if not f.exists(): continue
            j = json.loads(f.read_text())
            bon, other = [], []
            for b in j.get('bonuses', []):
                if b.get('type') == 'skilltree:attribute' and b.get('attribute') in ATTR:
                    a = ATTR[b['attribute']]; op = 1 if int(b.get('operation', 0)) else 0; v = float(b.get('amount', 0))
                    if not op and a in SCALE: v *= SCALE[a]   # (their pack's mobs are far tougher than ours)
                    bon.append({'attr': a, 'v': round(v, 4), 'op': op})
                else: other.append(b.get('type', '').split(':')[-1].replace('_', ' '))
            size = int(j.get('buttonSize', 16))
            import re
            n = {'id': sid.split(':', 1)[1], 'x': round(float(j.get('positionX', 0)), 2), 'y': round(float(j.get('positionY', 0)), 2),
                 'size': 2 if size >= 30 else 1 if size >= 22 else 0, 'title': re.sub(r'\u00a7.', '', j.get('title') or ''), 'links': [c.split(':', 1)[1] for c in j.get('directConnections', []) + j.get('longConnections', [])],
                 'one': [c.split(':', 1)[1] for c in j.get('oneWayConnections', [])], 'tags': j.get('tags', [])}
            if j.get('isStartingPoint') or j.get('isAlwaysStartingPoint'): n['start'] = True
            if bon: n['bonus'] = bon
            n['desc'] = '; '.join([describe(b) for b in bon] + ['(' + o + ')' for o in other])
            if j.get('titleColor'): n['col'] = j['titleColor']
            nodes.append(n)
        import re
        out['trees'][t] = {'name': re.sub(r'\u00a7.', '', lang.get('skilltree:' + t, t.title())), 'limits': tj.get('skillLimitations', {}), 'nodes': nodes}
        print('soulrend_tree: %s: %d nodes, %d start' % (t, len(nodes), sum(1 for n in nodes if n.get('start'))))
    od = d / 'local_models'; od.mkdir(parents=True, exist_ok=True)
    (od / 'skilltree.json').write_text(json.dumps(out, separators=(',', ':')))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else PACK, sys.argv[2] if len(sys.argv) > 2 else 'data')
