// audio/dsp.h — the original game's little DSP kit, ported sample for sample: the rendered-sound
// buffer (game/sfxlib.js SndBuf: noise, grains, modes, thump, pluck, vox; game/voices3.js throat,
// hiss, clack, room, shape) and the WebAudio pieces its live sounds were made of (oscillators,
// biquads, AudioParam ramps) so game/audio.js, sounds2.js and music*.js sound the same here.
#pragma once
#include "common/common.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cmath>

namespace dq::snd {

constexpr int SR = 48000;
using json = nlohmann::json;

// mulberry32 (core/util.js) — the original's seeded random
struct Rng {
  u32 a;
  explicit Rng(u32 seed = 1) : a(seed) {}
  float operator()() {
    a += 0x6D2B79F5u; u32 t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (float)((t ^ (t >> 14)) / 4294967296.0);
  }
  float pm() { return (*this)() * 2.f - 1.f; }
};
u32 seedFromString(const std::string& s);   // core/util.js seedFromString

// A biquad. sfxlib's own (`sndBiquad`: lp / hp / bp with a 0 dB peak, Q linear) and WebAudio's
// BiquadFilterNode (lowpass/highpass take Q in dB, bandpass Q linear).
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void set(char type, double f, double Q, double sr = SR);          // 'l' 'h' 'b' (sndBiquad)
  void setWA(const std::string& type, double f, double Q, double sr = SR);   // WebAudio names
  float run(float x) { double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return (float)y; }
};
char bqType(const std::string& t);   // "lp"/"lowpass" -> 'l' ...

// WebAudio AudioParam automation: setValueAtTime, linearRampToValueAtTime, exponentialRampToValueAtTime.
struct Param {
  struct Ev { int kind; double t; float v; };   // 0 set, 1 linear, 2 exponential
  float init = 0;
  std::vector<Ev> ev;
  size_t cur = 0;
  void set(float v, double t) { ev.push_back({0, t, v}); }
  void lin(float v, double t) { ev.push_back({1, t, v}); }
  void exp(float v, double t) { ev.push_back({2, t, v}); }
  float at(double t);                              // times must not go backwards between calls
};

// Band-limited oscillators (PolyBLEP saw/square; sine; triangle) like WebAudio's OscillatorNode.
struct Osc {
  int type = 0;   // 0 sine 1 square 2 sawtooth 3 triangle 4 harmonic (band-limited single partial) 5 deepbass
  double ph = 0;
  unsigned long long ibph = 0;   // integer BLIT phase for type 5
  double integ = 0;    // leaky integrator state for type 6 (BLIT saw)
  float tri = 0;
  static int typeOf(const std::string& s);
  float next(double freq);
};

// ---- the rendered-sound buffer (SndBuf) ---------------------------------------------------------------------
struct NoiseOpt { float att = 0.002f, curve = 1, sweep = 0; bool brown = false, two = false; };
struct GrainOpt { float front = 1, dMin = 0.004f, dMax = 0.014f; bool fade = false; char type = 'b'; };
struct SndBuf {
  int sr = SR;
  std::vector<float> d;
  Rng* r;
  SndBuf(double dur, Rng* rng) : d(std::max(1, (int)std::ceil(dur * SR)), 0.f), r(rng) {}
  void noise(double t0, double dur, char type, double f, double Q, double gain, const NoiseOpt& o = NoiseOpt());
  void grains(double t0, double span, int count, double fLo, double fHi, double Q, double gain, const GrainOpt& o = GrainOpt());
  struct Mode { double f, dec, a; };
  void modes(double t0, const std::vector<Mode>& list, double gain, double droop = 0);
  void thump(double t0, double f0, double f1, double dur, double gain);
  void pluck(double t0, double f, double dur, double gain, double damp = 0.996);
  void vox(double t0, double dur, double f0, double f1, const std::string& vowel, double gain, double breath = 0.3);
  void throat(double t0, double dur, const json& o);
  void hiss(double t0, double dur, const json& o);
  void clack(double t0, double f, double gain, double ring = 0.012);
  void room(double mix = 0.15, double size = 1);
  void shape(char type, double f, double q = 0.7);
  // the hand-written ones: a stick-slip hinge creak (sound3.js), a bow stave (bows.js), a rising pop (sfxlib.js)
  void creak(double t0, double dur, double f0, double gain);
  void bowDraw();
  void popSweep(double f0);
  void finish(float peak = 0.9f);
};

// Run one recorded recipe op (["noise", t0, dur, type, f, Q, gain, {opts}], ...) on a buffer.
void runOp(SndBuf& b, const json& op, const std::string& key);

// ---- live sounds (WebAudio graphs of game/audio.js and sounds2.js), rendered into a buffer -------------------
// ops: ["tone", delay, freq, dur, type, gain, slideTo] | ["nh", delay, dur, freq, q, gain, type]
//      | ["voice", {o}] | ["swell", {o}]; specials "cave" and "cave_scrape" are built in.
std::vector<float> renderLive(const json& ops, u32 seed);
std::vector<float> renderSpecial(const std::string& name, u32 seed);
// note blocks (audio.js note): n = 0..24, mat = the block underneath's sound
std::vector<float> renderNote(int n, const std::string& mat, u32 seed);

// ---- small pieces shared with the music, the mixer and the resynthesis -------------------------------------
void fft(std::vector<float>& re, std::vector<float>& im, bool inverse);   // radix-2, in place
struct Reverb {   // Freeverb-style: 8 combs + 4 allpasses per side, decay and damping settable
  void init(float size, float damp, int sr = SR);
  void process(float inL, float inR, float& outL, float& outR);
  struct Comb { std::vector<float> b; int p = 0; float lp = 0; };
  struct AP { std::vector<float> b; int p = 0; };
  Comb c[2][8]; AP a[2][4];
  float fb = 0.84f, damp = 0.2f, outGain = 1;   // outGain: about as loud as what goes in (like WebAudio's normalised convolver)
};

} // namespace dq::snd
