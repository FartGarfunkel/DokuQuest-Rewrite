//! Villages: 4–7 houses round a well, dirt paths to every door, a wheat farm. One chance per
//! 320-block region; deterministic, so each chunk builds only its own part.
use crate::blocks::id::*;
use crate::chunk::{Chunk, CS};
use crate::dims::SEA_LEVEL;
use crate::noise::{hash3, rand01};
use crate::worldgen::{Biome, Gen};

const REGION: i32 = 320;
const RADIUS: i32 = 34;

impl Gen {
    /// Village centre (x, ground y, z) of a region, if it has one.
    pub fn village_at(&self, rx: i32, rz: i32) -> Option<[i32; 3]> {
        let s = self.s(50);
        if rand01(s, rx, 0, rz) > 0.55 { return None; }
        let h = hash3(s, rx, 1, rz);
        let (x, z) = (rx * REGION + 48 + (h % 224) as i32, rz * REGION + 48 + ((h >> 16) % 224) as i32);
        let y = self.height(x, z);
        if y <= SEA_LEVEL + 1 || y > 140 { return None; }
        if !matches!(self.biome(x, z, y), Biome::Plains | Biome::Forest | Biome::Desert | Biome::Snowy) { return None; }
        let spread = [(-20, 0), (20, 0), (0, -20), (0, 20)].iter().map(|(dx, dz)| (self.height(x + dx, z + dz) - y).abs()).max().unwrap_or(0);
        (spread <= 6).then_some([x, y, z])
    }

    pub fn villages_near(&self, x: i32, z: i32, r: i32) -> Vec<[i32; 3]> {
        let mut v = Vec::new();
        for rz in (z - r).div_euclid(REGION)..=(z + r).div_euclid(REGION) {
            for rx in (x - r).div_euclid(REGION)..=(x + r).div_euclid(REGION) {
                if let Some(c) = self.village_at(rx, rz) {
                    if (c[0] - x).abs() <= r && (c[2] - z).abs() <= r { v.push(c); }
                }
            }
        }
        v
    }

    /// (house centre x, z, door side 0:+X 1:−X 2:+Z 3:−Z).
    fn houses(&self, c: [i32; 3]) -> Vec<(i32, i32, u8)> {
        let s = self.s(51);
        let n = 4 + (hash3(s, c[0], 0, c[2]) % 4) as i32;
        (0..n)
            .map(|k| {
                let a = k as f32 / n as f32 * std::f32::consts::TAU + rand01(s, c[0], k, c[2]) * 0.5;
                let d = 15.0 + rand01(s, c[2], k, c[0]) * 7.0;
                let (hx, hz) = (c[0] + (a.cos() * d) as i32, c[2] + (a.sin() * d) as i32);
                let side = if (c[0] - hx).abs() > (c[2] - hz).abs() { if c[0] > hx { 0 } else { 1 } } else if c[2] > hz { 2 } else { 3 };
                (hx, hz, side)
            })
            .collect()
    }

    pub(crate) fn villages(&self, ch: &mut Chunk, x0: i32, y0: i32, z0: i32) {
        for v in self.villages_near(x0 + 8, z0 + 8, RADIUS + 16) { self.build_village(ch, [x0, y0, z0], v); }
    }

    fn build_village(&self, ch: &mut Chunk, o: [i32; 3], v: [i32; 3]) {
        let inside = |x: i32, z: i32| x >= o[0] && x < o[0] + CS && z >= o[2] && z < o[2] + CS;
        let mut put = |x: i32, y: i32, z: i32, b: u16| {
            let (lx, ly, lz) = (x - o[0], y - o[1], z - o[2]);
            if (0..CS).contains(&lx) && (0..CS).contains(&ly) && (0..CS).contains(&lz) { ch.set(lx, ly, lz, b); }
        };
        let desert = self.biome(v[0], v[2], v[1]) == Biome::Desert;
        let (wall, floor) = if desert { (SANDSTONE, SANDSTONE) } else { (PLANKS, COBBLESTONE) };
        let houses = self.houses(v);
        // paths from the well to each door
        for &(hx, hz, side) in &houses {
            let (ex, ez) = match side { 0 => (hx + 4, hz), 1 => (hx - 4, hz), 2 => (hx, hz + 4), _ => (hx, hz - 4) };
            let n = (ex - v[0]).abs().max((ez - v[2]).abs()).max(1);
            for i in 0..=n {
                let (x, z) = (v[0] + (ex - v[0]) * i / n, v[2] + (ez - v[2]) * i / n);
                if !inside(x, z) { continue; }
                let y = self.height(x, z);
                put(x, y, z, DIRT_PATH);
                for yy in y + 1..y + 4 { put(x, yy, z, AIR); }
            }
        }
        // well
        for dz in -2..=2 {
            for dx in -2..=2 {
                let (x, z) = (v[0] + dx, v[2] + dz);
                if !inside(x, z) { continue; }
                let ring = dx.abs() == 2 || dz.abs() == 2;
                let post = dx.abs() == 2 && dz.abs() == 2;
                for y in v[1] - 3..=v[1] + 6 {
                    let b = if y <= v[1] { if ring || y < v[1] - 1 { COBBLESTONE } else { WATER } }
                        else if y == v[1] + 1 && ring { COBBLESTONE }
                        else if post && y <= v[1] + 3 { LOG }
                        else if y == v[1] + 4 { PLANKS } else { AIR };
                    put(x, y, z, b);
                }
            }
        }
        // farm: water channel down the middle, wheat either side
        let (fx, fz) = (v[0] + 7, v[2] - 3);
        let fy = self.height(fx + 3, fz + 3);
        for dz in 0..7 {
            for dx in 0..7 {
                let (x, z) = (fx + dx, fz + dz);
                if !inside(x, z) { continue; }
                for y in self.height(x, z).min(fy) - 1..fy { put(x, y, z, DIRT); }
                put(x, fy, z, if dz == 3 { WATER } else { FARMLAND });
                put(x, fy + 1, z, if dz == 3 { AIR } else { WHEAT_CROP });
                for y in fy + 2..fy + 6 { put(x, y, z, AIR); }
            }
        }
        // houses (7×7, log corners, windows, flat roof with a raised middle)
        for (k, &(hx, hz, side)) in houses.iter().enumerate() {
            let gy = self.height(hx, hz);
            for dz in -3..=3 {
                for dx in -3..=3 {
                    let (x, z) = (hx + dx, hz + dz);
                    if !inside(x, z) { continue; }
                    let edge = dx.abs() == 3 || dz.abs() == 3;
                    let corner = dx.abs() == 3 && dz.abs() == 3;
                    let door = match side { 0 => dx == 3 && dz == 0, 1 => dx == -3 && dz == 0, 2 => dz == 3 && dx == 0, _ => dz == -3 && dx == 0 };
                    let window = edge && !corner && !door && (dx.abs() == 1 || dz.abs() == 1);
                    for y in self.height(x, z).min(gy) - 1..=gy { put(x, y, z, floor); }
                    for y in gy + 1..=gy + 4 {
                        let b = if corner { LOG } else if !edge { AIR } else if door && y <= gy + 2 { AIR } else if window && y == gy + 2 { GLASS } else { wall };
                        put(x, y, z, b);
                    }
                    put(x, gy + 5, z, PLANKS);
                    put(x, gy + 6, z, if dx.abs() <= 1 && dz.abs() <= 1 { PLANKS } else { AIR });
                    for y in gy + 7..gy + 12 { put(x, y, z, AIR); }
                }
            }
            let (ix, iz) = if side < 2 { (0, 2) } else { (2, 0) };
            put(hx - ix, gy + 1, hz - iz, CRAFTING_TABLE);
            put(hx + ix, gy + 1, hz + iz, if k % 2 == 0 { CHEST } else { FURNACE });
            put(hx - 2 * (ix == 0) as i32 + 2 * (ix != 0) as i32, gy + 1, hz + 2, TORCH);
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::worldgen::Gen;
    #[test]
    fn some_villages_exist() {
        let g = Gen::new(42);
        let found: usize = (-3..3).flat_map(|z| (-3..3).map(move |x| (x, z))).filter(|&(x, z)| g.village_at(x, z).is_some()).count();
        assert!(found > 0, "no villages in 36 regions");
    }
}
