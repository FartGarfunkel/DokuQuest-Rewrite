//! Minecraft Java resource packs, like render/packs.js: the zip chosen in Options (from
//! <data>/resourcepacks/) repaints every tile using the JS packCandidates() names
//! (textures/block|item/<name>.png, first animation frame, scaled to 16×16 → one micro-voxel per
//! pixel). Applied at start-up, before anything is meshed. Packs are never shipped with the game.
use common_server::{save, tiles};
use std::io::Read;
use std::path::PathBuf;

pub fn dir() -> PathBuf { save::data_dir().join("resourcepacks") }

pub fn available() -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(dir())
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| { let n = e.file_name().to_string_lossy().to_string(); n.to_lowercase().ends_with(".zip").then_some(n) })
        .collect();
    v.sort();
    v
}

/// First square frame, box-filtered (or doubled up) to 16×16 RGBA.
fn to16(img: &image::RgbaImage) -> Vec<u8> {
    let w = img.width().max(1);
    let h = img.height().min(w).max(1);
    let mut out = vec![0u8; 1024];
    for y in 0..16u32 {
        for x in 0..16u32 {
            let (x0, y0) = (x * w / 16, y * h / 16);
            let (x1, y1) = (((x + 1) * w / 16).max(x0 + 1).min(w), ((y + 1) * h / 16).max(y0 + 1).min(h));
            let (mut c, mut n, mut tot) = ([0u32; 3], 0u32, 0u32);
            for sy in y0..y1 {
                for sx in x0..x1 {
                    let p = img.get_pixel(sx, sy).0;
                    tot += 1;
                    if p[3] >= 128 {
                        for k in 0..3 { c[k] += p[k] as u32; }
                        n += 1;
                    }
                }
            }
            let i = ((y * 16 + x) * 4) as usize;
            if n > 0 && n * 2 >= tot {
                for k in 0..3 { out[i + k] = (c[k] / n) as u8; }
                out[i + 3] = 255;
            }
        }
    }
    out
}

/// Repaints the tiles from pack `name`; returns how many textures it replaced.
pub fn apply(name: &str) -> Result<usize, String> {
    let f = std::fs::File::open(dir().join(name)).map_err(|e| e.to_string())?;
    let mut z = zip::ZipArchive::new(f).map_err(|e| e.to_string())?;
    const ROOT: &str = "assets/minecraft/textures/";
    let prefix = z.file_names().find_map(|n| n.find(ROOT).map(|i| n[..i + ROOT.len()].to_string())).ok_or("not a Minecraft resource pack")?;
    let mut data = tiles::base_data().to_vec();
    let mut n = 0;
    for line in tiles::PACKMAP.lines() {
        let c: Vec<&str> = line.split('\t').collect();
        if c.len() < 3 { continue; }
        let Ok(t) = c[0].parse::<usize>() else { continue };
        if (t + 1) * 1024 > data.len() { continue; }
        let block = c[1].split(',').filter(|s| !s.is_empty()).map(|s| format!("{prefix}block/{s}.png"));
        let item = c[2].split(',').filter(|s| !s.is_empty()).map(|s| format!("{prefix}item/{s}.png"));
        for path in block.chain(item) {
            let Ok(mut e) = z.by_name(&path) else { continue };
            let mut b = Vec::new();
            if e.read_to_end(&mut b).is_err() { continue; }
            let Ok(img) = image::load_from_memory(&b) else { continue };
            data[t * 1024..(t + 1) * 1024].copy_from_slice(&to16(&img.to_rgba8()));
            n += 1;
            break;
        }
    }
    tiles::install(data);
    Ok(n)
}
