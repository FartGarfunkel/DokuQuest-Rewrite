//! Micro-voxel mesher. Input: an 18³ padded block array (chunk + 1-block border). Output: plain
//! buffers (engine-agnostic). Faces are culled per block; each visible face is split into res×res
//! micro-cells coloured by style.rs (run-length merged per row), with smooth block-level AO.
//! Relief (raised micro-voxels) only at res 16. Plants are tiny voxel models.
use crate::blocks::{self, id, Kind, Pat};
use crate::noise::hash3;
use crate::style::{self, lin};

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
    /// Axis-aligned box with light per-face shading (used for plants and items).
    pub fn cuboid(&mut self, a: [f32; 3], b: [f32; 3], c: [f32; 3]) {
        for (f, face) in FACES.iter().enumerate() {
            let nk = axis(face.n);
            let (uk, vk) = (axis(face.u), axis(face.v));
            let plane = if face.n[nk] > 0 { b[nk] } else { a[nk] };
            let pt = |u: usize, v: usize| {
                let mut q = [0.0; 3];
                q[nk] = plane;
                q[uk] = if u == 0 { a[uk] } else { b[uk] };
                q[vk] = if v == 0 { a[vk] } else { b[vk] };
                q
            };
            let k = mul(c, SHADE[f]);
            self.quad([pt(0, 0), pt(1, 0), pt(1, 1), pt(0, 1)], fv(face.n), [k; 4]);
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
    pub o: [f32; 3],
    pub u: [i32; 3],
    pub v: [i32; 3],
}
/// +X −X +Y −Y +Z −Z. `o` is the (u=0,v=0) corner; on side faces v points up.
pub const FACES: [Face; 6] = [
    Face { n: [1, 0, 0], o: [1., 0., 0.], u: [0, 0, 1], v: [0, 1, 0] },
    Face { n: [-1, 0, 0], o: [0., 0., 0.], u: [0, 0, 1], v: [0, 1, 0] },
    Face { n: [0, 1, 0], o: [0., 1., 0.], u: [1, 0, 0], v: [0, 0, 1] },
    Face { n: [0, -1, 0], o: [0., 0., 0.], u: [1, 0, 0], v: [0, 0, 1] },
    Face { n: [0, 0, 1], o: [0., 0., 1.], u: [1, 0, 0], v: [0, 1, 0] },
    Face { n: [0, 0, -1], o: [0., 0., 0.], u: [1, 0, 0], v: [0, 1, 0] },
];
const SHADE: [f32; 6] = [0.9, 0.9, 1.0, 0.75, 0.95, 0.95];
fn axis(v: [i32; 3]) -> usize { if v[0] != 0 { 0 } else if v[1] != 0 { 1 } else { 2 } }
fn fv(v: [i32; 3]) -> [f32; 3] { [v[0] as f32, v[1] as f32, v[2] as f32] }

/// Meshes one chunk. `res`: micro-cells per block edge (16 near, 4 mid, 1 far). Returns (opaque, water).
pub fn mesh_chunk(p: &[u16], res: u32, origin: [i32; 3]) -> (MeshBuf, MeshBuf) {
    let (mut solid, mut water) = (MeshBuf::default(), MeshBuf::default());
    for y in 0..16 {
        for z in 0..16 {
            for x in 0..16 {
                let b = p[pidx(x, y, z)];
                if b == id::AIR { continue; }
                let l = [x, y, z];
                let w = [origin[0] + x, origin[1] + y, origin[2] + z];
                match blocks::kind(b) {
                    Kind::Plant => if res >= 4 { plant(&mut solid, b, l, w, res) },
                    Kind::Solid => {
                        for (f, face) in FACES.iter().enumerate() {
                            if !blocks::is_opaque(p[pidx(x + face.n[0], y + face.n[1], z + face.n[2])]) { emit_face(&mut solid, p, b, f, l, w, res, 1.0); }
                        }
                    }
                    Kind::Liquid => {
                        let top = if p[pidx(x, y + 1, z)] == b { 1.0 } else { 0.875 };
                        let out = if b == id::WATER { &mut water } else { &mut solid };
                        for (f, face) in FACES.iter().enumerate() {
                            let nb = p[pidx(x + face.n[0], y + face.n[1], z + face.n[2])];
                            if nb != b && !blocks::is_opaque(nb) { emit_face(out, p, b, f, l, w, res.min(4), top); }
                        }
                    }
                    Kind::Air => {}
                }
            }
        }
    }
    (solid, water)
}

/// A single block floating in air (held-item / inventory models).
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

#[allow(clippy::too_many_arguments)]
fn emit_face(out: &mut MeshBuf, p: &[u16], b: u16, f: usize, l: [i32; 3], w: [i32; 3], res: u32, top: f32) {
    let face = &FACES[f];
    let ao = if blocks::kind(b) == Kind::Liquid { [1.0; 4] } else { corner_ao(p, l, face) };
    let sh = SHADE[f];
    let (uf, vf, nf) = (fv(face.u), fv(face.v), fv(face.n));
    let base = [0, 1, 2].map(|i| l[i] as f32 + face.o[i]);
    let ytop = l[1] as f32 + top;
    let pt = |u: f32, v: f32, h: f32| {
        let mut q = [0, 1, 2].map(|i| base[i] + uf[i] * u + vf[i] * v + nf[i] * h);
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
        out.quad([pt(0., 0., 0.), pt(1., 0., 0.), pt(1., 1., 0.), pt(0., 1., 0.)], nf,
            [mul(c, aoat(0., 0.)), mul(c, aoat(1., 0.)), mul(c, aoat(1., 1.)), mul(c, aoat(0., 1.))]);
        return;
    }
    let r = res as i32;
    let step = 16 / r;
    let inv = 1.0 / r as f32;
    let mut row = [([0.0f32; 3], 0u8); 16];
    for j in 0..r {
        for i in 0..r {
            let mut c = style::cell(b, f, i * step + step / 2, j * step + step / 2, w);
            if res < 16 { c.1 = 0; }
            row[i as usize] = c;
        }
        let mut i = 0;
        while i < r {
            let (c, raise) = row[i as usize];
            let mut i2 = i + 1;
            if raise == 0 { while i2 < r && row[i2 as usize] == (c, 0) { i2 += 1; } }
            let (u0, u1, v0, v1) = (i as f32 * inv, i2 as f32 * inv, j as f32 * inv, (j + 1) as f32 * inv);
            let h = raise as f32 / 16.0;
            let col = |u: f32, v: f32| mul(c, aoat(u, v));
            out.quad([pt(u0, v0, h), pt(u1, v0, h), pt(u1, v1, h), pt(u0, v1, h)], nf, [col(u0, v0), col(u1, v0), col(u1, v1), col(u0, v1)]);
            if raise > 0 {
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

/// Plants built from 1/16 voxels (grass blades, flowers).
fn plant(out: &mut MeshBuf, b: u16, l: [i32; 3], w: [i32; 3], res: u32) {
    let d = blocks::def(b);
    let s = 1.0 / 16.0;
    let o = [l[0] as f32, l[1] as f32, l[2] as f32];
    let bx = |out: &mut MeshBuf, a: [f32; 3], z: [f32; 3], c: [f32; 3]| {
        out.cuboid([o[0] + a[0] * s, o[1] + a[1] * s, o[2] + a[2] * s], [o[0] + z[0] * s, o[1] + z[1] * s, o[2] + z[2] * s], c)
    };
    let h0 = hash3(0x6a55, w[0], w[1], w[2]);
    if d.pat == Pat::Flower {
        let (jx, jz) = ((h0 % 5) as f32 - 2.0, ((h0 >> 8) % 5) as f32 - 2.0);
        let (st, pe) = (lin(d.c[1]), lin(d.c2[0]));
        bx(out, [7. + jx, 0., 7. + jz], [8. + jx, 7., 8. + jz], st);
        bx(out, [8. + jx, 2., 7. + jz], [10. + jx, 3., 8. + jz], lin(d.c[0]));
        bx(out, [5. + jx, 7., 6. + jz], [10. + jx, 8., 9. + jz], pe);
        bx(out, [6. + jx, 7., 5. + jz], [9. + jx, 8., 10. + jz], pe);
        bx(out, [6. + jx, 8., 6. + jz], [9. + jx, 9., 9. + jz], lin(d.c2[2]));
        bx(out, [7. + jx, 9., 7. + jz], [8. + jx, 9.5, 8. + jz], lin(d.c2[3]));
    } else {
        let n = if res >= 16 { 7 } else { 3 };
        for k in 0..n {
            let h = hash3(0x6a55 + k, w[0], w[1], w[2]);
            let (x, z) = (1.0 + (h % 14) as f32, 1.0 + ((h >> 8) % 14) as f32);
            let ht = 5.0 + ((h >> 16) % 9) as f32;
            bx(out, [x, 0., z], [x + 1., ht - 2., z + 1.], lin(d.c[1]));
            bx(out, [x, ht - 2., z], [x + 1., ht, z + 1.], lin(d.c[2]));
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
    fn single_block() {
        let m = mesh_block(id::STONE, 1);
        assert_eq!(m.verts(), 24);
        assert_eq!(m.idx.len(), 36);
        check_winding(&m);
        check_winding(&mesh_block(id::COBBLESTONE, 16));
        check_winding(&mesh_block(id::RED_FLOWER, 16));
    }

    /// Prints vertex budgets per LOD for real terrain: `cargo test -p common_server --release -- --ignored --nocapture`
    #[test]
    #[ignore]
    fn budget() {
        let g = Gen::new(7);
        let sp = g.spawn_point();
        let cy = (sp[1] as i32).div_euclid(CS);
        let t = std::time::Instant::now();
        let mut chunks = std::collections::HashMap::new();
        for y in cy - 2..=cy + 2 { for z in -3..=3 { for x in -3..=3 { chunks.insert((x, y, z), g.generate(x, y, z)); } } }
        println!("gen {} chunks: {:?}", chunks.len(), t.elapsed());
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
