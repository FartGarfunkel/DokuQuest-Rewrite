// audio/song.cpp — songs: JSON, Standard MIDI Files, reading a recording by ear, and our own composer (audio/song.h).
#include "audio/song.h"
#include "audio/dsp.h"
#include "common/fs.h"
#include <miniaudio.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace dq::snd {

static const char* INS[MI_COUNT] = {"piano", "pad", "bell", "drone", "harp", "flute", "strings", "choir", "horn", "brass", "marimba", "kalimba",
  "musicbox", "celesta", "pluck", "clarinet", "cello", "taiko", "timpani", "snare", "hand", "frame", "shaker", "cymbal", "dulcimer", "bass", "ubass", "guitar",
  "bombard", "tremolo", "mandolin", "dulcbow", "bandora", "sawfiddle", "fiddle", "strumbo", "zeppelin", "kazoo", "gurdy", "dronelow", "mezzo"};
const char* insName(int i) { return i >= 0 && i < MI_COUNT ? INS[i] : "piano"; }
static bool isDrum(int i) { return i == MI_TAIKO || i == MI_SNARE || i == MI_HAND || i == MI_FRAME || i == MI_SHAKER || i == MI_CYMBAL; }

float Song::lengthBeats() const { float e = 0; for (auto& n : notes) e = std::max(e, n.beat + n.len); return e; }

// ---- JSON ---------------------------------------------------------------------------------------------------
json songToJson(const Song& s) {
  json n = json::array();
  for (auto& x : s.notes) n.push_back(json::array({std::round(x.beat * 1000) / 1000, std::round(x.len * 1000) / 1000, insName(x.ins), x.midi, std::round(x.vel * 100) / 100}));
  return json{{"name", s.name}, {"bpm", s.bpm}, {"realm", s.realm}, {"notes", n}};
}
Song songFromJson(const json& j) {
  Song s;
  if (!j.is_object()) return s;
  s.name = j.value("name", std::string("Untitled")); s.bpm = std::clamp(j.value("bpm", 90.f), 30.f, 260.f); s.realm = j.value("realm", std::string());
  if (j.contains("notes") && j["notes"].is_array())
    for (auto& a : j["notes"]) {
      if (!a.is_array() || a.size() < 5) continue;
      SongNote n; n.beat = a[0].get<float>(); n.len = a[1].get<float>();
      n.ins = (u8)(a[2].is_string() ? musicInsOf(a[2].get<std::string>()) : a[2].get<int>());
      n.midi = a[3].get<float>(); n.vel = a[4].get<float>();
      s.notes.push_back(n);
    }
  return s;
}
static std::string safeName(std::string n) {
  for (auto& c : n) if (!(std::isalnum((unsigned char)c) || c == '_' || c == ' ' || c == '.' || c == '-')) c = '_';
  while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
  return n.empty() ? "Untitled" : n;
}
std::vector<Song> loadUserSongs() {
  std::vector<Song> out;
  fs::path d = userDir() / "music";
  std::error_code ec;
  if (!fs::is_directory(d, ec)) return out;
  for (auto& e : fs::directory_iterator(d, ec)) {
    if (e.path().extension() != ".json") continue;
    auto txt = readFile(e.path());
    if (!txt) continue;
    json j = json::parse(*txt, nullptr, false);
    if (!j.is_discarded()) out.push_back(songFromJson(j));
  }
  std::sort(out.begin(), out.end(), [](const Song& a, const Song& b) { return a.name < b.name; });
  return out;
}
bool saveUserSong(const Song& s, std::string* err) {
  fs::path d = userDir() / "music";
  std::error_code ec; fs::create_directories(d, ec);
  Song c = s; c.name = safeName(s.name);
  if (!writeFileAtomic(d / (c.name + ".json"), songToJson(c).dump(1))) { if (err) *err = "Couldn't write " + (d / (c.name + ".json")).string(); return false; }
  return true;
}

// ---- MIDI ---------------------------------------------------------------------------------------------------
static int gmProgram(int i) {
  static const int P[MI_COUNT] = {0, 89, 14, 90, 46, 73, 48, 52, 60, 61, 12, 108, 10, 8, 45, 71, 42, -1, 47, -1, -1, -1, -1, -1, 15, 32, 33, 24,
    68, 44, 25, 110, 105, 40, 40, 24, 29, 64, 109, 91, 53};
  return P[std::clamp(i, 0, MI_COUNT - 1)];
}
static int gmDrumKey(int i, float midi) {
  switch (i) { case MI_TAIKO: return 36; case MI_SNARE: return 38; case MI_HAND: return 63; case MI_FRAME: return 64; case MI_SHAKER: return 70; case MI_CYMBAL: return 49; }
  return (int)midi;
}
static int insOfProgram(int p) {
  if (p < 8) return MI_PIANO;
  if (p == 8) return MI_CELESTA;
  if (p <= 10) return MI_MUSICBOX;
  if (p <= 13) return MI_MARIMBA;
  if (p == 14) return MI_BELL;
  if (p == 15) return MI_DULCIMER;
  if (p < 24) return MI_PAD;
  if (p < 32) return MI_GUITAR;
  if (p < 40) return p == 33 ? MI_UBASS : MI_BASS;
  if (p < 42) return MI_FIDDLE;
  if (p < 44) return MI_CELLO;
  if (p == 44) return MI_TREMOLO;
  if (p == 45) return MI_PLUCK;
  if (p == 46) return MI_HARP;
  if (p == 47) return MI_TIMPANI;
  if (p < 52) return MI_STRINGS;
  if (p < 55) return MI_CHOIR;
  if (p < 56) return MI_PAD;
  if (p < 64) return p == 60 ? MI_HORN : MI_BRASS;
  if (p < 72) return p >= 68 && p <= 69 ? MI_BOMBARD : MI_CLARINET;
  if (p < 80) return MI_FLUTE;
  if (p < 104) return MI_PAD;
  if (p < 108) return MI_PLUCK;
  if (p == 108) return MI_KALIMBA;
  if (p == 109) return MI_GURDY;
  if (p == 110) return MI_FIDDLE;
  if (p == 111) return MI_BOMBARD;
  if (p < 120) return MI_BELL;
  return MI_PAD;
}
static int insOfDrum(int key) {
  if (key == 35 || key == 36 || key == 41 || key == 43) return MI_TAIKO;
  if (key == 49 || key == 51 || key == 52 || key == 55 || key == 57 || key == 59) return MI_CYMBAL;
  if (key == 42 || key == 44 || key == 46 || key == 69 || key == 70 || key == 54) return MI_SHAKER;
  if (key >= 60 && key <= 63) return MI_HAND;
  if (key >= 64 && key <= 68) return MI_FRAME;
  return MI_SNARE;
}

static void vlq(std::vector<u8>& o, u32 v) {
  u8 b[5]; int n = 0; b[n++] = v & 0x7f;
  while (v >>= 7) b[n++] = 0x80 | (v & 0x7f);
  while (n) o.push_back(b[--n]);
}
static void be32(std::vector<u8>& o, u32 v) { for (int s = 24; s >= 0; s -= 8) o.push_back((v >> s) & 255); }
static void be16(std::vector<u8>& o, u16 v) { o.push_back(v >> 8); o.push_back(v & 255); }

std::vector<u8> songToMidi(const Song& s) {
  const int DIV = 480;
  std::map<int, std::vector<const SongNote*>> byIns;
  for (auto& n : s.notes) byIns[n.ins].push_back(&n);
  std::vector<std::vector<u8>> tracks;
  {   // the tempo track
    std::vector<u8> t; u32 us = (u32)std::lround(60000000.0 / std::max(1.f, s.bpm));
    vlq(t, 0); t.insert(t.end(), {0xff, 0x51, 0x03, (u8)(us >> 16), (u8)(us >> 8), (u8)us});
    vlq(t, 0); t.insert(t.end(), {0xff, 0x58, 0x04, 4, 2, 24, 8});
    std::string nm = s.name; vlq(t, 0); t.push_back(0xff); t.push_back(0x03); vlq(t, (u32)nm.size()); t.insert(t.end(), nm.begin(), nm.end());
    vlq(t, 0); t.insert(t.end(), {0xff, 0x2f, 0x00});
    tracks.push_back(t);
  }
  int nextCh = 0;
  for (auto& [ins, list] : byIns) {
    const bool drum = isDrum(ins) || gmProgram(ins) < 0;
    int ch = drum ? 9 : nextCh;
    if (!drum) { nextCh++; if (nextCh == 9) nextCh++; if (nextCh > 15) nextCh = 15; }
    struct Ev { u32 tick; bool on; u8 key, vel; };
    std::vector<Ev> ev;
    for (auto* n : list) {
      u8 key = (u8)std::clamp(drum ? gmDrumKey(ins, n->midi) : (int)std::lround(n->midi), 0, 127), vel = (u8)std::clamp((int)std::lround(n->vel * 127), 1, 127);
      u32 a = (u32)std::lround(std::max(0.f, n->beat) * DIV), b = a + std::max(1u, (u32)std::lround(n->len * DIV));
      ev.push_back({a, true, key, vel}); ev.push_back({b, false, key, 0});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.tick != y.tick ? x.tick < y.tick : (!x.on && y.on); });
    std::vector<u8> t;
    std::string nm = insName(ins); vlq(t, 0); t.push_back(0xff); t.push_back(0x03); vlq(t, (u32)nm.size()); t.insert(t.end(), nm.begin(), nm.end());
    if (!drum) { vlq(t, 0); t.push_back(0xc0 | ch); t.push_back((u8)gmProgram(ins)); }
    u32 last = 0;
    for (auto& e : ev) { vlq(t, e.tick - last); last = e.tick; t.push_back((e.on ? 0x90 : 0x80) | ch); t.push_back(e.key); t.push_back(e.on ? e.vel : 64); }
    vlq(t, 0); t.insert(t.end(), {0xff, 0x2f, 0x00});
    tracks.push_back(t);
  }
  std::vector<u8> o = {'M', 'T', 'h', 'd'};
  be32(o, 6); be16(o, 1); be16(o, (u16)tracks.size()); be16(o, DIV);
  for (auto& t : tracks) { o.insert(o.end(), {'M', 'T', 'r', 'k'}); be32(o, (u32)t.size()); o.insert(o.end(), t.begin(), t.end()); }
  return o;
}

bool songFromMidi(const std::vector<u8>& b, Song& out, std::string& err) {
  size_t p = 0;
  auto need = [&](size_t n) { return p + n <= b.size(); };
  auto u32at = [&](size_t i) { return (u32)b[i] << 24 | (u32)b[i + 1] << 16 | (u32)b[i + 2] << 8 | b[i + 3]; };
  if (!need(14) || std::string(b.begin(), b.begin() + 4) != "MThd") { err = "Not a MIDI file"; return false; }
  const int ntr = b[10] << 8 | b[11], div = b[12] << 8 | b[13];
  if (div & 0x8000) { err = "SMPTE-timed MIDI files aren't supported"; return false; }
  p = 8 + u32at(4);
  out = Song(); out.bpm = 120;
  bool tempoSet = false;
  for (int tr = 0; tr < ntr && need(8); tr++) {
    if (std::string(b.begin() + p, b.begin() + p + 4) != "MTrk") { err = "Damaged track"; return false; }
    size_t end = std::min(b.size(), p + 8 + u32at(p + 4)); p += 8;
    u32 tick = 0; u8 run = 0; int prog[16] = {0};
    std::map<int, std::vector<std::pair<u32, u8>>> on;   // (ch << 8 | key) -> the notes sounding on it (start tick, velocity), oldest first
    while (p < end) {
      u32 d = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = b[p++]; d = d << 7 | (c & 0x7f); if (!(c & 0x80)) break; }
      tick += d;
      if (p >= end) break;
      u8 st = b[p];
      if (st == 0xff) {   // meta
        if (p + 2 >= end) break;
        u8 type = b[p + 1]; p += 2;
        u32 len = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = b[p++]; len = len << 7 | (c & 0x7f); if (!(c & 0x80)) break; }
        if (type == 0x51 && len == 3 && p + 3 <= end && !tempoSet) { u32 us = b[p] << 16 | b[p + 1] << 8 | b[p + 2]; if (us) { out.bpm = 60000000.f / us; tempoSet = true; } }
        if (type == 0x03 && tr == 0 && len < 64 && p + len <= end) { std::string nm(b.begin() + p, b.begin() + p + len); if (!nm.empty()) out.name = nm; }
        p += len; continue;
      }
      if (st == 0xf0 || st == 0xf7) { p++; u32 len = 0; for (int k = 0; k < 4 && p < end; k++) { u8 c = b[p++]; len = len << 7 | (c & 0x7f); if (!(c & 0x80)) break; } p += len; continue; }
      if (st & 0x80) { run = st; p++; }
      const u8 kind = run & 0xf0, ch = run & 15;
      const int nData = kind == 0xc0 || kind == 0xd0 ? 1 : 2;
      if (p + nData > end) break;
      const u8 d1 = b[p], d2 = nData > 1 ? b[p + 1] : 0; p += nData;
      if (kind == 0xc0) prog[ch] = d1;
      else if (kind == 0x90 && d2 > 0) on[ch << 8 | d1].push_back({tick, d2});
      else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
        auto it = on.find(ch << 8 | d1);
        if (it == on.end() || it->second.empty()) continue;
        const auto first = it->second.front(); it->second.erase(it->second.begin());
        SongNote n; n.beat = first.first / (float)div; n.len = std::max(0.05f, (tick - first.first) / (float)div);
        n.vel = first.second / 127.f;
        if (ch == 9) { n.ins = (u8)insOfDrum(d1); n.midi = 0; n.len = std::min(n.len, 0.5f); }
        else { n.ins = (u8)insOfProgram(prog[ch]); n.midi = d1; }
        out.notes.push_back(n);
      }
    }
    p = end;
  }
  std::sort(out.notes.begin(), out.notes.end(), [](const SongNote& a, const SongNote& c) { return a.beat < c.beat; });
  if (out.notes.empty()) { err = "No notes in that file"; return false; }
  return true;
}

// ---- reading a recording ------------------------------------------------------------------------------------
static const char* PC[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
std::string keyName(int key, bool minor) { return std::string(PC[((key % 12) + 12) % 12]) + (minor ? " minor" : " major"); }

SongAnalysis analyseRecording(const std::string& path) {
  SongAnalysis A;
  const int SRA = 22050;
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, SRA);
  ma_uint64 n = 0; void* pcm = nullptr;
  if (ma_decode_file(path.c_str(), &cfg, &n, &pcm) != MA_SUCCESS || !pcm) { A.err = "Couldn't read that file (WAV, MP3 or FLAC)"; return A; }
  std::vector<float> x((float*)pcm, (float*)pcm + std::min<ma_uint64>(n, (ma_uint64)SRA * 120));
  ma_free(pcm, nullptr);
  if (x.size() < (size_t)SRA * 4) { A.err = "That recording is too short"; return A; }
  const int N = 2048, H = 512;
  const int frames = (int)((x.size() - N) / H);
  std::vector<float> flux(frames, 0), prev(N / 2, 0), win(N);
  for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * std::cos(2 * 3.14159265f * i / (N - 1));
  std::vector<std::array<float, 12>> chroma(frames);
  std::vector<float> re(N), im(N);
  for (int f = 0; f < frames; f++) {
    for (int i = 0; i < N; i++) { re[i] = x[(size_t)f * H + i] * win[i]; im[i] = 0; }
    fft(re, im, false);
    float fl = 0; chroma[f].fill(0);
    for (int k = 1; k < N / 2; k++) {
      const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
      const float lm = std::log1p(mag * 10);
      fl += std::max(0.f, lm - prev[k]); prev[k] = lm;
      const float hz = k * (float)SRA / N;
      if (hz < 55 || hz > 2000) continue;
      const float m = 69 + 12 * std::log2(hz / 440.f);
      chroma[f][((int)std::lround(m) % 12 + 12) % 12] += mag;
    }
    flux[f] = fl;
  }
  // tempo: the onset strength's autocorrelation, with a gentle lean toward a walking pace
  const float fr = (float)SRA / H;
  float mean = 0; for (float v : flux) mean += v; mean /= frames;
  for (float& v : flux) v = std::max(0.f, v - mean);
  // the autocorrelation at whole lags, then each tempo scored on its beat and the next three (a comb), so a beat's
  // multiples back it up and the off-beat tempos don't win
  const int maxLag = std::min(frames - 1, (int)(fr * 60 / 50) * 4 + 2);
  std::vector<double> ac(maxLag + 1, 0);
  for (int lag = 1; lag <= maxLag; lag++) { double s = 0; for (int i = lag; i < frames; i++) s += flux[i] * flux[i - lag]; ac[lag] = s / (frames - lag); }
  auto acAt = [&](float L) { int i = (int)L; float t = L - i; if (i + 1 > maxLag) return 0.0; return ac[i] * (1 - t) + ac[i + 1] * t; };
  float best = -1, bestLag = 0;
  for (float L = fr * 60 / 190; L <= fr * 60 / 55; L += 0.05f) {
    const double s = acAt(L) + 0.5 * acAt(2 * L) + 0.33 * acAt(3 * L) + 0.25 * acAt(4 * L);
    const float bpm = 60 * fr / L, prior = std::exp(-0.5f * std::pow(std::log2(bpm / 110.f) / 1.0f, 2.f));
    if (s * prior > best) { best = (float)(s * prior); bestLag = L; }
  }
  if (bestLag <= 0) { A.err = "Couldn't hear a beat"; return A; }
  A.bpm = std::round(60 * fr / bestLag * 2) / 2;
  // key: the whole piece's pitch classes against the major and minor key profiles (Krumhansl-Kessler)
  static const float MAJ[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
  static const float MIN[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
  std::array<float, 12> tot{}; for (auto& c : chroma) for (int i = 0; i < 12; i++) tot[i] += c[i];
  auto corr = [](const std::array<float, 12>& c, const float* prof, int k) {
    float mc = 0, mp = 0; for (int i = 0; i < 12; i++) { mc += c[i]; mp += prof[i]; } mc /= 12; mp /= 12;
    float num = 0, dc = 0, dp = 0;
    for (int i = 0; i < 12; i++) { float a = c[(i + k) % 12] - mc, b = prof[i] - mp; num += a * b; dc += a * a; dp += b * b; }
    return num / std::sqrt(std::max(1e-9f, dc * dp));
  };
  float kb = -2;
  for (int k = 0; k < 12; k++) for (int mi = 0; mi < 2; mi++) { float r = corr(tot, mi ? MIN : MAJ, k); if (r > kb) { kb = r; A.key = k; A.minor = mi; } }
  // chords: a bar (four beats) at a time, the best-fitting major or minor triad
  // the beat's phase: where the onsets fall on a grid of the beat; the bars start on it
  float phase = 0, pb = -1;
  for (float o = 0; o < bestLag; o += 0.5f) { double s = 0; for (float t = o; t < frames - 1; t += bestLag) s += flux[(int)t]; if (s > pb) { pb = (float)s; phase = o; } }
  for (int bar = 0; A.chords.size() < 64; bar++) {
    const int f0 = (int)std::lround(phase + bar * bestLag * 4), f1 = (int)std::lround(phase + (bar + 1) * bestLag * 4);
    if (f1 > frames) break;
    std::array<float, 12> c{}; for (int f = f0; f < f1; f++) for (int i = 0; i < 12; i++) c[i] += chroma[f][i];
    float cb = -1; int root = 0; bool mn = false;
    for (int r = 0; r < 12; r++) for (int mi = 0; mi < 2; mi++) {
      const float s = c[r] + c[(r + (mi ? 3 : 4)) % 12] + c[(r + 7) % 12] * 0.9f - 0.3f * (c[(r + (mi ? 4 : 3)) % 12]);
      if (s > cb) { cb = s; root = r; mn = mi; }
    }
    A.chordRoots.push_back(root); A.chordMinor.push_back(mn);
    A.chords.push_back(std::string(PC[root]) + (mn ? "m" : ""));
  }
  // a major key whose relative minor chord is heard as much as its own home chord is read as that minor
  if (!A.minor && !A.chordRoots.empty()) {
    int home = 0, rel = 0;
    for (size_t i = 0; i < A.chordRoots.size(); i++) { if (A.chordRoots[i] == A.key && !A.chordMinor[i]) home++; if (A.chordRoots[i] == (A.key + 9) % 12 && A.chordMinor[i]) rel++; }
    if (rel >= home && rel > 0) { A.key = (A.key + 9) % 12; A.minor = true; }
  }
  A.ok = true;
  return A;
}

// ---- writing pieces -----------------------------------------------------------------------------------------
const std::vector<std::string>& songMoods() {
  static const std::vector<std::string> M = {"day", "night", "cave", "sky", "depths", "menu", "creative", "plains", "forest", "jungle", "desert", "savanna", "frost",
    "peaks", "swamp", "ocean", "village", "deep", "sky2", "depths2", "cave2"};
  return M;
}
Song songFromMood(const std::string& mood, u32 seed) {
  Song s; s.name = mood + " " + std::to_string(seed % 1000); s.bpm = 60;   // a beat a second: the piece's own timing
  for (auto& n : composeMusic(mood, seed)) s.notes.push_back({(float)n.t, std::max(0.05f, n.d), n.ins, n.m, n.v});
  return s;
}

Song songFromAnalysis(const SongAnalysis& a, const std::string& style, u32 seed) {
  std::mt19937 rng(seed);
  auto rnd = [&] { return std::uniform_real_distribution<float>(0, 1)(rng); };
  Song s; s.bpm = a.bpm > 0 ? a.bpm : 96; s.name = style + " in " + keyName(a.key, a.minor);
  const int k = a.key;
  static const int MAJ[7] = {0, 2, 4, 5, 7, 9, 11}, MINS[7] = {0, 2, 3, 5, 7, 8, 10};
  const int* sc = a.minor ? MINS : MAJ;
  // the chord walk: what the recording was read as (up to 16 bars), else a common one in its key
  std::vector<std::pair<int, bool>> walk;
  for (size_t i = 0; i < a.chordRoots.size() && walk.size() < 16; i++) walk.push_back({a.chordRoots[i], a.chordMinor[i]});
  if (walk.size() < 4) {
    static const int MW[4] = {0, 4, 5, 3}, NW[4] = {0, 5, 2, 6};   // I V vi IV / i VI III VII
    walk.clear();
    for (int i = 0; i < 4; i++) { const int d = a.minor ? NW[i] : MW[i]; const int r = (k + sc[d]) % 12; const bool mn = a.minor ? (d == 0 || d == 3 || d == 4) : (d == 1 || d == 2 || d == 5); walk.push_back({r, mn}); }
  }
  for (size_t n0 = walk.size(); walk.size() < 16;) walk.push_back(walk[walk.size() % n0]);
  struct Set { int lead, pad, arp, bass; int drum1, drum2; float swing; };
  const Set S = style == "folk" ? Set{MI_FLUTE, MI_DULCIMER, MI_MANDOLIN, MI_UBASS, MI_HAND, MI_SHAKER, 0.f}
              : style == "epic" ? Set{MI_HORN, MI_STRINGS, MI_BRASS, MI_CELLO, MI_TAIKO, MI_SNARE, 0.f}
              : style == "dreamy" ? Set{MI_CELESTA, MI_CHOIR, MI_HARP, MI_PAD, -1, -1, 0.f}
              : Set{MI_PIANO, MI_PAD, MI_PIANO, MI_BASS, -1, -1, 0.f};   // calm
  auto add = [&](float beat, float len, int ins, float midi, float vel) { s.notes.push_back({beat, len, (u8)ins, midi, vel}); };
  auto inScale = [&](int m) { int pc = ((m - k) % 12 + 12) % 12; for (int i = 0; i < 7; i++) if (sc[i] == pc) return true; return false; };
  int prevMel = 60 + k % 12 + (k % 12 > 6 ? -12 : 0) + 12;
  static const std::vector<std::vector<float>> RH = {{1, 1, 1, 1}, {1.5f, 0.5f, 1, 1}, {0.5f, 0.5f, 1, 2}, {2, 1, 1}, {1, 0.5f, 0.5f, 2}, {3, 1}};
  for (int bar = 0; bar < 16; bar++) {
    const float b0 = bar * 4.f;
    const auto [root, mn] = walk[bar];
    const int third = mn ? 3 : 4;
    const int padRoot = 48 + root;
    // the bed: a held chord
    for (int iv : {0, third, 7}) add(b0, 4, S.pad, padRoot + iv, 0.45f);
    // the low line: root on one, the fifth on three
    add(b0, 1.8f, S.bass, 36 + root, 0.8f); add(b0 + 2, 1.8f, S.bass, 36 + root + 7 - (root + 7 >= 12 ? 12 : 0), 0.65f);
    // the moving part: the chord broken in eighths
    const int arp[8] = {0, 7, third + 12, 7, 12, 7, third + 12, 7};
    if (bar >= 2) for (int i = 0; i < 8; i++) add(b0 + i * 0.5f, 0.45f, S.arp, 60 + root + arp[i] - (root > 7 ? 12 : 0), i % 4 == 0 ? 0.55f : 0.38f);
    // the tune: from bar 4, a phrase over each bar, chord tones on the strong beats, steps of the scale between
    if (bar >= 4 && !(bar % 8 == 7 && rnd() < 0.5f)) {
      const auto& rh = RH[(size_t)(rnd() * RH.size()) % RH.size()];
      float t = 0;
      for (size_t i = 0; i < rh.size(); i++) {
        int m;
        if (t == 0 || t == 2) {   // a chord tone near the last note
          int bestM = prevMel, bd = 99;
          for (int o = -1; o <= 1; o++) for (int iv : {0, third, 7}) { int c = 60 + root + iv + 12 * o + 12; if (std::abs(c - prevMel) < bd && c >= 62 && c <= 86) { bd = std::abs(c - prevMel); bestM = c; } }
          m = bestM;
        } else {
          m = prevMel + (rnd() < 0.5f ? 1 : -1) * (rnd() < 0.7f ? 1 : 2);
          for (int g = 0; g < 3 && !inScale(m); g++) m += m > prevMel ? 1 : -1;
          m = std::clamp(m, 62, 86);
        }
        add(b0 + t, rh[i] * 0.95f, S.lead, (float)m, 0.7f + 0.15f * (t == 0));
        prevMel = m; t += rh[i];
      }
    }
    // the beat
    if (S.drum1 >= 0 && bar >= 1) {
      add(b0, 0.3f, S.drum1, 0, 0.9f); add(b0 + 2, 0.3f, S.drum1, 0, 0.7f);
      if (bar % 2) add(b0 + 3.5f, 0.3f, S.drum1, 0, 0.5f);
      for (int i = 0; i < 4; i++) add(b0 + i + 0.5f, 0.2f, S.drum2, 0, 0.35f);
    }
  }
  // close on the home chord
  for (int iv : {0, a.minor ? 3 : 4, 7, 12}) add(64, 4, S.pad, 48 + k + iv, 0.5f);
  add(64, 4, S.bass, 36 + k, 0.8f);
  add(64, 3, S.lead, 72 + k - (k > 7 ? 12 : 0), 0.7f);
  return s;
}

} // namespace dq::snd
