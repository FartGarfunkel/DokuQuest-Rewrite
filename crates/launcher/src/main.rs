//! DokuQuest launcher: the original 0.9 launcher page (web/launcher.html) in a native Rust (Tauri)
//! window. A small bridge recreates `window.emberLauncher` (the Electron preload API the page was
//! written for) on top of Rust commands: auto-update from GitHub Releases, Play, server, folder.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]
mod update;

use serde_json::{json, Value};
use std::process::Command;
use update::{base, exe, installed, Config};

const BRIDGE: &str = r#"
(() => {
  const inv = (cmd, args) => window.__TAURI__.core.invoke(cmd, args || {});
  let timer = null;
  window.emberLauncher = {
    version: '__V__',
    play: (opts) => inv('play', { opts: opts || {} }),
    server: () => inv('server'),
    openFolder: () => inv('open_folder'),
    quit: () => inv('quit'),
    gameVersion: () => inv('game_version'),
    installUpdate: () => inv('install_update'),
    news: () => inv('news'),
    gfx: () => Promise.resolve({ unlockFps: false }),
    updates: {
      config: (patch) => inv('updcfg', { patch: patch || null }),
      check: () => inv('check'),
      updateApp: () => Promise.resolve({ ok: false, msg: 'Get the newest launcher from the Releases page.' }),
      restart: () => inv('quit'),
      onProgress: (cb) => { clearInterval(timer); timer = setInterval(() => inv('progress').then(f => { if (f > 0 && f < 1) cb(f); }), 250); },
    },
    lan: {
      open: async () => ({ ok: false, msg: 'LAN play is coming to the Rust build.' }),
      close: async () => ({ ok: true }), scan: async () => [], scanStop: async () => ({ ok: true }), probe: async () => null,
    },
  };
})();
"#;

fn blocking<F: FnOnce() -> Value + Send + 'static>(f: F) -> impl std::future::Future<Output = Value> {
    async move { tauri::async_runtime::spawn_blocking(f).await.unwrap_or_else(|e| json!({ "ok": false, "msg": e.to_string() })) }
}

#[tauri::command]
fn game_version() -> String { installed().unwrap_or_default() }

#[tauri::command]
fn news() -> String { std::fs::read_to_string(base().join("news.html")).unwrap_or_default() }

#[tauri::command]
fn progress() -> f32 { update::progress() }

#[tauri::command]
async fn check() -> Value {
    blocking(|| match update::update(&Config::load()) {
        Ok((new, cur)) => json!({ "ok": true, "installed": new, "current": cur }),
        Err(e) => json!({ "ok": false, "msg": e.to_string() }),
    })
    .await
}

#[tauri::command]
async fn install_update() -> Value {
    blocking(|| match update::update(&Config::load()) {
        Ok((_, cur)) => json!({ "ok": true, "version": cur }),
        Err(e) => json!({ "ok": false, "msg": e.to_string() }),
    })
    .await
}

fn cfg_json(c: &Config) -> Value { json!({ "source": c.source, "using": c.repo(), "auto": c.auto, "hasToken": !c.token.is_empty() }) }

#[tauri::command]
fn updcfg(patch: Option<Value>) -> Value {
    let mut c = Config::load();
    let Some(p) = patch else { return json!({ "ok": true, "config": cfg_json(&c) }) };
    if let Some(s) = p["source"].as_str() {
        let s = s.trim();
        if !s.is_empty() && s.split('/').filter(|x| !x.is_empty()).count() != 2 {
            return json!({ "ok": false, "msg": "Use the form owner/name.", "config": cfg_json(&c) });
        }
        c.source = s.into();
    }
    if let Some(a) = p["auto"].as_bool() { c.auto = a; }
    if let Some(t) = p["token"].as_str() { c.token = t.trim().into(); }
    c.save();
    json!({ "ok": true, "config": cfg_json(&c) })
}

fn launch(bin: &str, args: &[&str]) -> Result<(), String> {
    let ver = installed().ok_or("The game isn't installed yet: open Options and check for updates.")?;
    let dir = base().join(ver);
    Command::new(dir.join(exe(bin))).current_dir(&dir).args(args).spawn().map(|_| ()).map_err(|e| e.to_string())
}

#[tauri::command]
async fn play(app: tauri::AppHandle, opts: Option<Value>) -> Value {
    let full = opts.as_ref().and_then(|o| o["fullscreen"].as_bool()).unwrap_or(false);
    let r = blocking(move || {
        if installed().is_none() { let _ = update::update(&Config::load()); }
        match launch("dokuquest", if full { &["--fullscreen"] } else { &[] }) { Ok(()) => json!({ "ok": true }), Err(m) => json!({ "ok": false, "msg": m }) }
    })
    .await;
    if r["ok"] == true { app.exit(0); }
    r
}

#[tauri::command]
fn server() -> Value {
    match launch("dq-server", &[]) { Ok(()) => json!({ "ok": true }), Err(m) => json!({ "ok": false, "msg": m }) }
}

#[tauri::command]
fn open_folder() {
    let dir = common_server::save::data_dir();
    let _ = std::fs::create_dir_all(&dir);
    let _ = Command::new(if cfg!(windows) { "explorer" } else { "xdg-open" }).arg(dir).spawn();
}

#[tauri::command]
fn quit(app: tauri::AppHandle) { app.exit(0); }

fn main() {
    let bridge = BRIDGE.replace("__V__", env!("CARGO_PKG_VERSION"));
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![game_version, news, progress, check, install_update, updcfg, play, server, open_folder, quit])
        .setup(move |app| {
            tauri::WebviewWindowBuilder::new(app, "main", tauri::WebviewUrl::App("launcher.html".into()))
                .title("DokuQuest")
                .inner_size(1000.0, 640.0)
                .min_inner_size(760.0, 520.0)
                .initialization_script(&bridge)
                .build()?;
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("DokuQuest launcher failed to start");
}
