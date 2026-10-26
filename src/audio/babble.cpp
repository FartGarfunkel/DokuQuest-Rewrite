// audio/babble.cpp — see babble.h. A walk through villagers.js babble() with the same numbers: the same schedule of
// blips (a letter each; a space or punctuation is a pause; half the consonants fold into the vowels), the same
// two-formant voice (a saw or triangle through bandpasses at the vowel's F1 and F2), the same pitch wobble, rising
// question and puff of noise for b d g k p t / s z f h x c — built from the WebAudio pieces in dsp.h.
#include "audio/babble.h"
#include "audio/dsp.h"
#include "audio/audio.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dq {
namespace snd {

namespace {
// BABBLE_V: the first two formants of a e i o u y
constexpr const char* VOWELS = "aeiouy";
constexpr float F1[6] = {800, 520, 320, 520, 360, 380}, F2[6] = {1200, 1850, 2300, 900, 720, 2100};
int vowelIndex(char c) { const char* p = c ? std::strchr(VOWELS, c) : nullptr; return p ? (int)(p - VOWELS) : -1; }

struct Blip { double t; float pitch; int vowel, cons; };   // cons: 0 none (or nasal), 1 stop, 2 hiss

bool parseKey(const std::string& k, BabbleVoice& v, float& rate, int& mx, std::string& text) {
  const char* s = k.c_str(); char* e = nullptr;
  v.pitch = std::strtof(s, &e); if (e == s || *e != '|') return false; s = e + 1;
  v.formant = std::strtof(s, &e); if (e == s || *e != '|') return false; s = e + 1;
  v.triangle = std::strtol(s, &e, 10) != 0; if (*e != '|') return false; s = e + 1;
  v.seed = (int)std::strtol(s, &e, 10); if (*e != '|') return false; s = e + 1;
  rate = std::strtof(s, &e); if (e == s || *e != '|') return false; s = e + 1;
  mx = (int)std::strtol(s, &e, 10); if (*e != '|') return false; s = e + 1;
  text = s;
  return rate > 1 && mx > 0;
}
} // namespace

std::string babbleKey(const std::string& text, const BabbleVoice& v, float rate, int maxBlips) {
  char head[96];
  std::snprintf(head, sizeof head, "%.2f|%.4f|%d|%d|%.2f|%d|", v.pitch, v.formant, v.triangle ? 1 : 0, v.seed, rate, maxBlips);
  return head + text;
}

std::vector<float> renderBabbleKey(const std::string& key, double* seconds) {
  BabbleVoice v; float rate = 26; int mx = 90; std::string text;
  if (seconds) *seconds = 0;
  if (!parseKey(key, v, rate, mx, text)) return {};
  // ---- the schedule: one blip per letter (villagers.js babble) ----
  const double t0 = 0.02, len = 0.07;
  double t = t0; int k = 0, n = 0;
  int chars = 0; for (unsigned char c : text) if ((c & 0xC0) != 0x80) chars++;
  size_t last = text.find_last_not_of(" \t\r\n\f\v");
  const bool ques = last != std::string::npos && text[last] == '?';
  std::vector<Blip> blips;
  int ci = -1;   // the character's index, as the original counts it (one per character, not per byte)
  for (size_t b = 0; b < text.size() && n < mx; b++) {
    unsigned char uc = (unsigned char)text[b];
    if ((uc & 0xC0) == 0x80) continue;   // the rest of a multi-byte character
    ci++;
    char c = (char)uc;
    if (uc >= 0x80) continue;             // not a letter of ours
    if (c == ' ') { t += 0.6 / rate; continue; }
    if (c && std::strchr(".,!?;:", c)) { t += (c == ',' ? 2 : 3.2) / rate; continue; }
    char lo = (char)std::tolower(uc);
    int vi = vowelIndex(lo), cons = 0;
    bool real = vi >= 0;                  // a e i o u y keep their own sound
    if (!real) {
      if (lo < 'a' || lo > 'z') continue;
      vi = vowelIndex("aeiou"[lo % 5]);
      cons = std::strchr("bdgkpt", lo) ? 1 : std::strchr("szfhxc", lo) ? 2 : 0;   // lo is a-z here, never NUL
    }
    if ((k++ % 2) && !real) { t += 0.25 / rate; continue; }   // half the consonants fold into the vowels
    n++;
    double prog = (double)ci / std::max(1, chars);
    double pitch = v.pitch * (1 + std::sin(ci * 1.7 + v.seed) * 0.07 + (ques && prog > 0.75 ? (prog - 0.75) * 1.4 : 0) - prog * 0.06);
    blips.push_back({t, (float)pitch, vi, cons});
    t += 1 / rate;
  }
  if (seconds) *seconds = t - t0;
  // ---- the voice ----
  Rng r(seedFromString(key));
  std::vector<float> d((size_t)((t + len + 0.1) * SR) + 1, 0.f);
  for (const Blip& b : blips) {
    Osc osc; osc.type = v.triangle ? 3 : 2;
    Param fr; fr.init = b.pitch * 1.06f; fr.set(b.pitch * 1.06f, b.t); fr.exp(b.pitch, b.t + len * 0.6);
    Param env; env.init = 0; env.set(0, b.t); env.lin(0.9f, b.t + 0.008); env.set(0.9f, b.t + len * 0.55); env.exp(0.001f, b.t + len);
    Biquad b1, b2; b1.setWA("bandpass", F1[b.vowel] * v.formant, 5); b2.setWA("bandpass", F2[b.vowel] * v.formant, 7);
    int i0 = std::max(0, (int)(b.t * SR)), i1 = std::min((int)d.size(), (int)((b.t + len + 0.02) * SR));
    for (int i = i0; i < i1; i++) {
      double tt = (double)i / SR;
      float x = osc.next(fr.at(tt));
      d[i] += (b1.run(x) + 0.55f * b2.run(x)) * env.at(tt);
    }
    if (b.cons) {   // the puff of a hard consonant: high-passed noise, 15 ms (stops) or 40 ms (hisses)
      bool hiss = b.cons == 2;
      Biquad hp; hp.setWA("highpass", hiss ? 3500 : 1800, 1);
      Param ne; ne.init = hiss ? 0.25f : 0.4f; ne.set(ne.init, b.t); ne.exp(0.001f, b.t + (hiss ? 0.04 : 0.015));
      int j1 = std::min((int)d.size(), (int)((b.t + 0.05) * SR));
      for (int i = i0; i < j1; i++) d[i] += hp.run(r.pm()) * ne.at((double)i / SR);
    }
  }
  return d;
}

} // namespace snd

int babble(AudioEngine& e, const std::string& text, const snd::BabbleVoice& v, float volume, float rate, int maxBlips) {
  if (text.empty()) return 0;
  return e.play("babble:" + snd::babbleKey(text, v, rate, maxBlips), volume);   // SfxLibrary::resolve makes the sound from the name
}

} // namespace dq
