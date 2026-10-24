// audio/sfxlib.h — every sound name the original game plays, and how to make it here.
// The original's recipes (game/sfxlib.js SND_MAT/SND_FX, voices3.js VOX, sound3.js, blocksounds.js and the
// modules that add to them) and its live WebAudio sounds (audio.js, sounds2.js VOICES/CAVE/WORLD) are
// data in data/sfx.json (tools/port/sfx_extract.py recorded them from the running original, take by take).
// A resource-pack equivalent (data/sounds.json, see resynth.h) wins over the original's synthesis, the way
// the original let a pack's recordings replace its own sounds — here resynthesised from profiles.
#pragma once
#include "audio/resynth.h"
#include <memory>
#include <mutex>

namespace dq::snd {

struct SoundRef {
  enum Kind { None, Pack, Recipe, Live, Special, Note } kind = None;
  std::string key;          // pack event / recipe key / sound name / "n:<n>:<mat>"
  float gain = 1, pv = 0, gap = 0.04f;
  bool mob = false;         // a creature's voice: the mob volume applies
  bool jitter = true;       // play a little louder or softer each time (Sfx.sample)
  int takes = 1;
  explicit operator bool() const { return kind != None; }
};

class SfxLibrary {
public:
  bool load(const std::filesystem::path& dataDir);
  bool ok() const { return loaded; }
  SoundRef resolve(const std::string& name) const;                                   // sfx.play(name)
  SoundRef material(const std::string& mat, const std::string& kind, bool placed) const;   // sfx.material / materialAt
  SoundRef note(int n, const std::string& underMat) const;
  // render one take (any thread); `variant` picks the pack file for Pack refs
  std::vector<float> render(const SoundRef& r, int take, u32 seed, float* pitch = nullptr, float* vol = nullptr) const;
  const PackSounds& packs() const { return pack; }
  std::vector<std::string> names() const;
  bool usePack = true;
private:
  bool loaded = false;
  PackSounds pack;
  json recipes, table;      // data/sfx.json: "recipes", "names"
  float matGain(const std::string& key) const;
};

} // namespace dq::snd
