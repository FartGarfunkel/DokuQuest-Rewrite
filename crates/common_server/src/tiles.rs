//! The game's own Doku texture tiles (16×16 RGBA, painted by the JS `buildAtlas()` and extracted by
//! tools/port). Each pixel becomes one micro-voxel. Resource packs can later replace this table.
use std::sync::OnceLock;

static DATA: &[u8] = include_bytes!("../data/tiles.bin");
pub const NONE: u16 = u16::MAX;

pub fn count() -> usize { DATA.len() / 1024 }

/// RGBA of pixel (x, y), y = 0 is the top row.
#[inline]
pub fn px(t: u16, x: i32, y: i32) -> [u8; 4] {
    if t as usize >= count() { return [255, 0, 255, 255]; }
    let i = t as usize * 1024 + (((y & 15) * 16 + (x & 15)) as usize) * 4;
    [DATA[i], DATA[i + 1], DATA[i + 2], DATA[i + 3]]
}

fn lut() -> &'static [f32; 256] {
    static L: OnceLock<[f32; 256]> = OnceLock::new();
    L.get_or_init(|| std::array::from_fn(|i| (i as f32 / 255.0).powf(2.2)))
}
/// sRGB byte → linear.
#[inline]
pub fn lin8(v: u8) -> f32 { lut()[v as usize] }

/// Mean linear colour and mean luminance of the opaque pixels of a tile.
pub fn mean(t: u16) -> ([f32; 3], f32) {
    static M: OnceLock<Vec<([f32; 3], f32)>> = OnceLock::new();
    let m = M.get_or_init(|| {
        (0..count() as u16)
            .map(|t| {
                let (mut c, mut n) = ([0.0f32; 3], 0.0f32);
                for i in 0..256 {
                    let p = px(t, i % 16, i / 16);
                    if p[3] >= 128 { for k in 0..3 { c[k] += lin8(p[k]); } n += 1.0; }
                }
                let c = if n > 0.0 { c.map(|v| v / n) } else { [0.5; 3] };
                (c, 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2])
            })
            .collect()
    });
    m.get(t as usize).copied().unwrap_or(([0.5; 3], 0.5))
}
