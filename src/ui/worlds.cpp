// ui/worlds.cpp — the world list (ui.js refreshWorlds): play, settings, back up, delete (with a confirm row),
// the New world form with game mode and the world options from game/rules.js (worldSettingsForm), importing a
// backup, and the World settings panel (rename, game mode, rules, day and night, terrain version).
// Worlds are folders in userDir()/saves; backups are .zip files in userDir()/backups.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "gen/generator.h"
#include <imgui.h>
#include <miniz.h>
#include <ctime>

namespace dq {
using namespace ui;

// ---- helpers ------------------------------------------------------------------------------------------------
std::string agoText(i64 t) {
  if (!t) return "never";
  double s = (unixMillis() - t) / 1000.0;
  if (s < 60) return "just now";
  if (s < 3600) return std::to_string((int)(s / 60)) + " min ago";
  if (s < 86400) return std::to_string((int)(s / 3600)) + " h ago";
  std::time_t tt = (std::time_t)(t / 1000); char buf[32]; std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&tt)); return buf;
}
static std::string fmtPlay(double sec) { int h = (int)(sec / 3600), m = (int)(sec / 60) % 60; return h ? std::to_string(h) + " h " + std::to_string(m) + " min" : std::to_string(m) + " min"; }
static std::string slug(const std::string& name) {
  std::string s; for (char c : name) if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') s += c;
  std::string o; bool sp = false;
  for (char c : s) { if (c == ' ') { sp = !o.empty(); continue; } if (sp) { o += '-'; sp = false; } o += (char)tolower((unsigned char)c); }
  return o.empty() ? "world" : o;
}

bool backupWorldFolder(const fs::path& folder, const std::string& name, fs::path& out, std::string& err) {
  if (app().session && app().session->level && app().session->level->folder == folder) for (auto& [r, w] : app().session->level->allRealms()) w->flush();
  char buf[32]; std::time_t t = std::time(nullptr); std::strftime(buf, sizeof buf, "%Y-%m-%d_%H.%M", std::localtime(&t));
  out = userSub("backups") / (slug(name) + "-dokuquest-backup-" + buf + ".zip");
  mz_zip_archive z{};
  if (!mz_zip_writer_init_file(&z, out.string().c_str(), 0)) { err = "can't write " + out.string(); return false; }
  bool ok = true;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(folder, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file()) continue;
    std::string rel = fs::relative(it->path(), folder, ec).generic_string();
    if (!mz_zip_writer_add_file(&z, rel.c_str(), it->path().string().c_str(), nullptr, 0, MZ_DEFAULT_COMPRESSION)) { ok = false; err = "couldn't add " + rel; break; }
  }
  if (ok && !mz_zip_writer_finalize_archive(&z)) { ok = false; err = "couldn't finish the file"; }
  mz_zip_writer_end(&z);
  if (!ok) fs::remove(out, ec);
  return ok;
}

// unzip a backup into a new folder in saves/; returns the world's name
static bool importWorldZip(const fs::path& zip, std::string& name, std::string& err) {
  mz_zip_archive z{};
  if (!mz_zip_reader_init_file(&z, zip.string().c_str(), 0)) { err = "That file isn't a world backup"; return false; }
  int n = (int)mz_zip_reader_get_num_files(&z);
  int levelIdx = -1;
  for (int i = 0; i < n; i++) { char fn[512]; mz_zip_reader_get_filename(&z, i, fn, sizeof fn); if (std::string(fn) == "level.json") levelIdx = i; }
  if (levelIdx < 0) { mz_zip_reader_end(&z); err = "That file isn't a world backup (no level.json)"; return false; }
  size_t sz = 0; void* p = mz_zip_reader_extract_to_heap(&z, levelIdx, &sz, 0);
  json lj; try { lj = json::parse(std::string((const char*)p, sz)); } catch (...) {}
  mz_free(p);
  name = lj.is_object() ? lj.value("name", std::string("World")) : "World";
  fs::path saves = userSub("saves");
  std::string base; for (char c : name) base += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
  if (base.empty()) base = "World";
  std::string folder = base; int k = 1;
  while (fs::exists(saves / folder)) folder = base + "_" + std::to_string(++k);
  bool ok = true;
  for (int i = 0; i < n && ok; i++) {
    char fn[512]; mz_zip_reader_get_filename(&z, i, fn, sizeof fn);
    if (mz_zip_reader_is_file_a_directory(&z, i)) continue;
    std::string rel = fn;
    if (rel.find("..") != std::string::npos || (!rel.empty() && (rel[0] == '/' || rel[0] == '\\'))) continue;   // stay inside the world folder
    fs::path dst = saves / folder / fs::path(rel);
    std::error_code ec; fs::create_directories(dst.parent_path(), ec);
    if (!mz_zip_reader_extract_to_file(&z, i, dst.string().c_str(), 0)) { ok = false; err = "couldn't unpack " + rel; }
  }
  mz_zip_reader_end(&z);
  if (ok) { json fix = lj; fix["folder"] = folder; writeJson(saves / folder / "level.json", fix); }
  return ok;
}

// ---- the world settings form (rules.js worldSettingsForm) ----------------------------------------------------
namespace {
const std::vector<std::array<const char*, 3>>& ruleToggles() {
  static const std::vector<std::array<const char*, 3>> t = {
    {"daylightCycle", "Day and night cycle", "Turn off to keep the sun where it is"},
    {"keepInventory", "Keep inventory on death", "You respawn with everything you had"},
    {"graves", "Graves", "What you drop on death is kept safe in a grave, with a compass that points to it"},
    {"mobSpawning", "Creatures spawn", "Animals and monsters appear on their own"},
    {"mobGriefing", "Creatures break blocks", "Explosions from creatures and boss attacks damage the land"},
    {"tntExplodes", "TNT explodes", "Turn off and TNT just fizzles"},
    {"fireSpread", "Fire spreads", "Fire can spread to wood and leaves nearby"},
    {"blockDrops", "Blocks drop items", "Broken blocks drop what they're made of"},
    {"fallDamage", "Fall damage", ""},
    {"drowning", "Drowning", "Your breath runs out under water"},
    {"fireDamage", "Fire and lava damage", ""},
    {"naturalRegen", "Slow natural healing", "Heal half a heart every few seconds when not hurt for a while (the classic game had none)"},
    {"grassSpread", "Grass spreads", "Grass slowly grows onto nearby bare dirt"},
    {"saplingsPlant", "Saplings plant themselves", "A sapling left lying on soil takes root"},
    {"showCoords", "Show coordinates", "Your position, shown in the corner"},
    {"commands", "Allow commands", "Chat commands and WorldEdit (press T or /)"},
    {"downed", "Downed, not dead (co-op)", "With someone else playing, you go down instead of dying, and they can help you back up"}};
  return t;
}
const int MINS[] = {3, 5, 7, 10, 15, 20, 30, 45, 60};

struct WorldForm {
  json rules; std::string name, mode; bool named = false, moded = false;
  void init(const json& r, bool creative, const std::string* nm, const std::string* md) {
    rules = ruleDefaults(creative);
    if (r.is_object()) for (auto& [k, v] : r.items()) rules[k] = v;
    rules["_v"] = 1;
    named = nm != nullptr; moded = md != nullptr;
    if (nm) name = *nm;
    if (md) mode = *md;
  }
  // oneColumn: the New world form (its toggles scroll in a short box)
  void draw(float width, bool oneColumn) {
    float z = Z();
    auto row = [&](const char* lbl) { label(F_BODY, col::dim, lbl); ImGui::SameLine(112 * z); };
    if (named) { ImGui::AlignTextToFramePadding(); row("Name"); inputText("##wsfname", &name, nullptr, 32, width - 112 * z); }
    if (moded) {
      ImGui::AlignTextToFramePadding(); row("Game mode");
      int m = mode == "creative" ? 1 : 0;
      if (combo("##wsfmode", &m, {"Survival", "Creative"}, width - 112 * z)) mode = m ? "creative" : "survival";
    }
    // Daytime lasts / Night lasts
    std::vector<std::string> opts; for (int m : MINS) opts.push_back(std::to_string(m) + " min");
    auto sel = [&](const char* key, const char* lbl, float w) {
      int v = rules.value(key, 10), cur = 3;
      for (int i = 0; i < 9; i++) if (MINS[i] == v) cur = i;
      ImGui::BeginGroup(); label(F_BODY, col::dim, lbl);
      if (combo((std::string("##") + key).c_str(), &cur, opts, w)) rules[key] = MINS[cur];
      ImGui::EndGroup();
    };
    float half = (width - 14 * z) * 0.5f;
    if (width < 360 * z) { sel("dayMinutes", "Daytime lasts", width); sel("nightMinutes", "Night lasts", width); }
    else { sel("dayMinutes", "Daytime lasts", half); ImGui::SameLine(0, 14 * z); sel("nightMinutes", "Night lasts", half); }
    ImGui::Dummy(ImVec2(1, 4 * z));
    int cols = oneColumn ? 1 : std::max(1, (int)(width / (230 * z)));
    float cw = (width - (cols - 1) * 14 * z) / cols;
    if (oneColumn) {
      ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, 0.34f * app().height));
      ImGui::BeginChild("##toggles", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
      padScrollHere();
    }
    int i = 0;
    for (auto& t : ruleToggles()) {
      if (i % cols) ImGui::SameLine(0, 14 * z);
      ImGui::BeginGroup();
      ImVec2 p0 = ImGui::GetCursorScreenPos();
      bool v = rules.value(t[0], false);
      ImGui::PushID(t[0]);
      ImGui::SetCursorScreenPos(p0 + ImVec2(6, 5) * z);
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - 12 * z);
      if (ImGui::Checkbox(t[1], &v)) { rules[t[0]] = v; playClick(); }
      ImGui::PopTextWrapPos();
      if (*t[2]) { ImGui::SetCursorScreenPos(ImVec2(p0.x + 6 * z + ImGui::GetFrameHeight() + 8 * z, ImGui::GetCursorScreenPos().y - 4 * z)); label(F_SMALL, col::dim, t[2], cw - ImGui::GetFrameHeight() - 26 * z); }
      ImGui::PopID();
      ImVec2 p1 = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + cw, p1.y), IM_COL32(0, 0, 0, 46));
      ImGui::GetWindowDrawList()->AddRect(p0, ImVec2(p0.x + cw, p1.y), col::line);
      ImGui::Dummy(ImVec2(cw, 2 * z));
      ImGui::EndGroup();
      i++;
    }
    if (oneColumn) ImGui::EndChild();
  }
};

struct WorldEntry { fs::path folder; json level; std::string dim = "Overworld"; int lvl = 0; };
std::vector<WorldEntry> listWorlds() {
  std::vector<WorldEntry> v;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(app().savesDir(), ec)) {
    if (!e.is_directory() || !fs::exists(e.path() / "level.json")) continue;
    WorldEntry w; w.folder = e.path();
    if (auto j = readJson(e.path() / "level.json")) w.level = *j; else continue;
    if (auto pj = readJson(e.path() / "players" / (app().settings.playerName + ".json"))) {
      w.lvl = pj->value("level", 0);
      if (pj->contains("pos") && (*pj)["pos"].is_array() && (*pj)["pos"].size() == 3) w.dim = dimName(dimOfY((int)(*pj)["pos"][1].get<double>()));
    }
    v.push_back(w);
  }
  std::sort(v.begin(), v.end(), [](const WorldEntry& a, const WorldEntry& b) { return a.level.value("lastPlayed", (i64)0) > b.level.value("lastPlayed", (i64)0); });
  return v;
}
}

// ---- world list + new world ----------------------------------------------------------------------------------
class WorldsScreen : public Screen {
public:
  const char* id() const override { return "worlds"; }
  std::vector<WorldEntry> worlds;
  std::string name = "New World", seed, msg, confirm;
  int mode = 0; bool moreOpen = false, importOpen = false;
  WorldForm form;
  double refreshAt = 0;
  void onOpen(App& a) override { worlds = listWorlds(); form.init(json(), false, nullptr, nullptr); }
  bool onBack(App&) override {
    if (backConsumedByText()) return true;
    if (!confirm.empty()) { confirm.clear(); return true; }
    if (importOpen) { importOpen = false; return true; }
    return false;
  }
  void update(App& a, double) override { if (a.time > refreshAt) { worlds = listWorlds(); refreshAt = a.time + 2; } }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    // the title's panorama keeps turning behind (title is below this in the stack)
    float z = Z();
    Panel pn;
    pn.begin("##worlds", "Worlds", 860, "Back");
    float bodyH = std::min(a.height - 32 * z - 56 * z - 44 * z, 500 * z);
    float leftW = std::floor(pn.width * 1.4f / 2.4f), rightW = pn.width - leftW;
    // left: the list
    ImGui::BeginChild("##list", ImVec2(leftW, bodyH), ImGuiChildFlags_NavFlattened);
    padScrollHere();
    ImGui::SetCursorPos(ImVec2(12 * z, 12 * z));
    ImGui::BeginGroup();
    float cardW = leftW - 24 * z - ImGui::GetStyle().ScrollbarSize;
    if (worlds.empty()) label(F_BODY, col::muted, "No worlds yet. Name one and press Create world.", cardW);
    for (auto& w : worlds) drawWorld(a, w, cardW);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 8 * z));
    ImGui::EndChild();
    ImVec2 lr = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(lr.x - 2 * z, ImGui::GetItemRectMin().y), ImVec2(lr.x, lr.y), col::line);
    ImGui::SameLine(0, 0);
    // right: New world
    ImGui::BeginChild("##new", ImVec2(rightW, bodyH), ImGuiChildFlags_NavFlattened);
    padScrollHere();
    ImGui::SetCursorPos(ImVec2(16 * z, 14 * z));
    ImGui::BeginGroup();
    float fw = rightW - 32 * z - ImGui::GetStyle().ScrollbarSize;
    if (importOpen) drawImport(a, fw); else drawNew(a, fw);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 8 * z));
    ImGui::EndChild();
    pn.beginFooter();
    label(F_MID, col::muted, "Worlds are stored in the game folder. Use Backup file to keep a copy.");
    pn.endFooter();
    pn.end();
    if (pn.closed) a.popScreen();
  }
  void drawWorld(App& a, WorldEntry& w, float cw) {
    float z = Z();
    const json& L = w.level;
    std::string nm = L.value("name", std::string("World"));
    ImGui::PushID(w.folder.string().c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    dl->ChannelsSplit(2); dl->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(p0 + ImVec2(12, 10) * z);
    ImGui::BeginGroup();
    float inner = cw - 24 * z;
    const char* badge = "This device";
    ImVec2 bs = textSize(F_SMALL, badge, 0, 11 / 12.5f) + ImVec2(12, 4) * z;
    ImGui::PushFont(font(F_BODY));
    label(F_BIG, col::text, ellipsize(F_BIG, nm, inner - bs.x - 10 * z).c_str());
    ImGui::PopFont();
    { ImVec2 bp(p0.x + cw - 12 * z - bs.x, p0.y + 10 * z); dl->AddRect(bp, bp + bs, col::lineHi, 3 * z); text(dl, F_SMALL, bp + ImVec2(6, 2) * z, col::muted, badge, 0, 11 / 12.5f); }
    std::string seedS = L.contains("seedText") && !L.value("seedText", std::string()).empty() ? L.value("seedText", std::string()) : std::to_string(L.value("seed", 0));
    double play = L.contains("data") && L["data"].is_object() ? L["data"].value("playTime", 0.0) : 0.0;
    std::string meta = w.dim + "    Level " + std::to_string(w.lvl) + "    Played " + fmtPlay(play) + "    Last saved " + agoText(L.value("lastPlayed", (i64)0)) + "    Seed " + seedS;
    label(F_SMALL, col::muted, meta.c_str(), inner);
    ImGui::Dummy(ImVec2(1, 2 * z));
    if (button("Play", BTN_PRIMARY, true)) { fs::path f = w.folder; a.settings.extra["played"] = true; uiDefer([f](App& app) { app.openWorld(f); }); }
    ImGui::SameLine(0, 6 * z);
    if (button("Settings", BTN_NORMAL, true)) a.pushScreen(makeWorldSettingsScreen(w.folder));
    ImGui::SameLine(0, 6 * z);
    if (button("Backup file", BTN_NORMAL, true)) { fs::path out; std::string err; msg = backupWorldFolder(w.folder, nm, out, err) ? "Backup saved: " + out.string() : "Backup failed: " + err; }
    ImGui::SameLine(0, 6 * z);
    if (button("Delete", BTN_DANGER, true)) confirm = w.folder.string();
    if (confirm == w.folder.string()) {
      std::string q = "Delete \xE2\x80\x9C" + nm + "\xE2\x80\x9D for good?";
      ImGui::AlignTextToFramePadding(); label(F_MID, col::rgb(0xffcabf), q.c_str());
      ImGui::SameLine(0, 8 * z);
      if (button("Delete##yes", BTN_DANGER, true)) { std::error_code ec; fs::remove_all(w.folder, ec); confirm.clear(); worlds = listWorlds(); ImGui::EndGroup(); dl->ChannelsMerge(); ImGui::PopID(); return; }
      ImGui::SameLine(0, 6 * z);
      if (button("Keep", BTN_NORMAL, true) || ImGui::IsWindowAppearing()) confirm.clear();
    }
    ImGui::EndGroup();
    ImVec2 p1(p0.x + cw, ImGui::GetItemRectMax().y + 10 * z);
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(p0, p1, col::slot, 4 * z); dl->AddRect(p0, p1, col::line, 4 * z, 0, 2 * z);
    dl->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 8 * z));
    ImGui::Dummy(ImVec2(cw, 0));
    ImGui::PopID();
  }
  void drawNew(App& a, float fw) {
    float z = Z();
    label(F_H3, col::text, "New world");
    label(F_MID, col::muted, "Name"); inputText("##nwname", &name, nullptr, 32, fw);
    ImGui::PushFont(font(F_MID)); ImGui::PushStyleColor(ImGuiCol_Text, col::muted); ImGui::TextUnformatted("Seed"); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, withAlpha(col::muted, 0.8f)); ImGui::TextUnformatted("(optional \xE2\x80\x94 words or numbers)"); ImGui::PopStyleColor(2); ImGui::PopFont();
    inputText("##nwseed", &seed, "Random", 40, fw);
    label(F_MID, col::muted, "Game mode");
    if (combo("##nwmode", &mode, {"Survival \xE2\x80\x94 gather, craft, stay alive", "Creative \xE2\x80\x94 every block, flight, no damage"}, fw)) form.rules["commands"] = mode == 1;
    // <details class="more-opts"><summary> in gold, wrapping
    {
      const char* sum = "More world options: day and night, game rules, commands";
      float tri = 10 * z; ImVec2 ts = textSize(F_BODY, sum, fw - tri - 6 * z);
      ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton("##more", ImVec2(fw, ts.y + 4 * z), ImGuiButtonFlags_EnableNav)) { moreOpen = !moreOpen; playClick(); }
      bool hot = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && navVisible());
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 c(p.x + tri * 0.5f, p.y + textSize(F_BODY, "A").y * 0.5f + 1 * z);
      if (moreOpen) dl->AddTriangleFilled(ImVec2(c.x - 5 * z, c.y - 3 * z), ImVec2(c.x + 5 * z, c.y - 3 * z), ImVec2(c.x, c.y + 4 * z), col::gold);
      else dl->AddTriangleFilled(ImVec2(c.x - 3 * z, c.y - 5 * z), ImVec2(c.x + 4 * z, c.y), ImVec2(c.x - 3 * z, c.y + 5 * z), col::gold);
      text(dl, F_BODY, ImVec2(p.x + tri + 6 * z, p.y), hot ? col::rgb(0xffd877) : col::gold, sum, fw - tri - 6 * z);
    }
    if (moreOpen) form.draw(fw, true);
    ImGui::Dummy(ImVec2(1, 2 * z));
    if (button("Create world", BTN_PRIMARY, false, fw)) {
      LevelInfo li; li.name = name.empty() ? "New World" : name; li.seedText = seed; li.mode = mode ? "creative" : "survival";
      li.rules = form.rules;
      a.settings.extra["played"] = true;
      uiDefer([li](App& app) { if (!app.createWorld(li)) app.toast("Could not create the world", "warn"); });
    }
    ImGui::Dummy(ImVec2(1, 4 * z));
    if (button("Import world file", BTN_NORMAL, false, fw)) { importOpen = true; msg.clear(); }
    if (!msg.empty()) label(F_MID, col::muted, msg.c_str(), fw);
  }
  void drawImport(App& a, float fw) {
    label(F_H3, col::text, "Import world file");
    fs::path dir = userSub("backups");
    label(F_MID, col::muted, ("World backups (.zip) in " + dir.string() + " \xE2\x80\x94 put a backup there to bring it in.").c_str(), fw);
    std::error_code ec; int n = 0;
    for (auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file() || e.path().extension() != ".zip") continue;
      n++;
      std::string fn = e.path().filename().string();
      ImGui::PushID(fn.c_str());
      if (button(ellipsize(F_MID, fn, fw - 40 * Z()).c_str(), BTN_NORMAL, true, fw)) {
        std::string nm, err;
        if (importWorldZip(e.path(), nm, err)) { msg = "Imported \xE2\x80\x9C" + nm + "\xE2\x80\x9D."; worlds = listWorlds(); importOpen = false; }
        else msg = err;
      }
      ImGui::PopID();
    }
    if (!n) label(F_MID, col::muted, "No backups found yet.", fw);
    if (button("Back##import", BTN_GHOST, false, fw)) importOpen = false;
    if (!msg.empty()) label(F_MID, col::muted, msg.c_str(), fw);
  }
};
std::unique_ptr<Screen> makeWorldsScreen() { return std::make_unique<WorldsScreen>(); }

// ---- World settings ----------------------------------------------------------------------------------------
class WorldSettingsScreen : public Screen {
public:
  explicit WorldSettingsScreen(fs::path f) : folder(std::move(f)) {}
  fs::path folder; json level; WorldForm form; bool live = false, upgrade = false; int gv = 3;
  const char* id() const override { return "wsettings"; }
  bool onBack(App&) override { return backConsumedByText(); }
  void onOpen(App& a) override {
    live = folder.empty();
    LevelInfo* li = live && a.session ? &a.session->level->info : nullptr;
    if (live && !li) { a.popScreen(); return; }
    if (!live) { auto j = readJson(folder / "level.json"); level = j ? *j : json::object(); }
    std::string nm = li ? li->name : level.value("name", std::string("World")), md = li ? li->mode : level.value("mode", std::string("survival"));
    json rules = li ? li->rules : level.value("rules", json::object());
    form.init(rules, md == "creative", &nm, &md);
    gv = li ? li->genVersion : level.value("genVersion", 1);
  }
  void save(App& a) {
    if (live && a.session) {
      LevelInfo& li = a.session->level->info;
      li.rules = form.rules;
      if (!form.name.empty()) li.name = form.name;
      if (form.mode != li.mode) { li.mode = form.mode; if (Player* p = a.session->player) { p->mode = form.mode; if (!p->creative()) p->flying = false; } }
      if (upgrade) { li.genVersion = WorldGenOptions().genVersion; a.toast("Terrain upgraded: new land generates the new way from now on", "good"); }
      a.session->level->save(false);
    } else {
      level["rules"] = form.rules; if (!form.name.empty()) level["name"] = form.name; level["mode"] = form.mode;
      if (upgrade) level["genVersion"] = WorldGenOptions().genVersion;
      writeJson(folder / "level.json", level);
    }
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z();
    Panel pn;
    std::string title = live ? "World settings" : "Settings \xE2\x80\x94 " + level.value("name", std::string("World"));
    pn.begin("##wsettings", title.c_str(), 860, "Cancel");
    pn.beginBody(60);
    ImGui::SetCursorPos(ImVec2(18 * z, 10 * z));
    ImGui::BeginGroup();
    float fw = pn.width - 36 * z - ImGui::GetStyle().ScrollbarSize;
    form.draw(fw, false);
    int cur = WorldGenOptions().genVersion;
    if (gv < cur) {
      ImGui::Dummy(ImVec2(1, 6 * z));
      label(F_BODY, col::gold, "Terrain from an older version");
      label(F_MID, col::dim, "Land you haven't changed still generates the way it did when this world was made. Upgrade to get the newest biomes, caves and features in every place you haven't touched yet. Everything you built or changed stays exactly as it is (there may be seams at the edges). Make a backup first if you're unsure.", fw);
      if (button(upgrade ? "Will upgrade when you save##up" : ("Upgrade terrain to version " + std::to_string(cur) + "##up").c_str(), BTN_NORMAL, false, 0, upgrade)) upgrade = true;
    } else if (!live) label(F_MID, col::muted, ("Terrain is up to date (version " + std::to_string(gv) + ").").c_str());
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 12 * z));
    pn.endBody();
    pn.beginFooter();
    if (button("Save", BTN_PRIMARY)) { save(a); a.popScreen(); pn.endFooter(); pn.end(); return; }
    pn.endFooter();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
std::unique_ptr<Screen> makeWorldSettingsScreen(const fs::path& folder) { return std::make_unique<WorldSettingsScreen>(folder); }

} // namespace dq
