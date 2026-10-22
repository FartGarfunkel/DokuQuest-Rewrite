//! Items, stacks, inventory, crafting and smelting. Data is generated from the JS ITEMS / RECIPES /
//! TAGS / SMELTING tables (data/*.tsv); mining rules follow core/blocks.js (hardness, tool, tier).
use crate::blocks::{self, BLOCKS};
use crate::tiles;
use std::collections::HashMap;
use std::sync::OnceLock;

static ITEMS_TSV: &str = include_str!("../data/items.tsv");
static RECIPES_TSV: &str = include_str!("../data/recipes.tsv");
static TAGS_TSV: &str = include_str!("../data/tags.tsv");
static SMELT_TSV: &str = include_str!("../data/smelting.tsv");

#[derive(Debug)]
pub struct ItemDef {
    pub key: String,
    pub name: String,
    pub max_stack: u32,
    /// Tile of the inventory icon / voxel sprite.
    pub icon: u16,
    /// Block this item places.
    pub block: Option<u16>,
    /// "pickaxe", "axe", "shovel", "sword", "hoe", "bow" … or "".
    pub tool: String,
    pub tier: u8,
    pub speed: f32,
    pub damage: f32,
    pub durability: u32,
    /// Health restored when eaten (0 = not food).
    pub food: f32,
    pub armor_slot: i8,
    pub armor: f32,
}

/// `w == 0` → shapeless. `cells[i]` = allowed items (empty = must be empty).
pub struct Recipe {
    pub out: u16,
    pub n: u32,
    pub w: usize,
    pub h: usize,
    pub cells: Vec<Vec<u16>>,
}

pub struct Registry {
    items: Vec<ItemDef>,
    keys: HashMap<String, u16>,
    pub recipes: Vec<Recipe>,
    smelt: HashMap<u16, u16>,
    block_item: Vec<Option<u16>>,
}

pub fn reg() -> &'static Registry {
    static R: OnceLock<Registry> = OnceLock::new();
    R.get_or_init(load)
}

fn load() -> Registry {
    let (mut items, mut keys) = (Vec::new(), HashMap::new());
    for line in ITEMS_TSV.lines() {
        let c: Vec<&str> = line.split('\t').collect();
        if c.len() < 13 { continue; }
        let f = |i: usize| c[i].parse::<f32>().unwrap_or(0.0);
        let d = ItemDef {
            key: c[0].into(),
            name: c[1].into(),
            max_stack: c[2].parse().unwrap_or(64u32).max(1),
            icon: c[3].parse().unwrap_or(tiles::NONE),
            block: c[4].parse::<i32>().ok().filter(|v| *v >= 0).map(|v| v as u16),
            tool: c[5].into(),
            tier: f(6) as u8,
            speed: f(7),
            damage: f(8),
            durability: f(9) as u32,
            food: f(10),
            armor_slot: c[11].parse().unwrap_or(-1),
            armor: f(12),
        };
        keys.insert(d.key.clone(), items.len() as u16);
        items.push(d);
    }
    let mut block_item = vec![None; BLOCKS.len()];
    for (i, it) in items.iter().enumerate() {
        if let Some(b) = it.block {
            if let Some(slot) = block_item.get_mut(b as usize) { if slot.is_none() { *slot = Some(i as u16); } }
        }
    }
    let tags: HashMap<&str, Vec<u16>> =
        TAGS_TSV.lines().filter_map(|l| l.split_once('\t')).map(|(t, m)| (t, m.split(',').filter_map(|k| keys.get(k).copied()).collect())).collect();
    let resolve = |s: &str| -> Vec<u16> {
        s.split('|').flat_map(|a| if a.starts_with('#') { tags.get(a).cloned().unwrap_or_default() } else { keys.get(a).copied().into_iter().collect() }).collect()
    };
    let mut recipes = Vec::new();
    for line in RECIPES_TSV.lines() {
        let c: Vec<&str> = line.split('\t').collect();
        if c.len() < 5 { continue; }
        let Some(&out) = keys.get(c[0]) else { continue };
        let specs: Vec<&str> = c[4].split(',').collect();
        let cells: Vec<Vec<u16>> = specs.iter().map(|s| if s.is_empty() { vec![] } else { resolve(s) }).collect();
        if specs.iter().zip(&cells).any(|(s, v)| !s.is_empty() && v.is_empty()) { continue; }
        let (w, h) = (c[2].parse().unwrap_or(0usize), c[3].parse().unwrap_or(0usize));
        if w > 0 && cells.len() < w * h { continue; }
        recipes.push(Recipe { out, n: c[1].parse().unwrap_or(1), w, h, cells });
    }
    let smelt = SMELT_TSV.lines().filter_map(|l| l.split_once('\t')).filter_map(|(a, b)| Some((*keys.get(a)?, *keys.get(b)?))).collect();
    Registry { items, keys, recipes, smelt, block_item }
}

impl Registry {
    pub fn len(&self) -> usize { self.items.len() }
    pub fn is_empty(&self) -> bool { self.items.is_empty() }
    pub fn item(&self, i: u16) -> &ItemDef { &self.items[(i as usize).min(self.items.len() - 1)] }
    pub fn by_key(&self, k: &str) -> Option<u16> { self.keys.get(k).copied() }
    /// The item that places block `b`.
    pub fn of_block(&self, b: u16) -> Option<u16> { self.block_item.get(b as usize).copied().flatten() }
    pub fn smelt(&self, i: u16) -> Option<u16> { self.smelt.get(&i).copied() }

    /// What mining `b` with `tool` drops (None: nothing, or the tool is too weak).
    pub fn drop_of(&self, b: u16, tool: Option<&ItemDef>) -> Option<u16> {
        let d = blocks::def(b);
        if !self.can_harvest(b, tool) { return None; }
        match d.drop {
            "-" | "air" => None,
            "" => self.of_block(b),
            k => self.by_key(k).or_else(|| blocks::by_key(k).and_then(|bb| self.of_block(bb))),
        }
    }
    pub fn can_harvest(&self, b: u16, tool: Option<&ItemDef>) -> bool {
        let d = blocks::def(b);
        d.tier == 0 || d.tool != 1 || tool.is_some_and(|t| t.tool == "pickaxe" && t.tier >= d.tier)
    }
    /// Seconds to break `b` holding `tool`.
    pub fn break_time(&self, b: u16, tool: Option<&ItemDef>) -> f32 {
        let d = blocks::def(b);
        if d.hardness <= 0.0 { return 0.05; }
        let want = match d.tool { 1 => "pickaxe", 2 => "axe", 3 => "shovel", _ => "" };
        let speed = tool.filter(|t| !want.is_empty() && t.tool == want).map(|t| t.speed.max(1.0)).unwrap_or(1.0);
        d.hardness * if self.can_harvest(b, tool) { 1.5 } else { 5.0 } / speed
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Stack {
    pub item: u16,
    pub count: u32,
}
pub type Slot = Option<Stack>;

/// 0..9 hotbar, 9..36 backpack, 36..40 armour.
#[derive(Clone, Debug, PartialEq)]
pub struct Inventory {
    pub slots: Vec<Slot>,
}

impl Default for Inventory {
    fn default() -> Self { Inventory { slots: vec![None; Self::SIZE] } }
}

impl Inventory {
    pub const SIZE: usize = 40;
    /// Adds as much as fits (stacks first, then empty slots). Returns what didn't fit.
    pub fn add(&mut self, mut s: Stack) -> Option<Stack> {
        let max = reg().item(s.item).max_stack;
        for slot in self.slots[..36].iter_mut().flatten() {
            if slot.item == s.item && slot.count < max {
                let k = (max - slot.count).min(s.count);
                slot.count += k;
                s.count -= k;
                if s.count == 0 { return None; }
            }
        }
        for slot in self.slots[..36].iter_mut() {
            if slot.is_none() {
                let k = s.count.min(max);
                *slot = Some(Stack { item: s.item, count: k });
                s.count -= k;
                if s.count == 0 { return None; }
            }
        }
        Some(s)
    }
    /// Removes one item from slot `i`.
    pub fn take_one(&mut self, i: usize) -> Option<u16> {
        let s = self.slots.get_mut(i)?.as_mut()?;
        let it = s.item;
        s.count -= 1;
        if s.count == 0 { self.slots[i] = None; }
        Some(it)
    }
    pub fn to_text(&self) -> String {
        self.slots.iter().map(|s| match s { Some(s) => format!("{}*{}", reg().item(s.item).key, s.count), None => "-".into() }).collect::<Vec<_>>().join(",")
    }
    pub fn from_text(t: &str) -> Inventory {
        let mut inv = Inventory::default();
        for (i, p) in t.split(',').enumerate().take(Self::SIZE) {
            if let Some((k, n)) = p.split_once('*') {
                if let Some(item) = reg().by_key(k) { inv.slots[i] = Some(Stack { item, count: n.parse().unwrap_or(1) }); }
            }
        }
        inv
    }
}

/// Inventory click rules. Left: pick up / put down / merge / swap. Right: half / one.
pub fn click(slot: &mut Slot, cursor: &mut Slot, right: bool) {
    match (slot.as_mut(), cursor.as_mut()) {
        (Some(s), None) => {
            if right {
                let half = s.count.div_ceil(2);
                *cursor = Some(Stack { item: s.item, count: half });
                s.count -= half;
                if s.count == 0 { *slot = None; }
            } else {
                *cursor = slot.take();
            }
        }
        (None, Some(c)) => {
            if right {
                *slot = Some(Stack { item: c.item, count: 1 });
                c.count -= 1;
                if c.count == 0 { *cursor = None; }
            } else {
                *slot = cursor.take();
            }
        }
        (Some(s), Some(c)) if s.item == c.item => {
            let max = reg().item(s.item).max_stack;
            let k = if right { 1.min(max - s.count.min(max)) } else { (max - s.count.min(max)).min(c.count) };
            s.count += k;
            c.count -= k;
            if c.count == 0 { *cursor = None; }
        }
        (Some(_), Some(_)) => std::mem::swap(slot, cursor),
        (None, None) => {}
    }
}

/// Result of a `w`-wide crafting grid (row-major), if a recipe matches: (recipe index, output).
pub fn craft(grid: &[Slot], w: usize) -> Option<(usize, Stack)> {
    let h = grid.len() / w;
    let filled: Vec<(usize, usize)> = (0..h).flat_map(|y| (0..w).map(move |x| (x, y))).filter(|&(x, y)| grid[y * w + x].is_some()).collect();
    if filled.is_empty() { return None; }
    let (x0, x1) = (filled.iter().map(|p| p.0).min()?, filled.iter().map(|p| p.0).max()?);
    let (y0, y1) = (filled.iter().map(|p| p.1).min()?, filled.iter().map(|p| p.1).max()?);
    let (bw, bh) = (x1 - x0 + 1, y1 - y0 + 1);
    let items: Vec<u16> = filled.iter().filter_map(|&(x, y)| grid[y * w + x].map(|s| s.item)).collect();
    for (ri, r) in reg().recipes.iter().enumerate() {
        let ok = if r.w == 0 {
            let specs: Vec<&Vec<u16>> = r.cells.iter().filter(|c| !c.is_empty()).collect();
            let mut used = vec![false; items.len()];
            specs.len() == items.len()
                && specs.iter().all(|sp| match (0..items.len()).find(|&i| !used[i] && sp.contains(&items[i])) { Some(i) => { used[i] = true; true } None => false })
        } else if r.w == bw && r.h == bh {
            (0..2).any(|m| {
                (0..bh).all(|y| (0..bw).all(|x| {
                    let sx = if m == 1 { bw - 1 - x } else { x };
                    let spec = &r.cells[y * r.w + sx];
                    match grid[(y0 + y) * w + x0 + x] { None => spec.is_empty(), Some(s) => spec.contains(&s.item) }
                }))
            })
        } else {
            false
        };
        if ok { return Some((ri, Stack { item: r.out, count: r.n })); }
    }
    None
}

/// Uses up one of each ingredient after crafting.
pub fn consume(grid: &mut [Slot]) {
    for s in grid.iter_mut() {
        if let Some(st) = s {
            st.count -= 1;
            if st.count == 0 { *s = None; }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn crafting_and_mining() {
        let r = reg();
        assert!(r.len() > 1000 && r.recipes.len() > 900);
        let planks = r.by_key("planks").unwrap();
        let mut g: Vec<Slot> = vec![None; 4];
        g[0] = Some(Stack { item: planks, count: 1 });
        g[2] = Some(Stack { item: planks, count: 1 });
        let (_, out) = craft(&g, 2).expect("stick recipe");
        assert_eq!(r.item(out.item).key, "stick");
        let log = r.of_block(blocks::id::LOG).unwrap();
        let (_, p) = craft(&[None, Some(Stack { item: log, count: 1 }), None, None], 2).expect("planks from log");
        assert_eq!(p.item, planks);
        assert_eq!(r.drop_of(blocks::id::STONE, None), None);
        let pick = r.item(r.by_key("wooden_pickaxe").or(r.by_key("wood_pickaxe")).unwrap());
        assert_eq!(r.item(r.drop_of(blocks::id::STONE, Some(pick)).unwrap()).key, "cobblestone");
        let mut inv = Inventory::default();
        assert!(inv.add(Stack { item: planks, count: 100 }).is_none());
        assert_eq!(Inventory::from_text(&inv.to_text()), inv);
    }
}
