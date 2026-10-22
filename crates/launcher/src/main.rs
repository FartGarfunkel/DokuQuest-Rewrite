//! DokuQuest launcher. On start: ask GitHub for the latest release, download the build for this
//! platform, verify its SHA-256, unpack it into <data>/game/<tag>/, then run the game. Offline or on
//! any error it starts the last installed build. `--offline` skips the check; other args go to the game.
use sha2::{Digest, Sha256};
use std::error::Error;
use std::{fs, io::Read, path::Path, process::Command, time::Duration};

const REPO: &str = "FartGarfunkel/DokuQuest-Rewrite";

fn target() -> &'static str { if cfg!(windows) { "windows-x86_64" } else { "linux-x86_64" } }
fn exe(name: &str) -> String { if cfg!(windows) { format!("{name}.exe") } else { name.to_string() } }

fn main() {
    let base = common_server::save::data_dir().join("game");
    let _ = fs::create_dir_all(&base);
    let args: Vec<String> = std::env::args().skip(1).collect();
    if !args.iter().any(|a| a == "--offline") {
        let installed = fs::read_to_string(base.join("version.txt")).ok();
        match update(&base, installed.as_deref().map(str::trim)) {
            Ok(Some(v)) => println!("DokuQuest updated to {v}"),
            Ok(None) => println!("DokuQuest is up to date"),
            Err(e) => eprintln!("Update check failed ({e}); starting the installed build"),
        }
    }
    let Some(ver) = fs::read_to_string(base.join("version.txt")).ok().map(|s| s.trim().to_string()) else {
        eprintln!("No game installed yet and no release could be downloaded. Check your connection and try again.");
        std::process::exit(1);
    };
    let dir = base.join(&ver);
    match Command::new(dir.join(exe("dokuquest"))).current_dir(&dir).args(args.iter().filter(|a| *a != "--offline")).status() {
        Ok(s) => std::process::exit(s.code().unwrap_or(0)),
        Err(e) => {
            eprintln!("Could not start DokuQuest {ver}: {e}");
            std::process::exit(1);
        }
    }
}

/// Returns Some(tag) if a new build was installed.
fn update(base: &Path, installed: Option<&str>) -> Result<Option<String>, Box<dyn Error>> {
    let agent = ureq::AgentBuilder::new().user_agent("DokuQuest-Launcher").timeout(Duration::from_secs(60)).build();
    // Newest release first, pre-releases included (alpha builds are often marked pre-release).
    let list: serde_json::Value = agent.get(&format!("https://api.github.com/repos/{REPO}/releases?per_page=10")).call()?.into_json()?;
    let rel = list.as_array().and_then(|v| v.iter().find(|r| r["draft"] != true)).cloned().ok_or("no releases published yet")?;
    let tag = rel["tag_name"].as_str().ok_or("release has no tag")?.to_string();
    if installed == Some(tag.as_str()) && base.join(&tag).join(exe("dokuquest")).exists() { return Ok(None); }
    let want = format!("dokuquest-{}.tar.gz", target());
    let assets = rel["assets"].as_array().ok_or("release has no assets")?;
    let url_of = |n: &str| assets.iter().find(|a| a["name"] == n).and_then(|a| a["browser_download_url"].as_str()).map(String::from);
    let url = url_of(&want).ok_or("no build for this platform in the latest release")?;
    println!("Downloading DokuQuest {tag}…");
    let mut bytes = Vec::new();
    agent.get(&url).call()?.into_reader().read_to_end(&mut bytes)?;
    if let Some(sum_url) = url_of(&format!("{want}.sha256")) {
        let expect = agent.get(&sum_url).call()?.into_string()?;
        let got = format!("{:x}", Sha256::digest(&bytes));
        if expect.split_whitespace().next() != Some(got.as_str()) { return Err("download failed its SHA-256 check".into()); }
    }
    let (dir, tmp) = (base.join(&tag), base.join(format!("{tag}.partial")));
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(&tmp)?;
    tar::Archive::new(flate2::read::GzDecoder::new(&bytes[..])).unpack(&tmp)?;
    let _ = fs::remove_dir_all(&dir);
    fs::rename(&tmp, &dir)?;
    fs::write(base.join("version.txt"), &tag)?;
    for e in fs::read_dir(base)?.flatten() {
        if e.path().is_dir() && e.path() != dir { let _ = fs::remove_dir_all(e.path()); }
    }
    Ok(Some(tag))
}
