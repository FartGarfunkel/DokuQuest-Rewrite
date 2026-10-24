// audio/audio.h — sound output (miniaudio) and the procedural sound engine. Every sound is
// generated on the fly: resynthesised from the analysed profiles of the resource-pack sounds
// (data/sounds.json, made by tools/port/sound_analyze.py) and from the original game's
// synthesised sounds (game/sfxlib.js, sounds2.js, sound3.js, voices3.js, music.js...).
#pragma once
#include "common/settings.h"

namespace dq {


class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  bool init(const Settings& s);
  void shutdown();
  void setListener(vec3 pos, vec3 forward, vec3 up);
  void setVolumes(const Settings& s);
  // The same calls the original game makes on its `sfx` object (game/audio.js), so ported code keeps
  // its sound names: sfx.play('click') -> play("click"), sfx.playAt('chest', x, y, z) -> playAt("chest", pos),
  // sfx.material('stone', 'dig') -> material("stone", "dig"), sfx.note(...) -> note(...).
  int play(const std::string& name, float volume = 1, float pitch = 1);
  int playAt(const std::string& name, vec3 pos, float range = 28, float volume = 1, float pitch = 1);
  void material(const std::string& mat, const std::string& kind);               // kind: dig, place, step, break, hit, fall, land
  void materialAt(const std::string& mat, const std::string& kind, vec3 pos);
  void note(const std::string& instrument, int pitch, vec3 pos);                 // note blocks, instruments
  void voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch = 1);   // say | hurt | death | step | attack
  void stop(int handle);
  void setCategoryVolume(const std::string& cat, float v);   // master | sfx | mobs | music | ambience
  void update(double dt);           // ambience, music, fades
  bool ok() const;
  struct Impl; std::unique_ptr<Impl> impl;
};

} // namespace dq
