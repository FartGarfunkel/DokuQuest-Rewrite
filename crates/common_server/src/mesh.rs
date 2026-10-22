//! Micro-voxel mesher (engine-agnostic, plain buffers).
//! - Cubes: each visible face = res×res micro-cells coloured from the block's own tile (run-length
//!   merged per row), smooth block AO, 1/16 relief at res 16.
//! - Shapes (slabs, fences, doors, torches…): the same cells clipped to the shape's box.
//! - Plants / items: their sprite extruded into 1/16 voxels (Cube World style).
//! - Mob parts: boxes skinned with their tiles at 1/16 voxel size.
use crate::blocks::{self, id, Kind};
use crate::style::{self, tile_rgb};
use crate::tiles;

pub const PAD: usize = 18;
#[inline]
pub fn pidx(x: i32, y: i32, z: i32) -> usize { ((x + 1) + (z + 1) * 18 + (y + 1) * 324) as usize }

#[derive(Default, Clone, Debug)]
pub struct MeshBuf {
    pub pos: Vec<[f32; 3]>,
    pub nrm: Vec<[f32; 3]>,
    pub col: Vec<[f32; 4]>,
    pub idx: Vec<u32>,
}

fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] { [a[0] - b[0], a[1] - b[1], a[2] - b[2]] }
fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] { [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]] }
fn dot(a: [f32; 3], b: [f32; 3]) -> f32 { a[0] * b[0] + a[1] * b[1] + a[2] * b[2] }
fn mul(c: [f32; 3], k: f32) -> [f32; 3] { [c[0] * k, c[1] * k, c[2] * k] }
fn neg(a: [f32; 3]) -> [f32; 3] { [-a[0], -a[1], -a[2]] }

impl MeshBuf {
    pub fn is_empty(&self) -> bool { self.idx.is_empty() }
    pub fn verts(&self) -> usize { self.pos.len() }
    /// Adds a quad; winding is fixed automatically so the front face points along `n`.
    pub fn quad(&mut self, p: [[f32; 3]; 4], n: [f32; 3], c: [[f32; 3]; 4]) {
        let base = self.pos.len() as u32;
        let front = dot(cross(sub(p[1], p[0]), sub(p[2], p[0])), n) >= 0.0;
        for k in 0..4 {
            self.pos.push(p[k]);
            self.nrm.push(n);
            self.col.push([c[k][0], c[k][1], c[k][2], 1.0]);
        }
        let o: [u32; 6] = if front { [0, 1, 2, 0, 2, 3] } else { [0, 2, 1, 0, 3, 2] };
        self.idx.extend(o.iter().map(|i| base + i));
    }
    /// Plain coloured box with light per-face shading.
    pub fn cuboid(&mut self, a: [f32; 3], b: [f32; 3], c: [f32; 3]) {
        for (f, fc) in FACES.iter().enumerate() {
            let (nk, uk, vk) = (axis(fc.n), axis(fc.u), axis(fc.v));
            let plane = if fc.n[nk] > 0 { b[nk] } else { a[nk] };
            let pt = |u: usize, v: usize| {
                let mut q = [0.0; 3];
                q[nk] = plane;
                q[uk] = if u == 0 { a[uk] } else { b[uk] };
                q[vk] = if v == 0 { a[vk] } else { b[vk] };
                q
            };
            self.quad([pt(0, 0), pt(1, 0), pt(1, 1), pt(0, 1)], fv(fc.n), [mul(c, SHADE[f]); 4]);
        }
    }
    pub fn append(&mut self, o: MeshBuf) {
        let base = self.pos.len() as u32;
        self.pos.extend(o.pos);
        self.nrm.extend(o.nrm);
        self.col.extend(o.col);
        self.idx.extend(o.idx.into_iter().map(|i| i + base));
    }
}

pub struct Face {
    pub n: [i32; 3],
    pub u: [i32; 3],
    pub v: [i32; 3],
}
/// +X −X +Y −Y +Z −Z; on side faces v points up.
pub const FACES: [Face; 6] = [
    Face { n: [1, 0, 0], u: [0, 0, 1], v: [0, 1, 0] },
    Face { n: [-1, 0, 0], u: [0, 0, 1], v: [0, 1, 0] },
    Face { n: [0, 1, 0], u: [1, 0, 0], v: [0, 0, 1] },
    Face { n: [0, -1, 0], u: [1, 0, 0], v: [0, 0, 1] },
    Face { n: [0, 0, 1], u: [1, 0, 0], v: [0, 1, 0] },
    Face { n: [0, 0, -1], u: [1, 0, 0], v: [0, 1, 0] },
];
const SHADE: [f32; 6] = [0.9, 0.9, 1.0, 0.75, 0.95, 0.95];
const FULL: [f32; 6] = [0.0, 0.0, 0.0, 1.0, 1.0, 1.0];
fn axis(v: [i32; 3]) -> usize { if v[0] != 0 { 0 } else if v[1] != 0 { 1 } else { 2 } }
fn fv(v: [i32; 3]) -> [f32; 3] { [v[0] as f32, v[1] as f32, v[2] as f32] }

/// Meshes one chunk. `res`: micro-cells per block edge (16 near … 1 far). Returns (opaque, water).
pub fn mesh_chunk(p: &[u16], res: u32, origin: [i32; 3]) -> (MeshBuf, MeshBuf) {
    let (mut solid, mut water) = (MeshBuf::default(), MeshBuf::default());
    for y in 0..16 {
        for z in 0..16 {
            for x in 0..16 {
                let b = p[pidx(x, y, z)];
                if b == id::AIR { continue; }
                let (l, w) = ([x, y, z], [origin[0] + x, origin[1] + y, origin[2] + z]);
                let d = blocks::def(b);
                let nb = |f: usize| { let n = FACES[f].n; p[pidx(x + n[0], y + n[1], z + n[2])] };
                match d.kind {
                    Kind::Air | Kind::Hidden => {}
                    Kind::Plant => if res >= 8 { plant(&mut solid, b, l, res) },
                    Kind::Solid if res == 1 => {} // greedy pass below
                    Kind::Solid => {
                        for f in 0..6 {
                            let n = nb(f);
                            if !blocks::is_opaque(n) && !(n == b && !d.opaque) { face(&mut solid, p, b, f, l, w, res, FULL, true, 1.0); }
                        }
                    }
                    Kind::Shape => {
                        let bx = blocks::shape_box(b);
                        for f in 0..6 {
                            let nk = axis(FACES[f].n);
                            let edge = if FACES[f].n[nk] > 0 { bx[3 + nk] >= 1.0 } else { bx[nk] <= 0.0 };
                            if !(edge && blocks::is_opaque(nb(f))) { face(&mut solid, p, b, f, l, w, res, bx, false, 1.0); }
                        }
                    }
                    Kind::Liquid => {
                        let top = if p[pidx(x, y + 1, z)] == b { 1.0 } else { 0.875 };
                        let out = if b == id::WATER { &mut water } else { &mut solid };
                        for f in 0..6 {
                            let n = nb(f);
                            if n != b && !blocks::is_opaque(n) { face(out, p, b, f, l, w, res.min(4), FULL, false, top); }
                        }
                    }
                }
            }
        }
    }
    if res == 1 { greedy_far(p, origin, &mut solid); }
    (solid, water)
}

/// Far LOD: classic greedy meshing — merges same-block faces with flat AO into big rectangles per
/// slice; faces with AO gradients are emitted one by one.
fn greedy_far(p: &[u16], origin: [i32; 3], out: &mut MeshBuf) {
    for (f, fc) in FACES.iter().enumerate() {
        let (nk, uk, vk) = (axis(fc.n), axis(fc.u), axis(fc.v));
        let plane = if fc.n[nk] > 0 { 1.0 } else { 0.0 };
        for slice in 0..16 {
            let mut mask = [[None::<u16>; 16]; 16];
            for vv in 0..16 {
                for uu in 0..16 {
                    let mut l = [0; 3];
                    l[nk] = slice;
                    l[uk] = uu;
                    l[vk] = vv;
                    let b = p[pidx(l[0], l[1], l[2])];
                    if blocks::kind(b) != Kind::Solid { continue; }
                    let nb = p[pidx(l[0] + fc.n[0], l[1] + fc.n[1], l[2] + fc.n[2])];
                    if blocks::is_opaque(nb) || (nb == b && !blocks::is_opaque(b)) { continue; }
                    if corner_ao(p, l, fc) == [1.0; 4] {
                        mask[vv as usize][uu as usize] = Some(b);
                    } else {
                        face(out, p, b, f, l, [origin[0] + l[0], origin[1] + l[1], origin[2] + l[2]], 1, FULL, true, 1.0);
                    }
                }
            }
            for v0 in 0..16 {
                let mut u0 = 0;
                while u0 < 16 {
                    let Some(b) = mask[v0][u0] else { u0 += 1; continue };
                    let mut w = 1;
                    while u0 + w < 16 && mask[v0][u0 + w] == Some(b) { w += 1; }
                    let mut h = 1;
                    while v0 + h < 16 && (u0..u0 + w).all(|u| mask[v0 + h][u] == Some(b)) { h += 1; }
                    for row in mask.iter_mut().skip(v0).take(h) { for c in row.iter_mut().skip(u0).take(w) { *c = None; } }
                    let c = [mul(style::far(b, f, [0; 3]), SHADE[f]); 4];
                    let pt = |u: usize, v: usize| {
                        let mut q = [0.0; 3];
                        q[nk] = slice as f32 + plane;
                        q[uk] = u as f32;
                        q[vk] = v as f32;
                        q
                    };
                    out.quad([pt(u0, v0), pt(u0 + w, v0), pt(u0 + w, v0 + h), pt(u0, v0 + h)], fv(fc.n), c);
                    u0 += w;
                }
            }
        }
    }
}

/// A single block floating in air (held-item / dropped-item models).
pub fn mesh_block(b: u16, res: u32) -> MeshBuf {
    let mut p = vec![id::AIR; PAD * PAD * PAD];
    p[pidx(0, 0, 0)] = b;
    let (mut a, w) = mesh_chunk(&p, res, [0, 0, 0]);
    a.append(w);
    a
}

fn corner_ao(p: &[u16], l: [i32; 3], f: &Face) -> [f32; 4] {
    const CURVE: [f32; 4] = [0.5, 0.68, 0.84, 1.0];
    let mut out = [1.0; 4];
    for (k, (su, sv)) in [(-1, -1), (1, -1), (1, 1), (-1, 1)].into_iter().enumerate() {
        let at = |du: i32, dv: i32| {
            let q = [0, 1, 2].map(|i| l[i] + f.n[i] + f.u[i] * du + f.v[i] * dv);
            blocks::is_opaque(p[pidx(q[0], q[1], q[2])]) as i32
        };
        let (s1, s2, c) = (at(su, 0), at(0, sv), at(su, sv));
        out[k] = CURVE[if s1 == 1 && s2 == 1 { 0 } else { (3 - s1 - s2 - c) as usize }];
    }
    out
}

/// One face of box `bx` (block units) of block `b`, split into micro-cells.
#[allow(clippy::too_many_arguments)]
fn face(out: &mut MeshBuf, p: &[u16], b: u16, f: usize, l: [i32; 3], w: [i32; 3], res: u32, bx: [f32; 6], use_ao: bool, top: f32) {
    let fc = &FACES[f];
    let (nk, uk, vk) = (axis(fc.n), axis(fc.u), axis(fc.v));
    let plane = if fc.n[nk] > 0 { bx[3 + nk] } else { bx[nk] };
    let (ulo, uhi, vlo, vhi) = (bx[uk], bx[3 + uk], bx[vk], bx[3 + vk]);
    let ao = if use_ao { corner_ao(p, l, fc) } else { [1.0; 4] };
    let sh = SHADE[f];
    let nf = fv(fc.n);
    let lf = l.map(|v| v as f32);
    let ytop = lf[1] + top;
    let relief = res == 16 && blocks::kind(b) != Kind::Liquid;
    let pt = |u: f32, v: f32, h: f32| {
        let mut q = [0.0; 3];
        q[nk] = lf[nk] + plane + nf[nk] * h;
        q[uk] = lf[uk] + u;
        q[vk] = lf[vk] + v;
        if top < 1.0 && q[1] > ytop { q[1] = ytop; }
        q
    };
    let aoat = |u: f32, v: f32| {
        let a = ao[0] + (ao[1] - ao[0]) * u;
        let c = ao[3] + (ao[2] - ao[3]) * u;
        (a + (c - a) * v) * sh
    };
    if res <= 1 {
        let c = style::far(b, f, w);
        let col = |u: f32, v: f32| mul(c, aoat(u, v));
        out.quad([pt(ulo, vlo, 0.), pt(uhi, vlo, 0.), pt(uhi, vhi, 0.), pt(ulo, vhi, 0.)], nf, [col(ulo, vlo), col(uhi, vlo), col(uhi, vhi), col(ulo, vhi)]);
        return;
    }
    let r = res as i32;
    let (step, inv) = (16 / r, 1.0 / r as f32);
    let mut row: [Option<([f32; 3], u8)>; 16] = [None; 16];
    for j in 0..r {
        let (v0, v1) = ((j as f32 * inv).max(vlo), ((j + 1) as f32 * inv).min(vhi));
        if v1 <= v0 + 1e-4 { continue; }
        for i in 0..r {
            let inside = ((i + 1) as f32 * inv).min(uhi) > (i as f32 * inv).max(ulo) + 1e-4;
            row[i as usize] = if inside { style::cell(b, f, i * step + step / 2, j * step + step / 2, w).map(|(c, z)| (c, if relief { z } else { 0 })) } else { None };
        }
        let mut i = 0;
        while i < r {
            let Some((c, raise)) = row[i as usize] else { i += 1; continue };
            let mut i2 = i + 1;
            if raise == 0 { while i2 < r && row[i2 as usize] == Some((c, 0)) { i2 += 1; } }
            let (u0, u1) = ((i as f32 * inv).max(ulo), (i2 as f32 * inv).min(uhi));
            let h = raise as f32 / 16.0;
            let col = |u: f32, v: f32| mul(c, aoat(u, v));
            out.quad([pt(u0, v0, h), pt(u1, v0, h), pt(u1, v1, h), pt(u0, v1, h)], nf, [col(u0, v0), col(u1, v0), col(u1, v1), col(u0, v1)]);
            if raise > 0 {
                let (uf, vf) = (fv(fc.u), fv(fc.v));
                let sc = [mul(c, 0.78 * aoat((u0 + u1) * 0.5, (v0 + v1) * 0.5)); 4];
                out.quad([pt(u0, v0, 0.), pt(u1, v0, 0.), pt(u1, v0, h), pt(u0, v0, h)], neg(vf), sc);
                out.quad([pt(u0, v1, 0.), pt(u1, v1, 0.), pt(u1, v1, h), pt(u0, v1, h)], vf, sc);
                out.quad([pt(u0, v0, 0.), pt(u0, v1, 0.), pt(u0, v1, h), pt(u0, v0, h)], neg(uf), sc);
                out.quad([pt(u1, v0, 0.), pt(u1, v1, 0.), pt(u1, v1, h), pt(u1, v0, h)], uf, sc);
            }
            i = i2;
        }
    }
}

/// Cross plants: two crossed voxel planes extruded from the block's sprite.
fn plant(out: &mut MeshBuf, b: u16, l: [i32; 3], res: u32) {
    let d = blocks::def(b);
    let step = if res >= 16 { 1 } else { 2 };
    let o = l.map(|v| v as f32);
    extrude(out, d.faces[0], d.tint, step, 7.5, 8.5, &|s| [o[0] + s[0] / 16.0, o[1] + s[1] / 16.0, o[2] + s[2] / 16.0]);
    extrude(out, d.faces[0], d.tint, step, 7.5, 8.5, &|s| [o[0] + s[2] / 16.0, o[1] + s[1] / 16.0, o[2] + s[0] / 16.0]);
}

/// Extrudes tile `t` into voxels. Sprite space (1/16 units): pixel (x, y) fills u ∈ [x, x+step],
/// v ∈ [15−y…16−y] (up), depth ∈ [d0, d1]; `map` places sprite space in the output.
pub fn extrude(out: &mut MeshBuf, t: u16, tint: u8, step: i32, d0: f32, d1: f32, map: &dyn Fn([f32; 3]) -> [f32; 3]) {
    let o = map([0.0; 3]);
    let dir = |n: [f32; 3]| {
        let v = sub(map(n), o);
        let len = dot(v, v).sqrt().max(1e-6);
        mul(v, 1.0 / len)
    };
    let (nu, nv, nd) = (dir([1., 0., 0.]), dir([0., 1., 0.]), dir([0., 0., 1.]));
    let at = |x: i32, y: i32| if (0..16).contains(&x) && (0..16).contains(&y) { tile_rgb(t, x, y, tint) } else { None };
    let q = |u: f32, v: f32, dd: f32| map([u, v, dd]);
    let mut y = 0;
    while y < 16 {
        let (v0, v1) = ((16 - y - step) as f32, (16 - y) as f32);
        let mut x = 0;
        while x < 16 {
            let Some(c) = at(x, y) else { x += step; continue };
            let mut x2 = x + step;
            while x2 < 16 && at(x2, y) == Some(c) { x2 += step; }
            let (u0, u1) = (x as f32, x2 as f32);
            let ce = [mul(c, 0.8); 4];
            out.quad([q(u0, v0, d1), q(u1, v0, d1), q(u1, v1, d1), q(u0, v1, d1)], nd, [c; 4]);
            out.quad([q(u0, v0, d0), q(u1, v0, d0), q(u1, v1, d0), q(u0, v1, d0)], neg(nd), [mul(c, 0.9); 4]);
            if at(x - step, y).is_none() { out.quad([q(u0, v0, d0), q(u0, v1, d0), q(u0, v1, d1), q(u0, v0, d1)], neg(nu), ce); }
            if at(x2, y).is_none() { out.quad([q(u1, v0, d0), q(u1, v1, d0), q(u1, v1, d1), q(u1, v0, d1)], nu, ce); }
            let mut xx = x;
            while xx < x2 {
                let (a0, a1) = (xx as f32, (xx + step) as f32);
                if at(xx, y - step).is_none() { out.quad([q(a0, v1, d0), q(a1, v1, d0), q(a1, v1, d1), q(a0, v1, d1)], nv, ce); }
                if at(xx, y + step).is_none() { out.quad([q(a0, v0, d0), q(a1, v0, d0), q(a1, v0, d1), q(a0, v0, d1)], neg(nv), ce); }
                xx += step;
            }
            x = x2;
        }
        y += step;
    }
}

/// An item's sprite as a voxel model in [0,1]² × [0, depth/16] (Cube World style).
pub fn mesh_sprite(t: u16, depth_px: f32) -> MeshBuf {
    let mut m = MeshBuf::default();
    extrude(&mut m, t, 0, 1, 0.0, depth_px, &|s| [s[0] / 16.0, s[1] / 16.0, s[2] / 16.0]);
    m
}

/// Mob part: box [min, max] (block units) skinned with tiles (+X −X +Y −Y +Z −Z) as 1/16 voxels.
pub fn textured_box(out: &mut MeshBuf, min: [f32; 3], max: [f32; 3], tex: [u16; 6]) {
    for (f, fc) in FACES.iter().enumerate() {
        let (nk, uk, vk) = (axis(fc.n), axis(fc.u), axis(fc.v));
        let plane = if fc.n[nk] > 0 { max[nk] } else { min[nk] };
        let (su, sv) = (max[uk] - min[uk], max[vk] - min[vk]);
        let (cu, cv) = (((su * 16.0).round() as i32).clamp(1, 32), ((sv * 16.0).round() as i32).clamp(1, 32));
        let t = tex[f];
        let colour = |i: i32, j: i32| {
            if t == tiles::NONE { return Some([0.5, 0.45, 0.4]); }
            tile_rgb(t, i * 16 / cu, 15 - j * 16 / cv, 0)
        };
        let pt = |u: f32, v: f32| {
            let mut q = [0.0; 3];
            q[nk] = plane;
            q[uk] = min[uk] + su * u;
            q[vk] = min[vk] + sv * v;
            q
        };
        for j in 0..cv {
            let mut i = 0;
            while i < cu {
                let Some(c) = colour(i, j) else { i += 1; continue };
                let mut i2 = i + 1;
                while i2 < cu && colour(i2, j) == Some(c) { i2 += 1; }
                let (u0, u1, v0, v1) = (i as f32 / cu as f32, i2 as f32 / cu as f32, j as f32 / cv as f32, (j + 1) as f32 / cv as f32);
                out.quad([pt(u0, v0), pt(u1, v0), pt(u1, v1), pt(u0, v1)], fv(fc.n), [mul(c, SHADE[f]); 4]);
                i = i2;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::chunk::CS;
    use crate::worldgen::Gen;

    fn check_winding(m: &MeshBuf) {
        for t in m.idx.chunks(3) {
            let (a, b, c) = (m.pos[t[0] as usize], m.pos[t[1] as usize], m.pos[t[2] as usize]);
            assert!(dot(cross(sub(b, a), sub(c, a)), m.nrm[t[0] as usize]) >= -1e-6, "triangle faces inward");
        }
    }

    #[test]
    fn models() {
        let m = mesh_block(id::STONE, 1);
        assert_eq!(m.verts(), 24);
        check_winding(&m);
        for b in [id::COBBLESTONE, id::FLOWER_RED, id::TORCH, id::GLASS, id::OAK_DOOR] { check_winding(&mesh_block(b, 16)); }
        let s = mesh_sprite(crate::items::reg().item(crate::items::reg().by_key("iron_sword").unwrap()).icon, 1.0);
        assert!(!s.is_empty());
        check_winding(&s);
    }

    /// Vertex budgets per LOD: `cargo test -p common_server --release -- --ignored --nocapture`
    #[test]
    #[ignore]
    fn budget() {
        let g = Gen::new(7);
        let sp = g.spawn_point();
        let cy = (sp[1] as i32).div_euclid(CS);
        let mut chunks = std::collections::HashMap::new();
        for y in cy - 2..=cy + 2 { for z in -3..=3 { for x in -3..=3 { chunks.insert((x, y, z), g.generate(x, y, z)); } } }
        for res in [16u32, 8, 4, 1] {
            let (mut v, t) = (0usize, std::time::Instant::now());
            for y in cy - 1..=cy + 1 { for z in -2..=2 { for x in -2..=2 {
                let mut p = vec![0u16; PAD * PAD * PAD];
                for py in -1..=16 { for pz in -1..=16 { for px in -1..=16 {
                    let (gx, gy, gz) = (x * CS + px, y * CS + py, z * CS + pz);
                    if let Some(c) = chunks.get(&(gx.div_euclid(CS), gy.div_euclid(CS), gz.div_euclid(CS))) {
                        p[pidx(px, py, pz)] = c.get(gx.rem_euclid(CS), gy.rem_euclid(CS), gz.rem_euclid(CS));
                    }
                }}}
                let (a, b) = mesh_chunk(&p, res, [x * CS, y * CS, z * CS]);
                check_winding(&a);
                v += a.verts() + b.verts();
            }}}
            println!("res {res:>2}: {} verts / 75 chunks ({} per chunk) in {:?}", v, v / 75, t.elapsed());
        }
    }
}
