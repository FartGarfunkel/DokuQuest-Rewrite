#!/usr/bin/env python3
"""Write data/skilltree.json: four original path trees. See docs/superpowers/specs/2026-11-21-path-trees-design.md."""
import json
from pathlib import Path

def B(attr, v, pct=False):
    return {"attr": attr, "v": v, "op": 1 if pct else 0}

def node(id, title, desc, x, y, size=0, bonus=None, tags=None, start=False, col="ebd93b"):
    return {
        "id": id, "title": title, "desc": desc, "x": float(x), "y": float(y),
        "size": size, "bonus": bonus or [], "links": [], "one": [],
        "tags": tags or [], "start": start, "col": col,
    }

def chain(nodes, ids):
    for a, b in zip(ids, ids[1:]):
        nodes[a]["links"].append(b)
        nodes[b]["links"].append(a)

def tree(key, name, start_id, start, branches, col_start):
    by = {}
    s = node(start_id, start[0], start[1], 0, 0, size=2, bonus=start[2], start=True, col=col_start)
    by[start_id] = s
    heads = []
    for br in branches:
        prev = None
        for i, n in enumerate(br):
            by[n["id"]] = n
            if i == 0:
                heads.append(n["id"])
            if prev:
                by[prev]["links"].append(n["id"])
                n["links"].append(prev)
            prev = n["id"]
    for h in heads:
        s["links"].append(h)
        by[h]["links"].append(start_id)
    return {
        "name": name,
        "limits": {"Keystone": 1},
        "nodes": list(by.values()),
    }

STEP = 56

def branch(prefix, title_col, xs, ys, rows):
    """rows: list of (suffix, title, desc, size, bonus) size 2 tagged Keystone."""
    out = []
    for i, (suf, title, desc, size, bonus) in enumerate(rows):
        tags = ["Keystone"] if size == 2 else []
        out.append(node(f"{prefix}_{suf}", title, desc, xs(i), ys(i), size=size, bonus=bonus, tags=tags, col=title_col))
    return out

def right(i):
    return (i + 1) * STEP, 0
def up(i):
    return 0, (i + 1) * STEP
def left(i):
    return -(i + 1) * STEP, 0

def series(col, direction, rows, prefix):
    def xs(i):
        return direction(i)[0]
    def ys(i):
        return direction(i)[1]
    return branch(prefix, col, xs, ys, rows)

# ---- Knight (key warrior) --------------------------------------------------------------------------------
knight = tree("warrior", "Knight", "warrior",
    ("Knight", "A practiced stance: harder blows and a little more blood to spend.",
     [B("damage", 2.0), B("health", 4.0)]),
    [
        series("c45c26", right, [
            ("might1", "Deeper Cut", "Your blows bite deeper.", 0, [B("damage", 0.08, True)]),
            ("might2", "Quick Hands", "You recover into the next swing sooner.", 0, [B("attack_speed", 0.08, True)]),
            ("might3", "Guard Break", "Hits shake a stance harder.", 0, [B("posture_damage", 0.12, True)]),
            ("might4", "Planted Hit", "A blow that lands also shoves.", 1, [B("damage", 0.12, True), B("knockback", 0.15, True)]),
            ("might5", "Faster Follow", "The next cut comes quicker still.", 0, [B("attack_speed", 0.08, True)]),
            ("might6", "Breaker", "You crack a guard open and the blow hits harder.", 2, [B("damage", 0.15, True), B("posture_damage", 0.20, True)]),
        ], "w"),
        series("6a8ad0", up, [
            ("guard1", "Long Breath", "More stamina to spend.", 0, [B("stamina", 10.0)]),
            ("guard2", "Thick Hide", "A little less of every blow reaches you.", 0, [B("damage_reduction", 0.04, True)]),
            ("guard3", "Set Feet", "You are harder to shove.", 0, [B("knock_resist", 0.15, True)]),
            ("guard4", "Mail", "Armour sits better and stamina comes back.", 1, [B("armor", 2.0), B("stamina_regen", 0.10, True)]),
            ("guard5", "Steady Head", "Stuns and chills bite less.", 0, [B("stun_armor", 0.12, True)]),
            ("guard6", "Bulwark", "Less harm, and more of you to take it.", 2, [B("damage_reduction", 0.08, True), B("health", 0.10, True)]),
        ], "w"),
        series("a03030", left, [
            ("wound1", "Nick", "Blows sometimes open a bleed.", 0, [B("bleed_chance", 0.12, True)]),
            ("wound2", "Wider Cut", "That bleed comes more often.", 0, [B("bleed_chance", 0.12, True)]),
            ("wound3", "Take It Back", "A share of the harm you deal returns as health.", 0, [B("lifesteal", 0.04, True)]),
            ("wound4", "Open Veins", "Bleeding is a habit of your blade.", 1, [B("bleed_chance", 0.16, True)]),
            ("wound5", "Cornered", "The lower you are, the harder you hit.", 0, [B("rage", 0.20, True)]),
            ("wound6", "Last Stand", "Low health hits harder still, and more of the harm comes back.", 2, [B("rage", 0.25, True), B("lifesteal", 0.06, True)]),
        ], "w"),
    ], "ebd93b")

# ---- Ranger ---------------------------------------------------------------------------------------------
ranger = tree("ranger", "Ranger", "ranger_1",
    ("Ranger", "The bow comes up quicker and the shot hits harder.",
     [B("arrow_damage", 0.08, True), B("draw_speed", 0.08, True)]),
    [
        series("4a8a50", right, [
            ("shot1", "Heavier Shaft", "Arrows hit harder.", 0, [B("arrow_damage", 0.08, True)]),
            ("shot2", "True Flight", "Arrows fly faster.", 0, [B("arrow_speed", 0.10, True)]),
            ("shot3", "Nocked", "The string comes back sooner.", 0, [B("draw_speed", 0.10, True)]),
            ("shot4", "Spent Shafts", "Sometimes the arrow is still yours after the shot.", 1, [B("arrow_return", 0.15, True)]),
            ("shot5", "Split Nock", "A second arrow sometimes flies with the first.", 0, [B("multishot", 0.10, True)]),
            ("shot6", "Twin Flight", "Two arrows more often, and they hit harder.", 2, [B("multishot", 0.15, True), B("arrow_damage", 0.12, True)]),
        ], "r"),
        series("8a6a3a", up, [
            ("hunt1", "Downwind", "Sneaking, creatures notice you later.", 0, [B("stealth", 0.08, True)]),
            ("hunt2", "Easy Stride", "You move a little quicker.", 0, [B("speed", 0.05, True)]),
            ("hunt3", "Drawn Walk", "You keep more speed with a bow drawn.", 0, [B("use_move", 0.12, True)]),
            ("hunt4", "Quiet Draw", "Harder to spot, and you still walk with the string back.", 1, [B("stealth", 0.12, True), B("use_move", 0.10, True)]),
            ("hunt5", "Spring", "You jump a little higher.", 0, [B("jump", 0.05, True)]),
            ("hunt6", "Soft Step", "Very hard to notice, and quicker on your feet.", 2, [B("stealth", 0.15, True), B("speed", 0.06, True)]),
        ], "r"),
        series("6ab04a", left, [
            ("gift1", "Finders", "Drops come a little more often.", 0, [B("luck", 0.08, True)]),
            ("gift2", "Double Pouch", "A kill sometimes drops its loot twice.", 0, [B("loot_double", 0.08, True)]),
            ("gift3", "Lessons Stick", "You gain a little more experience.", 0, [B("xp_gain", 0.05, True)]),
            ("gift4", "Full Bag", "Luckier finds, and more of them.", 1, [B("luck", 0.10, True), B("loot_double", 0.08, True)]),
            ("gift5", "Kind Rest", "Healing you take goes further.", 0, [B("heal_received", 0.08, True)]),
            ("gift6", "Root the Foe", "You learn Entangle, and luck still favours you.", 2, [B("grant_spell.entangle", 1.0), B("luck", 0.08, True)]),
        ], "r"),
    ], "6ac08a")

# ---- Rogue ----------------------------------------------------------------------------------------------
rogue = tree("rogue", "Rogue", "rogue_1",
    ("Rogue", "A cut that finds the gap.",
     [B("crit_chance", 0.04, True), B("attack_speed", 0.05, True)]),
    [
        series("d84a3a", right, [
            ("edge1", "Through Cloth", "More of a blow ignores armour.", 0, [B("armor_pierce", 0.08, True)]),
            ("edge2", "Flick", "Cuts come quicker.", 0, [B("attack_speed", 0.08, True)]),
            ("edge3", "Numb", "Blows sometimes slow what they hit.", 0, [B("slow_chance", 0.12, True)]),
            ("edge4", "Through the Seam", "Armour counts for less.", 1, [B("armor_pierce", 0.12, True)]),
            ("edge5", "Night Oil", "Blows sometimes poison.", 0, [B("poison_chance", 0.12, True)]),
            ("edge6", "Grave Oil", "Poison more often, and it takes on the undead.", 2, [B("poison_chance", 0.18, True), B("venom_undead", 1.0)]),
        ], "g"),
        series("4a4a6a", up, [
            ("shad1", "Low Profile", "Sneaking, you are noticed later.", 0, [B("stealth", 0.10, True)]),
            ("shad2", "Slip", "A chance to evade a blow outright.", 0, [B("dodge", 0.04, True)]),
            ("shad3", "Light Feet", "You move a little quicker.", 0, [B("speed", 0.04, True)]),
            ("shad4", "Sidestep", "Harder to hit and harder to see.", 1, [B("dodge", 0.06, True), B("stealth", 0.08, True)]),
            ("shad5", "Bound", "You jump a little higher.", 0, [B("jump", 0.05, True)]),
            ("shad6", "Afterimage", "You duck more blows and cut faster.", 2, [B("dodge", 0.08, True), B("attack_speed", 0.10, True)]),
        ], "g"),
        series("d8a04a", left, [
            ("fort1", "Telling Blow", "Critical hits a little more often.", 0, [B("crit_chance", 0.05, True)]),
            ("fort2", "Meaner Crit", "Those hits hurt more.", 0, [B("crit_damage", 0.10, True)]),
            ("fort3", "Cut Purse", "A kill sometimes drops loot twice.", 0, [B("loot_double", 0.08, True)]),
            ("fort4", "Loaded Dice", "Luckier, and more criticals.", 1, [B("luck", 0.10, True), B("crit_chance", 0.05, True)]),
            ("fort5", "Quick Study", "You gain a little more experience.", 0, [B("xp_gain", 0.06, True)]),
            ("fort6", "High Stakes", "Criticals hit much harder, and loot comes twice more often.", 2, [B("crit_damage", 0.20, True), B("loot_double", 0.12, True)]),
        ], "g"),
    ], "9a6ad8")

# ---- Mage -----------------------------------------------------------------------------------------------
mage = tree("mage", "Mage", "mage",
    ("Mage", "A deeper breath before the word.",
     [B("mana", 10.0), B("mana_regen", 0.08, True)]),
    [
        series("ff7a2a", right, [
            ("fire1", "Ember", "Fire spells hit harder.", 0, [B("spell_power.fire", 0.12, True)]),
            ("fire2", "Spark", "Blows and spells sometimes set a foe alight.", 0, [B("burn_chance", 0.12, True)]),
            ("fire3", "Hotter Word", "Fire spells hit harder still.", 0, [B("spell_power.fire", 0.12, True)]),
            ("fire4", "Kindling", "Flame takes more often, and fire spells bite.", 1, [B("burn_chance", 0.18, True), B("spell_power.fire", 0.10, True)]),
            ("fire5", "Short Rest", "Spells ready a little sooner.", 0, [B("cooldown", 0.06, True)]),
            ("fire6", "The Great Spark", "You learn Fireball, and fire spells hit harder.", 2, [B("grant_spell.fireball", 1.0), B("spell_power.fire", 0.15, True)]),
        ], "m"),
        series("8ad8ff", up, [
            ("frost1", "Rime", "Frost spells hit harder.", 0, [B("spell_power.frost", 0.12, True)]),
            ("frost2", "Numb Air", "Hits sometimes slow a foe.", 0, [B("slow_chance", 0.12, True)]),
            ("frost3", "Deeper Cold", "Frost spells hit harder still.", 0, [B("spell_power.frost", 0.12, True)]),
            ("frost4", "Ice Mail", "You take less from spells, and frost bites.", 1, [B("spell_resist", 0.08, True), B("spell_power.frost", 0.10, True)]),
            ("frost5", "Quick Tongue", "Spells leave your mouth sooner.", 0, [B("cast_time", 0.06, True)]),
            ("frost6", "Shatter", "You learn Ice Nova, and frost spells hit harder.", 2, [B("grant_spell.icenova", 1.0), B("spell_power.frost", 0.15, True)]),
        ], "m"),
        series("9a6ad8", left, [
            ("word1", "Louder Word", "Spells of every school hit a little harder.", 0, [B("spell_power", 0.08, True)]),
            ("word2", "Ready Again", "Cooldowns shrink a little.", 0, [B("cooldown", 0.06, True)]),
            ("word3", "Deep Breath", "More mana to spend.", 0, [B("mana", 15.0)]),
            ("word4", "Afterglow", "Healing past full becomes a ward, and mana returns faster.", 1, [B("overheal", 0.10, True), B("mana_regen", 0.12, True)]),
            ("word5", "Called Aid", "Summoned allies hit harder.", 0, [B("summon_damage", 0.15, True)]),
            ("word6", "Hearth Ward", "You learn Stoneskin, and all spells hit a little harder.", 2, [B("grant_spell.stoneskin", 1.0), B("spell_power", 0.10, True)]),
        ], "m"),
    ], "6a8ad8")

out = {"trees": {"warrior": knight, "ranger": ranger, "rogue": rogue, "mage": mage}}

def main():
    root = Path(__file__).resolve().parents[2]
    dest = root / "data" / "skilltree.json"
    dest.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    n = {k: len(t["nodes"]) for k, t in out["trees"].items()}
    print(f"wrote {dest} {n}")

if __name__ == "__main__":
    main()
