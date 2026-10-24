// audio/engine.h — the inside of AudioEngine (audio.cpp), shared with the music (music.cpp) and the
// world's sound (ambience.cpp). Threads: the GAME thread calls the API, composes music and runs the
// ambience; one WORKER renders sound takes into buffers (cached); the AUDIO thread (miniaudio's callback)
// only mixes: it takes commands from a lock-free queue, plays buffers, synthesises the music band and
// the wind live, and runs the reverbs.
#pragma once
#include "audio/audio.h"
#include "audio/sfxlib.h"
#include <atomic>
#include <thread>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <map>
#include <random>

namespace dq {

struct AudioBuffer {
  std::vector<float> d;        // mono, 48 kHz
  double lastStart = -1e9;     // game-clock time it was last started (eviction)
};

enum AudioCat : u8 { CAT_SFX = 0, CAT_MOBS = 1, CAT_AMB = 2 };

struct AudioCmd {
  enum T : u8 { Play, Stop, Note, Bus, Wind, Verb, MusicClear } t = Play;
  u8 cat = 0, bus = 0, ins = 0;
  int handle = 0;
  const AudioBuffer* buf = nullptr;
  float rate = 1, gL = 1, gR = 1, lp = 0, delay = 0;
  double at = 0;                 // Note: start time on the audio clock (seconds)
  float a = 0, b = 0, c = 0;     // Note: midi, dur, vel | Bus: target, ramp s | Wind: gain, freq | Verb: room, cave
};

// single producer (game thread), single consumer (audio thread)
struct AudioQueue {
  static constexpr u32 N = 4096;
  AudioCmd q[N];
  std::atomic<u32> head{0}, tail{0};
  bool push(const AudioCmd& c) { u32 h = head.load(std::memory_order_relaxed), n = (h + 1) % N; if (n == tail.load(std::memory_order_acquire)) return false; q[h] = c; head.store(n, std::memory_order_release); return true; }
  bool pop(AudioCmd& c) { u32 t = tail.load(std::memory_order_relaxed); if (t == head.load(std::memory_order_acquire)) return false; c = q[t]; tail.store((t + 1) % N, std::memory_order_release); return true; }
};

namespace snd {
class MusicSynth;      // audio thread (music.cpp)
class MusicDirector;   // game thread (music.cpp)
class Ambience;        // game thread (ambience.cpp): ambience.js + sound3.js
}

struct AudioEngine::Impl {
  // ---- device and shared state ----
  void* device = nullptr;          // ma_device*
  bool ok = false, offline = false;
  std::atomic<float> vMaster{0.6f}, vSfx{1}, vMobs{1}, vMusic{0.5f}, vAmb{0.7f};
  std::atomic<u64> frames{0};      // samples mixed: the audio clock
  AudioQueue q;
  double audioNow() const { return frames.load(std::memory_order_relaxed) / (double)snd::SR; }

  // ---- audio thread ----
  static constexpr int MAXV = 56;
  struct Voice { bool on = false; int handle = 0; const AudioBuffer* b = nullptr; double pos = 0, rate = 1; float gL = 1, gR = 1, fade = 1, fadeStep = 0; int wait = 0; u8 cat = 0; bool lpOn = false; snd::Biquad lpL; };
  Voice voices[MAXV];
  snd::Reverb room, cave;
  float roomG = 0, caveG = 0, roomT = 0, caveT = 0;
  struct Wind { float g = 0, target = 0, f = 420, ft = 420; snd::Biquad bp; snd::Rng r{77}; } wind;
  std::unique_ptr<snd::MusicSynth> music;
  void mix(float* out, int frames);
  void startVoice(const AudioCmd& c);

  // ---- worker ----
  snd::SfxLibrary lib;
  std::atomic<bool> libReady{false};
  std::thread worker;
  std::mutex jmx; std::condition_variable jcv; std::deque<std::function<void()>> jobs; bool quit = false;
  std::mutex cmx;                                         // the cache
  std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> cache;
  std::unordered_map<std::string, bool> rendering;
  size_t cacheBytes = 0;
  std::atomic<int> nRenders{0}; int nPlays = 0;   // shown in the log at shutdown
  void post(std::function<void()> f);
  std::shared_ptr<AudioBuffer> cached(const std::string& key);
  void requestRender(const std::string& key, const snd::SoundRef& r, int take, u32 seed);

  // ---- game thread ----
  double now = 0;                                         // game-side clock (seconds since init)
  vec3 lpos{0}, lfwd{0, 0, -1};
  std::unordered_map<std::string, double> last;           // Sfx.ok: last time each name played
  std::mt19937 rng{12345};
  float rnd() { return std::uniform_real_distribution<float>(0, 1)(rng); }
  int nextHandle = 1;
  struct Pending { std::string key; snd::SoundRef ref; int take; AudioCmd cmd; double t; };
  std::vector<Pending> pending;
  AudioScene scene;
  std::unique_ptr<snd::MusicDirector> director;
  std::unique_ptr<snd::Ambience> ambience;

  // play a resolved sound: gain/pan/occlusion already worked out into `c` (handle, cat, gains, lp, rate)
  int start(const snd::SoundRef& r, AudioCmd c, float pitchMul);
  bool place(vec3 p, float range, float vol, AudioCmd& c);   // sound3.js placeNode: distance, walls, direction
  bool gapOk(const std::string& key, float gap);
  void send(const AudioCmd& c) { if (ok) q.push(c); }
};

} // namespace dq
