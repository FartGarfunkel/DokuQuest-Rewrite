//! Procedural generation, one function per dimension band (see dims.rs). Pure + deterministic:
//! `generate(seed, chunk)` always gives the same chunk, so trees spanning chunk borders are placed by
//! scanning nearby tree origins rather than writing into neighbours.
use crate::blocks::id::*;
use crate::chunk::{Chunk, CS};
use crate::dims::{band_at, SEA_LEVEL};
use crate::noise::*;

/// Bump when terrain changes; old worlds keep their saved chunks.
pub const GEN_VERSION: u32 = 1;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Biome { Ocean, Beach, Plains, Forest, Desert, Snowy, Mountain }

pub struct Gen {
    pub seed: u64,
}

impl Gen {
    pub fn new(seed: u64) -> Self { Gen { seed } }
    #[inline]
    fn s(&self, n: u64) -> u64 { mix64(self.seed ^ n.wrapping_mul(0x9E3779B97F4A7C15)) }

    /// Overworld surface height at a column.
    pub fn height(&self, x: i32, z: i32) -> i32 {
        let (fx, fz) = (x as f32, z as f32);
        let c = fbm2(self.s(1), fx / 700.0, fz / 700.0, 4);
        let hills = fbm2(self.s(2), fx / 140.0, fz / 140.0, 4);
        let r = 1.0 - perlin2(self.s(3), fx / 320.0, fz / 320.0).abs().min(1.0);
        let m = smoothstep(0.05, 0.45, c);
        let h = 63.0 + c * 34.0 + hills * 16.0 * (0.6 + c.max(0.0)) + r * r * r * m * 95.0;
        h.clamp(6.0, 240.0) as i32
    }

    pub fn biome(&self, x: i32, z: i32, h: i32) -> Biome {
        let temp = fbm2(self.s(4), x as f32 / 900.0, z as f32 / 900.0, 3);
        let wet = fbm2(self.s(5), x as f32 / 400.0, z as f32 / 400.0, 3);
        if h <= SEA_LEVEL - 2 { Biome::Ocean }
        else if h <= SEA_LEVEL + 1 && temp > -0.25 { Biome::Beach }
        else if h > 150 { Biome::Mountain }
        else if temp > 0.25 { Biome::Desert }
        else if temp < -0.25 { Biome::Snowy }
        else if wet > 0.05 { Biome::Forest }
        else { Biome::Plains }
    }

    /// Feet position of a dry-land spawn near the origin.
    pub fn spawn_point(&self) -> [f32; 3] {
        for i in 0..64 {
            let a = i as f32 * 2.4;
            let (x, z) = ((a.cos() * i as f32 * 24.0) as i32, (a.sin() * i as f32 * 24.0) as i32);
            let h = self.height(x, z);
            if h > SEA_LEVEL + 1 && h < 140 {
                return [x as f32 + 0.5, h as f32 + 1.0, z as f32 + 0.5];
            }
        }
        [0.5, (self.height(0, 0).max(SEA_LEVEL) + 2) as f32, 0.5]
    }

    pub fn generate(&self, cx: i32, cy: i32, cz: i32) -> Chunk {
        let (x0, y0, z0) = (cx * CS, cy * CS, cz * CS);
        let mut c = Chunk::default();
        match band_at(y0).map(|b| b.key) {
            Some("overworld") => self.overworld(&mut c, x0, y0, z0),
            Some("nether") => self.nether(&mut c, x0, y0, z0),
            Some("skylands") => self.islands(&mut c, x0, y0, z0, 256, 110.0, 55.0, false),
            Some("end") => self.islands(&mut c, x0, y0, z0, 512, 100.0, 30.0, true),
            _ => {}
        }
        c.compact();
        c
    }

    fn overworld(&self, c: &mut Chunk, x0: i32, y0: i32, z0: i32) {
        for lz in 0..CS {
            for lx in 0..CS {
                let (x, z) = (x0 + lx, z0 + lz);
                let h = self.height(x, z);
                if y0 > h.max(SEA_LEVEL) + 1 { continue; }
                let b = self.biome(x, z, h);
                let bed = 1 + (hash3(self.s(6), x, 0, z) % 3) as i32;
                let (top, under) = match b {
                    Biome::Desert | Biome::Beach => (SAND, SAND),
                    Biome::Snowy => (SNOW, DIRT),
                    Biome::Mountain => if h > 190 { (SNOW, STONE) } else { (STONE, STONE) },
                    Biome::Ocean => if h < SEA_LEVEL - 6 { (GRAVEL, GRAVEL) } else { (SAND, SAND) },
                    _ => (GRASS, DIRT),
                };
                for ly in 0..CS {
                    let y = y0 + ly;
                    let mut blk = if y < bed { BEDROCK }
                        else if y <= h { let d = h - y; if d == 0 { top } else if d < 4 { under } else { STONE } }
                        else if y <= SEA_LEVEL { WATER } else { AIR };
                    if blk != AIR && blk != WATER && blk != BEDROCK && y < h - 3 && y > 5 {
                        let (fx, fy, fz) = (x as f32, y as f32, z as f32);
                        let cheese = fbm3(self.s(7), fx / 40.0, fy / 26.0, fz / 40.0, 2) > 0.45;
                        let worm = perlin3(self.s(8), fx / 30.0, fy / 20.0, fz / 30.0).abs() < 0.06
                            && perlin3(self.s(9), fx / 30.0, fy / 20.0, fz / 30.0).abs() < 0.06;
                        if cheese || worm { blk = AIR; }
                    }
                    if blk == STONE { blk = self.ore(x, y, z); }
                    c.set(lx, ly, lz, blk);
                }
                // surface plants
                let py = h + 1;
                if matches!(b, Biome::Plains | Biome::Forest) && py >= y0 && py < y0 + CS && h > SEA_LEVEL {
                    let r = rand01(self.s(10), x, h, z);
                    let p = if r < 0.10 { TALL_GRASS } else if r < 0.115 { RED_FLOWER } else if r < 0.13 { YELLOW_FLOWER } else { AIR };
                    c.set(lx, py - y0, lz, p);
                }
            }
        }
        if y0 > 250 || y0 + CS < SEA_LEVEL - 4 { return; }
        for z in z0 - 2..z0 + CS + 2 {
            for x in x0 - 2..x0 + CS + 2 {
                if let Some((h, th)) = self.tree_at(x, z) { tree(c, x0, y0, z0, x, h, z, th); }
            }
        }
    }

    fn ore(&self, x: i32, y: i32, z: i32) -> u16 {
        if rand01(self.s(12), x >> 1, y >> 1, z >> 1) > 0.25 { return STONE; }
        let r = rand01(self.s(11), x, y, z);
        if y < 16 && r < 0.012 { DIAMOND_ORE }
        else if y < 32 && r < 0.02 { GOLD_ORE }
        else if y < 64 && r < 0.05 { IRON_ORE }
        else if y < 128 && r < 0.07 { COAL_ORE }
        else { STONE }
    }

    /// (ground height, trunk height) if a tree grows at this column.
    fn tree_at(&self, x: i32, z: i32) -> Option<(i32, i32)> {
        let r = rand01(self.s(13), x, 0, z);
        if r > 1.0 / 30.0 { return None; }
        let h = self.height(x, z);
        let p = match self.biome(x, z, h) { Biome::Forest => 1.0 / 30.0, Biome::Plains => 1.0 / 250.0, Biome::Snowy => 1.0 / 120.0, _ => 0.0 };
        if r >= p || h <= SEA_LEVEL { return None; }
        Some((h, 4 + (hash3(self.s(14), x, 1, z) % 3) as i32))
    }

    /// Nether: cavernous netherrack between a bedrock floor and roof, low lava sea, glowstone up top.
    fn nether(&self, c: &mut Chunk, x0: i32, y0: i32, z0: i32) {
        for lz in 0..CS {
            for lx in 0..CS {
                let (x, z) = (x0 + lx, z0 + lz);
                let floor = 1 + (hash3(self.s(20), x, 0, z) % 3) as i32;
                let roof = 253 - (hash3(self.s(21), x, 1, z) % 3) as i32;
                let soul = fbm2(self.s(22), x as f32 / 20.0, z as f32 / 20.0, 2) > 0.25;
                for ly in 0..CS {
                    let y = y0 + ly;
                    let l = y + 256;
                    let blk = if l < floor || l >= roof { BEDROCK } else {
                        let v = (l as f32 - 128.0) / 128.0;
                        let d = fbm3(self.s(23), x as f32 / 40.0, l as f32 / 28.0, z as f32 / 40.0, 3) + v * v * 0.9 - 0.3;
                        if d > 0.0 {
                            if l > 150 && d < 0.06 && rand01(self.s(24), x, y, z) < 0.3 { GLOWSTONE }
                            else if l < 70 && soul { SOUL_SAND } else { NETHERRACK }
                        } else if l <= 31 { LAVA } else { AIR }
                    };
                    c.set(lx, ly, lz, blk);
                }
            }
        }
    }

    /// Floating islands (Skylands / End). `centre`/`spread` shape the island layer inside the band.
    #[allow(clippy::too_many_arguments)]
    fn islands(&self, c: &mut Chunk, x0: i32, y0: i32, z0: i32, base: i32, centre: f32, spread: f32, end: bool) {
        let l0 = (y0 - base) as f32;
        if l0 + CS as f32 + 4.0 < centre - spread * 1.6 || l0 > centre + spread * 1.6 { return; }
        let seed = self.s(if end { 40 } else { 30 });
        let solid = |x: i32, l: i32, z: i32| {
            let v = (l as f32 - centre) / spread;
            let mut d = fbm3(seed, x as f32 / 70.0, l as f32 / 36.0, z as f32 / 70.0, 3) - v * v * 0.6;
            if end { let r = ((x * x + z * z) as f32).sqrt(); d += (0.45 * (1.0 - r / 90.0)).max(0.0); }
            d > 0.2
        };
        for lz in 0..CS {
            for lx in 0..CS {
                let (x, z) = (x0 + lx, z0 + lz);
                let mut above = [false; 3];
                for k in 0..3 { above[k] = solid(x, y0 - base + CS + k as i32, z); }
                for ly in (0..CS).rev() {
                    let l = y0 - base + ly;
                    let s = solid(x, l, z);
                    if s {
                        let blk = if end { END_STONE } else if !above[0] { GRASS } else if !above[1] || !above[2] { DIRT } else { STONE };
                        c.set(lx, ly, lz, blk);
                    } else if !end && above[0] == false && ly > 0 && solid(x, l - 1, z) && rand01(seed, x, l, z) < 0.08 {
                        c.set(lx, ly, lz, TALL_GRASS);
                    }
                    above = [s, above[0], above[1]];
                }
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn tree(c: &mut Chunk, x0: i32, y0: i32, z0: i32, tx: i32, h: i32, tz: i32, th: i32) {
    let mut put = |x: i32, y: i32, z: i32, b: u16, force: bool| {
        let (lx, ly, lz) = (x - x0, y - y0, z - z0);
        if (0..CS).contains(&lx) && (0..CS).contains(&ly) && (0..CS).contains(&lz) {
            let cur = c.get(lx, ly, lz);
            if force || cur == AIR || cur == TALL_GRASS || cur == RED_FLOWER || cur == YELLOW_FLOWER { c.set(lx, ly, lz, b); }
        }
    };
    let top = h + th;
    for y in top - 2..=top + 1 {
        let r: i32 = if y >= top { 1 } else { 2 };
        for dz in -r..=r {
            for dx in -r..=r {
                if dx.abs() == r && dz.abs() == r && (y == top + 1 || hash3(7, tx + dx, y, tz + dz) & 1 == 0) { continue; }
                put(tx + dx, y, tz + dz, LEAVES, false);
            }
        }
    }
    for y in h + 1..=top { put(tx, y, tz, LOG, true); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bands_generate() {
        let g = Gen::new(42);
        let sp = g.spawn_point();
        assert!(sp[1] > SEA_LEVEL as f32);
        let surf = g.generate(0, (sp[1] as i32 - 1).div_euclid(CS), 0);
        assert!(!surf.is_air());
        let deep_nether = g.generate(0, -15, 0); // y -240..-224 (lava sea level)
        assert!(deep_nether.blocks.iter().any(|&b| b == NETHERRACK || b == LAVA || b == BEDROCK));
        assert!(g.generate(0, 47, 0).is_air()); // top of the End band is void
    }
}
