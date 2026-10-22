//! Mob types and their jointed voxel bodies, generated from the JS MOB_TYPES and MODEL_DEFS rigs
//! (data/mobs.tsv, data/models.tsv). Behaviour flags keep the JS names (hostile, flyer, swims…).
use std::collections::HashMap;
use std::sync::OnceLock;

static MOBS_TSV: &str = include_str!("../data/mobs.tsv");
static MODELS_TSV: &str = include_str!("../data/models.tsv");

#[derive(Debug)]
pub struct MobType {
    pub key: String,
    pub label: String,
    pub model: String,
    pub hp: f32,
    pub w: f32,
    pub h: f32,
    pub speed: f32,
    pub damage: f32,
    pub hostile: bool,
    pub flags: Vec<String>,
    /// (item key, min, max, chance)
    pub drops: Vec<(String, u32, u32, f32)>,
    pub xp: u32,
}

impl MobType {
    pub fn has(&self, f: &str) -> bool { self.flags.iter().any(|x| x == f) }
    pub fn flies(&self) -> bool { self.has("flyer") || self.has("fly") || self.has("floaty") }
    pub fn aquatic(&self) -> bool { self.has("water") }
}

#[derive(Debug)]
pub struct Part {
    pub name: String,
    pub parent: Option<usize>,
    /// Joint position (relative to the parent's joint; model space for roots), block units.
    pub pivot: [f32; 3],
    pub min: [f32; 3],
    pub max: [f32; 3],
    /// "head", "legFR", "armL", … (used to animate).
    pub limb: String,
    pub side: f32,
    pub front: bool,
    pub tex: [u16; 6],
}

#[derive(Debug)]
pub struct Model {
    pub scale: f32,
    pub parts: Vec<Part>,
}

pub struct MobData {
    pub types: Vec<MobType>,
    pub keys: HashMap<String, usize>,
    pub models: HashMap<String, Model>,
}

impl MobData {
    pub fn get(&self, key: &str) -> Option<&MobType> { self.keys.get(key).map(|&i| &self.types[i]) }
}

fn v3(s: &str) -> [f32; 3] {
    let v: Vec<f32> = s.split(',').map(|x| x.parse().unwrap_or(0.0)).collect();
    [v.first().copied().unwrap_or(0.0), v.get(1).copied().unwrap_or(0.0), v.get(2).copied().unwrap_or(0.0)]
}

pub fn data() -> &'static MobData {
    static D: OnceLock<MobData> = OnceLock::new();
    D.get_or_init(|| {
        let mut types = Vec::new();
        for l in MOBS_TSV.lines() {
            let c: Vec<&str> = l.split('\t').collect();
            if c.len() < 12 { continue; }
            let f = |i: usize| c[i].parse::<f32>().unwrap_or(0.0);
            let drops = c[10].split(';').filter(|s| !s.is_empty()).filter_map(|d| {
                let p: Vec<&str> = d.split(':').collect();
                Some((p.first()?.to_string(), p.get(1)?.parse().unwrap_or(1), p.get(2)?.parse().unwrap_or(1), p.get(3)?.parse().unwrap_or(1.0)))
            });
            types.push(MobType {
                key: c[0].into(),
                label: c[1].into(),
                model: c[2].into(),
                hp: f(3).max(1.0),
                w: f(4).max(0.2),
                h: f(5).max(0.2),
                speed: f(6).max(0.3),
                damage: f(7),
                hostile: c[8] == "1",
                flags: c[9].split(',').filter(|s| !s.is_empty()).map(String::from).collect(),
                drops: drops.collect(),
                xp: f(11) as u32,
            });
        }
        let keys = types.iter().enumerate().map(|(i, t)| (t.key.clone(), i)).collect();
        let mut models: HashMap<String, Model> = HashMap::new();
        let mut cur: Option<(String, Model)> = None;
        for l in MODELS_TSV.lines() {
            let c: Vec<&str> = l.split('\t').collect();
            match c.first() {
                Some(&"M") if c.len() >= 3 => {
                    if let Some((k, m)) = cur.take() { models.insert(k, m); }
                    cur = Some((c[1].into(), Model { scale: c[2].parse().unwrap_or(1.0), parts: Vec::new() }));
                }
                Some(&"P") if c.len() >= 10 => {
                    if let Some((_, m)) = cur.as_mut() {
                        let parent = m.parts.iter().position(|p| p.name == c[2]);
                        let t: Vec<u16> = c[9].split(',').map(|x| x.parse().unwrap_or(u16::MAX)).collect();
                        m.parts.push(Part {
                            name: c[1].into(),
                            parent,
                            pivot: v3(c[3]),
                            min: v3(c[4]),
                            max: v3(c[5]),
                            limb: c[6].into(),
                            side: c[7].parse().unwrap_or(0.0),
                            front: c[8] == "1",
                            tex: std::array::from_fn(|i| t.get(i).copied().unwrap_or(u16::MAX)),
                        });
                    }
                }
                _ => {}
            }
        }
        if let Some((k, m)) = cur.take() { models.insert(k, m); }
        MobData { types, keys, models }
    })
}

#[cfg(test)]
mod tests {
    #[test]
    fn loads() {
        let d = super::data();
        let cow = d.get("cow").expect("cow");
        assert!(d.models.get(&cow.model).is_some_and(|m| m.parts.len() >= 5));
        assert!(d.types.iter().any(|t| t.hostile));
    }
}
