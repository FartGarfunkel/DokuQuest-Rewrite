//! 16³ cubic chunks (cubic so the stacked dimensions load only the layers near the player).

pub const CS: i32 = 16;
pub const CS3: usize = 4096;

/// Block ids, x fastest then z then y. An empty Vec means "all air" (most sky chunks).
#[derive(Clone, Default, Debug, PartialEq)]
pub struct Chunk {
    pub blocks: Vec<u16>,
}

impl Chunk {
    #[inline]
    pub fn idx(x: i32, y: i32, z: i32) -> usize { (x + z * CS + y * CS * CS) as usize }
    #[inline]
    pub fn get(&self, x: i32, y: i32, z: i32) -> u16 {
        if self.blocks.is_empty() { 0 } else { self.blocks[Self::idx(x, y, z)] }
    }
    pub fn set(&mut self, x: i32, y: i32, z: i32, b: u16) {
        if self.blocks.is_empty() {
            if b == 0 { return; }
            self.blocks = vec![0; CS3];
        }
        self.blocks[Self::idx(x, y, z)] = b;
    }
    pub fn is_air(&self) -> bool { self.blocks.is_empty() }
    pub fn compact(&mut self) {
        if self.blocks.iter().all(|&b| b == 0) { self.blocks = Vec::new(); }
    }
    pub fn to_bytes(&self) -> Vec<u8> { self.blocks.iter().flat_map(|b| b.to_le_bytes()).collect() }
    pub fn from_bytes(d: &[u8]) -> Option<Chunk> {
        if d.is_empty() { return Some(Chunk::default()); }
        if d.len() != CS3 * 2 { return None; }
        let mut c = Chunk { blocks: d.chunks_exact(2).map(|p| u16::from_le_bytes([p[0], p[1]])).collect() };
        c.compact();
        Some(c)
    }
}
