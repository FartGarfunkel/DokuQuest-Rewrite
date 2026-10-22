//! Save layout (same for client, server and launcher):
//!   <data>/saves/<World>/world.txt        key=value metadata
//!   <data>/saves/<World>/chunks/x_y_z.bin edited chunks only (raw u16 LE), unedited ones regenerate
//!   <data>/saves/last_played.txt          name of the world the title screen shows
//!   <data>/game/<version>/                launcher-installed game builds
//! <data> = $DOKUQUEST_HOME, else %APPDATA%\DokuQuest, else $XDG_DATA_HOME or ~/.local/share/DokuQuest.
use crate::chunk::Chunk;
use std::{fs, io, path::{Path, PathBuf}};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct WorldMeta {
    pub name: String,
    pub seed: u64,
    pub gen_version: u32,
    /// Player feet position.
    pub pos: [f32; 3],
    pub yaw: f32,
    pub pitch: f32,
    pub flying: bool,
    pub last_played: u64,
}

impl WorldMeta {
    pub fn to_text(&self) -> String {
        format!(
            "name={}\nseed={}\ngen_version={}\npos={} {} {}\nyaw={}\npitch={}\nflying={}\nlast_played={}\n",
            self.name, self.seed, self.gen_version, self.pos[0], self.pos[1], self.pos[2], self.yaw, self.pitch, self.flying, self.last_played
        )
    }
    pub fn from_text(s: &str) -> Option<Self> {
        let mut m = WorldMeta::default();
        for line in s.lines() {
            let Some((k, v)) = line.split_once('=') else { continue };
            match k.trim() {
                "name" => m.name = v.trim().to_string(),
                "seed" => m.seed = v.trim().parse().ok()?,
                "gen_version" => m.gen_version = v.trim().parse().unwrap_or(1),
                "pos" => {
                    let p: Vec<f32> = v.split_whitespace().filter_map(|x| x.parse().ok()).collect();
                    if p.len() == 3 { m.pos = [p[0], p[1], p[2]]; }
                }
                "yaw" => m.yaw = v.trim().parse().unwrap_or(0.0),
                "pitch" => m.pitch = v.trim().parse().unwrap_or(0.0),
                "flying" => m.flying = v.trim() == "true",
                "last_played" => m.last_played = v.trim().parse().unwrap_or(0),
                _ => {}
            }
        }
        (!m.name.is_empty()).then_some(m)
    }
}

pub fn data_dir() -> PathBuf {
    if let Ok(d) = std::env::var("DOKUQUEST_HOME") { return d.into(); }
    #[cfg(windows)]
    if let Ok(a) = std::env::var("APPDATA") { return Path::new(&a).join("DokuQuest"); }
    if let Ok(x) = std::env::var("XDG_DATA_HOME") { return Path::new(&x).join("DokuQuest"); }
    if let Ok(h) = std::env::var("HOME") { return Path::new(&h).join(".local/share/DokuQuest"); }
    PathBuf::from("DokuQuestData")
}
pub fn saves_dir() -> PathBuf { data_dir().join("saves") }
fn sanitize(n: &str) -> String { n.chars().map(|c| if c.is_ascii_alphanumeric() || " -_".contains(c) { c } else { '_' }).collect() }
pub fn world_dir(name: &str) -> PathBuf { saves_dir().join(sanitize(name)) }
pub fn now() -> u64 { std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0) }

pub fn save_meta(m: &WorldMeta) -> io::Result<()> {
    let d = world_dir(&m.name);
    fs::create_dir_all(d.join("chunks"))?;
    fs::write(d.join("world.txt"), m.to_text())
}
pub fn load_meta(name: &str) -> Option<WorldMeta> { WorldMeta::from_text(&fs::read_to_string(world_dir(name).join("world.txt")).ok()?) }
pub fn set_last_played(name: &str) {
    let _ = fs::create_dir_all(saves_dir());
    let _ = fs::write(saves_dir().join("last_played.txt"), name);
}
pub fn last_played() -> Option<WorldMeta> { load_meta(fs::read_to_string(saves_dir().join("last_played.txt")).ok()?.trim()) }
/// All worlds, most recently played first.
pub fn list_worlds() -> Vec<WorldMeta> {
    let mut v: Vec<WorldMeta> = fs::read_dir(saves_dir()).into_iter().flatten().flatten()
        .filter_map(|e| WorldMeta::from_text(&fs::read_to_string(e.path().join("world.txt")).ok()?)).collect();
    v.sort_by(|a, b| b.last_played.cmp(&a.last_played));
    v
}

fn chunk_file(name: &str, p: [i32; 3]) -> PathBuf { world_dir(name).join("chunks").join(format!("{}_{}_{}.bin", p[0], p[1], p[2])) }
pub fn load_chunk(name: &str, p: [i32; 3]) -> Option<Chunk> { Chunk::from_bytes(&fs::read(chunk_file(name, p)).ok()?) }
pub fn save_chunk(name: &str, p: [i32; 3], c: &Chunk) -> io::Result<()> {
    let f = chunk_file(name, p);
    if let Some(d) = f.parent() { fs::create_dir_all(d)?; }
    fs::write(f, c.to_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn meta_roundtrip() {
        let m = WorldMeta { name: "My World 2".into(), seed: 99, gen_version: 1, pos: [1.5, 70.0, -3.25], yaw: 1.2, pitch: -0.3, flying: true, last_played: 5 };
        assert_eq!(WorldMeta::from_text(&m.to_text()), Some(m));
    }
}
