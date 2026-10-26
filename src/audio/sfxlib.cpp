// audio/sfxlib.cpp — see sfxlib.h. The dispatch follows the original's chain (voices3.js -> sfxlib.js ->
// sounds2.js -> audio.js) as recorded in data/sfx.json "names"; a pack equivalent is tried first.
#include "audio/sfxlib.h"
#include "audio/babble.h"
#include "common/fs.h"

namespace dq::snd {

void addHalloweenSounds(json& table);   // audio/hw_sfx.cpp

bool SfxLibrary::load(const std::filesystem::path& dir) {
  auto j = readJson(dir / "sfx.json");
  if (!j) { DQ_WARN("audio: data/sfx.json missing; the original's sounds are off"); }
  else { recipes = std::move((*j)["recipes"]); table = std::move((*j)["names"]); }
  addHalloweenSounds(table);
  if (!pack.load(dir / "sounds.json", dir / "sounds.bin")) DQ_WARN("audio: no resource-pack sound profiles (data/sounds.*)");
  loaded = j.has_value() || pack.ok();
  return loaded;
}

std::vector<std::string> SfxLibrary::names() const {
  std::vector<std::string> v;
  for (auto& [k, x] : table.items()) v.push_back(k);
  return v;
}

float SfxLibrary::matGain(const std::string& key) const {
  auto it = recipes.find(key);
  return it != recipes.end() ? it->value("gain", 0.8f) : 0.8f;
}

static int takesOf(const json& rec) { return std::max(1, (int)rec["takes"].size()); }

SoundRef SfxLibrary::resolve(const std::string& name) const {
  SoundRef r;
  if (name.rfind("babble:", 0) == 0) {   // a villager's line (babble.h): the name carries its voice, pace and words
    r.kind = SoundRef::Babble; r.key = name.substr(7); r.pv = 0; r.gap = 0; r.jitter = false; r.takes = 1;
    return r;
  }
  auto t = table.find(name);
  const json* e = t != table.end() ? &*t : nullptr;
  float gap = e ? e->value("gap", 0.04f) : 0.04f;
  bool mob = e && e->value("mob", 0);
  // a pack's own recording of it (voices3.js: mob.<name>, the player's hurt fx.hurt; sfxlib.js: fx.<name>)
  if (usePack) {
    const char* pre[] = {"mob.", "fx."};
    for (const char* p : pre) {
      std::string k = p + name;
      if (name == "hurt" && p[0] == 'm') k = "fx.hurt";
      if (auto v = pack.event(k)) {
        r.kind = SoundRef::Pack; r.key = k; r.takes = std::max(3, (int)v->size()); r.gap = gap;
        if (p[0] == 'm') { r.mob = true; r.gain = name.rfind("mstep_", 0) == 0 ? 0.3f : 0.95f; r.pv = 0.05f; }
        else {
          auto rc = recipes.find("fx." + name);
          r.gain = rc != recipes.end() ? rc->value("gain", 0.8f) : 0.8f; r.pv = name == "levelup" ? 0 : 0.07f; r.mob = mob;
        }
        return r;
      }
    }
  }
  if (!e) {
    // not a name the original ever played by itself: try the recipe tables directly
    for (std::string k : {"vx." + name, "fx." + name}) {
      auto rc = recipes.find(k);
      if (rc != recipes.end()) { r.kind = SoundRef::Recipe; r.key = k; r.gain = rc->value("gain", 0.8f); r.pv = 0.06f; r.takes = takesOf(*rc); r.mob = k[0] == 'v'; return r; }
    }
    return r;
  }
  r.gap = gap; r.mob = mob;
  if (e->contains("r")) {
    std::string k = (*e)["r"].get<std::string>();
    auto rc = recipes.find(k);
    if (rc == recipes.end()) return r;
    r.kind = SoundRef::Recipe; r.key = k; r.takes = takesOf(*rc);
    r.gain = e->value("gain", 0.8f); r.pv = e->value("pv", 0.07f);
    return r;
  }
  if (e->contains("live")) { r.kind = SoundRef::Live; r.key = name; r.takes = (int)(*e)["live"].size(); r.jitter = false; return r; }
  if (e->contains("special")) { r.kind = SoundRef::Special; r.key = name == "cave_scrape" ? "cave_scrape" : "cave"; r.takes = 2; r.jitter = false; return r; }
  return r;
}

SoundRef SfxLibrary::material(const std::string& matIn, const std::string& kindIn, bool placed) const {
  // sfxlib.js: SND_MAT[mat] || stone; spec = M[kind] || M.dig. blocksounds.js: a pack's wood serves logs.
  std::string mat = matIn.empty() ? "stone" : matIn, kind = kindIn;
  if (!recipes.contains(mat + ".step") && !recipes.contains(mat + ".dig")) mat = "stone";
  if (!recipes.contains(mat + "." + kind)) kind = "dig";
  std::string key = mat + "." + kind;
  SoundRef r; r.gap = kind == "step" ? 0.16f : 0.045f;
  float specGain = matGain(key);
  if (usePack) {
    std::string pk = key;
    if (mat == "log" && !pack.event(pk)) pk = "wood." + kind;
    if (auto v = pack.event(pk)) {
      r.kind = SoundRef::Pack; r.key = pk; r.takes = std::max(3, (int)v->size());
      r.gain = placed ? specGain : specGain * 1.1f; r.pv = placed ? 0.1f : 0.06f;
      return r;
    }
  }
  auto rc = recipes.find(key);
  if (rc == recipes.end()) return r;
  r.kind = SoundRef::Recipe; r.key = key; r.takes = takesOf(*rc); r.gain = specGain;
  r.pv = placed ? 0.1f : kind == "step" ? 0.1f : 0.08f;
  return r;
}

SoundRef SfxLibrary::note(int n, const std::string& under) const {
  SoundRef r; r.kind = SoundRef::Note; r.key = "n:" + std::to_string(std::clamp(n, 0, 24)) + ":" + (under.empty() ? "wood" : under);
  r.gap = 0.02f; r.jitter = false; r.takes = 1;
  return r;
}

std::vector<float> SfxLibrary::render(const SoundRef& r, int take, u32 seed, float* pitch, float* vol) const {
  if (pitch) *pitch = 1;
  if (vol) *vol = 1;
  switch (r.kind) {
    case SoundRef::Pack: {
      auto v = pack.event(r.key);
      if (!v || v->empty()) return {};
      const PackSounds::Variant& var = (*v)[take % v->size()];
      if (pitch) *pitch = var.pitch;
      if (vol) *vol = var.vol;
      const Profile* p = pack.profile(var.file);
      return p ? pack.render(*p, seed) : std::vector<float>{};
    }
    case SoundRef::Recipe: {
      auto rc = recipes.find(r.key);
      if (rc == recipes.end()) return {};
      const json& takes = (*rc)["takes"];
      int v = take % std::max<int>(1, (int)takes.size());
      Rng rng(seedFromString(r.key) + (u32)v * 7919u + seed * 0x9E3779B9u);
      SndBuf b((*rc)["len"].get<double>(), &rng);
      for (auto& op : takes[v]) { try { runOp(b, op, r.key); } catch (const std::exception& ex) { DQ_WARN("audio: %s: %s", r.key.c_str(), ex.what()); } }
      b.finish(0.9f);
      return std::move(b.d);
    }
    case SoundRef::Live: {
      auto t = table.find(r.key);
      if (t == table.end()) return {};
      const json& l = (*t)["live"];
      return renderLive(l[take % l.size()], seed);
    }
    case SoundRef::Special: return renderSpecial(r.key, seed);
    case SoundRef::Babble: return renderBabbleKey(r.key);
    case SoundRef::Note: {
      int n = std::atoi(r.key.c_str() + 2); std::string mat = r.key.substr(r.key.find(':', 2) + 1);
      return renderNote(n, mat, seed);
    }
    default: return {};
  }
}

} // namespace dq::snd
