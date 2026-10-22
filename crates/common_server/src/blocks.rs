//! Block registry. Add a block: append ONE line to the `blocks!` list. Ids are assigned in order and
//! stored in saves, so never reorder or delete lines (rename the display name instead).

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind { Air, Solid, Liquid, Plant }

/// Which micro-voxel look `style.rs` builds for the block.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Pat { None, Noise, Grass, Snowy, Log, Planks, Bricks, Cobble, Ore, Leaves, Liquid, Glow, TallGrass, Flower }

#[derive(Debug)]
pub struct BlockDef {
    pub key: &'static str,
    pub name: &'static str,
    pub kind: Kind,
    pub pat: Pat,
    /// Main shades (sRGB hex): base, darker, lighter, darkest.
    pub c: [u32; 4],
    /// Secondary shades: dirt under grass, ore specks, log rings, petals.
    pub c2: [u32; 4],
}

macro_rules! blocks {
    ($($id:ident $key:literal $name:literal $kind:ident $pat:ident $c:expr, $c2:expr;)*) => {
        /// Numeric block ids (`id::STONE` …).
        #[allow(dead_code)]
        pub mod id { blocks!(@ids 0u16; $($id)*); }
        pub static BLOCKS: &[BlockDef] = &[$(BlockDef { key: $key, name: $name, kind: Kind::$kind, pat: Pat::$pat, c: $c, c2: $c2 },)*];
    };
    (@ids $n:expr; $first:ident $($rest:ident)*) => { pub const $first: u16 = $n; blocks!(@ids $n + 1; $($rest)*); };
    (@ids $n:expr;) => {};
}

const NO: [u32; 4] = [0; 4];
const C_STONE: [u32; 4] = [0x7d7a75, 0x6b6863, 0x8c8984, 0x5c5955];
const C_DIRT: [u32; 4] = [0x7a5536, 0x6a4a2e, 0x85603e, 0x5a3e26];
const C_GREEN: [u32; 4] = [0x5f9a3a, 0x4f8a2e, 0x6faa45, 0x467a28];

blocks! {
    AIR "air" "Air" Air None NO, NO;
    STONE "stone" "Stone" Solid Noise C_STONE, NO;
    DIRT "dirt" "Dirt" Solid Noise C_DIRT, NO;
    GRASS "grass_block" "Grass" Solid Grass C_GREEN, C_DIRT;
    SAND "sand" "Sand" Solid Noise [0xd8c48e, 0xcbb67f, 0xe2cf9c, 0xbfa972], NO;
    GRAVEL "gravel" "Gravel" Solid Noise [0x857f7a, 0x6e6863, 0x9a948e, 0x5a5550], NO;
    WATER "water" "Water" Liquid Liquid [0x2f64b5, 0x295aa6, 0x3a72c2, 0x22508f], NO;
    BEDROCK "bedrock" "Bedrock" Solid Noise [0x3a3a3a, 0x2a2a2a, 0x555555, 0x1c1c1c], NO;
    LOG "oak_log" "Oak Log" Solid Log [0x5e4328, 0x4e3720, 0x6b4d2f, 0x3f2c19], [0xa8834f, 0x8f6d3e, 0xb8925a, 0x6b4d2f];
    LEAVES "oak_leaves" "Oak Leaves" Solid Leaves [0x3f7a2a, 0x356b22, 0x4b8a33, 0x2a5a1b], NO;
    PLANKS "oak_planks" "Oak Planks" Solid Planks [0xa8814c, 0x987342, 0xb68e57, 0x6e5230], NO;
    COBBLESTONE "cobblestone" "Cobblestone" Solid Cobble [0x8a8782, 0x76736e, 0x9a9792, 0x4f4c48], NO;
    COAL_ORE "coal_ore" "Coal Ore" Solid Ore C_STONE, [0x262626, 0x1a1a1a, 0x333333, 0x0f0f0f];
    IRON_ORE "iron_ore" "Iron Ore" Solid Ore C_STONE, [0xd8a982, 0xc4916a, 0xe6bf9c, 0xa87452];
    GOLD_ORE "gold_ore" "Gold Ore" Solid Ore C_STONE, [0xf5d442, 0xe0b92a, 0xfff07a, 0xc49a1a];
    DIAMOND_ORE "diamond_ore" "Diamond Ore" Solid Ore C_STONE, [0x5ee8e0, 0x3fd0c8, 0x9ff7f2, 0x2aa8a0];
    SNOW "snow_block" "Snowy Grass" Solid Snowy [0xf2f6fa, 0xe4eaf0, 0xffffff, 0xd6dee6], C_DIRT;
    NETHERRACK "netherrack" "Netherrack" Solid Noise [0x7a2e2a, 0x6a2522, 0x8a3a34, 0x561c1a], NO;
    GLOWSTONE "glowstone" "Glowstone" Solid Glow [0xf2c76a, 0xd9a84a, 0xffe39a, 0xb8862e], NO;
    SOUL_SAND "soul_sand" "Soul Sand" Solid Noise [0x5a4434, 0x4a372a, 0x6a5240, 0x3a2a20], NO;
    LAVA "lava" "Lava" Liquid Liquid [0xe8641c, 0xd04a10, 0xffa030, 0xb83a08], NO;
    END_STONE "end_stone" "End Stone" Solid Noise [0xdedca0, 0xcfcd8f, 0xebe9b4, 0xbfbd7e], NO;
    TALL_GRASS "tall_grass" "Tall Grass" Plant TallGrass [0x5f9a3a, 0x4f8a2e, 0x7fba55, 0x3f7a24], NO;
    RED_FLOWER "poppy" "Poppy" Plant Flower C_GREEN, [0xd8302a, 0xb82020, 0xf05a4a, 0xffe060];
    YELLOW_FLOWER "dandelion" "Dandelion" Plant Flower C_GREEN, [0xf5d030, 0xe0b820, 0xffe870, 0xc08010];
    BRICKS "bricks" "Bricks" Solid Bricks [0x9a4a38, 0x8a3e2e, 0xa85844, 0xc8bba8], NO;
}

#[inline]
pub fn def(b: u16) -> &'static BlockDef { BLOCKS.get(b as usize).unwrap_or(&BLOCKS[0]) }
#[inline]
pub fn kind(b: u16) -> Kind { def(b).kind }
/// Hides neighbouring faces.
#[inline]
pub fn is_opaque(b: u16) -> bool { kind(b) == Kind::Solid }
/// Blocks movement.
#[inline]
pub fn is_solid(b: u16) -> bool { kind(b) == Kind::Solid }
pub fn by_key(k: &str) -> Option<u16> { BLOCKS.iter().position(|d| d.key == k).map(|i| i as u16) }

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ids_match_keys() {
        assert_eq!(by_key("stone"), Some(id::STONE));
        assert_eq!(by_key("bricks"), Some(id::BRICKS));
        assert_eq!(BLOCKS.len(), id::BRICKS as usize + 1);
    }
}
