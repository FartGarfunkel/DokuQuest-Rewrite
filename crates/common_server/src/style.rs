//! Micro-voxel look: every block face is the block's own 16×16 Doku tile, one tiny voxel per pixel.
//! Bright pixels are raised 1/16 (relief: ore specks, cobble tops, bark ridges); grey pixels of
//! grass/foliage blocks are tinted like the JS biome tint.
use crate::blocks::def;
use crate::tiles::{self, lin8};

/// sRGB hex → linear RGB.
pub fn lin(hex: u32) -> [f32; 3] { [lin8((hex >> 16) as u8), lin8((hex >> 8) as u8), lin8(hex as u8)] }

fn tint_rgb(t: u8) -> [f32; 3] {
    match t { 1 => [0.42, 0.72, 0.28], 2 => [0.33, 0.62, 0.22], _ => [1.0; 3] }
}

/// Linear colour of a tile pixel, tinted when the block is tinted and the pixel is grey.
pub fn tile_rgb(t: u16, x: i32, y: i32, tint: u8) -> Option<[f32; 3]> {
    let p = tiles::px(t, x, y);
    if p[3] < 128 { return None; }
    let c = [lin8(p[0]), lin8(p[1]), lin8(p[2])];
    let grey = (p[0] as i32 - p[1] as i32).abs() < 14 && (p[1] as i32 - p[2] as i32).abs() < 14;
    Some(if tint != 0 && grey { let k = tint_rgb(tint); [c[0] * k[0] * 1.6, c[1] * k[1] * 1.6, c[2] * k[2] * 1.6] } else { c })
}

/// Colour + raise of micro-voxel (px, py) ∈ 0..16 on `face` (0..6 = +X −X +Y −Y +Z −Z; py runs up on
/// side faces). None = see-through pixel (glass, pane gaps).
pub fn cell(b: u16, face: usize, px: i32, py: i32, _w: [i32; 3]) -> Option<([f32; 3], u8)> {
    let d = def(b);
    let t = d.faces[face];
    let (x, y) = if face == 2 || face == 3 { (px, 15 - py) } else { (px, 15 - py) };
    match tile_rgb(t, x, y, d.tint) {
        Some(c) => {
            let (_, ml) = tiles::mean(t);
            let l = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
            Some((c, (d.key.ends_with("_ore") && l > ml * 1.22 && ml > 0.01) as u8))
        }
        None if d.opaque => Some((tiles::mean(t).0.map(|v| v * 0.45), 0)),
        None => None,
    }
}

/// One colour per face for far LOD (Ace-of-Spades look): the tile's mean colour. Flat per block
/// type so the greedy mesher can merge whole areas into single quads.
pub fn far(b: u16, face: usize, _w: [i32; 3]) -> [f32; 3] {
    let d = def(b);
    let (c, _) = tiles::mean(d.faces[face]);
    if d.tint != 0 && face == 2 { let k = tint_rgb(d.tint); [c[0] * k[0] * 1.6, c[1] * k[1] * 1.6, c[2] * k[2] * 1.6] } else { c }
}
