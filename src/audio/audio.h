// audio/audio.h — sound output (miniaudio) and the procedural sound engine. Every sound is
// generated on the fly: resynthesised from the analysed profiles of the resource-pack sounds
// (data/sounds.json, made by tools/port/sound_analyze.py) and from the original game's
// synthesised sounds (game/sfxlib.js, sounds2.js, sound3.js, voices3.js, music.js...).
#pragma once
#include "common/settings.h"

namespace dq {

class World;

// What the game shows the sound engine each frame (the session's audio layer fills it in; game thread).
struct AudioScene {
  bool inWorld = false, paused = false, dead = false, creative = false, eyeWater = false;
  const World* world = nullptr;   // sound3.js: the air around the listener, walls between, the ring of caves
  vec3 player{0}, head{0};
  int dim = 2;                    // DIM_OVERWORLD (world/dims.h)
  float day = 1;                  // env.day: 0.2 (night) .. 1 (day)
  int skyAtHead = 15;             // sky light at the head, 0..15
  double time = 0;                // world clock in seconds (the wind's slow wobble)
  float gust = 0;                 // weather: how hard the wind gusts, 0..1
  std::string place;              // the music of the place: plains forest jungle desert savanna frost peaks swamp ocean village
  bool jukebox = false;           // a jukebox is playing nearby: the music keeps quiet
  // the nearest boss within 40 blocks that has turned on the player (music2.js battle music)
  bool boss = false, bossDragon = false, bossEerie = false; float bossHp = 1; u32 bossId = 0;
};

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine();
  bool init(const Settings& s);
  bool initOffline(const Settings& s);       // no device: pull the mix with mixOffline (tests, tools)
  void shutdown();
  void setListener(vec3 pos, vec3 forward, vec3 up);
  void setVolumes(const Settings& s);
  // The same calls the original game makes on its `sfx` object (game/audio.js), so ported code keeps
  // its sound names: sfx.play('click') -> play("click"), sfx.playAt('chest', x, y, z) -> playAt("chest", pos),
  // sfx.material('stone', 'dig') -> material("stone", "dig"), sfx.note(...) -> note(...).
  int play(const std::string& name, float volume = 1, float pitch = 1);
  int playAt(const std::string& name, vec3 pos, float range = 28, float volume = 1, float pitch = 1);
  void material(const std::string& mat, const std::string& kind);               // kind: dig, place, step, break, hit, fall, land
  void materialAt(const std::string& mat, const std::string& kind, vec3 pos, float range = 16, float volume = 0.5f);
  void note(const std::string& instrument, int pitch, vec3 pos);                 // note blocks: instrument = the block below's sound
  void voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch = 1);   // say | hurt | death | angry | step
  void stop(int handle);
  void setCategoryVolume(const std::string& cat, float v);   // master | sfx | mobs | music | ambience
  void setScene(const AudioScene& s);
  void update(double dt);           // ambience, music, fades, sounds waiting to be rendered
  bool ok() const;
  // tests and tools
  void mixOffline(float* stereoOut, int frames);              // after initOffline
  std::vector<float> renderSound(const std::string& name, int take = 0, u32 seed = 0);   // one take, mono 48 kHz, no mixer
  std::vector<std::string> soundNames() const;
  void preloadWait(double seconds);                          // let the worker render (tests)
  struct Impl; std::unique_ptr<Impl> impl;
};

} // namespace dq
