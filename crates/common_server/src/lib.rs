//! DokuQuest shared core. No external dependencies, no engine types: compiles in seconds,
//! unit-testable, and used by the client (`game_engine`), the dedicated server and the launcher.
pub mod blocks;
pub mod chunk;
pub mod dims;
pub mod mesh;
pub mod noise;
pub mod save;
pub mod style;
pub mod worldgen;
