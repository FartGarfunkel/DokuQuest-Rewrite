//! Seeded hash + Perlin noise (2D/3D) and fBm. Deterministic on every platform.

#[inline]
pub fn mix64(mut x: u64) -> u64 {
    x ^= x >> 33;
    x = x.wrapping_mul(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x = x.wrapping_mul(0xc4ceb9fe1a85ec53);
    x ^ (x >> 33)
}
#[inline]
pub fn hash3(seed: u64, x: i32, y: i32, z: i32) -> u64 {
    mix64(seed
        ^ (x as u32 as u64).wrapping_mul(0x9E3779B185EBCA87)
        ^ (y as u32 as u64).wrapping_mul(0xC2B2AE3D27D4EB4F)
        ^ (z as u32 as u64).wrapping_mul(0x165667B19E3779F9))
}
/// Uniform 0..1 from a hash.
#[inline]
pub fn rand01(seed: u64, x: i32, y: i32, z: i32) -> f32 { (hash3(seed, x, y, z) >> 40) as f32 / 16_777_216.0 }

#[inline]
fn fade(t: f32) -> f32 { t * t * t * (t * (t * 6.0 - 15.0) + 10.0) }
#[inline]
fn lerp(a: f32, b: f32, t: f32) -> f32 { a + (b - a) * t }

const G2: [(f32, f32); 8] = [(1., 0.), (-1., 0.), (0., 1.), (0., -1.), (0.7071, 0.7071), (-0.7071, 0.7071), (0.7071, -0.7071), (-0.7071, -0.7071)];
const G3: [[f32; 3]; 12] = [
    [1., 1., 0.], [-1., 1., 0.], [1., -1., 0.], [-1., -1., 0.], [1., 0., 1.], [-1., 0., 1.],
    [1., 0., -1.], [-1., 0., -1.], [0., 1., 1.], [0., -1., 1.], [0., 1., -1.], [0., -1., -1.],
];

/// Roughly -1..1.
pub fn perlin2(seed: u64, x: f32, y: f32) -> f32 {
    let (xi, yi) = (x.floor() as i32, y.floor() as i32);
    let (xf, yf) = (x - xi as f32, y - yi as f32);
    let g = |dx: i32, dy: i32| {
        let (gx, gy) = G2[(hash3(seed, xi + dx, yi + dy, 0) & 7) as usize];
        gx * (xf - dx as f32) + gy * (yf - dy as f32)
    };
    let (u, v) = (fade(xf), fade(yf));
    lerp(lerp(g(0, 0), g(1, 0), u), lerp(g(0, 1), g(1, 1), u), v) * 1.4
}

/// Roughly -1..1.
pub fn perlin3(seed: u64, x: f32, y: f32, z: f32) -> f32 {
    let (xi, yi, zi) = (x.floor() as i32, y.floor() as i32, z.floor() as i32);
    let (xf, yf, zf) = (x - xi as f32, y - yi as f32, z - zi as f32);
    let g = |dx: i32, dy: i32, dz: i32| {
        let gr = G3[(hash3(seed, xi + dx, yi + dy, zi + dz) % 12) as usize];
        gr[0] * (xf - dx as f32) + gr[1] * (yf - dy as f32) + gr[2] * (zf - dz as f32)
    };
    let (u, v, w) = (fade(xf), fade(yf), fade(zf));
    let a = lerp(lerp(g(0, 0, 0), g(1, 0, 0), u), lerp(g(0, 1, 0), g(1, 1, 0), u), v);
    let b = lerp(lerp(g(0, 0, 1), g(1, 0, 1), u), lerp(g(0, 1, 1), g(1, 1, 1), u), v);
    lerp(a, b, w) * 1.1
}

pub fn fbm2(seed: u64, x: f32, y: f32, oct: u32) -> f32 {
    let (mut s, mut a, mut f, mut n) = (0.0, 1.0, 1.0, 0.0);
    for i in 0..oct {
        s += a * perlin2(seed.wrapping_add(i as u64 * 0x9E37), x * f, y * f);
        n += a;
        a *= 0.5;
        f *= 2.0;
    }
    s / n
}

pub fn fbm3(seed: u64, x: f32, y: f32, z: f32, oct: u32) -> f32 {
    let (mut s, mut a, mut f, mut n) = (0.0, 1.0, 1.0, 0.0);
    for i in 0..oct {
        s += a * perlin3(seed.wrapping_add(i as u64 * 0x9E37), x * f, y * f, z * f);
        n += a;
        a *= 0.5;
        f *= 2.0;
    }
    s / n
}

pub fn smoothstep(a: f32, b: f32, x: f32) -> f32 {
    let t = ((x - a) / (b - a)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}
