//! Block registry, generated from the JS game's own BLOCKS table (tools/port/gen.py → gen_blocks.rs).
//! Never hand-edit gen_blocks.rs: re-run the generator. Ids follow the JS ids.

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Air,
    /// Exists but has no block mesh yet (signs, machines drawn by models in JS).
    Hidden,
    Solid,
    Liquid,
    /// Cross sprites (flowers, grass, saplings) → extruded voxel plants.
    Plant,
    /// Partial boxes (slabs, fences, doors, torches, furniture) → `SHAPE_BOXES[shape]`.
    Shape,
}

#[derive(Debug)]
pub struct BlockDef {
    pub key: &'static str,
    pub name: &'static str,
    pub kind: Kind,
    /// Hides neighbouring faces.
    pub opaque: bool,
    /// Blocks movement.
    pub solid: bool,
    pub hardness: f32,
    /// 0 none, 1 pickaxe, 2 axe, 3 shovel.
    pub tool: u8,
    /// Minimum tool tier to get drops (0 hand … 4 diamond).
    pub tier: u8,
    /// "" = drops itself, "-" = nothing, else item key.
    pub drop: &'static str,
    pub light: u8,
    pub shape: u8,
    /// 0 none, 1 grass, 2 foliage (tints grey pixels).
    pub tint: u8,
    /// Tile per face: +X −X +Y −Y +Z −Z.
    pub faces: [u16; 6],
}

pub use crate::gen_blocks::{id, BLOCKS, SHAPE_BOXES};

#[inline]
pub fn def(b: u16) -> &'static BlockDef { BLOCKS.get(b as usize).unwrap_or(&BLOCKS[0]) }
#[inline]
pub fn kind(b: u16) -> Kind { def(b).kind }
#[inline]
pub fn is_opaque(b: u16) -> bool { def(b).opaque }
/// Collides with entities (uses `shape_box` for partial shapes).
#[inline]
pub fn is_solid(b: u16) -> bool { let d = def(b); d.solid && matches!(d.kind, Kind::Solid | Kind::Shape) }
pub fn by_key(k: &str) -> Option<u16> { BLOCKS.iter().position(|d| d.key == k).map(|i| i as u16) }
/// Block-local box [x0,y0,z0,x1,y1,z1] in block units.
pub fn shape_box(b: u16) -> [f32; 6] {
    let d = def(b);
    let s = if d.kind == Kind::Shape { SHAPE_BOXES[d.shape as usize] } else { SHAPE_BOXES[0] };
    s.map(|v| v as f32 / 16.0)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ids_match_keys() {
        assert_eq!(by_key("stone"), Some(id::STONE));
        assert_eq!(BLOCKS[id::GRASS as usize].key, "grass");
        assert!(is_opaque(id::STONE) && !is_opaque(id::AIR) && kind(id::WATER) == Kind::Liquid);
    }
}
