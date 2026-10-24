// audio/audio.h — sound output (miniaudio) and the procedural sound engine. Every sound is
// generated on the fly: resynthesised from the analysed profiles of the resource-pack sounds
// (data/sounds.json, made by tools/port/sound_analyze.py) and from the original game's
// synthesised sounds (game/sfxlib.js, sounds2.js, sound3.js, voices3.js, music.js...).
#pragma once
#include "common/settings.h"

namespace dq {

struct SoundParams {
  float volume = 1, pitch = 1;
  bool positional = false; vec3 pos{0};
  float minDist = 2, maxDist = 32;
  std::string category = "block";   // master | block | mob | music | ambience | ui | player | weather
};

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  bool init(const Settings& s);
  void shutdown();
  void setListener(vec3 pos, vec3 forward, vec3 up);
  void setVolumes(const Settings& s);
  // play a named sound event (e.g. "dig.stone", "step.grass", "mob.zombie.say", "ui.click", "random.pop")
  int play(const std::string& event, const SoundParams& p = {});
  void stop(int handle);
  void update(double dt);           // ambience, music, fades
  bool ok() const;
  struct Impl; std::unique_ptr<Impl> impl;
};

} // namespace dq
