// launcher/modstudio_docs.cpp — Mod Studio: drawing helpers, mod templates, the Overview and DQ API pages
// (updated for the C++ mod system: JS mods, data packs, native plugins, shader packs, resource packs) and
// the engine source browser that replaces the HTML studio's "module browser".
#include "launcher/modstudio.h"
#include <imgui_stdlib.h>
#include <imgui_internal.h>
#include <iterator>

namespace dq::studio {

// ---------------------------------------------------------------- helpers
void panelBegin(Launcher& L, const char* title) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, S_PANEL);
  ImGui::PushStyleColor(ImGuiCol_Border, S_LINE);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::BeginChild(title, ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetWindowPos(); float w = ImGui::GetWindowWidth(), hh = L.fTitle->FontSize * 0.8f + 18;
  dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + w - 2, p.y + hh), S_PANEL2, 5, ImDrawFlags_RoundCornersTop);
  dl->AddLine(ImVec2(p.x, p.y + hh), ImVec2(p.x + w, p.y + hh), S_LINE, 2);
  const char* end = ImGui::FindRenderedTextEnd(title);
  dl->AddText(L.fTitle, L.fTitle->FontSize * 0.8f, ImVec2(p.x + 14, p.y + 9), S_EMBER, title, end);
  ImGui::SetCursorPos(ImVec2(16, hh + 14));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(w - 16);
  ImGui::PushItemWidth(-16);
}
void panelEnd() {
  ImGui::PopItemWidth();
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0, 8));
}
void cardBegin(Launcher& L, const char* title) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, S_SLOT);
  ImGui::PushStyleColor(ImGuiCol_Border, S_LINE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
  ImGui::BeginChild(title, ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoScrollbar);
  ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
  const char* end = ImGui::FindRenderedTextEnd(title);
  ImGui::PushStyleColor(ImGuiCol_Text, S_GOLD);
  ImGui::PushFont(L.fUi); ImGui::TextUnformatted(title, end); ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::PushTextWrapPos(0);
}
void cardEnd() { ImGui::PopTextWrapPos(); ImGui::EndChild(); }

void hint(Launcher& L, const char* text) {
  ImGui::PushFont(L.fSmall); ImGui::PushStyleColor(ImGuiCol_Text, S_MUTED);
  ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(text); ImGui::PopTextWrapPos();
  ImGui::PopStyleColor(); ImGui::PopFont();
}

// one line of text where `code` spans are drawn in the code colour, wrapped by words
static void richLine(Launcher& L, const char* s, float indent) {
  ImFont* f = L.fSmall;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 org = ImGui::GetCursorScreenPos();
  float right = org.x + ImGui::GetContentRegionAvail().x, x = org.x + indent, y = org.y, lh = f->FontSize + 4;
  bool code = false;
  const char* p = s;
  while (*p) {
    if (*p == '`') { code = !code; p++; continue; }
    const char* w = p; while (*w && *w != ' ' && *w != '`') w++;
    const char* we = w; while (*we == ' ') we++;
    float ww = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, p, w).x, wsp = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, p, we).x;
    if (x + ww > right && x > org.x + indent) { x = org.x + indent; y += lh; }
    dl->AddText(f, f->FontSize, ImVec2(x, y), code ? S_FROST : S_MUTED, p, w);
    x += wsp; p = we;
  }
  ImGui::Dummy(ImVec2(right - org.x, y - org.y + lh));
}
void bullets(Launcher& L, std::initializer_list<const char*> items) {
  for (const char* it : items) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 6, p.y + L.fSmall->FontSize * 0.5f + 2), 2, S_MUTED);
    richLine(L, it, 16);
  }
}
void codeBlock(Launcher& L, const char* code) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, S_PRE);
  ImGui::PushStyleColor(ImGuiCol_Border, S_LINE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
  ImGui::BeginChild(ImGui::GetID(code), ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
  ImGui::PushFont(L.fMono); ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(228, 221, 210, 255));
  ImGui::TextUnformatted(code);
  ImGui::PopStyleColor(); ImGui::PopFont();
  ImGui::EndChild();
  ImGui::PopStyleVar(); ImGui::PopStyleColor(2);
}
bool button(Launcher& L, const char* label, bool primary, bool enabled) {
  ImFont* f = L.fUi;
  const char* end = ImGui::FindRenderedTextEnd(label);
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, label, end);
  ImVec2 size(ts.x + 28, 36);
  if (!enabled) ImGui::BeginDisabled();
  bool pressed = ImGui::InvisibleButton(label, size);
  bool hot = enabled && (ImGui::IsItemHovered() || ImGui::IsItemFocused());
  if (!enabled) ImGui::EndDisabled();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (primary) dl->AddRectFilledMultiColor(a, b, IM_COL32(255, 154, 82, 255), IM_COL32(255, 154, 82, 255), IM_COL32(196, 82, 27, 255), IM_COL32(196, 82, 27, 255));
  else dl->AddRectFilledMultiColor(a, b, S_SLOTHI, S_SLOTHI, S_SLOT, S_SLOT);
  dl->AddRect(a, b, primary ? (hot ? C_WHITE : IM_COL32(255, 176, 122, 255)) : (hot ? S_LINEHI : S_LINE), 4, 0, 2);
  if (!enabled) dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 110), 4);
  ImVec2 tp(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f));
  dl->AddText(f, f->FontSize, tp, primary ? IM_COL32(26, 13, 5, 255) : S_TEXT, label, end);
  if (primary) dl->AddText(f, f->FontSize, ImVec2(tp.x + 1, tp.y), IM_COL32(26, 13, 5, 255), label, end);
  return pressed && enabled;
}
void message(Launcher& L, const std::string& text, bool error) {
  if (text.empty()) return;
  ImGui::PushFont(L.fSmall); ImGui::PushStyleColor(ImGuiCol_Text, error ? S_BAD : S_OK);
  ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(text.c_str()); ImGui::PopTextWrapPos();
  ImGui::PopStyleColor(); ImGui::PopFont();
}
bool twoColumns(const char* id) {
  bool wide = ImGui::GetContentRegionAvail().x > 700;
  if (!ImGui::BeginTable(id, wide ? 2 : 1, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return false;
  ImGui::TableNextColumn();
  return true;
}
void nextCell() { ImGui::TableNextColumn(); }
void endColumns() { ImGui::EndTable(); }

// ---------------------------------------------------------------- templates (the HTML studio's TPL)
const std::vector<Template>& templates() {
  static const std::vector<Template> t = {
    {"welcome", "Welcome + command", R"(DQ.name = 'Welcome Toast';

DQ.on('worldOpen', (worldName) => {
  DQ.say('Welcome to ' + (worldName || 'this world') + '!');
});

DQ.command('hello', 'Say hello from the mod', (player, args) => {
  const who = player && player.name ? player.name : 'traveler';
  return 'Hello, ' + who + '!';
});
)"},
    {"breaklog", "Log block breaks", R"(DQ.name = 'Break Logger';

DQ.on('blockBreak', (player, x, y, z, key) => {
  const who = player && player.name ? player.name : 'Someone';
  DQ.say(who + ' broke ' + key + ' at ' + x + ',' + y + ',' + z);
});
)"},
    {"kit", "Starter kit command", R"(DQ.name = 'Starter Kit';

DQ.command('kit', 'Get a small starter kit', (player) => {
  if (!player || !player.name) return 'No player.';
  DQ.give(player.name, 'bread', 8);
  DQ.give(player.name, 'torch', 16);
  return 'Starter kit given.';
});
)"},
    {"plugin", "Host plugin (chat log)", R"(DQ.name = 'Chat Logger';
DQ.plugin = true;

DQ.on('chat', (name, text) => {
  // return false; // uncomment to cancel the chat
  DQ.log('[chat]', name, text);
});

DQ.on('join', (name) => {
  DQ.say(name + ' joined the host world.');
});
)"},
    {"item", "New item + recipe", R"(DQ.name = 'Lucky Charm';

DQ.item('lucky_charm', { name: 'Lucky Charm', icon: 'emerald', desc: 'It does nothing. Probably.' });
DQ.recipe({ out: 'lucky_charm', n: 1, items: ['emerald', 'gold_nugget', 'string'] });

DQ.on('blockBreak', (player, x, y, z, key) => {
  if (key === 'diamond_ore') DQ.say(player.name + ' found diamonds!');
});
)"},
    {"blank", "Blank", R"(DQ.name = 'My Mod';

// DQ.on('worldOpen', (name) => { });
// DQ.command('example', 'Does nothing yet', (player, args) => '');
)"},
  };
  return t;
}

// ---------------------------------------------------------------- Overview
void drawOverview(Launcher& L) {
  panelBegin(L, "How the game is built");
  hint(L, "DokuQuest is a C++ engine now: the launcher (this window: news, Play, the dedicated server and this studio), the game "
          "(its own window, OpenGL 3.3 Legacy or 4.6 Modern) and a headless server. Mods are built in, like Fabric, Forge and OptiFine together.");
  ImGui::Spacing();
  if (twoColumns("##ov")) {
    cardBegin(L, "Runtime");
    bullets(L, {"Launcher -> Play starts `dokuquest --launcher --renderer modern|legacy`",
                "Game start: game data, then data packs from enabled mods",
                "Open a world -> stream chunks (worker threads) -> JS mods get `worldOpen`",
                "Frame: input -> simulation (20 ticks/s) -> meshing -> draw"});
    cardEnd(); nextCell();
    cardBegin(L, "Layers (src/ folders)");
    bullets(L, {"`common/ core/` settings, logging, blocks, items, recipes", "`world/ gen/` chunks, the stacked dimension column, terrain",
                "`sim/` player, mobs, gameplay; `net/ server/` multiplayer", "`render/ assets/ audio/ ui/ input/` the game window", "`mod/` the mod loader and the DQ API"});
    cardEnd(); nextCell();
    cardBegin(L, "Ways to change the game");
    bullets(L, {"JS mod — safest: the `DQ` API, runs in QuickJS", "Data pack — JSON blocks, items, recipes; no code", "Native plugin — a .so/.dll with the C++ API, full speed",
                "Shader pack — replace any engine shader", "Resource pack — textures (become micro-voxels) and sounds", "Engine edit — build the C++ source yourself"});
    cardEnd(); nextCell();
    cardBegin(L, "Your files");
    bullets(L, {"`mods/` JS mods, data packs, plugins (enabled list in mods.json)", "`resourcepacks/` and `shaderpacks/`", "`saves/` worlds, `servers/` server worlds",
                "`settings.json` shared by the launcher and the game"});
    if (button(L, "Open the game folder")) openPath(userDir());
    cardEnd();
    endColumns();
  }
  panelEnd();
  panelBegin(L, "Quick start — first mod in 5 minutes");
  bullets(L, {"Open the Mod editor tab and pick the template \"Welcome + command\".", "Press Save to mods folder.", "Check it is switched on under Installed mods.",
              "Press Play and open a world — you should see the chat line; try `/hello`."});
  panelEnd();
}

// ---------------------------------------------------------------- DQ API
void drawApi(Launcher& L) {
  panelBegin(L, "DQ API reference");
  hint(L, "A JS mod is the body of a function given the API as DQ, run in strict mode: (function (DQ) { your code }) — the same as the "
          "HTML game's Mods panel, so its mods work unchanged. Set DQ.name. DQ.plugin = true makes it a host/server plugin.");
  ImGui::Spacing();
  if (twoColumns("##api")) {
    cardBegin(L, "Registration");
    codeBlock(L, "DQ.name = 'My Mod';\nDQ.plugin = false; // true = server plugin\n\nDQ.on('worldOpen', (worldName) => { ... });\nDQ.command('cmd', 'help text', (player, args) => {\n  return 'reply';\n});\nDQ.item('new_key', { name: 'Nice Name', icon: 'emerald' });\nDQ.recipe({ out: 'new_key', n: 1, items: ['a', 'b'] });\nDQ.recipe({ out: 'new_key', shape: [' R ', 'R R'], key: { R: 'ruby' } });");
    cardEnd(); nextCell();
    cardBegin(L, "World & players");
    codeBlock(L, "DQ.world.getBlock(x, y, z); // key or null\nDQ.world.setBlock(x, y, z, 'stone');\n\nDQ.players(); // [{ name, x, y, z, dim }, ...]\nDQ.give('PlayerName', 'bread', 4);\nDQ.spawn('zombie', x, y, z); // host only\nDQ.say('Hello all');\nDQ.toast('Hi');\nDQ.time(); DQ.time(0.5); // day fraction\nDQ.log('to the log / server console');\nDQ.version; // game version");
    hint(L, "y is global: the worlds are stacked — The End 0–255, Ember Depths 256–511, Overworld 512–767, Skylands 768–1023.");
    cardEnd(); nextCell();
    cardBegin(L, "Events");
    bullets(L, {"`worldOpen` — world ready (name)", "`tick` — every simulation tick (dt) — keep it light!", "`blockBreak` — player, x, y, z, key", "`blockPlace` — player, x, y, z, key",
                "`mobDeath` — mob, killer", "`chat` / `join` / `leave` — host and server (return false cancels chat)", "`serverStart` / `serverStop` / `serverCommand` / `log`"});
    cardEnd(); nextCell();
    cardBegin(L, "Trust & limits");
    bullets(L, {"Mods run with the game's trust — only install code you trust.", "Item keys: `^[a-z0-9_]{2,40}$`, must be new.", "Commands: `^[a-z0-9_]{1,24}$` (any case).",
                "Changes apply the next time a world opens.", "Stored in `mods/<id>/` (mod.json + main.js) or as a single `mods/<name>.js`."});
    cardEnd();
    endColumns();
  }
  panelEnd();
  panelBegin(L, "Data packs, plugins, shader and resource packs");
  if (twoColumns("##api2")) {
    cardBegin(L, "Data packs (no code)");
    hint(L, "A mod folder with a data/ folder (or the files at its top). Loaded when the game starts, before any world opens. The Data packs tab writes these for you.");
    codeBlock(L, "mods/my_pack/mod.json   { \"name\": \"My Pack\", \"kind\": \"data\" }\nmods/my_pack/data/blocks.json\n  [{ \"key\": \"ruby_block\", \"name\": \"Ruby Block\", \"tex\": \"ruby_block\",\n     \"hardness\": 3, \"tool\": \"pickaxe\", \"tier\": 2 }]\nmods/my_pack/data/items.json\n  { \"ruby\": { \"key\": \"ruby\", \"name\": \"Ruby\", \"icon\": \"emerald\" } }\nmods/my_pack/data/recipes.json\n  { \"recipes\": [{ \"out\": \"ruby_block\", \"shape\": [\"RRR\",\"RRR\",\"RRR\"],\n                 \"key\": { \"R\": \"ruby\" } }],\n    \"smelting\": { \"ruby_ore\": \"ruby\" }, \"tags\": {} }\nmods/my_pack/data/textures/ruby_block.png  (tex: \"textures/ruby_block.png\")");
    cardEnd(); nextCell();
    cardBegin(L, "Native plugins (C++)");
    bullets(L, {"A mod folder with `mod.json` (kind `native`) and a library: `plugin.so` (Linux) or `plugin.dll` (Windows).",
                "Built against the engine headers (`src/mod/`), it gets the same events as JS mods and the engine itself, at full speed.",
                "For heavy mods: new world generators, render layers, systems.", "The plugin API header ships with the mods update; until then use JS mods and data packs."});
    cardEnd(); nextCell();
    cardBegin(L, "Shader packs");
    bullets(L, {"A folder in `shaderpacks/`; choose it in Options.", "Any file with the name of an engine shader (`chunk.glsl`, `sky.glsl`, `common.glsl` …) replaces it.",
                "One file holds both stages: `#ifdef VERTEX` / `#ifdef FRAGMENT`; GLSL 330; `#include \"common.glsl\"` works.",
                "`DQ_MODERN` is defined on the Modern renderer. A broken shader falls back to the built-in one."});
    cardEnd(); nextCell();
    cardBegin(L, "Resource packs");
    bullets(L, {"A folder or .zip in `resourcepacks/` in the Minecraft 1.19 layout (`assets/minecraft/textures/block/…`).",
                "Every pixel of a block texture becomes a micro-voxel; items become 3D voxel models.", "Sounds are resynthesised from the pack's sounds — no audio files ship with the game.",
                "\"Automatic\" uses GeruDoku when it is installed, else the built-in Doku tiles."});
    cardEnd();
    endColumns();
  }
  panelEnd();
}

// ---------------------------------------------------------------- engine source browser
namespace {
struct Area { const char* dir; const char* what; const char* links; };
const Area areas[] = {
  {"common", "types, logging, files/JSON/zlib, settings, thread pool, noise (exact util.js port)", "—"},
  {"core", "registry (blocks/items/recipes/tags/mobs from data/*.json), ItemStack, initCore()", "common"},
  {"world", "Chunk (16×256×16 band, 16 sections), World (streaming, global y, saving), light, regions", "core"},
  {"gen", "world generators (one per dimension band), structures", "world"},
  {"sim", "Level (realms, entities, time, systems), Entity, Player, physics, events, gameplay", "world"},
  {"net", "ENet protocol, host/guest", "sim"},
  {"mod", "mod loader, QuickJS DQ API, native plugin API", "sim"},
  {"server", "DedicatedServer (the launcher's Server tab + dokuquest-server)", "sim, net"},
  {"render", "GL backend, mesher, world/LOD/entity/portal/sky/water/particle renderers", "client"},
  {"assets", "tiles (+ resource packs), item voxel models, skins -> voxel characters", "client"},
  {"audio", "miniaudio output + procedural synth", "client"},
  {"ui", "ImGui screens styled like the original, HUD, inventory", "client"},
  {"input", "keyboard, mouse, controllers", "client"},
  {"client", "App (window, loop, screens), GameSession (world, player, render layers)", "client"},
  {"launcher", "this launcher: news, Play, options, server, Mod Studio, updater", "client"},
};
struct EngineState { int sel = 0; fs::path srcRoot; bool searched = false; std::vector<std::pair<std::string, int>> files; int fileSel = -1; std::string preview; int listedFor = -1; };
EngineState& es() { static EngineState s; return s; }
fs::path findSource() {
  for (fs::path p : {installDir() / ".." / "src", installDir() / ".." / ".." / "src", installDir() / "src"}) {
    std::error_code ec; if (fs::exists(p / "common" / "common.h", ec)) return fs::weakly_canonical(p, ec);
  }
  return {};
}
}

void drawEngine(Launcher& L) {
  EngineState& E = es();
  if (!E.searched) { E.srcRoot = findSource(); E.searched = true; }
  const std::string repo = "https://github.com/" + L.updater.source;
  panelBegin(L, "Engine source");
  hint(L, "The engine is open source. Each folder under src/ is one area; the lower ones build on the upper ones. "
          "dqcore (common … server) has no graphics and is all the dedicated server needs; dqclient adds the game window.");
  ImGui::Spacing();
  if (button(L, "Open the source on GitHub", true)) openUrl(repo);
  ImGui::SameLine();
  if (button(L, "Releases")) openUrl(repo + "/releases");
  ImGui::Spacing();
  if (twoColumns("##eng")) {
    cardBegin(L, "Areas");
    for (int i = 0; i < (int)std::size(areas); i++) {
      std::string lbl = std::string("src/") + areas[i].dir;
      if (ImGui::Selectable((lbl + "##a").c_str(), E.sel == i)) { E.sel = i; E.fileSel = -1; E.preview.clear(); }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", areas[i].what);
    }
    cardEnd(); nextCell();
    const Area& a = areas[E.sel];
    cardBegin(L, (std::string("src/") + a.dir + "##card").c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, S_TEXT); ImGui::TextUnformatted(a.what); ImGui::PopStyleColor();
    hint(L, (std::string("Builds on: ") + a.links).c_str());
    if (button(L, "Open on GitHub")) openUrl(repo + "/tree/main/src/" + a.dir);
    if (!E.srcRoot.empty()) {
      if (E.listedFor != E.sel) {
        E.files.clear(); std::error_code ec;
        for (auto& f : fs::directory_iterator(E.srcRoot / a.dir, ec)) {
          if (!f.is_regular_file()) continue;
          auto t = readFile(f.path()); int lines = t ? (int)std::count(t->begin(), t->end(), '\n') : 0;
          E.files.push_back({f.path().filename().string(), lines});
        }
        std::sort(E.files.begin(), E.files.end());
        E.listedFor = E.sel;
      }
      ImGui::Spacing();
      hint(L, ("Local source: " + E.srcRoot.string()).c_str());
      for (int i = 0; i < (int)E.files.size(); i++) {
        if (ImGui::Selectable(strFormat("%s  (%d lines)##f%d", E.files[i].first.c_str(), E.files[i].second, i).c_str(), E.fileSel == i)) {
          E.fileSel = i;
          auto t = readFile(E.srcRoot / a.dir / E.files[i].first);
          E.preview = t ? (t->size() > 60000 ? t->substr(0, 60000) + "\n\n/* … truncated for preview … */" : *t) : "";
        }
      }
    } else hint(L, "This copy has no source next to it; the files are on GitHub.");
    cardEnd();
    endColumns();
  }
  if (!E.preview.empty()) {
    ImGui::PushFont(L.fMono);
    ImGui::InputTextMultiline("##prev", &E.preview, ImVec2(-16, 320), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
  }
  panelEnd();
  panelBegin(L, "Editing the engine safely");
  static bool chk[5] = {};
  const char* items[] = {"I kept a backup of my worlds (the saves folder)", "I preferred a mod or data pack when the DQ API was enough",
                         "My change is small and testable on its own", "If I touched world generation, I will test a new world",
                         "I ran the smoke test: create a world, run 300 frames, no \"[error]\" lines"};
  for (int i = 0; i < 5; i++) ImGui::Checkbox(items[i], &chk[i]);
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, S_GOLD); ImGui::TextUnformatted("Build it yourself"); ImGui::PopStyleColor();
  codeBlock(L, ("git clone " + repo + ".git\ncd " + L.updater.source.substr(L.updater.source.find('/') + 1) +
                "\nbash tools/fetch_deps.sh\ncmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release\nninja -C build\n./build/dokuquest --create Test --smoke-frames 300 --smoke-shot shot.png").c_str());
  hint(L, "Heavy work inside per-chunk generation slows loading for everyone. Keep generation cheap and put new features in their own file (one feature per file).");
  panelEnd();
}

} // namespace dq::studio
