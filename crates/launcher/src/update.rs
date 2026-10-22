//! Game updates from GitHub Releases: newest release (pre-releases included) → this platform's
//! `dokuquest-<target>.tar.gz` → SHA-256 check → unpack into <data>/game/<tag>/. Release notes are
//! kept as news.html for the launcher page.
use sha2::{Digest, Sha256};
use std::error::Error;
use std::io::Read;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU32, Ordering};
use std::{fs, time::Duration};

pub const DEFAULT_REPO: &str = "FartGarfunkel/DokuQuest-Rewrite";
static PROGRESS: AtomicU32 = AtomicU32::new(0);

pub fn progress() -> f32 { f32::from_bits(PROGRESS.load(Ordering::Relaxed)) }
fn set_progress(f: f32) { PROGRESS.store(f.to_bits(), Ordering::Relaxed); }

pub fn base() -> PathBuf { common_server::save::data_dir().join("game") }
fn target() -> &'static str { if cfg!(windows) { "windows-x86_64" } else { "linux-x86_64" } }
pub fn exe(name: &str) -> String { if cfg!(windows) { format!("{name}.exe") } else { name.to_string() } }
pub fn installed() -> Option<String> {
    let v = fs::read_to_string(base().join("version.txt")).ok()?.trim().to_string();
    base().join(&v).join(exe("dokuquest")).exists().then_some(v)
}

/// Launcher settings (the page's Options → Updates panel).
#[derive(Clone, Debug)]
pub struct Config {
    pub source: String,
    pub auto: bool,
    pub token: String,
}
impl Config {
    fn path() -> PathBuf { common_server::save::data_dir().join("launcher.txt") }
    pub fn load() -> Config {
        let mut c = Config { source: String::new(), auto: true, token: String::new() };
        for l in fs::read_to_string(Self::path()).unwrap_or_default().lines() {
            match l.split_once('=') {
                Some(("source", v)) => c.source = v.trim().into(),
                Some(("auto", v)) => c.auto = v.trim() != "false",
                Some(("token", v)) => c.token = v.trim().into(),
                _ => {}
            }
        }
        c
    }
    pub fn save(&self) {
        let _ = fs::create_dir_all(common_server::save::data_dir());
        let _ = fs::write(Self::path(), format!("source={}\nauto={}\ntoken={}\n", self.source, self.auto, self.token));
    }
    pub fn repo(&self) -> &str { if self.source.is_empty() { DEFAULT_REPO } else { &self.source } }
}

fn get(agent: &ureq::Agent, url: &str, cfg: &Config) -> Result<ureq::Response, Box<dyn Error>> {
    let mut r = agent.get(url).set("Accept", "application/vnd.github+json");
    if !cfg.token.is_empty() { r = r.set("Authorization", &format!("Bearer {}", cfg.token)); }
    Ok(r.call()?)
}

fn esc(s: &str) -> String { s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;") }

/// Release notes (markdown) → simple HTML for the news panel.
fn notes_html(tag: &str, body: &str) -> String {
    let mut h = format!("<h3>{}</h3>", esc(tag));
    for l in body.lines().map(str::trim).filter(|l| !l.is_empty()) {
        if let Some(i) = l.strip_prefix("- ").or_else(|| l.strip_prefix("* ")) { h += &format!("<p>• {}</p>", esc(i)); }
        else if let Some(t) = l.strip_prefix('#').map(|t| t.trim_start_matches('#').trim()) { h += &format!("<h4>{}</h4>", esc(t)); }
        else { h += &format!("<p>{}</p>", esc(l)); }
    }
    h
}

/// Checks the newest release; installs it when newer. Returns (newly installed tag, current tag).
pub fn update(cfg: &Config) -> Result<(Option<String>, String), Box<dyn Error>> {
    let agent = ureq::AgentBuilder::new().user_agent("DokuQuest-Launcher").timeout(Duration::from_secs(120)).build();
    let list: serde_json::Value = get(&agent, &format!("https://api.github.com/repos/{}/releases?per_page=10", cfg.repo()), cfg)?.into_json()?;
    let rel = list.as_array().and_then(|v| v.iter().find(|r| r["draft"] != true)).cloned().ok_or("no releases published yet")?;
    let tag = rel["tag_name"].as_str().ok_or("release has no tag")?.to_string();
    let _ = fs::create_dir_all(base());
    let _ = fs::write(base().join("news.html"), notes_html(&tag, rel["body"].as_str().unwrap_or("")));
    if installed().as_deref() == Some(tag.as_str()) { return Ok((None, tag)); }
    let want = format!("dokuquest-{}.tar.gz", target());
    let assets = rel["assets"].as_array().ok_or("release has no files")?;
    let url_of = |n: &str| assets.iter().find(|a| a["name"] == n).and_then(|a| a["browser_download_url"].as_str()).map(String::from);
    let url = url_of(&want).ok_or("the newest release has no build for this computer yet")?;
    let resp = get(&agent, &url, cfg)?;
    let total = resp.header("Content-Length").and_then(|s| s.parse::<usize>().ok()).unwrap_or(0);
    let (mut bytes, mut buf, mut rd) = (Vec::new(), vec![0u8; 1 << 16], resp.into_reader());
    set_progress(0.001);
    loop {
        let n = rd.read(&mut buf)?;
        if n == 0 { break; }
        bytes.extend_from_slice(&buf[..n]);
        if total > 0 { set_progress((bytes.len() as f32 / total as f32).min(0.999)); }
    }
    set_progress(1.0);
    if let Some(sum_url) = url_of(&format!("{want}.sha256")) {
        let expect = get(&agent, &sum_url, cfg)?.into_string()?;
        if expect.split_whitespace().next() != Some(format!("{:x}", Sha256::digest(&bytes)).as_str()) {
            return Err("the download was damaged (SHA-256 mismatch); try again".into());
        }
    }
    let (dir, tmp) = (base().join(&tag), base().join(format!("{tag}.partial")));
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(&tmp)?;
    tar::Archive::new(flate2::read::GzDecoder::new(&bytes[..])).unpack(&tmp)?;
    let _ = fs::remove_dir_all(&dir);
    fs::rename(&tmp, &dir)?;
    fs::write(base().join("version.txt"), &tag)?;
    for e in fs::read_dir(base())?.flatten() {
        if e.path().is_dir() && e.path() != dir { let _ = fs::remove_dir_all(e.path()); }
    }
    Ok((Some(tag.clone()), tag))
}
