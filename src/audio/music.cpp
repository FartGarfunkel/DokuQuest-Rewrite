// audio/music.cpp — see music.h. composePiece / composePiece2 / battleBar / fanfare are line-by-line ports of
// game/music.js and game/music2.js; the instruments rebuild their WebAudio graphs (oscillators, filters,
// AudioParam ramps) as small live voices.
#include "audio/music.h"
#include "world/dims.h"

namespace dq::snd {

static constexpr double DPI = 3.14159265358979323846;
static double hz(double m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

int musicInsOf(const std::string& n) {
  static const char* names[MI_COUNT] = {"piano", "pad", "bell", "drone", "harp", "flute", "strings", "choir", "horn", "brass", "marimba", "kalimba",
    "musicbox", "celesta", "pluck", "clarinet", "cello", "taiko", "timpani", "snare", "hand", "frame", "shaker", "cymbal"};
  for (int i = 0; i < MI_COUNT; i++) if (n == names[i]) return i;
  return MI_PIANO;
}

// ==== the synth (audio thread) ===================================================================================
// One note: up to 5 oscillators (each with its own frequency ramp, detune, vibrato and gain ramp) into an optional
// filter (or the choir's three vowel bands), a gain ramp; and an optional burst of filtered noise beside it.
struct MV {
  int bus = 0; double t0 = 0, end = 0;
  struct O { Osc o; Param f; float cents = 0, mul = 1, vibRate = 0; Param vib; bool hasVib = false; Param pg; bool hasPg = false; double stop = 1e9; } o[5];
  int no = 0;
  Param g;
  int filt = 0;  // 0 none, 1 biquad, 2 choir formants
  std::string ftype; Param ff; float fq = 1; Biquad bq; Biquad fm[3];
  bool noise = false; Param ng; Biquad nb; double nEnd = 0; Rng nr{1};
};

struct MusicSynth::Impl {
  std::vector<AudioCmd> waiting;        // notes not started yet (by start time)
  std::vector<MV> voices;
  float bus[3] = {1, 0, 1}, busT[3] = {1, 0, 1}, busStep[3] = {0, 0, 0};
  Biquad tone; Reverb verb; float comp = 1;
  u32 seed = 1;
  void build(const AudioCmd& c, MV& v);
};

MusicSynth::MusicSynth() : m(std::make_unique<Impl>()) { m->tone.setWA("lowpass", 6500, 1); m->verb.init(2.4f, 0.3f); m->voices.reserve(128); m->waiting.reserve(512); }
MusicSynth::~MusicSynth() = default;

void MusicSynth::command(const AudioCmd& c) {
  if (c.t == AudioCmd::Note) { if (m->waiting.size() < 2000) m->waiting.push_back(c); }
  else if (c.t == AudioCmd::Bus) {
    int b = std::clamp((int)c.bus, 0, 2);
    m->busT[b] = c.a;
    m->busStep[b] = c.b <= 0 ? 1e9f : std::abs(c.a - m->bus[b]) / (c.b * SR);
  } else if (c.t == AudioCmd::MusicClear) {
    std::vector<AudioCmd> keep; for (auto& n : m->waiting) if (n.bus != c.bus) keep.push_back(n);
    m->waiting.swap(keep);
  }
}

// The instruments of music.js / music2.js (times in seconds from the note's start).
void MusicSynth::Impl::build(const AudioCmd& c, MV& v) {
  double mm = c.a, d = c.b, vel = c.c, f = hz(mm);
  v.bus = c.bus; v.t0 = c.at; v.nr = Rng(++seed * 2654435761u);
  auto osc = [&](int type, double fr, double stop, float cents = 0, float mul = 1) -> MV::O& {
    MV::O& o = v.o[v.no++]; o.o.type = type; o.f.init = (float)fr; o.f.set((float)fr, 0); o.cents = cents; o.mul = mul; o.stop = stop; o.o.ph = 0; return o;
  };
  auto vib = [&](MV::O& o, double rate, double depth, double delay) { o.hasVib = true; o.vibRate = (float)rate; o.vib.set(0, 0); o.vib.lin((float)depth, delay + 0.3); };
  auto filter = [&](const char* type, float q) { v.filt = 1; v.ftype = type; v.fq = q; };
  auto noise = [&](double dur, const char* type, double nf, double q, double gain, double att = 0.002) {
    v.noise = true; v.nb.setWA(type, nf, q); v.ng.set(0.0001f, 0); v.ng.lin((float)gain, att); v.ng.exp(0.0001f, dur); v.nEnd = dur + 0.05;
  };
  auto bar = [&](double m2, std::initializer_list<std::array<double, 3>> parts, double dec, double lvl) {
    double ff = hz(m2);
    v.g.set(0.0001f, 0); v.g.exp((float)(lvl * vel), 0.003); v.g.exp(0.0001f, dec);
    for (auto& p : parts) { if (ff * p[0] > 10000 || v.no >= 5) continue; auto& o = osc(0, ff * p[0], dec + 0.05); o.hasPg = true; o.pg.set((float)p[1], 0); o.pg.exp(0.0001f, dec * p[2]); }
    v.end = dec + 0.05;
  };
  auto strings = [&](double m2, double dd, double vv) {
    double ff = hz(m2); bool sh = dd < 0.35; double att = sh ? 0.02 : std::min(0.5, dd * 0.25), len = std::max(0.12, dd);
    filter("lowpass", 0.6f); v.ff.set(sh ? 2600 : 1700, 0);
    v.g.set(0.0001f, 0); v.g.lin((float)(0.022 * vv), att); v.g.set((float)(0.022 * vv), len); v.g.lin(0.0001f, len + (sh ? 0.08 : 0.5));
    v.end = len + (sh ? 0.12 : 0.6);
    for (float det : {-8.f, 0.f, 7.f}) { auto& o = osc(2, ff, v.end, det); if (!sh) vib(o, 5 + det * 0.03, ff * 0.004, 0.3); }
  };
  switch (c.ins) {
    case MI_PIANO: {   // a few slightly stretched partials fading at their own rates, and a felt-hammer start
      double decay = std::clamp(2.8 - (mm - 48) * 0.035, 0.7, 3.2), len = std::max(d, 0.4) + decay;
      v.g.set(0.0001f, 0); v.g.exp((float)(0.16 * vel), 0.006); v.g.exp((float)(0.06 * vel), 0.25); v.g.exp(0.0001f, len);
      const double parts[5][2] = {{1, 1}, {2.003, 0.42}, {3.009, 0.18}, {4.02, 0.08}, {5.04, 0.04}};
      for (auto& p : parts) { if (f * p[0] > 9000) continue; auto& o = osc(0, f * p[0], len + 0.05); o.hasPg = true; o.pg.set((float)p[1], 0); o.pg.exp((float)(p[1] * 0.05 + 0.0001), len / (p[0] * 0.8)); }
      v.end = len + 0.05; break;
    }
    case MI_PAD: {     // detuned saws through a slowly opening filter
      filter("lowpass", 0.5f); v.ff.set(400, 0); v.ff.lin((float)(900 + vel * 600), d * 0.5); v.ff.lin(500, d);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.022 * vel), std::min(2.5, d * 0.4)); v.g.set((float)(0.022 * vel), d * 0.7); v.g.lin(0.0001f, d + 1.5);
      osc(2, f, d + 1.6, -6); osc(2, f, d + 1.6, 5); osc(0, f / 2, d + 1.6, 0, 1.4f);
      v.end = d + 1.6; break;
    }
    case MI_BELL: {
      v.g.set(1, 0);
      const double parts[3][3] = {{1, 0.06, 2.2}, {2.76, 0.025, 1.1}, {5.4, 0.012, 0.6}};
      for (auto& p : parts) { auto& o = osc(0, f * p[0], p[2] + 0.05); o.hasPg = true; o.pg.set(0.0001f, 0); o.pg.exp((float)(p[1] * vel), 0.004); o.pg.exp(0.0001f, p[2]); }
      v.end = 2.25; break;
    }
    case MI_DRONE: {
      filter("lowpass", 1); v.ff.set(260, 0);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.03 * vel), 4); v.g.set((float)(0.03 * vel), d - 4); v.g.lin(0.0001f, d);
      osc(3, f, d + 0.1, 0); osc(3, f * 1.5, d + 0.1, 4); osc(3, f * 2, d + 0.1, -3);
      v.end = d + 0.1; break;
    }
    case MI_HARP: {    // bright when plucked, mellowing as it rings
      double dec = std::clamp(2.6 - (mm - 48) * 0.03, 0.9, 3.0);
      filter("lowpass", 1); v.ff.set(5200, 0); v.ff.exp(900, dec * 0.6);
      v.g.set(0.0001f, 0); v.g.exp((float)(0.11 * vel), 0.004); v.g.exp(0.0001f, dec);
      osc(3, f, dec + 0.05); osc(0, f * 2, dec * 0.5, 3);
      v.end = dec + 0.05; break;
    }
    case MI_FLUTE: {   // breathy, with a slow vibrato
      double len = std::max(0.25, d);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.06 * vel), 0.07); v.g.set((float)(0.055 * vel), len * 0.85); v.g.lin(0.0001f, len + 0.25);
      auto& a = osc(0, f, len + 0.3); vib(a, 5.2, f * 0.006, 0.2);
      auto& b = osc(3, f, len + 0.3, 0, 0.15f); vib(b, 5.2, f * 0.006, 0.2);
      noise(std::min(len, 0.4) + 0.2, "bandpass", f * 2, 1.4, 0.012 * vel, 0.05);
      v.end = len + 0.3; break;
    }
    case MI_STRINGS: strings(mm, d, vel); break;
    case MI_CELLO: strings(mm - 12, d, vel * 1.5); break;
    case MI_CHOIR: {   // saws singing "ah" through vowel resonances
      double len = std::max(0.6, d);
      v.filt = 2; v.fm[0].setWA("bandpass", 730, 9); v.fm[1].setWA("bandpass", 1090, 10); v.fm[2].setWA("bandpass", 2440, 12);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.05 * vel), std::min(0.8, len * 0.4)); v.g.set((float)(0.05 * vel), len); v.g.lin(0.0001f, len + 1.2);
      v.end = len + 1.3;
      for (float det : {-10.f, 9.f}) { auto& o = osc(2, f, v.end, det); vib(o, 4.6 + det * 0.02, f * 0.005, 0.4); }
      break;
    }
    case MI_HORN: {
      double len = std::max(0.3, d);
      filter("lowpass", 1); v.ff.set(350, 0); v.ff.lin(1500, 0.15); v.ff.lin(1100, len);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.05 * vel), 0.1); v.g.set((float)(0.045 * vel), len); v.g.lin(0.0001f, len + 0.35);
      v.end = len + 0.4; osc(2, f, v.end, -4); osc(2, f, v.end, 5); osc(0, f / 2, v.end);
      break;
    }
    case MI_BRASS: {
      double len = std::max(0.15, d);
      filter("lowpass", 1.2f); v.ff.set(500, 0); v.ff.lin(3400, 0.05); v.ff.exp(1500, len);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.05 * vel), 0.03); v.g.exp((float)(0.03 * vel), len); v.g.lin(0.0001f, len + 0.2);
      v.end = len + 0.25; osc(2, f, v.end, -6); osc(2, f, v.end, 6);
      break;
    }
    case MI_MARIMBA: bar(mm, {{{1, 1, 1}}, {{3.93, 0.35, 0.12}}, {{9.2, 0.08, 0.05}}}, 0.75, 0.16); break;
    case MI_KALIMBA: bar(mm, {{{1, 1, 1}}, {{5.4, 0.25, 0.1}}, {{2, 0.12, 0.4}}}, 1.1, 0.13); break;
    case MI_MUSICBOX: bar(mm + 12, {{{1, 1, 1}}, {{2.98, 0.3, 0.25}}, {{5.9, 0.1, 0.1}}}, 1.3, 0.07); break;
    case MI_CELESTA: bar(mm + 12, {{{1, 1, 1}}, {{4, 0.2, 0.2}}, {{2, 0.15, 0.5}}}, 1.6, 0.06); break;
    case MI_PLUCK: {   // an oud / lute: bright, then quickly dark
      double dec = std::clamp(1.6 - (mm - 48) * 0.02, 0.6, 1.8);
      filter("lowpass", 2); v.ff.set(3800, 0); v.ff.exp(500, 0.35);
      v.g.set(0.0001f, 0); v.g.exp((float)(0.075 * vel), 0.003); v.g.exp(0.0001f, dec);
      osc(2, f, dec + 0.05); osc(2, f, dec + 0.05, 9); v.end = dec + 0.05; break;
    }
    case MI_CLARINET: {
      double len = std::max(0.25, d);
      filter("lowpass", 1); v.ff.set(1300, 0);
      v.g.set(0.0001f, 0); v.g.lin((float)(0.035 * vel), 0.06); v.g.set((float)(0.032 * vel), len); v.g.lin(0.0001f, len + 0.2);
      auto& o = osc(1, f, len + 0.25); vib(o, 4.8, f * 0.004, 0.3); v.end = len + 0.25; break;
    }
    case MI_TAIKO: {
      v.g.set(0.0001f, 0); v.g.exp((float)(0.5 * vel), 0.004); v.g.exp(0.0001f, 0.55);
      auto& o = osc(0, 150, 0.6); o.f.exp(52, 0.22);
      noise(0.12, "lowpass", 700, 0.7, 0.22 * vel); v.end = 0.6; break;
    }
    case MI_TIMPANI: {
      v.g.set(0.0001f, 0); v.g.exp((float)(0.32 * vel), 0.006); v.g.exp(0.0001f, 1.3);
      auto& o = osc(0, f * 1.03, 1.35); o.f.exp((float)f, 0.1); osc(0, f * 1.5, 0.6);
      noise(0.08, "lowpass", 400, 0.7, 0.12 * vel); v.end = 1.35; break;
    }
    case MI_SNARE: {
      noise(0.14, "highpass", 1400, 0.7, 0.14 * vel);
      v.g.set((float)(0.12 * vel), 0); v.g.exp(0.0001f, 0.08); osc(3, 190, 0.1); v.end = 0.19; break;
    }
    case MI_HAND: {
      double hf = mm > 60 ? 240 : 130;
      v.g.set(0.0001f, 0); v.g.exp((float)(0.16 * vel), 0.003); v.g.exp(0.0001f, mm > 60 ? 0.12 : 0.28);
      auto& o = osc(0, hf * 1.4, 0.3); o.f.exp((float)hf, 0.05);
      if (mm > 60) noise(0.05, "bandpass", 2400, 1.5, 0.05 * vel);
      v.end = 0.3; break;
    }
    case MI_FRAME: {   // doum (low) and tek (high, dry)
      if (mm < 60) { v.g.set(0.0001f, 0); v.g.exp((float)(0.22 * vel), 0.004); v.g.exp(0.0001f, 0.32); auto& o = osc(0, 120, 0.35); o.f.exp(78, 0.12); v.end = 0.35; }
      else { v.g.set(0, 0); noise(0.06, "bandpass", 2600, 1.8, 0.09 * vel); v.end = 0.11; }
      break;
    }
    case MI_SHAKER: v.g.set(0, 0); noise(0.07, "highpass", 6500, 0.7, 0.035 * vel, 0.012); v.end = 0.12; break;
    case MI_CYMBAL: {
      v.g.set(0, 0); v.noise = true; v.nb.setWA("highpass", 5200, 1);
      v.ng.set(0.0001f, 0); v.ng.exp((float)(0.06 * vel), std::max(0.01, d)); v.ng.exp(0.0001f, d + 1.4); v.nEnd = d + 1.5; v.end = d + 1.5; break;
    }
    default: v.end = 0;
  }
  v.end = std::max(v.end, v.noise ? v.nEnd : 0.0);
}

void MusicSynth::render(float* out, int n, double t0, float volume) {
  Impl& s = *m;
  // start the notes whose time has come
  double t1 = t0 + (double)n / SR;
  for (size_t i = 0; i < s.waiting.size();) {
    if (s.waiting[i].at < t1) {
      if (s.voices.size() < 160) { s.voices.emplace_back(); s.build(s.waiting[i], s.voices.back()); if (s.voices.back().end <= 0) s.voices.pop_back(); }
      s.waiting[i] = s.waiting.back(); s.waiting.pop_back();
    } else i++;
  }
  bool busy = !s.voices.empty();
  static thread_local std::vector<float> mono[3];
  for (auto& b : mono) b.assign(n, 0.f);
  constexpr int BLK = 16;
  for (auto& v : s.voices) {
    std::vector<float>& dst = mono[std::clamp(v.bus, 0, 2)];
    for (int b0 = 0; b0 < n; b0 += BLK) {
      int b1 = std::min(n, b0 + BLK);
      double ta = t0 + (double)b0 / SR - v.t0, tb = t0 + (double)b1 / SR - v.t0;
      if (tb <= 0) continue;
      if (ta > v.end) break;
      double tm = std::max(0.0, ta);
      float ga = v.g.at(tm), gb = v.g.at(std::max(0.0, tb)), gstep = (gb - ga) / (b1 - b0);
      if (v.filt == 1) v.bq.setWA(v.ftype, v.ff.at(tm), v.fq);
      float fr[5], pa[5], pstep[5], vb[5];
      for (int k = 0; k < v.no; k++) {
        auto& o = v.o[k];
        fr[k] = o.f.at(tm) * std::pow(2.f, o.cents / 1200.f);
        vb[k] = o.hasVib ? o.vib.at(tm) : 0;
        pa[k] = o.hasPg ? o.pg.at(tm) : 1; float pe = o.hasPg ? o.pg.at(std::max(0.0, tb)) : 1; pstep[k] = (pe - pa[k]) / (b1 - b0);
      }
      float na = 0, nstep = 0;
      if (v.noise && tm < v.nEnd) { na = v.ng.at(tm); nstep = (v.ng.at(std::max(0.0, tb)) - na) / (b1 - b0); }
      float g = ga;
      for (int i = b0; i < b1; i++) {
        double t = t0 + (double)i / SR - v.t0;
        if (t < 0) { g += gstep; for (int k = 0; k < v.no; k++) pa[k] += pstep[k]; na += nstep; continue; }
        float x = 0;
        for (int k = 0; k < v.no; k++) {
          auto& o = v.o[k];
          if (t < o.stop) {
            double ff = fr[k];
            if (o.hasVib) ff += std::sin(2 * DPI * o.vibRate * t) * vb[k];
            x += o.o.next(ff) * o.mul * pa[k];
          }
          pa[k] += pstep[k];
        }
        if (v.filt == 1) x = v.bq.run(x);
        else if (v.filt == 2) x = (v.fm[0].run(x) + v.fm[1].run(x) * 0.5f + v.fm[2].run(x) * 0.25f) * 3;
        float y = x * g; g += gstep;
        if (v.noise && t < v.nEnd) y += v.nb.run(v.nr.pm()) * na;
        na += nstep;
        dst[i] += y;
      }
    }
  }
  s.voices.erase(std::remove_if(s.voices.begin(), s.voices.end(), [&](const MV& v) { return t1 - v.t0 > v.end; }), s.voices.end());
  // the bus: three mixes (quiet pieces, battle, fanfare) -> tone -> dry + hall -> volume -> a gentle limiter
  float vol = volume * 0.55f;
  for (int i = 0; i < n; i++) {
    for (int b = 0; b < 3; b++) { if (s.bus[b] < s.busT[b]) s.bus[b] = std::min(s.busT[b], s.bus[b] + s.busStep[b]); else if (s.bus[b] > s.busT[b]) s.bus[b] = std::max(s.busT[b], s.bus[b] - s.busStep[b]); }
    if (!busy && vol <= 0) continue;
    float x = s.tone.run(mono[0][i] * s.bus[0] + mono[1][i] * s.bus[1] + mono[2][i] * s.bus[2]);
    float wl, wr; s.verb.process(x, x, wl, wr);
    float L = (x * 0.72f + wl * 0.5f) * vol, R = (x * 0.72f + wr * 0.5f) * vol;
    // compressor: threshold -16 dB, ratio 4
    float lev = std::max(std::abs(L), std::abs(R)), th = 0.158f;
    float want = lev > th ? std::pow(lev / th, -0.75f) : 1.f;
    s.comp += (want - s.comp) * (want < s.comp ? 0.0021f : 0.00007f);
    out[2 * i] += L * s.comp; out[2 * i + 1] += R * s.comp;
  }
}

// ==== composing (game thread) =====================================================================================
static const std::map<std::string, std::vector<int>>& modes() {
  static const std::map<std::string, std::vector<int>> M = {
    {"major", {0, 2, 4, 5, 7, 9, 11}}, {"lydian", {0, 2, 4, 6, 7, 9, 11}}, {"mixolydian", {0, 2, 4, 5, 7, 9, 10}}, {"dorian", {0, 2, 3, 5, 7, 9, 10}},
    {"aeolian", {0, 2, 3, 5, 7, 8, 10}}, {"phrygian", {0, 1, 3, 5, 7, 8, 10}}, {"harmonic", {0, 2, 3, 5, 7, 8, 11}}, {"hijaz", {0, 1, 4, 5, 7, 8, 10}},
    {"penta", {0, 2, 4, 7, 9, 12, 14}}, {"minpenta", {0, 3, 5, 7, 10, 12, 15}}, {"whole", {0, 2, 4, 6, 8, 10, 12}}};
  return M;
}
struct Mood {
  std::vector<std::string> modes; std::vector<int> roots; float tempo[2]; float density; int reg; std::vector<std::vector<int>> progs; float bell;
  bool drone = false, arp = false; std::string lead, lead2, lh, padIns, perc, bellIns;
};
static const std::map<std::string, Mood>& moods() {
  static const std::map<std::string, Mood> M = {
    // music.js
    {"day", {{"major", "lydian", "mixolydian"}, {48, 50, 53, 55}, {62, 76}, 0.62f, 0, {{0, 4, 5, 3}, {0, 5, 3, 4}, {3, 0, 4, 5}, {0, 3, 5, 4}, {0, 2, 3, 4}}, 0}},
    {"night", {{"dorian", "aeolian"}, {45, 47, 50, 52}, {52, 64}, 0.45f, -2, {{0, 5, 3, 4}, {0, 3, 6, 4}, {0, 6, 5, 4}, {0, 4, 5, 3}}, 0.35f}},
    {"cave", {{"phrygian", "aeolian", "dorian"}, {40, 43, 45}, {46, 56}, 0.3f, -5, {{0, 1, 0, 6}, {0, 5, 6, 0}, {0, 3, 1, 0}}, 0.2f, true}},
    {"sky", {{"lydian", "major"}, {53, 55, 57}, {60, 70}, 0.55f, 5, {{0, 1, 4, 0}, {0, 4, 1, 5}}, 0.6f}},
    {"depths", {{"harmonic", "phrygian"}, {38, 40, 41}, {48, 58}, 0.35f, -5, {{0, 1, 0, 4}, {0, 5, 1, 4}}, 0, true}},
    {"menu", {{"major", "dorian", "lydian"}, {48, 50, 52}, {58, 68}, 0.55f, 0, {{0, 5, 3, 4}, {0, 3, 0, 4}, {5, 3, 0, 4}}, 0.25f}},
    {"creative", {{"lydian", "major"}, {50, 52, 55}, {66, 80}, 0.75f, 3, {{0, 4, 5, 3}, {3, 4, 0, 5}, {0, 1, 4, 3}}, 0.4f}},
    // music2.js
    {"plains", {{"major", "lydian", "mixolydian"}, {50, 52, 53, 55}, {66, 80}, 0.6f, 2, {{0, 4, 5, 3}, {0, 3, 4, 0}, {0, 5, 3, 4}, {3, 4, 0, 0}}, 0.12f, false, false, "flute", "piano", "harp", "strings"}},
    {"forest", {{"dorian", "mixolydian", "major"}, {48, 50, 52}, {58, 70}, 0.55f, 0, {{0, 6, 3, 4}, {0, 3, 0, 4}, {0, 5, 6, 4}}, 0.2f, false, false, "piano", "flute", "harp", "strings"}},
    {"jungle", {{"penta", "mixolydian"}, {50, 53, 55}, {84, 100}, 0.7f, 2, {{0, 4, 3, 4}, {0, 3, 4, 3}, {0, 5, 3, 4}}, 0, false, false, "marimba", "kalimba", "kalimba", "pad", "hand"}},
    {"desert", {{"hijaz", "phrygian"}, {50, 52}, {70, 84}, 0.55f, 0, {{0, 1, 0, 6}, {0, 6, 1, 0}, {0, 3, 1, 0}}, 0, true, false, "pluck", "flute", "pluck", "pad", "frame"}},
    {"savanna", {{"penta", "minpenta"}, {48, 50, 53}, {64, 76}, 0.55f, 0, {{0, 3, 4, 3}, {0, 4, 3, 0}}, 0, false, false, "kalimba", "flute", "pluck", "pad", "shaker"}},
    {"frost", {{"lydian", "aeolian", "dorian"}, {52, 55, 57}, {50, 60}, 0.4f, 5, {{0, 5, 3, 4}, {0, 1, 4, 0}, {0, 3, 5, 4}}, 0.55f, false, false, "musicbox", "piano", "piano", "strings", "", "celesta"}},
    {"peaks", {{"dorian", "mixolydian", "aeolian"}, {45, 48, 50}, {54, 64}, 0.45f, 0, {{0, 6, 0, 4}, {0, 3, 6, 4}, {0, 4, 3, 0}}, 0.1f, true, false, "horn", "flute", "harp", "strings"}},
    {"swamp", {{"dorian", "aeolian"}, {43, 45, 48}, {52, 62}, 0.45f, -3, {{0, 6, 0, 4}, {0, 3, 1, 0}}, 0, true, false, "clarinet", "", "pluck", "pad", "hand"}},
    {"ocean", {{"lydian", "major", "mixolydian"}, {50, 53, 55}, {56, 68}, 0.5f, 2, {{0, 4, 1, 4}, {0, 3, 4, 0}, {0, 5, 3, 4}}, 0.3f, false, true, "flute", "piano", "harp", "choir"}},
    {"village", {{"major", "mixolydian"}, {50, 52, 55}, {76, 92}, 0.72f, 2, {{0, 3, 4, 0}, {0, 4, 5, 3}, {0, 3, 0, 4}}, 0.1f, false, false, "pluck", "flute", "harp", "pad", "shaker"}},
    {"deep", {{"lydian", "dorian"}, {43, 45, 48}, {44, 52}, 0.3f, -2, {{0, 1, 0, 4}, {0, 5, 3, 0}}, 0.45f, true, false, "musicbox", "", "pad", "choir", "", "musicbox"}},
    {"sky2", {{"lydian", "major"}, {53, 55, 57}, {60, 72}, 0.55f, 5, {{0, 1, 4, 0}, {0, 4, 1, 5}, {0, 5, 3, 4}}, 0.5f, false, true, "flute", "harp", "harp", "choir"}},
    {"depths2", {{"harmonic", "phrygian", "hijaz"}, {38, 40, 41}, {48, 58}, 0.4f, -5, {{0, 1, 0, 4}, {0, 5, 1, 4}}, 0, true, false, "cello", "clarinet", "pluck", "choir", "frame"}},
    {"cave2", {{"phrygian", "aeolian", "whole"}, {40, 43, 45}, {46, 56}, 0.3f, -4, {{0, 1, 0, 6}, {0, 5, 6, 0}}, 0.35f, true, false, "piano", "musicbox", "harp", "strings", "", "musicbox"}},
  };
  return M;
}
// night: the same instruments, darker and slower (musicNight)
static Mood night(Mood M) {
  bool hij = std::find(M.modes.begin(), M.modes.end(), "hijaz") != M.modes.end();
  M.modes = hij ? std::vector<std::string>{"hijaz", "phrygian"} : std::vector<std::string>{"dorian", "aeolian"};
  M.tempo[0] *= 0.85f; M.tempo[1] *= 0.85f; M.density *= 0.8f; M.bell = std::max(M.bell, 0.3f);
  if (M.perc == "shaker") M.perc = "";
  M.reg -= 2;
  return M;
}

struct Composer {
  Rng r; const Mood& M; std::vector<int> mode; int root; double beat; std::vector<MusicNote> notes;
  Composer(const Mood& m, u32 seed) : r(seed), M(m) {}
  template <class T> T pick(const std::vector<T>& a) { return a[std::min(a.size() - 1, (size_t)std::floor(r() * a.size()))]; }   // by value: callers pass temporaries
  int deg(int d, int oct = 0) { int o = (int)std::floor(d / 7.0); return root + mode[((d % 7) + 7) % 7] + 12 * (o + oct); }
  void add(double bt, const std::string& ins, double m, double d, double v) { notes.push_back({bt * beat, (u8)musicInsOf(ins), (float)m, (float)(d * beat), (float)v}); }
  void start() {
    mode = modes().at(pick(M.modes)); root = pick(M.roots) + M.reg;
    double bpm = M.tempo[0] + r() * (M.tempo[1] - M.tempo[0]); beat = 60 / bpm;
  }
  struct Step { double s, len; bool rest; };
  std::vector<Step> motif(double restP, bool eight) {
    std::vector<std::vector<double>> rh = {{2, 1, 1}, {1.5, 0.5, 2}, {1, 1, 2}, {3, 1}, {1, 0.5, 0.5, 2}, {2, 2}, {1, 1, 1, 1}};
    if (eight) rh.push_back({0.5, 0.5, 1, 2});
    std::vector<Step> mo; double st = 0;
    for (double len : pick(rh)) { mo.push_back({st, len, r() < restP}); st += pick(std::vector<int>{-2, -1, -1, 1, 1, 2, 0, 3}); }
    return mo;
  }
  // music.js composePiece
  void piece1() {
    start();
    const auto& prog = pick(M.progs);
    struct Sec { int bars; bool mel; double lh; bool vary = false; int up = 0; bool end = false; };
    std::vector<Sec> secs = {{2, false, 0.3}, {8, true, 1}};
    Sec s3{8, true, 1, true}; s3.up = r() < 0.4 ? 7 : 0; secs.push_back(s3);
    Sec s4{4, r() < 0.5, 0.5}; s4.end = true; secs.push_back(s4);
    auto mo = motif(0.12, false);
    double t = 0; int bar = 0;
    for (auto& S : secs) {
      for (int b = 0; b < S.bars; b++, bar++) {
        int pc = prog[(bar / 2) % prog.size()]; bool last = S.end && b == S.bars - 1;
        int ch = pc + (last ? -pc : 0);
        double barT = t + b * 4;
        if (bar % 2 == 0 || last) for (int k : {0, 2, 4}) add(barT, "pad", deg(ch + k, -1), last ? 6 : 8, 0.7 + r() * 0.3);
        if (M.drone && bar % 4 == 0) add(barT, "drone", root - 12, 16, 0.8);
        if (r() < 0.85 * S.lh) {
          const auto& pat = pick(std::vector<std::vector<int>>{{0, 4, 7, 4}, {0, 2, 4, 7}, {0, 4, 9, 7}, {0, 7, 4, 2}});
          for (int i = 0; i < 4; i++) if (r() < 0.82) { int off = pat[i] == 0 ? 0 : pat[i] == 2 ? 2 : pat[i] == 4 ? 4 : pat[i] == 7 ? 6 : 7; add(barT + i, "piano", deg(ch + off, -1), 1.5, 0.45 + r() * 0.2); }
        }
        if (S.mel && (b % 2 == 0 || r() < M.density * 0.6) && r() < M.density + 0.3) {
          double at = barT + (r() < 0.5 ? 0 : pick(std::vector<double>{0.5, 1, 2}));
          int st = ch + pick(std::vector<int>{0, 2, 4}) + 7 + S.up;
          for (auto& n : mo) {
            int vary = S.vary && r() < 0.35 ? pick(std::vector<int>{-1, 1, 2}) : 0;
            if (!n.rest && at < barT + 4.5) add(at, "piano", deg(st + (int)n.s + vary), n.len, 0.7 + r() * 0.25);
            at += n.len;
          }
        }
        if (M.bell > 0 && r() < M.bell * 0.5) { double bt = barT + pick(std::vector<double>{1, 2, 3, 3.5}); add(bt, "bell", deg(ch + pick(std::vector<int>{0, 2, 4}), 2), 1, 0.6); }
      }
      t += S.bars * 4;
    }
    add(t, "piano", deg(0, -1), 4, 0.5); add(t + 0.02, "piano", deg(4, -1), 4, 0.4); add(t + 0.04, "piano", deg(0), 4, 0.45);
  }
  // music2.js composePiece2
  void piece2() {
    start();
    const auto& prog = pick(M.progs); const auto& progB = pick(M.progs);
    std::string lead = M.lead.empty() ? "piano" : M.lead, lead2 = M.lead2.empty() ? lead : M.lead2, lh = M.lh.empty() ? "piano" : M.lh, padI = M.padIns.empty() ? "pad" : M.padIns;
    struct Sec { int bars; bool mel; double lh; bool vary = false; int up = 0; bool end = false, B = false; std::string ins; };
    std::vector<Sec> secs;
    secs.push_back({2, false, 0.4}); secs.push_back({8, true, 1, false, 0, false, false, lead});
    { Sec s{8, true, 1, true}; s.up = r() < 0.4 ? 7 : 0; s.ins = lead2; secs.push_back(s); }
    { Sec s{8, r() < 0.7, 0.8}; s.B = true; s.ins = lead2; secs.push_back(s); }
    secs.push_back({8, true, 1, true, 0, false, false, lead});
    { Sec s{4, r() < 0.5, 0.5}; s.end = true; s.ins = lead; secs.push_back(s); }
    auto mo = motif(0.1, true), moB = motif(0.1, true);
    double t = 0; int bar = 0;
    for (auto& S : secs) {
      const auto& P = S.B ? progB : prog;
      for (int b = 0; b < S.bars; b++, bar++) {
        bool last = S.end && b == S.bars - 1;
        int ch = last ? 0 : P[(b / 2) % P.size()];
        double barT = t + b * 4;
        if (b % 2 == 0 || last) for (int k : {0, 2, 4}) add(barT, padI, deg(ch + k, -1), last ? 6 : 8, 0.65 + r() * 0.25);
        if (M.drone && bar % 4 == 0) add(barT, "drone", root - 12, 16, 0.8);
        if (r() < 0.88 * S.lh) {
          if (M.arp || (lh == "harp" && r() < 0.5)) { const int up[8] = {0, 2, 4, 7, 9, 7, 4, 2}; for (int i = 0; i < 8; i++) if (r() < 0.9) add(barT + i * 0.5, lh, deg(ch + up[i], -1), 1.2, 0.4 + r() * 0.15); }
          else {
            const auto& pat = pick(std::vector<std::vector<int>>{{0, 4, 7, 4}, {0, 2, 4, 7}, {0, 4, 9, 7}, {0, 7, 4, 2}});
            for (int i = 0; i < 4; i++) if (r() < 0.82) { int off = pat[i] == 0 ? 0 : pat[i] == 2 ? 2 : pat[i] == 4 ? 4 : pat[i] == 7 ? 4 : 5; add(barT + i, lh, deg(ch + off, -1) + (pat[i] == 7 ? 12 : 0), 1.5, 0.42 + r() * 0.2); }
          }
        }
        if (!M.perc.empty() && S.lh >= 0.8 && !last) {
          if (M.perc == "frame") { const double P5[5][3] = {{0, 0, 0.9}, {1, 1, 0.5}, {1.5, 1, 0.4}, {2, 0, 0.7}, {3, 1, 0.5}}; for (auto& p : P5) add(barT + p[0], "frame", p[1] ? 72 : 48, 0.2, p[2]); }
          else if (M.perc == "hand") { const double P6[6][3] = {{0, 0, 0.8}, {0.75, 1, 0.4}, {1.5, 1, 0.5}, {2, 0, 0.6}, {2.5, 1, 0.4}, {3.5, 1, 0.45}}; for (auto& p : P6) add(barT + p[0], "hand", p[1] ? 72 : 48, 0.2, p[2] * (0.85 + r() * 0.3)); }
          else if (M.perc == "shaker") for (int i = 0; i < 8; i++) add(barT + i * 0.5, "shaker", 0, 0.1, i % 2 ? 0.5 : 0.8);
        }
        const auto& m2 = S.B ? moB : mo;
        if (S.mel && (b % 2 == 0 || r() < M.density * 0.6) && r() < M.density + 0.3) {
          double at = barT + (r() < 0.5 ? 0 : pick(std::vector<double>{0.5, 1, 2}));
          int st = ch + pick(std::vector<int>{0, 2, 4}) + 7 + S.up;
          for (auto& n : m2) {
            int vary = S.vary && r() < 0.35 ? pick(std::vector<int>{-1, 1, 2}) : 0;
            if (!n.rest && at < barT + 4.5) add(at, S.ins.empty() ? lead : S.ins, deg(st + (int)n.s + vary), n.len, 0.7 + r() * 0.25);
            at += n.len;
          }
        }
        if (M.bell > 0 && r() < M.bell * 0.5) { double bt = barT + pick(std::vector<double>{1, 2, 3, 3.5}); add(bt, M.bellIns.empty() ? "bell" : M.bellIns, deg(ch + pick(std::vector<int>{0, 2, 4}), 2), 1, 0.6); }
      }
      t += S.bars * 4;
    }
    add(t, lh, deg(0, -1), 4, 0.5); add(t + 0.03, lh, deg(4, -1), 4, 0.4); add(t + 0.06, lead == "horn" ? "harp" : lead, deg(0), 4, 0.45);
  }
};

std::vector<MusicNote> composeMusic(const std::string& moodIn, u32 seed) {
  auto colon = moodIn.find(':');
  std::string k = moodIn.substr(0, colon);
  auto it = moods().find(k); if (it == moods().end()) it = moods().find("day");
  Mood M = colon != std::string::npos ? night(it->second) : it->second;
  Composer c(M, seed);
  if (M.lead.empty() && M.lh.empty() && M.perc.empty() && M.padIns.empty()) c.piece1(); else c.piece2();
  std::stable_sort(c.notes.begin(), c.notes.end(), [](const MusicNote& a, const MusicNote& b) { return a.t < b.t; });
  return c.notes;
}

// ==== the director (game thread): when to play, what, and the battle ===============================================
struct MusicDirector::Impl {
  AudioEngine::Impl& e;
  std::mt19937 rng{(u32)std::random_device{}()};
  float rnd() { return std::uniform_real_distribution<float>(0, 1)(rng); }
  std::vector<MusicNote> notes; size_t ni = 0; double start = 0, end = 0;
  double wait; bool playing = false, menuDone = false; double inWorldT = 0;
  struct Battle { bool on = false, dragon = false; double bpm = 140; int root = 50; std::vector<std::vector<int>> progs; std::vector<int> prog{0, 5, 3, 4};
    std::vector<std::pair<double, double>> motif; int bar = 0; double next = 0, lost = 0; float hp = 1; } battle;
  explicit Impl(AudioEngine::Impl& en) : e(en) { wait = 25 + rnd() * 30; }

  void note(double at, int ins, double m, double d, double v, int bus) {
    AudioCmd c; c.t = AudioCmd::Note; c.at = at; c.ins = (u8)ins; c.a = (float)m; c.b = (float)d; c.c = (float)v; c.bus = (u8)bus; e.send(c);
  }
  void busTo(int bus, float target, float ramp) { AudioCmd c; c.t = AudioCmd::Bus; c.bus = (u8)bus; c.a = target; c.b = ramp; e.send(c); }
  void stop() {
    notes.clear(); ni = 0; playing = false;
    AudioCmd c; c.t = AudioCmd::MusicClear; c.bus = 0; e.send(c);
    busTo(0, 0, 2);
  }
  void schedule() {
    double horizon = e.audioNow() + 2.5;
    while (ni < notes.size() && start + notes[ni].t < horizon) {
      auto& n = notes[ni++];
      note(std::max(e.audioNow() + 0.02, start + n.t), n.ins, n.m, n.d, n.v, 0);
    }
  }
  std::string moodNow(const AudioScene& s) {
    if (!s.inWorld) return "menu";
    if (s.dim == DIM_NETHER) return rnd() < 0.5 ? "depths" : "depths2";
    if (s.dim == DIM_AETHER) return rnd() < 0.35 ? "sky" : "sky2";
    if (s.dim == DIM_END) return "cave2";
    if (s.eyeWater) return "deep";
    int localY = (int)std::floor(s.head.y) - dimBase(DIM_OVERWORLD);
    if (s.skyAtHead < 4 && localY < 64 - 4) return rnd() < 0.5 ? "cave" : "cave2";
    if (s.creative && rnd() < 0.25) return "creative";
    std::string m = s.place.empty() ? "plains" : s.place;
    return s.day < 0.45f ? m + ":night" : m;
  }
  void play(const std::string& mood) {
    busTo(0, 1, 0);
    notes = composeMusic(mood, (u32)(rnd() * 1e9));
    ni = 0; start = e.audioNow() + 0.3; playing = true;
    end = start + (notes.empty() ? 1 : notes.back().t + 6);
    schedule();
  }
  // ---- the battle (music2.js) ----
  void startBattle(const AudioScene& s) {
    if (playing) stop();
    Rng r((u32)(rnd() * 1e9));
    auto pick = [&](const auto& a) { auto v = a[std::min(a.size() - 1, (size_t)std::floor(r() * a.size()))]; return v; };
    battle = Battle(); battle.on = true;
    std::vector<std::vector<double>> rh = {{1, 0.5, 0.5, 1, 1}, {1.5, 0.5, 1, 1}, {0.5, 0.5, 1, 0.5, 0.5, 1}, {2, 1, 1}};
    double st = 0; for (double len : pick(rh)) { battle.motif.push_back({st, len}); st += pick(std::vector<int>{-1, 1, 2, -2, 1, 3}); }
    bool eerie = s.bossEerie, dragon = s.bossDragon;
    battle.dragon = dragon || eerie;
    battle.bpm = eerie ? 132 : dragon ? 124 : 142 + r() * 8;
    battle.root = eerie ? 45 : dragon ? 50 : pick(std::vector<int>{45, 50, 52});
    battle.progs = eerie ? std::vector<std::vector<int>>{{0, 1, 0, 4}, {0, 5, 1, 4}, {0, 1, 6, 4}} : std::vector<std::vector<int>>{{0, 5, 3, 4}, {0, 3, 5, 4}, {0, 5, 6, 4}, {0, 1, 0, 4}};
    battle.next = e.audioNow() + 0.6;
    busTo(1, 1, 1.5);
  }
  void endBattle(bool won) {
    if (!battle.on) return;
    Battle b = battle; battle.on = false;
    double now = e.audioNow();
    busTo(1, 0, won ? 0.8f : 4.f);
    wait = won ? 50 + rnd() * 40 : 25 + rnd() * 20;
    if (won) fanfare(b, now + 0.6);
  }
  void fanfare(const Battle& b, double t) {
    double beat = 60.0 / 120; int root = b.root + 3;
    auto at = [&](double k) { return t + k * beat; };
    const int up[4] = {0, 4, 7, 12};
    for (int i = 0; i < 4; i++) { note(at(i * 0.5), MI_BRASS, root + up[i], 0.45 * beat, 0.9, 2); note(at(i * 0.5), MI_BRASS, root + up[i] - 12, 0.45 * beat, 0.6, 2); }
    for (int s : {0, 4, 7, 12, 16}) note(at(2), MI_BRASS, root + s, 3.5 * beat, 0.8, 2);
    for (int s : {0, 4, 7}) note(at(2), MI_CHOIR, root + s, 5 * beat, 0.9, 2);
    for (int i = 0; i < 12; i++) note(at(i * 0.125), MI_TIMPANI, root - 12, 0.2, 0.3 + i * 0.05, 2);
    note(at(2), MI_TIMPANI, root - 12, 1, 1, 2); note(at(1.6), MI_CYMBAL, 0, 0.4, 1, 2);
    note(at(2), MI_STRINGS, root - 12, 4 * beat, 1, 2);
  }
  void battleBar(Battle& b, double t0) {
    double beat = 60 / b.bpm; const auto& mode = modes().at("harmonic"); int root = b.root;
    auto deg = [&](int d, int oct = 0) { int k = (int)std::floor(d / 7.0); return root + mode[((d % 7) + 7) % 7] + 12 * (k + oct); };
    int bar = b.bar, fierce = b.hp < 0.5f ? 2 : 1; bool intro = bar < 2;
    if (bar % 8 == 0) b.prog = b.progs[(size_t)(rnd() * b.progs.size()) % b.progs.size()];
    int ch = b.prog[(bar / 2) % b.prog.size()];
    auto T = [&](double x) { return t0 + x * beat; };
    for (int i = 0; i < 8; i++) note(T(i * 0.5), MI_STRINGS, deg(ch, -1) + (i % 4 == 3 ? 12 : 0), 0.42 * beat, i % 4 == 0 ? 1 : 0.7, 1);
    auto drum = [&](double x, double v) { if (b.dragon) note(T(x), MI_TIMPANI, deg(0, -2) + (x == 2 ? 7 : 0), 0.3, v, 1); else note(T(x), MI_TAIKO, 0, 0.3, v, 1); };
    for (auto [x, v] : {std::pair{0.0, 1.0}, {1.5, 0.6}, {2.0, 0.85}, {3.0, 0.6}}) drum(x, v);
    if (fierce > 1 || bar % 2 == 1) for (auto [x, v] : {std::pair{0.75, 0.5}, {2.5, 0.55}, {3.5, 0.6}}) drum(x, v);
    if (bar % 4 == 3) for (int i = 0; i < 8; i++) note(T(3 + i * 0.125), MI_SNARE, 0, 0.1, 0.3 + i * 0.08, 1);
    else for (double x : {1.0, 3.0}) note(T(x), MI_SNARE, 0, 0.1, 0.45, 1);
    if (bar % 8 == 7) note(T(2), MI_CYMBAL, 0, 2 * beat, 1, 1);
    if (intro) { b.bar++; return; }
    if (bar % 2 == 0) {
      for (int k : {0, 2, 4}) note(T(0), MI_BRASS, deg(ch + k), 1.3 * beat, 0.8, 1);
      for (int k : {0, 2, 4}) note(T(0), MI_CHOIR, deg(ch + k), 7.5 * beat, b.dragon ? 1 : 0.7, 1);
    }
    if (fierce > 1) for (int k : {0, 2, 4}) note(T(2.5), MI_BRASS, deg(ch + k), 0.4 * beat, 0.6, 1);
    int ph = bar % 16;
    if ((ph >= 4 && ph < 8) || (ph >= 12 && ph < 16) || (fierce > 1 && ph >= 8 && ph < 12)) {
      double at = 0;
      for (auto& [s, len] : b.motif) {
        if (at < 4) { int m = deg(ch + 7 + (int)s + (ph >= 12 ? 2 : 0)); if (b.dragon) note(T(at), MI_HORN, m, len * beat, 0.9, 1); else note(T(at), MI_STRINGS, m + 12, len * beat, 0.9, 1); }
        at += len;
      }
    }
    b.bar++;
  }
};

MusicDirector::MusicDirector(AudioEngine::Impl& e) : m(std::make_unique<Impl>(e)) {}
MusicDirector::~MusicDirector() = default;

void MusicDirector::update(double dt, const AudioScene& s) {
  Impl& d = *m;
  float vol = d.e.vMusic.load();
  if (vol <= 0) { if (d.battle.on) d.endBattle(false); if (d.playing) d.stop(); return; }
  double now = d.e.audioNow();
  bool boss = s.inWorld && s.boss;
  if (boss && !d.battle.on && !s.jukebox) d.startBattle(s);
  if (d.battle.on) {
    auto& b = d.battle;
    if (boss) { b.lost = 0; b.hp = s.bossHp; }
    if (boss && s.bossHp <= 0) { d.endBattle(true); return; }
    if (!boss) { b.lost += dt; if (b.lost > 6 || !s.inWorld) { d.endBattle(false); return; } }
    if (b.next < now) b.next = now + 0.05;
    while (b.next < now + 1.2) { d.battleBar(b, b.next); b.next += 4 * 60 / b.bpm; }
    return;
  }
  // music.js update, with music2.js's shorter quiet spells
  bool wasPlaying = d.playing;
  if (s.jukebox && d.playing) d.stop();
  if (d.playing) {
    d.schedule();
    if (now > d.end) { d.playing = false; d.wait = s.inWorld ? 180 + d.rnd() * 300 : 90 + d.rnd() * 60; }
    if (wasPlaying && !d.playing && s.inWorld) d.wait = std::min(d.wait, 50.0 + d.rnd() * 80);
    return;
  }
  if (s.jukebox) return;
  if (!s.inWorld) {
    if (!d.menuDone) { d.wait = std::min(d.wait, 2.0); d.wait -= dt; if (d.wait <= 0) { d.menuDone = true; d.play("menu"); } }
    d.inWorldT = 0;
    return;
  }
  if (d.inWorldT == 0) d.wait = 40 + d.rnd() * 50;
  d.inWorldT += dt;
  if (d.inWorldT > 0 && d.inWorldT < dt * 1.5) d.wait = std::min(d.wait, 15.0 + d.rnd() * 20);
  if (s.paused) return;
  d.wait -= dt;
  if (d.wait <= 0) d.play(d.moodNow(s));
}

} // namespace dq::snd
