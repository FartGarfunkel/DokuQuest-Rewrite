#include "mod/modloader.h"
#include "core/registry.h"

namespace dq {

struct ModLoader::Impl {};
ModLoader::ModLoader() : impl(std::make_unique<Impl>()) {}
ModLoader::~ModLoader() = default;

void ModLoader::scan() {
  mods.clear();
  fs::path dir = userSub("mods");
  json state = readJson(dir / "mods.json").value_or(json::object());
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec)) {
    ModInfo m;
    if (e.is_directory()) {
      auto mj = readJson(e.path() / "mod.json");
      m.id = e.path().filename().string();
      if (mj) { m.name = mj->value("name", m.id); m.version = mj->value("version", ""); m.author = mj->value("author", ""); m.description = mj->value("description", ""); m.kind = mj->value("kind", "data"); }
      else { m.name = m.id; m.kind = fs::exists(e.path() / "main.js") ? "js" : "data"; }
    } else if (e.path().extension() == ".js") {
      m.id = e.path().stem().string(); m.name = m.id; m.kind = "js";
    } else continue;
    m.path = e.path();
    if (state.contains(m.id)) m.enabled = state[m.id].value("enabled", true);
    mods.push_back(m);
  }
}
void ModLoader::setEnabled(const std::string& id, bool on) { for (auto& m : mods) if (m.id == id) m.enabled = on; }
void ModLoader::saveState() {
  json s = json::object();
  for (auto& m : mods) s[m.id] = {{"enabled", m.enabled}};
  writeJson(userSub("mods") / "mods.json", s);
}
void ModLoader::loadData() {
  for (auto& m : mods) if (m.enabled && fs::is_directory(m.path)) {
    if (reg().loadDataPack(m.path / "data", m.id) || reg().loadDataPack(m.path, m.id)) DQ_LOG("mod data: %s", m.id.c_str());
  }
  reg().finalize();
}
void ModLoader::onWorldOpen(Level&, EventBus&, bool) {}
void ModLoader::onWorldClose() {}

} // namespace dq
