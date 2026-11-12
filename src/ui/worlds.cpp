// ui/worlds.cpp — Select World, Create New World and World Settings laid out like Better than Adventure 8.0
// (ScreenSelectWorld, ScreenCreateWorld, ScreenWorldSettings; ui/paged.h), with the original's features (ui.js
// refreshWorlds, rules.js worldSettingsForm): play, rename, delete (BTA's confirmation), the world's settings (rules,
// mob spawns, day and night, game mode, difficulty, terrain version, back up), importing a backup.
// Worlds are folders in userDir()/saves; backups are .zip files in userDir()/backups.
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/paged.h"
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


// ---- world settings, the BTA way (ScreenWorldSettings: World Settings / Game Rules / Mob Spawning) -------------
namespace {
struct RuleRow { const char* key; const char* label; const char* tip; int page; };
const std::vector<RuleRow>& ruleRows() {
  static const std::vector<RuleRow> t = {
    {"daylightCycle", "Day and Night Cycle", "Turn off to keep the sun where it is", 0},
    {"keepInventory", "Keep Inventory", "You respawn with everything you had", 0},
    {"graves", "Graves", "What you drop on death is kept safe in a grave, with a compass that points to it", 0},
    {"mobGriefing", "Mob Griefing", "Explosions from creatures and boss attacks damage the land", 0},
    {"tntExplodes", "TNT Explodes", "Turn off and TNT just fizzles", 0},
    {"fireSpread", "Fire Spreads", "Fire can spread to wood and leaves nearby", 0},
    {"blockDrops", "Blocks Drop Items", "Broken blocks drop what they're made of", 0},
    {"allowSprinting", "Sprinting", "Turn off and nobody can sprint", 0},
    {"allowSleeping", "Sleeping Skips the Night", "Turn off and beds only set your spawn point", 0},
    {"nightmares", "Nightmares", "Sleep in the dark with monsters about and one may come to your bed and wake you", 0},
    {"weatherCycle", "Weather Cycle", "Turn off to keep the weather as it is", 0},
    {"treeFelling", "Whole Trees Fall", "Chop the bottom of a trunk and the whole tree comes down (sneak to take one log)", 0},
    {"fallDamage", "Fall Damage", "", 0},
    {"drowning", "Drowning", "Your breath runs out under water", 0},
    {"fireDamage", "Fire Damage", "Fire and lava hurt", 0},
    {"naturalRegen", "Natural Regeneration", "Heal half a heart every few seconds when not hurt for a while (the classic game had none)", 0},
    {"grassSpread", "Grass Spreads", "Grass slowly grows onto nearby bare dirt", 0},
    {"saplingsPlant", "Saplings Plant Themselves", "A sapling left lying on soil takes root", 0},
    {"showCoords", "Show Coordinates", "Your position, shown in the corner", 0},
    {"commands", "Allow Commands", "Chat commands and WorldEdit (press T or /)", 0},
    {"downed", "Downed, Not Dead", "With someone else playing, you go down instead of dying, and they can help you back up", 0},
    {"immersive", "Immersive Progression (experimental)", "TerraFirmaCraft-style beginnings: no punching trees, gather stones and sticks, knap your first tools, fire clay, smelt and smith metal. Off: the classic crafting", 1},
    {"startInWild", "Start in the Wild", "Begin out in the wilds, well away from any village, instead of at a village's edge", 1},
    {"seasons", "Seasons", "Spring, summer, autumn and winter, seven days each: snow in the winter, colour in the autumn", 1},
    {"mobSpawning", "Mob Spawning", "Master switch. Off stops animals and monsters alike", 2},
    {"hostileSpawns", "Hostile Spawns", "Monsters appear on their own", 2},
    {"passiveSpawns", "Passive Spawns", "Animals and the quiet creatures appear on their own", 2}};
  return t;
}
const char* const WORLD_TYPES[] = {"default", "skyblock", "large_biomes", "amplified", "islands", "floating", "winter", "hell", "flat"};
constexpr int N_WORLD_TYPES = 9;
const char* const MODE_LABELS[] = {"Survival", "Creative", "Hardcore", "Adventure"};
const char* const DIFFS[] = {"peaceful", "easy", "normal", "hard"};
const int MINS[] = {3, 5, 7, 10, 15, 20, 30, 45, 60};
const std::vector<PagedPage>& wsPages() {
  static const std::vector<PagedPage> p = {{"World Settings", "item/brush"}, {"Game Rules", "item/paper"}, {"Mob Spawning", "item/egg"}};
  return p;
}
bool ruleOn(const json& rules, const std::string& key) {
  bool fallback = key == "hostileSpawns" || key == "passiveSpawns" || key == "mobSpawning" || key == "treeFelling";
  return rules.value(key, fallback);
}

// The pages of World Settings, shared by a new world and one being edited.
struct WorldForm {
  json rules; std::string name, mode, difficulty = "normal"; bool named = false, moded = false;
  void init(const json& r, bool creative, const std::string* nm, const std::string* md) {
    rules = ruleDefaults(creative);
    if (r.is_object()) for (auto& [k, v] : r.items()) rules[k] = v;
    rules["_v"] = 1;
    named = nm != nullptr; moded = md != nullptr;
    if (nm) name = *nm;
    if (md) mode = *md;
  }
  void draw(Paged& k, int page, int* worldType) {
    const json D = ruleDefaults(false);
    auto ruleToggle = [&](const RuleRow& t) {
      bool v = ruleOn(rules, t.key);
      bool def = D.contains(t.key) ? D.value(t.key, v) : ruleOn(json::object(), t.key);
      if (k.toggle(t.label, &v, def, t.tip[0] ? t.tip : nullptr)) rules[t.key] = v;
    };
    if (page == 0) {
      if (worldType) {
        int wt = clampv(*worldType, 0, N_WORLD_TYPES - 1);
        if (k.cycle("World Type", &wt, {"Default", "Skyblock", "Bigger Biomes", "Amplified", "Islands", "Floating Islands", "Endless Winter", "Hell", "Flat"}, 0,
                    "Default: the whole world. Skyblock: one small island in an empty sky. Bigger Biomes: every land three times as wide. Amplified: the hills and mountains near twice as high. "
                    "Islands: the seas risen over most of the land. Floating Islands: the land in islands hanging over the void. Endless Winter: snow everywhere, the lakes frozen. "
                    "Hell: a world that ended long ago, scorched, its seas turned to lava. Flat: an endless field of grass. (The Nether, Skylands and End are as ever.)", 120)) *worldType = wt;
      }
      if (named || moded) if (k.category("World")) {
        if (named) k.textField("World Name", &name, "New World", nullptr, 140, 0, 32);
        if (moded) {
          static const char* MODES[4] = {"survival", "creative", "adventure", "spectator"};
          int m = mode == "creative" ? 1 : mode == "adventure" ? 2 : mode == "spectator" ? 3 : 0;
          if (k.cycle("Game Mode", &m, {"Survival", "Creative", "Adventure", "Spectator"}, 0)) mode = MODES[clampv(m, 0, 3)];
        }
      }
      if (k.category("Overworld")) {
        int di = 2; for (int i = 0; i < 4; i++) if (difficulty == DIFFS[i]) di = i;
        if (k.cycle("Difficulty", &di, {"Peaceful", "Easy", "Normal", "Hard"}, 2, "Peaceful: no monsters appear on their own.")) difficulty = DIFFS[di];
        std::vector<std::string> opts; for (int m : MINS) opts.push_back(std::to_string(m) + " min");
        auto sel = [&](const char* key, const char* lbl, const char* tip) {
          int v = rules.value(key, 10), cur = 3;
          for (int i = 0; i < 9; i++) if (MINS[i] == v) cur = i;
          if (k.cycle(lbl, &cur, opts, 3, tip)) rules[key] = MINS[cur];
        };
        sel("dayMinutes", "Daytime Length", "How long the day lasts.");
        sel("nightMinutes", "Night Length", "How long the night lasts.");
        for (auto& t : ruleRows()) if (t.page == 1) ruleToggle(t);
      }
      if (k.category("Nether")) k.note("The Nether is part of this world, right under the Overworld. Ground nobody has visited yet uses the newest nether lands.");
    } else if (page == 1) {
      if (k.category("Game Rules")) for (auto& t : ruleRows()) if (t.page == 0) ruleToggle(t);
    } else {
      if (k.category("Hostile Spawns")) for (auto& t : ruleRows()) if (t.page == 2 && std::string(t.key) != "passiveSpawns") ruleToggle(t);
      if (k.category("Passive Spawns")) for (auto& t : ruleRows()) if (t.page == 2 && std::string(t.key) == "passiveSpawns") ruleToggle(t);
    }
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
std::string randomWorldName() {
  static const char* A[] = {"Amber", "Quiet", "Hollow", "Ember", "Misty", "Old", "Silver", "Golden", "Wild", "Far", "Lost", "Green", "Frosty", "Sunny", "Starlit"};
  static const char* B[] = {"Vale", "Hills", "Shore", "Woods", "Fields", "Peaks", "Hollow", "Isles", "Meadow", "Reach", "Marsh", "Glen", "Highlands", "Cove", "Plains"};
  u32 r = (u32)(unixMillis() * 2654435761u);
  return std::string(A[r % 15]) + " " + B[(r / 15) % 15];
}
std::string modeLabel(const json& L) {
  std::string m = L.value("mode", std::string("survival"));
  if (L.contains("rules") && L["rules"].is_object() && L["rules"].value("hardcore", false)) return "Hardcore Mode";
  return m == "creative" ? "Creative Mode" : m == "adventure" ? "Adventure Mode" : m == "spectator" ? "Spectator Mode" : "Survival Mode";
}
// the folder button's picture (BTA's button_folder): a little folder
void folderGlyph(ImDrawList* dl, ImVec2 p, float s, bool hot) {
  ImU32 c = hot ? IM_COL32(255, 255, 160, 255) : IM_COL32(230, 200, 120, 255), d = IM_COL32(120, 90, 40, 255);
  ImVec2 a(p.x + s * 0.22f, p.y + s * 0.32f), b(p.x + s * 0.78f, p.y + s * 0.72f);
  dl->AddRectFilled(ImVec2(a.x, a.y - s * 0.07f), ImVec2(a.x + s * 0.22f, a.y + s * 0.02f), c);
  dl->AddRectFilled(a, b, c);
  dl->AddRect(a, b, d, 0, 0, std::max(1.f, s * 0.05f));
}
}

// ---- Select World / Create New World ---------------------------------------------------------------------------
class WorldsScreen : public Screen {
public:
  const char* id() const override { return "worlds"; }
  enum View { LIST, CREATE, SETTINGS, IMPORT, RENAME, DELETE };
  std::vector<WorldEntry> worlds;
  std::string name = "New World", seed, msg, renameTo;
  int mode = 0, wtype = 0, selected = -1;
  View view = LIST;
  WorldForm form;
  Paged kit, setKit;
  double refreshAt = 0;
  WorldEntry* sel() { return selected >= 0 && selected < (int)worlds.size() ? &worlds[selected] : nullptr; }
  void onOpen(App& a) override {
    worlds = listWorlds(); form.init(json(), false, nullptr, nullptr);
    view = LIST; selected = worlds.empty() ? -1 : 0;
  }
  bool onBack(App&) override {
    if (backConsumedByText()) return true;
    if (view == SETTINGS) { view = CREATE; return true; }
    if (view != LIST) { view = view == IMPORT ? CREATE : LIST; return true; }
    return false;
  }
  void update(App& a, double) override {
    if (view != LIST || a.time <= refreshAt) return;
    worlds = listWorlds(); refreshAt = a.time + 2;
    if (selected >= (int)worlds.size()) selected = (int)worlds.size() - 1;
  }
  void playSelected(App& a) {
    WorldEntry* w = sel(); if (!w) return;
    fs::path f = w->folder; a.settings.extra["played"] = true;
    uiDefer([f](App& app) { app.openWorld(f); });
  }
  void createWorld(App& a) {
    LevelInfo li; li.name = name.empty() ? "New World" : name; li.seedText = seed;
    li.mode = mode == 1 ? "creative" : mode == 3 ? "adventure" : "survival";
    li.difficulty = form.difficulty;
    li.rules = form.rules;
    if (wtype > 0 && wtype < N_WORLD_TYPES) li.worldType = WORLD_TYPES[wtype];
    if (mode == 2) { li.rules["hardcore"] = true; li.rules["commands"] = false; li.rules["keepInventory"] = false; }
    a.settings.extra["played"] = true;
    uiDefer([li](App& app) { if (!app.createWorld(li)) app.toast("Could not create the world", "warn"); });
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    switch (view) {
    case LIST: drawList(a); break;
    case CREATE: drawCreate(a); break;
    case SETTINGS: drawSettings(a); break;
    case IMPORT: drawImport(a); break;
    case RENAME: drawRename(a); break;
    case DELETE: drawDelete(a); break;
    }
  }

  // BTA's ScreenSelectWorld: the list between a 32-unit band and a 64-unit band, two rows of buttons
  void drawList(App& a) {
    kit.begin(a, "##worlds", "Select World", {}, 0, 32, 64);
    const float U = kit.u();
    kit.beginContent();
    if (worlds.empty()) kit.note("No worlds yet. Create New World to start one.");
    for (int i = 0; i < (int)worlds.size(); i++) {
      WorldEntry& we = worlds[i];
      const json& L = we.level;
      std::string nm = L.value("name", std::string("World"));
      std::string seedS = !L.value("seedText", std::string()).empty() ? L.value("seedText", std::string()) : std::to_string(L.value("seed", 0));
      int gv = L.value("genVersion", 1), cur = WorldGenOptions().genVersion;
      std::string line1 = we.folder.filename().string() + " (" + agoText(L.value("lastPlayed", (i64)0)) + ")";
      std::string line2 = modeLabel(L) + ", " + we.dim + ", Level " + std::to_string(we.lvl) + ", Seed " + seedS;
      if (L.contains("rules") && L["rules"].is_object() && L["rules"].value("commands", false)) line2 += ", Cheats";
      if (gv < cur) line2 += ", older terrain (Edit to upgrade)";
      bool dbl = false;
      if (kit.listRow(we.folder.string().c_str(), 32, i == selected, &dbl, 270)) {
        selected = i;
        if (dbl) playSelected(a);
      }
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = kit.rowMin();
      float tw = kit.rowMax().x - p.x - 4 * U;
      kit.textAt(ellipsize(F_MID, nm, tw), p + ImVec2(2, 1) * U, IM_COL32(255, 255, 255, 255));
      kit.textAt(ellipsize(F_BODY, line1, tw), p + ImVec2(2, 12) * U, IM_COL32(128, 128, 128, 255), F_BODY);
      kit.textAt(ellipsize(F_BODY, line2, tw), p + ImVec2(2, 22) * U, gv < cur ? IM_COL32(255, 255, 85, 255) : IM_COL32(128, 128, 128, 255), F_BODY);
      (void)dl;
    }
    if (!msg.empty()) kit.note(msg);
    kit.endContent();
    WorldEntry* w = sel();
    const float cx = a.width * 0.5f, y1 = a.height - 52 * U, y2 = a.height - 28 * U;
    if (kit.buttonAt("Play Selected World", ImVec2(cx - 154 * U, y1), 150, 20, !w)) playSelected(a);
    if (kit.buttonAt("Create New World", ImVec2(cx + 4 * U, y1), 150, 20)) { view = CREATE; msg.clear(); name = "New World"; seed.clear(); mode = 0; wtype = 0; form.init(json(), false, nullptr, nullptr); }
    // the folder button, Rename, Delete | Edit (this world's settings, backups), Cancel
    ImVec2 fp(cx - 154 * U, y2);
    if (kit.buttonAt("##folder", fp, 20, 20)) {
      std::error_code ec; fs::create_directories(a.savesDir(), ec);
      std::string url = "file://" + fs::absolute(a.savesDir(), ec).generic_string();
      SDL_OpenURL(url.c_str());
    }
    folderGlyph(ImGui::GetWindowDrawList(), fp, 20 * U, ImGui::IsItemHovered());
    if (ImGui::IsItemHovered()) kit.tooltip("Open the worlds folder");
    if (kit.buttonAt("Rename", ImVec2(cx - 126 * U, y2), 57, 20, !w) && w) { renameTo = w->level.value("name", std::string("World")); view = RENAME; }
    if (kit.buttonAt("Delete", ImVec2(cx - 61 * U, y2), 57, 20, !w) && w) view = DELETE;
    if (kit.buttonAt("Edit", ImVec2(cx + 4 * U, y2), 73, 20, !w) && w) a.pushScreen(makeWorldSettingsScreen(w->folder));
    if (ImGui::IsItemHovered() && w) kit.tooltip("World settings: name, game mode, rules, mob spawns, back up, terrain upgrade");
    if (kit.buttonAt("Cancel", ImVec2(cx + 81 * U, y2), 73, 20)) { kit.end(); a.popScreen(); return; }
    kit.end();
  }

  // BTA's ScreenCreateWorld
  void drawCreate(App& a) {
    kit.beginPlain(a, "##createworld", "Create New World");
    const float U = kit.u(), cx = a.width * 0.5f;
    kit.textAt("World Name", ImVec2(cx - 100 * U, 47 * U));
    kit.fieldAt("##nwname", &name, ImVec2(cx - 100 * U, 60 * U), 176, "New World", 0, 32);
    if (kit.buttonAt("?##rname", ImVec2(cx + 80 * U, 60 * U), 20, 20)) name = randomWorldName();
    if (ImGui::IsItemHovered()) kit.tooltip("A random name");
    kit.textAt("Seed", ImVec2(cx - 100 * U, 88 * U));
    kit.fieldAt("##nwseed", &seed, ImVec2(cx - 100 * U, 100 * U), 176, "Leave blank for a random seed", 0, 40);
    if (kit.buttonAt("?##rseed", ImVec2(cx + 80 * U, 100 * U), 20, 20)) seed = std::to_string((i64)(unixMillis() * 2654435761ull % 9000000000000ull) - 4500000000000ll);
    if (ImGui::IsItemHovered()) kit.tooltip("A random seed");
    std::string modeBtn = std::string("Mode: ") + MODE_LABELS[clampv(mode, 0, 3)];
    if (kit.buttonAt(modeBtn.c_str(), ImVec2(cx - 100 * U, 124 * U), 98, 20)) { mode = (mode + 1) % 4; form.rules["commands"] = mode == 1; }
    bool cheats = form.rules.value("commands", false);
    if (kit.buttonAt(cheats ? "Cheats: ON###cheats" : "Cheats: OFF###cheats", ImVec2(cx + 2 * U, 124 * U), 98, 20, mode == 2)) form.rules["commands"] = !cheats;
    if (kit.buttonAt("World Settings", ImVec2(cx - 100 * U, 148 * U), 200, 20)) { setKit.page = 0; view = SETTINGS; }
    if (kit.buttonAt("Import World File", ImVec2(cx - 100 * U, 172 * U), 200, 20)) { view = IMPORT; msg.clear(); }
    float yc = std::max(a.height / 4.f + 116 * U, 200 * U);
    if (kit.buttonAt("Create New World", ImVec2(cx - 100 * U, yc), 200, 20)) createWorld(a);
    if (kit.buttonAt("Cancel##create", ImVec2(cx - 100 * U, yc + 24 * U), 200, 20)) view = LIST;
    if (!msg.empty()) kit.centerText(msg, yc + 50 * U, IM_COL32(160, 160, 160, 255), F_BODY);
    kit.end();
  }

  void drawSettings(App& a) {
    setKit.begin(a, "##newworldsettings", "World Settings", wsPages());
    setKit.beginContent();
    form.draw(setKit, setKit.page, &wtype);
    setKit.endContent();
    int f = setKit.footer({"Done"});
    setKit.end();
    if (f == 0) view = CREATE;
  }

  void drawImport(App& a) {
    kit.begin(a, "##importworld", "Import World File", {}, 0, 32, 36);
    kit.beginContent();
    fs::path dir = userSub("backups");
    kit.note("World backups (.zip) in " + dir.string() + ". Put a backup there to bring it in.");
    std::error_code ec; int n = 0;
    for (auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file() || e.path().extension() != ".zip") continue;
      n++;
      std::string fn = e.path().filename().string();
      if (kit.listRow(fn.c_str(), 20, false, nullptr, 270)) {
        std::string nm, err;
        if (importWorldZip(e.path(), nm, err)) { msg = "Imported \xE2\x80\x9C" + nm + "\xE2\x80\x9D."; worlds = listWorlds(); view = LIST; }
        else msg = err;
      }
      kit.textAt(ellipsize(F_MID, fn, kit.rowMax().x - kit.rowMin().x - 8 * kit.u()), kit.rowMin() + ImVec2(4, 5) * kit.u(), IM_COL32(255, 255, 255, 255));
    }
    if (!n) kit.note("No backups found yet.");
    if (!msg.empty()) kit.note(msg);
    kit.endContent();
    int f = kit.footer({"Cancel"});
    kit.end();
    if (f == 0) view = CREATE;
  }

  // BTA's rename screen: a field and Rename / Cancel
  void drawRename(App& a) {
    WorldEntry* w = sel(); if (!w) { view = LIST; return; }
    kit.beginPlain(a, "##renameworld", "Rename World");
    const float U = kit.u(), cx = a.width * 0.5f;
    kit.textAt("World Name", ImVec2(cx - 100 * U, 47 * U));
    bool enter = kit.fieldAt("##rename", &renameTo, ImVec2(cx - 100 * U, 60 * U), 200, nullptr, ImGuiInputTextFlags_EnterReturnsTrue, 32);
    float yb = std::max(a.height / 4.f + 96 * U, 100 * U);
    bool ok = kit.buttonAt("Rename##do", ImVec2(cx - 100 * U, yb), 200, 20, renameTo.empty()) || (enter && !renameTo.empty());
    bool cancel = kit.buttonAt("Cancel##rename", ImVec2(cx - 100 * U, yb + 24 * U), 200, 20);
    kit.end();
    if (ok) { w->level["name"] = renameTo; writeJson(w->folder / "level.json", w->level); msg = "Renamed to " + renameTo + "."; worlds = listWorlds(); view = LIST; }
    else if (cancel) view = LIST;
  }

  // BTA's delete confirmation
  void drawDelete(App& a) {
    WorldEntry* w = sel(); if (!w) { view = LIST; return; }
    kit.beginPlain(a, "##deleteworld", "Delete World");
    const float U = kit.u(), cx = a.width * 0.5f;
    std::string nm = w->level.value("name", std::string("World"));
    kit.centerText("Are you sure you want to delete this world?", a.height / 2.f - 30 * U);
    kit.centerText("'" + nm + "' will be lost forever! (A long time!)", a.height / 2.f - 16 * U, IM_COL32(160, 160, 160, 255));
    bool del = kit.buttonAt("Delete##yes", ImVec2(cx - 154 * U, a.height / 2.f + 16 * U), 150, 20);
    bool cancel = kit.buttonAt("Cancel##no", ImVec2(cx + 4 * U, a.height / 2.f + 16 * U), 150, 20);
    kit.end();
    if (del) { std::error_code ec; fs::remove_all(w->folder, ec); worlds = listWorlds(); selected = worlds.empty() ? -1 : 0; msg.clear(); view = LIST; }
    else if (cancel) view = LIST;
  }
};
std::unique_ptr<Screen> makeWorldsScreen() { return std::make_unique<WorldsScreen>(); }

// ---- World Settings of a saved world, or the one being played (pause menu) -------------------------------------
class WorldSettingsScreen : public Screen {
public:
  explicit WorldSettingsScreen(fs::path f) : folder(std::move(f)) {}
  fs::path folder; json level; WorldForm form; bool live = false, upgrade = false; int gv = 3, wtype = 0;
  std::string info;
  Paged kit;
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
    form.difficulty = li ? li->difficulty : level.value("difficulty", std::string("normal"));
    gv = li ? li->genVersion : level.value("genVersion", 1);
    std::string wt = li ? li->worldType : level.value("worldType", std::string("default"));
    wtype = wt == "skyblock" ? 1 : wt == "large_biomes" ? 2 : 0;
  }
  void save(App& a) {
    if (live && a.session) {
      LevelInfo& li = a.session->level->info;
      li.rules = form.rules;
      li.difficulty = form.difficulty;
      if (!form.name.empty()) li.name = form.name;
      if (form.mode != li.mode) { li.mode = form.mode; if (Player* p = a.session->player) { p->mode = form.mode; if (!p->creative()) p->flying = false; } }
      if (upgrade) { li.genVersion = WorldGenOptions().genVersion; a.toast("Terrain upgraded: new land generates the new way from now on", "good"); }
      li.worldType = WORLD_TYPES[clampv(wtype, 0, N_WORLD_TYPES - 1)];
      a.session->level->save(false);
    } else {
      level["rules"] = form.rules; if (!form.name.empty()) level["name"] = form.name; level["mode"] = form.mode;
      level["difficulty"] = form.difficulty;
      level["worldType"] = WORLD_TYPES[clampv(wtype, 0, N_WORLD_TYPES - 1)];
      if (upgrade) level["genVersion"] = WorldGenOptions().genVersion;
      writeJson(folder / "level.json", level);
    }
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    kit.begin(a, "##wsettings", "World Settings", wsPages());
    kit.beginContent();
    form.draw(kit, kit.page, &wtype);
    if (kit.page == 0 && kit.category("Terrain and Backups")) {
      int cur = WorldGenOptions().genVersion;
      if (gv < cur) {
        std::string lb = upgrade ? "Will Upgrade" : "Upgrade";
        if (kit.action(("Terrain Version " + std::to_string(gv)).c_str(), lb.c_str(), "Land you haven't changed still generates the way it did when this world was made. Upgrade to get the newest biomes, caves and features in every place you haven't touched yet. Everything you built stays. There may be seams where old land meets new. Make a backup first if you're unsure.", upgrade)) upgrade = true;
      } else kit.action(("Terrain Version " + std::to_string(gv)).c_str(), "Up to Date", "New chunks use the newest terrain.", true);
      if (kit.action("Back Up This World", "Back Up", "A .zip of the whole world in the backups folder.")) {
        fs::path f = live && a.session ? a.session->level->folder : folder, out; std::string err;
        info = backupWorldFolder(f, form.name.empty() ? "World" : form.name, out, err) ? "Backup saved: " + out.filename().string() : "Backup failed: " + err;
      }
      if (!info.empty()) kit.note(info);
    }
    kit.endContent();
    int f = kit.footer({"Done", "Cancel"});
    kit.end();
    if (f == 0) { save(a); a.popScreen(); }
    else if (f == 1) a.popScreen();
  }
};
std::unique_ptr<Screen> makeWorldSettingsScreen(const fs::path& folder) { return std::make_unique<WorldSettingsScreen>(folder); }

} // namespace dq
