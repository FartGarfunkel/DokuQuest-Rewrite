//! Stacked dimensions: ONE world, split into vertical bands. Dig through the bedrock floor and you
//! are in the Nether; fly high enough and you reach the Skylands. Portals just move you between bands
//! at the same x/z, so every transition is seamless. Move/add a dimension by editing `BANDS`
//! (keep bands 16-aligned) and adding its generator in worldgen.rs.

pub struct Band {
    pub key: &'static str,
    pub name: &'static str,
    pub min_y: i32,
    pub max_y: i32,
}

pub const BANDS: &[Band] = &[
    Band { key: "nether", name: "The Nether", min_y: -256, max_y: 0 },
    Band { key: "overworld", name: "the Overworld", min_y: 0, max_y: 256 },
    Band { key: "skylands", name: "the Skylands", min_y: 256, max_y: 512 },
    Band { key: "end", name: "The End", min_y: 512, max_y: 768 },
];

pub const WORLD_MIN_Y: i32 = -256;
pub const WORLD_MAX_Y: i32 = 768;
pub const SEA_LEVEL: i32 = 64;

pub fn band_index(y: i32) -> Option<usize> { BANDS.iter().position(|b| y >= b.min_y && y < b.max_y) }
pub fn band_at(y: i32) -> Option<&'static Band> { band_index(y).map(|i| &BANDS[i]) }
