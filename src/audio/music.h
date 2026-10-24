// audio/music.h — game/music.js + game/music2.js: quiet generative pieces (piano, pads, bells, and a
// whole band: harp, flute, strings, choir, horn, marimba, kalimba, music box, oud, clarinet, drums), the
// music of the place and the hour, and battle music when a boss turns on you, ending in a fanfare.
// The director (game thread) writes the pieces and schedules notes a little ahead on the audio clock;
// the synth (audio thread) plays them live through the music bus (tone filter, hall reverb, limiter).
#pragma once
#include "audio/engine.h"

namespace dq::snd {

enum MusicIns : u8 { MI_PIANO, MI_PAD, MI_BELL, MI_DRONE, MI_HARP, MI_FLUTE, MI_STRINGS, MI_CHOIR, MI_HORN, MI_BRASS, MI_MARIMBA,
  MI_KALIMBA, MI_MUSICBOX, MI_CELESTA, MI_PLUCK, MI_CLARINET, MI_CELLO, MI_TAIKO, MI_TIMPANI, MI_SNARE, MI_HAND, MI_FRAME,
  MI_SHAKER, MI_CYMBAL, MI_COUNT };
int musicInsOf(const std::string& name);

class MusicSynth {
public:
  MusicSynth();
  ~MusicSynth();
  void command(const AudioCmd& c);                           // Note / Bus / MusicClear
  void render(float* stereo, int n, double t0, float volume); // adds the music bus into stereo
  struct Impl; std::unique_ptr<Impl> m;
};

class MusicDirector {
public:
  explicit MusicDirector(AudioEngine::Impl& e);
  ~MusicDirector();
  void update(double dt, const AudioScene& s);
  struct Impl; std::unique_ptr<Impl> m;
};

// (tests) write a piece for a mood, as the original would: [t seconds, instrument, midi, length s, velocity]
struct MusicNote { double t; u8 ins; float m, d, v; };
std::vector<MusicNote> composeMusic(const std::string& mood, u32 seed);

} // namespace dq::snd
