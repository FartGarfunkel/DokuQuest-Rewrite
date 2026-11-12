// audio/resynth.h — the resource-pack sounds, resynthesised from their analysed profiles
// (data/sounds.json + data/sounds.bin, made by tools/port/sound_analyze.py; no recordings are shipped).
// A profile holds the loudness every 128 samples below and above ~1 kHz, the noise in 26 ERB bands per
// hop, and the tonal partials (frequency + level per hop). render() builds random-phase noise from the
// bands (overlap-add), adds the partials as oscillators and matches each half to its loudness track —
// the same steps as the tool's Python reference, so the quality numbers measured there hold here.
#pragma once
#include "audio/dsp.h"
#include <filesystem>
#include <unordered_map>

namespace dq::snd {

struct Profile {
  u32 n = 0; int hop = 480, nfr = 0;
  std::vector<u8> fineLo, fineHi, bands;      // dB codes (0 = silence, v -> -100 + v/2 dB); bands band-major [NB][nfr]
  struct Track { int s; std::vector<float> f, a; };   // start frame; Hz and linear amplitude per frame
  std::vector<Track> tracks;
};

class PackSounds {
public:
  struct Variant { std::string file; float pitch = 1, vol = 1; };
  bool load(const std::filesystem::path& jsonPath, const std::filesystem::path& binPath);
  bool ok() const { return !profiles.empty(); }
  const Profile* profile(const std::string& file) const { auto it = profiles.find(file); return it == profiles.end() ? nullptr : &it->second; }
  // the files (with Minecraft's per-entry pitch and volume) standing in for one of our sounds: "stone.step", "fx.chest", "mob.moo_hurt"...
  const std::vector<Variant>* event(const std::string& key) const { auto it = events.find(key); return it == events.end() ? nullptr : &it->second; }
  std::vector<float> render(const Profile& p, u32 seed) const;
  size_t count() const { return profiles.size(); }
  const std::unordered_map<std::string, std::vector<Variant>>& allEvents() const { return events; }
private:
  std::unordered_map<std::string, Profile> profiles;
  std::unordered_map<std::string, std::vector<Variant>> events;
  int nb = 26, nfft = 2048, fine = 128; float db0 = -100;
  std::vector<float> centE;      // band centres on the ERB scale
  float xo = 1000;               // the split between the two loudness tracks
};

} // namespace dq::snd
