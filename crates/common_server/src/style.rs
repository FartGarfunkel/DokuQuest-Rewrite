//! Micro-voxel looks ("Doku-style", no texture files). Every block face is a 16×16 grid of tiny
//! voxels; `cell()` gives each one a colour (linear RGB) and a raise (in 1/16ths) for relief.
//! Noise patterns use world-continuous coordinates, so terrain never shows a repeating tile.
//! Resource packs (later): swap the colour source for the pack PNG pixel at (px, py).
use crate::blocks::{def, Pat};
use crate::noise::hash3;

/// sRGB hex → linear RGB.
pub fn lin(hex: u32) -> [f32; 3] {
    let f = |s: u32| ((s & 255) as f32 / 255.0).powf(2.2);
    [f(hex >> 16), f(hex >> 8), f(hex)]
}
#[inline]
fn r01(h: u64) -> f32 { (h >> 40) as f32 / 16_777_216.0 }
#[inline]
fn scale(c: [f32; 3], k: f32) -> [f32; 3] { [c[0] * k, c[1] * k, c[2] * k] }
fn mix(a: [f32; 3], b: [f32; 3], t: f32) -> [f32; 3] { [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t] }
/// Pick one of 4 shades (base/dark/light/darkest) from a 0..1 value.
fn shade(c: &[u32; 4], v: f32) -> [f32; 3] { lin(c[if v < 0.16 { 3 } else if v < 0.45 { 1 } else if v < 0.84 { 0 } else { 2 }]) }
/// Like `shade` but never the darkest (used where darkest is mortar/seams).
fn shade3(c: &[u32; 4], v: f32) -> [f32; 3] { lin(c[if v < 0.33 { 1 } else if v < 0.78 { 0 } else { 2 }]) }

fn vnoise(seed: u64, x: f32, y: f32) -> f32 {
    let (xi, yi) = (x.floor() as i32, y.floor() as i32);
    let (fx, fy) = (x - xi as f32, y - yi as f32);
    let h = |a: i32, b: i32| r01(hash3(seed, a, b, 3));
    let (sx, sy) = (fx * fx * (3.0 - 2.0 * fx), fy * fy * (3.0 - 2.0 * fy));
    let t = h(xi, yi) + (h(xi + 1, yi) - h(xi, yi)) * sx;
    let b = h(xi, yi + 1) + (h(xi + 1, yi + 1) - h(xi, yi + 1)) * sx;
    t + (b - t) * sy
}
/// Blotchy 0..1: soft 4-pixel blobs plus per-pixel grain.
fn blotch(seed: u64, x: i32, y: i32) -> f32 { 0.82 * vnoise(seed, x as f32 / 4.0, y as f32 / 4.0) + 0.18 * r01(hash3(seed, x, y, 11)) }

/// Colour + raise of micro-voxel (px, py) ∈ 0..16 on `face` (0..6 = +X −X +Y −Y +Z −Z; py runs up on
/// side faces) of block `b` at world position `w`.
pub fn cell(b: u16, face: usize, px: i32, py: i32, w: [i32; 3]) -> ([f32; 3], u8) {
    let d = def(b);
    let (top, bottom) = (face == 2, face == 3);
    let (gx, gy) = match face { 0 | 1 => (w[2] * 16 + px, w[1] * 16 + py), 2 | 3 => (w[0] * 16 + px, w[2] * 16 + py), _ => (w[0] * 16 + px, w[1] * 16 + py) };
    let s = b as u64 * 7919 + (top || bottom) as u64;
    let n = blotch(s, gx, gy);
    let fine = r01(hash3(s, gx, gy, 5));
    match d.pat {
        Pat::Noise | Pat::Liquid | Pat::TallGrass | Pat::Flower | Pat::None => (shade(&d.c, n), 0),
        Pat::Glow => (shade(&d.c, n), (n > 0.75) as u8),
        Pat::Grass | Pat::Snowy => {
            if top { (shade(&d.c, n), (d.pat == Pat::Grass && fine > 0.82) as u8) }
            else if bottom { (shade(&d.c2, n), 0) }
            else {
                let fringe = 12 + (hash3(s, gx, 0, 9) % 3) as i32;
                if py >= fringe { (shade(&d.c, n), (py == 15 && fine > 0.5) as u8) } else { (shade(&d.c2, blotch(s ^ 77, gx, gy)), 0) }
            }
        }
        Pat::Log => {
            if top || bottom {
                let (dx, dy) = (px as f32 - 7.5, py as f32 - 7.5);
                let r = (dx * dx + dy * dy).sqrt();
                if r > 6.6 { (shade(&d.c, n), 0) } else { (lin(d.c2[((r as i32 + (fine > 0.85) as i32) % 3) as usize]), 0) }
            } else {
                let col = r01(hash3(s, gx, 0, 13));
                let groove = col < 0.22;
                let v = vnoise(s, gx as f32, gy as f32 / 6.0);
                (if groove { lin(d.c[3]) } else { shade3(&d.c, v) }, (!groove && col > 0.78) as u8)
            }
        }
        Pat::Planks => {
            let board = py / 4;
            if py % 4 == 3 || px == (board * 5 + 3) % 16 { (lin(d.c[3]), 0) } else {
                let t = r01(hash3(s, w[0] * 7 + w[2] * 13 + board, w[1], 17));
                (shade3(&d.c, 0.75 * t + 0.25 * fine), 0)
            }
        }
        Pat::Bricks => {
            let row = py / 4;
            let off = if row % 2 == 1 { 4 } else { 0 };
            if py % 4 == 3 || (px + off) % 8 == 7 { (lin(d.c[3]), 0) } else {
                let t = r01(hash3(s, (px + off) / 8 + w[0] * 3 + w[2] * 5, row + w[1] * 4, 19));
                (shade3(&d.c, 0.8 * t + 0.2 * fine), 1)
            }
        }
        Pat::Cobble => {
            let (mut d1, mut d2, mut id1) = (99.0f32, 99.0f32, 0u64);
            for k in 0..7u64 {
                let h = hash3(s ^ k, w[0], w[1], w[2] + face as i32 * 1000);
                let (fx, fy) = ((h & 15) as f32 + 0.5, ((h >> 4) & 15) as f32 + 0.5);
                let dd = ((px as f32 + 0.5 - fx).powi(2) + (py as f32 + 0.5 - fy).powi(2)).sqrt();
                if dd < d1 { d2 = d1; d1 = dd; id1 = h; } else if dd < d2 { d2 = dd; }
            }
            if d2 - d1 < 1.1 { (lin(d.c[3]), 0) } else { (shade3(&d.c, 0.7 * r01(id1.wrapping_mul(0x2545F4914F6CDD1D)) + 0.3 * fine), (d1 < 2.5) as u8) }
        }
        Pat::Ore => {
            let mut ore = false;
            for k in 0..3u64 {
                let h = hash3(s ^ k, w[0], w[1], w[2] * 31 + face as i32);
                let (cx, cy) = (((h & 15) as f32).clamp(3.0, 12.0), (((h >> 4) & 15) as f32).clamp(3.0, 12.0));
                let (dx, dy) = (px as f32 - cx, py as f32 - cy);
                if dx * dx + dy * dy < 4.5 && fine > 0.2 { ore = true; }
            }
            if ore { (shade(&d.c2, fine), 1) } else { (shade(&d.c, n), 0) }
        }
        Pat::Leaves => if fine < 0.1 { (scale(lin(d.c[3]), 0.55), 0) } else { (shade(&d.c, n), (n > 0.62) as u8) },
    }
}

/// One colour per face for far LOD (Ace-of-Spades look). Ores read as plain stone from afar.
pub fn far(b: u16, face: usize, w: [i32; 3]) -> [f32; 3] {
    let d = def(b);
    let c = match d.pat {
        Pat::Grass | Pat::Snowy => if face == 2 { lin(d.c[0]) } else if face == 3 { lin(d.c2[0]) } else { mix(lin(d.c2[0]), lin(d.c[0]), 0.3) },
        Pat::Log => if face == 2 || face == 3 { lin(d.c2[0]) } else { lin(d.c[0]) },
        _ => lin(d.c[0]),
    };
    scale(c, 0.93 + 0.12 * r01(hash3(b as u64, w[0], w[1], w[2])))
}
