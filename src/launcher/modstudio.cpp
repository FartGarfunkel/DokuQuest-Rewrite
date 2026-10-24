// launcher/modstudio.cpp — the Mod Studio tab (port of dokuquest-mod-studio.html): Overview, DQ API, Mod editor
// (templates, syntax check with QuickJS exactly as the game compiles mods, save into userDir()/mods/<id>/),
// Installed mods (enable/disable through mod/modloader.h), Data packs (forms that write blocks.json /
// items.json / recipes.json for a mod) and the Engine source browser.
#include "launcher/modstudio.h"
#include "mod/modloader.h"
#include "core/registry.h"
#include <imgui_stdlib.h>
#include <quickjs.h>
#include <regex>

namespace dq {

bool jsSyntaxCheck(const std::string& code, std::string& err) {
  // the game runs a mod as (function (DQ) { "use strict"; <code> }) — compile the same wrapper; keeping the
  // prologue on line 1 keeps QuickJS's line numbers equal to the editor's
  JSRuntime* rt = JS_NewRuntime();
  if (!rt) { err = "QuickJS could not start"; return false; }
  JSContext* ctx = JS_NewContext(rt);
  std::string src = "(function (DQ) {\"use strict\";" + code + "\n})";
  JSValue v = JS_Eval(ctx, src.c_str(), src.size(), "mod.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
  bool ok = !JS_IsException(v);
  if (!ok) {
    JSValue e = JS_GetException(ctx);
    const char* m = JS_ToCString(ctx, e);
    err = m ? m : "syntax error";
    if (m) JS_FreeCString(ctx, m);
    JSValue stack = JS_GetPropertyStr(ctx, e, "stack");
    if (JS_IsString(stack)) {
      const char* s = JS_ToCString(ctx, stack);
      if (s) { std::string st = s; size_t p = st.find("mod.js:"); if (p != std::string::npos) err += " (line " + std::to_string(std::atoi(st.c_str() + p + 7)) + ")"; JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, e);
  }
  JS_FreeValue(ctx, v);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);
  return ok;
}

namespace studio {
namespace {
enum Page { P_OVERVIEW, P_API, P_EDITOR, P_MODS, P_DATA, P_ENGINE, P_COUNT };
const char* pageNames[] = {"Overview", "DQ API", "Mod editor", "Installed mods", "Data packs", "Engine source"};

struct Editor {
  bool inited = false; int tpl = 0;
  std::string code, id, version = "1.0.0", author, description, msg; bool err = false, idTouched = false;
};
struct DataForm {
  std::string modId = "my_pack", modName = "My Pack";
  std::string bKey, bName, bTex, bTool = "pickaxe", bSound = "stone", bDrop; float bHard = 1.5f; int bTier = 0, bLight = 0; bool bSolid = true, bItem = true;
  std::string iKey, iName, iIcon, iDesc; int iStack = 64;
  bool shaped = true; std::string grid[9], out; int count = 1; std::string smeltIn, smeltOut;
  std::string msg, lastJson; bool err = false;
};
struct State { int page = P_OVERVIEW; Editor ed; DataForm df; double scannedAt = -100; };
State& S() { static State s; return s; }

std::string modNameOf(const std::string& code) {
  static const std::regex re(R"(DQ\.name\s*=\s*['"]([^'"]+)['"])");
  std::smatch m; return std::regex_search(code, m, re) ? m[1].str() : "";
}
std::string slug(const std::string& s) {
  std::string o; bool dash = false;
  for (char c : toLower(s)) { if (std::isalnum((unsigned char)c)) { o += c; dash = false; } else if (!dash && !o.empty()) { o += '-'; dash = true; } }
  while (!o.empty() && o.back() == '-') o.pop_back();
  return o.empty() ? "my-mod" : o.substr(0, 40);
}
bool validKey(const std::string& k) { static const std::regex re("^[a-z0-9_]{2,40}$"); return std::regex_match(k, re); }
bool validModId(const std::string& k) { static const std::regex re("^[a-z0-9_.-]{1,40}$"); return std::regex_match(k, re) && k != "." && k != ".."; }

// ---------------------------------------------------------------- Mod editor
void drawEditor(Launcher& L) {
  Editor& E = S().ed;
  auto& tpls = templates();
  if (!E.inited) { E.code = tpls[0].code; E.inited = true; }
  panelBegin(L, "Mod editor");
  ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(S_MUTED), "Template"); ImGui::PopFont();
  ImGui::SetNextItemWidth(320);
  if (ImGui::BeginCombo("##tpl", tpls[E.tpl].label)) {
    for (int i = 0; i < (int)tpls.size(); i++) if (ImGui::Selectable(tpls[i].label, E.tpl == i)) { E.tpl = i; E.code = tpls[i].code; E.msg.clear(); if (!E.idTouched) E.id.clear(); }
    ImGui::EndCombo();
  }
  std::string name = modNameOf(E.code);
  if (!E.idTouched) E.id = slug(name.empty() ? "my-mod" : name);
  // labels in one table row, the fields in the next, so they line up
  if (ImGui::BeginTable("##meta", 4, ImGuiTableFlags_SizingStretchSame, ImVec2(-16, 0))) {
    const char* labels[] = {"Mod id (folder)", "Version", "Author", "Description"};
    std::string* vals[] = {&E.id, &E.version, &E.author, &E.description};
    const char* hints[] = {"my-mod", "1.0.0", "your name", "what it does"};
    ImGui::TableNextRow();
    for (int i = 0; i < 4; i++) { ImGui::TableNextColumn(); ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(S_MUTED), "%s", labels[i]); ImGui::PopFont(); }
    ImGui::TableNextRow();
    for (int i = 0; i < 4; i++) {
      ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
      if (ImGui::InputTextWithHint((std::string("##m") + labels[i]).c_str(), hints[i], vals[i]) && i == 0) E.idTouched = true;
    }
    ImGui::EndTable();
  }
  ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(S_MUTED), "Mod source%s", name.empty() ? " — set DQ.name = '…' to name it" : (" — " + name).c_str()); ImGui::PopFont();
  ImGui::PushFont(L.fMono);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, S_PRE); ImGui::PushStyleColor(ImGuiCol_Border, S_LINE);
  ImGui::InputTextMultiline("##code", &E.code, ImVec2(-16, std::max(240.f, ImGui::GetTextLineHeight() * 22)), ImGuiInputTextFlags_AllowTabInput);
  ImGui::PopStyleColor(2);
  ImGui::PopFont();
  std::string err;
  if (button(L, "Save to mods folder", true)) {
    if (!jsSyntaxCheck(E.code, err)) { E.msg = "Fix the syntax before saving: " + err; E.err = true; }
    else if (!validModId(E.id)) { E.msg = "The mod id may use a-z, 0-9, - _ . (up to 40)."; E.err = true; }
    else {
      fs::path dir = userSub("mods") / E.id; std::error_code ec; fs::create_directories(dir, ec);
      json mj = {{"id", E.id}, {"name", name.empty() ? E.id : name}, {"version", E.version}, {"author", E.author}, {"description", E.description},
                 {"kind", "js"}, {"main", "main.js"}, {"plugin", std::regex_search(E.code, std::regex(R"(\bDQ\.plugin\s*=\s*true)"))}, {"game", std::string(">=") + DQ_VERSION}};
      bool ok = writeFileAtomic(dir / "main.js", E.code) && writeJson(dir / "mod.json", mj);
      S().scannedAt = -100;
      E.msg = ok ? "Saved to " + dir.string() + ". It runs the next time a world opens." : "Could not write to " + dir.string() + ".";
      E.err = !ok;
    }
  }
  ImGui::SameLine();
  if (button(L, "Check syntax")) {
    if (jsSyntaxCheck(E.code, err)) { E.msg = "Syntax OK (the same check the game uses)."; E.err = false; }
    else { E.msg = "Error: " + err; E.err = true; }
  }
  ImGui::SameLine();
  if (button(L, "Copy to clipboard")) { ImGui::SetClipboardText(E.code.c_str()); E.msg = "Copied."; E.err = false; }
  ImGui::SameLine();
  if (button(L, "Open mods folder")) openPath(userSub("mods"));
  message(L, E.msg, E.err);
  ImGui::Spacing();
  hint(L, "Saved mods go to the game folder's mods/<id>/ (mod.json + main.js) and are switched on. They run when a world opens. "
          "Syntax is checked the way the game compiles mods: QuickJS, as (function (DQ) { … }).");
  panelEnd();
}

// ---------------------------------------------------------------- Installed mods
void rescan(Launcher& L) {
  L.mods->scan(); S().scannedAt = L.time;
  for (auto& m : L.mods->list())   // single-file JS mods: show their DQ.name
    if (m.name == m.id && m.path.extension() == ".js") if (auto t = readFile(m.path)) { std::string n = modNameOf(*t); if (!n.empty()) m.name = n; }
}

void drawInstalled(Launcher& L) {
  if (L.time - S().scannedAt > 5) rescan(L);
  panelBegin(L, "Installed mods");
  hint(L, "Everything in the game folder's mods/ folder: JS mods, data packs and plugins. Switching one on or off applies the next time the game starts or a world opens.");
  ImGui::Spacing();
  if (button(L, "Refresh list")) rescan(L);
  ImGui::SameLine();
  if (button(L, "Open mods folder")) openPath(userSub("mods"));
  ImGui::Spacing();
  auto& list = L.mods->list();
  if (list.empty()) hint(L, "No mods yet. Write one in the Mod editor, make a data pack, or drop .js files and mod folders into mods/.");
  bool dirty = false;
  for (size_t i = 0; i < list.size(); i++) {
    ModInfo& m = list[i];
    ImGui::PushID((int)i);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, S_SLOT); ImGui::PushStyleColor(ImGuiCol_Border, S_LINE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
    ImGui::BeginChild("##row", ImVec2(-16, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
    bool on = m.enabled;
    if (ImGui::Checkbox("##on", &on)) { L.mods->setEnabled(m.id, on); dirty = true; }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, C_WHITE); ImGui::TextUnformatted(m.name.c_str()); ImGui::PopStyleColor();
    ImGui::SameLine();
    std::string kind = m.kind.empty() ? "mod" : m.kind; if (m.plugin) kind += " · plugin";
    ImVec2 p = ImGui::GetCursorScreenPos(); ImVec2 ts = L.fSmall->CalcTextSizeA(L.fSmall->FontSize, FLT_MAX, 0, kind.c_str());
    ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x, p.y + 1), ImVec2(p.x + ts.x + 12, p.y + ts.y + 5), S_LINEHI, 3);
    ImGui::GetWindowDrawList()->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(p.x + 6, p.y + 3), S_MUTED, kind.c_str());
    ImGui::Dummy(ImVec2(ts.x + 12, ts.y + 4));
    std::string meta = (m.version.empty() ? "" : "v" + m.version) + (m.author.empty() ? "" : "  by " + m.author) + "  ·  " + m.path.filename().string();
    ImGui::SameLine(); ImGui::PushFont(L.fSmall); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(S_MUTED), "%s", meta.c_str()); ImGui::PopFont();
    if (!m.description.empty()) hint(L, m.description.c_str());
    if (!m.error.empty()) message(L, m.error, true);
    bool isJs = m.kind == "js" || m.path.extension() == ".js";
    if (isJs && button(L, "Load in editor")) {
      fs::path f = m.path;
      if (fs::is_directory(f)) { auto mj = readJson(f / "mod.json"); f = f / (mj ? mj->value("main", "main.js") : "main.js"); }
      if (auto t = readFile(f)) {
        Editor& E = S().ed; E.inited = true; E.code = *t; E.id = fs::is_directory(m.path) ? m.id : slug(m.name); E.idTouched = true;
        E.version = m.version.empty() ? "1.0.0" : m.version; E.author = m.author; E.description = m.description;
        E.msg = "Loaded " + m.name + "."; E.err = false; S().page = P_EDITOR;
      }
    }
    if (isJs) ImGui::SameLine();
    if (button(L, "Open folder")) openPath(fs::is_directory(m.path) ? m.path : m.path.parent_path());
    ImGui::EndChild();
    ImGui::PopID();
  }
  if (dirty) L.mods->saveState();
  panelEnd();
}

// ---------------------------------------------------------------- Data packs
json readOr(const fs::path& p, json def) { auto j = readJson(p); return j ? *j : def; }
bool ensurePack(DataForm& D, std::string& err) {
  if (!validModId(D.modId)) { err = "The pack id may use a-z, 0-9, - _ . (up to 40)."; return false; }
  fs::path dir = userSub("mods") / D.modId; std::error_code ec; fs::create_directories(dir / "data" / "textures", ec);
  if (!fs::exists(dir / "mod.json")) writeJson(dir / "mod.json", {{"id", D.modId}, {"name", D.modName.empty() ? D.modId : D.modName}, {"version", "1.0.0"}, {"kind", "data"}, {"game", std::string(">=") + DQ_VERSION}});
  return true;
}
bool knownItem(DataForm& D, const std::string& k) {
  if (k.empty() || k[0] == '#') return true;
  if (reg().item(k) || reg().blockIds.count(k)) return true;
  fs::path d = userSub("mods") / D.modId / "data";
  json items = readOr(d / "items.json", json::object());
  if (items.is_object() && items.contains(k)) return true;
  json blocks = readOr(d / "blocks.json", json::array());
  const json& arr = blocks.is_array() ? blocks : blocks.value("blocks", json::array());
  for (auto& b : arr) if (b.value("key", "") == k) return true;
  return false;
}
void addItemEntry(const fs::path& dataDir, const json& it) {
  json items = readOr(dataDir / "items.json", json::object());
  if (!items.is_object()) items = json::object();
  items[it["key"].get<std::string>()] = it;
  writeJson(dataDir / "items.json", items);
}

void drawData(Launcher& L) {
  DataForm& D = S().df;
  fs::path dataDir = userSub("mods") / D.modId / "data";
  panelBegin(L, "Data packs");
  hint(L, "Add blocks, items and recipes without code. Each Add writes into mods/<pack id>/data/ (blocks.json, items.json, recipes.json); "
          "the game loads data packs when it starts. Textures: a tile name from the game (like stone) or textures/<file>.png placed in the pack's data/textures/.");
  ImGui::Spacing();
  float w = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemWidth(w * 0.3f); ImGui::InputTextWithHint("Pack id", "my_pack", &D.modId);
  ImGui::SameLine(); ImGui::SetNextItemWidth(w * 0.3f); ImGui::InputTextWithHint("Pack name", "My Pack", &D.modName);
  ImGui::SameLine(); if (button(L, "Open pack folder")) openPath(userSub("mods") / D.modId);
  std::string err;
  const char* tools[] = {"none", "pickaxe", "axe", "shovel", "hoe", "sword", "shears"};
  const char* sounds[] = {"stone", "wood", "gravel", "grass", "sand", "glass", "wool", "metal", "snow"};
  if (twoColumns("##dp")) {
    cardBegin(L, "New block");
    ImGui::PushItemWidth(-90);
    ImGui::InputTextWithHint("Key", "ruby_block", &D.bKey);
    ImGui::InputTextWithHint("Name", "Ruby Block", &D.bName);
    ImGui::InputTextWithHint("Texture", "a tile name, or textures/ruby_block.png", &D.bTex);
    ImGui::SliderFloat("Hardness", &D.bHard, 0, 50, "%.1f");
    int ti = 0; for (int i = 0; i < 7; i++) if (D.bTool == tools[i]) ti = i;
    if (ImGui::Combo("Tool", &ti, tools, 7)) D.bTool = tools[ti];
    ImGui::SliderInt("Tool tier", &D.bTier, 0, 4);
    ImGui::SliderInt("Light", &D.bLight, 0, 15);
    int si = 0; for (int i = 0; i < 9; i++) if (D.bSound == sounds[i]) si = i;
    if (ImGui::Combo("Sound", &si, sounds, 9)) D.bSound = sounds[si];
    ImGui::InputTextWithHint("Drops", "itself", &D.bDrop);
    ImGui::PopItemWidth();
    ImGui::Checkbox("Solid", &D.bSolid); ImGui::SameLine(); ImGui::Checkbox("Add an item that places it", &D.bItem);
    if (button(L, "Add block", true)) {
      if (!validKey(D.bKey)) { D.msg = "Block keys use a-z, 0-9 and _ (2 to 40)."; D.err = true; }
      else if (ensurePack(D, err)) {
        json b = {{"key", D.bKey}, {"name", D.bName.empty() ? D.bKey : D.bName}, {"tex", D.bTex.empty() ? D.bKey : D.bTex}, {"render", 1}, {"solid", D.bSolid}, {"opaque", D.bSolid},
                  {"hardness", D.bHard}, {"tool", D.bTool == "none" ? json(nullptr) : json(D.bTool)}, {"tier", D.bTier}, {"light", D.bLight}, {"sound", D.bSound},
                  {"drop", D.bDrop.empty() ? D.bKey : D.bDrop}};
        json blocks = readOr(dataDir / "blocks.json", json::array());
        if (!blocks.is_array()) blocks = blocks.value("blocks", json::array());
        for (auto it = blocks.begin(); it != blocks.end();) { if (it->value("key", "") == D.bKey) it = blocks.erase(it); else ++it; }
        blocks.push_back(b);
        writeJson(dataDir / "blocks.json", blocks);
        if (D.bItem) addItemEntry(dataDir, {{"key", D.bKey}, {"name", b["name"]}, {"icon", "block:" + D.bKey}, {"block", D.bKey}, {"maxStack", 64}});
        D.lastJson = b.dump(2);
        bool texOk = startsWith(b["tex"].get<std::string>(), "textures/") || reg().tile(b["tex"]) >= 0;
        D.msg = "Added block " + D.bKey + " to " + (dataDir / "blocks.json").string() + "." + (texOk ? "" : " Note: there is no game tile called \"" + b["tex"].get<std::string>() + "\".");
        D.err = !texOk;
      } else { D.msg = err; D.err = true; }
    }
    cardEnd(); nextCell();
    cardBegin(L, "New item");
    ImGui::PushItemWidth(-90);
    ImGui::InputTextWithHint("Key##i", "ruby", &D.iKey);
    ImGui::InputTextWithHint("Name##i", "Ruby", &D.iName);
    ImGui::InputTextWithHint("Icon##i", "an item or tile, like emerald", &D.iIcon);
    ImGui::SliderInt("Stack##i", &D.iStack, 1, 64);
    ImGui::InputTextWithHint("Text##i", "shown in the tooltip", &D.iDesc);
    ImGui::PopItemWidth();
    if (button(L, "Add item", true)) {
      if (!validKey(D.iKey)) { D.msg = "Item keys use a-z, 0-9 and _ (2 to 40)."; D.err = true; }
      else if (reg().item(D.iKey)) { D.msg = "The game already has an item called " + D.iKey + "."; D.err = true; }
      else if (ensurePack(D, err)) {
        json it = {{"key", D.iKey}, {"name", D.iName.empty() ? D.iKey : D.iName}, {"icon", D.iIcon.empty() ? D.iKey : D.iIcon}, {"maxStack", D.iStack}};
        if (!D.iDesc.empty()) it["desc"] = D.iDesc;
        addItemEntry(dataDir, it);
        D.lastJson = it.dump(2); D.msg = "Added item " + D.iKey + "."; D.err = false;
      } else { D.msg = err; D.err = true; }
    }
    cardEnd(); nextCell();
    cardBegin(L, "New recipe");
    ImGui::Checkbox("Shaped (the pattern matters)", &D.shaped);
    float cw = std::min(110.f, (ImGui::GetContentRegionAvail().x - 20) / 3);
    for (int i = 0; i < 9; i++) {
      ImGui::PushID(i); ImGui::SetNextItemWidth(cw);
      ImGui::InputTextWithHint("##g", "item", &D.grid[i]);
      ImGui::PopID();
      if (i % 3 != 2) ImGui::SameLine();
    }
    ImGui::PushItemWidth(-90);
    ImGui::InputTextWithHint("Makes", "item key", &D.out);
    ImGui::SliderInt("Count", &D.count, 1, 64);
    ImGui::PopItemWidth();
    if (button(L, "Add recipe", true)) {
      std::vector<std::string> unknown;
      for (auto& g : D.grid) if (!g.empty() && !knownItem(D, g)) unknown.push_back(g);
      int filled = 0; for (auto& g : D.grid) if (!g.empty()) filled++;
      if (D.out.empty() || !knownItem(D, D.out)) { D.msg = "\"" + D.out + "\" is not an item in the game or this pack (add it first)."; D.err = true; }
      else if (!filled) { D.msg = "Put at least one item in the grid."; D.err = true; }
      else if (!unknown.empty()) { D.msg = "Unknown items: " + unknown[0] + (unknown.size() > 1 ? " and more" : "") + " (use item keys, or #tags like #planks)."; D.err = true; }
      else if (ensurePack(D, err)) {
        json r = {{"out", D.out}, {"n", D.count}};
        if (D.shaped) {
          int x0 = 3, x1 = -1, y0 = 3, y1 = -1;
          for (int i = 0; i < 9; i++) if (!D.grid[i].empty()) { x0 = std::min(x0, i % 3); x1 = std::max(x1, i % 3); y0 = std::min(y0, i / 3); y1 = std::max(y1, i / 3); }
          std::map<std::string, char> letters; json key = json::object(), shape = json::array();
          for (int y = y0; y <= y1; y++) {
            std::string row;
            for (int x = x0; x <= x1; x++) {
              const std::string& g = D.grid[y * 3 + x];
              if (g.empty()) { row += ' '; continue; }
              if (!letters.count(g)) { char c = (char)('A' + letters.size()); letters[g] = c; key[std::string(1, c)] = g; }
              row += letters[g];
            }
            shape.push_back(row);
          }
          r["shape"] = shape; r["key"] = key;
        } else { json items = json::array(); for (auto& g : D.grid) if (!g.empty()) items.push_back(g); r["items"] = items; }
        json rj = readOr(dataDir / "recipes.json", json::object());
        if (!rj.is_object()) rj = json::object();
        if (!rj.contains("recipes")) rj["recipes"] = json::array();
        if (!rj.contains("smelting")) rj["smelting"] = json::object();
        if (!rj.contains("tags")) rj["tags"] = json::object();
        rj["recipes"].push_back(r);
        writeJson(dataDir / "recipes.json", rj);
        D.lastJson = r.dump(2); D.msg = "Added a recipe for " + D.out + "."; D.err = false;
      } else { D.msg = err; D.err = true; }
    }
    cardEnd(); nextCell();
    cardBegin(L, "New smelting");
    ImGui::PushItemWidth(-90);
    ImGui::InputTextWithHint("Smelt", "ruby_ore", &D.smeltIn);
    ImGui::InputTextWithHint("Into", "ruby", &D.smeltOut);
    ImGui::PopItemWidth();
    if (button(L, "Add smelting", true)) {
      if (!knownItem(D, D.smeltIn) || !knownItem(D, D.smeltOut) || D.smeltIn.empty() || D.smeltOut.empty()) { D.msg = "Both sides must be items in the game or this pack."; D.err = true; }
      else if (ensurePack(D, err)) {
        json rj = readOr(dataDir / "recipes.json", json::object());
        if (!rj.is_object()) rj = json::object();
        if (!rj.contains("recipes")) rj["recipes"] = json::array();
        if (!rj.contains("tags")) rj["tags"] = json::object();
        rj["smelting"][D.smeltIn] = D.smeltOut;
        writeJson(dataDir / "recipes.json", rj);
        D.lastJson = json{{D.smeltIn, D.smeltOut}}.dump(2); D.msg = "Added smelting " + D.smeltIn + " → " + D.smeltOut + "."; D.err = false;
      } else { D.msg = err; D.err = true; }
    }
    cardEnd();
    endColumns();
  }
  message(L, D.msg, D.err);
  if (!D.lastJson.empty()) codeBlock(L, D.lastJson.c_str());
  panelEnd();
}
}

void drawStudioPage(Launcher& L) {
  switch (S().page) {
    case P_OVERVIEW: drawOverview(L); break;
    case P_API: drawApi(L); break;
    case P_EDITOR: drawEditor(L); break;
    case P_MODS: drawInstalled(L); break;
    case P_DATA: drawData(L); break;
    case P_ENGINE: drawEngine(L); break;
  }
}
} // namespace studio

void studioOpenPage(const std::string& name) {
  static const char* ids[] = {"overview", "api", "editor", "mods", "data", "engine"};
  for (int i = 0; i < studio::P_COUNT; i++) if (name == ids[i]) studio::S().page = i;
}

void drawStudio(Launcher& L) {
  using namespace studio;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  beginDokuPanel("##studio", avail, false, ImGuiWindowFlags_NavFlattened);
  ImGui::PopStyleVar();
  // header: title, blurb, nav
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetWindowPos(); float W = ImGui::GetWindowWidth();
  float hh = L.fTitle->FontSize + 26 + 40;
  dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + W - 2, p.y + hh), S_PANEL2, 3, ImDrawFlags_RoundCornersTop);
  dl->AddLine(ImVec2(p.x, p.y + hh), ImVec2(p.x + W, p.y + hh), S_LINE, 2);
  dl->AddText(L.fTitle, L.fTitle->FontSize, ImVec2(p.x + 18, p.y + 10), S_EMBER, "DokuQuest Mod Studio");
  float tw = L.fTitle->CalcTextSizeA(L.fTitle->FontSize, FLT_MAX, 0, "DokuQuest Mod Studio").x;
  dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(p.x + 18 + tw + 16, p.y + 10 + L.fTitle->FontSize - L.fSmall->FontSize - 6), S_MUTED, "Learn the engine, write DQ mods and data packs, browse the C++ source.", nullptr, W - tw - 60);
  ImGui::SetCursorPos(ImVec2(18, L.fTitle->FontSize + 18));
  for (int i = 0; i < P_COUNT; i++) {
    if (i) ImGui::SameLine(0, 6);
    if (tabButton(L, pageNames[i], S().page == i)) S().page = i;
  }
  ImGui::SetCursorPos(ImVec2(2, hh + 1));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(15, 12, 10, 200));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
  ImGui::BeginChild("##studiopage", ImVec2(W - 4, ImGui::GetWindowHeight() - hh - 3 - L.fSmall->FontSize - 14), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened);
  ImGui::PopStyleVar(); ImGui::PopStyleColor();
  drawStudioPage(L);
  ImGui::EndChild();
  // footer
  float fy = ImGui::GetWindowHeight() - L.fSmall->FontSize - 10;
  dl->AddLine(ImVec2(p.x, p.y + fy - 4), ImVec2(p.x + W, p.y + fy - 4), S_LINE, 2);
  dl->AddText(L.fSmall, L.fSmall->FontSize, ImVec2(p.x + 18, p.y + fy), S_MUTED, "DokuQuest Mod Studio — mods run with the game's trust; only install code you trust.");
  endDokuPanel();
}

} // namespace dq
