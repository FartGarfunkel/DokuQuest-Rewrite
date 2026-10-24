// audio/audio.cpp — miniaudio output, the mixer and the sfx API (game/audio.js, sfxlib.js's sample(),
// sound3.js's placeNode). See engine.h for who runs where.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
#include "audio/engine.h"
#include "audio/music.h"
#include "audio/ambience.h"
#include "common/fs.h"
#include <chrono>

namespace dq {

using namespace snd;
static double steadyNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

// ---- the mixer (audio thread) ---------------------------------------------------------------------------------
void AudioEngine::Impl::startVoice(const AudioCmd& c) {
  if (!c.buf || c.buf->d.size() < 2) return;
  int slot = -1; float quiet = 1e9f;
  for (int i = 0; i < MAXV; i++) if (!voices[i].on) { slot = i; break; }
  if (slot < 0) {   // all busy: take over the quietest, least-remaining one
    for (int i = 0; i < MAXV; i++) {
      const Voice& v = voices[i];
      float rem = 1.f - (float)(v.pos / v.b->d.size()), score = (v.gL + v.gR) * v.fade * rem * (v.cat == CAT_AMB ? 0.5f : 1.f);
      if (score < quiet) { quiet = score; slot = i; }
    }
  }
  Voice& v = voices[slot];
  v = Voice();
  v.on = true; v.handle = c.handle; v.b = c.buf; v.pos = 0; v.rate = std::clamp(c.rate, 0.25f, 4.f);
  v.gL = c.gL; v.gR = c.gR; v.cat = c.cat; v.wait = (int)(c.delay * SR);
  if (c.lp > 0 && c.lp < 16000) { v.lpOn = true; v.lpL.setWA("lowpass", c.lp, 0.5); }
}

void AudioEngine::Impl::mix(float* out, int n) {
  AudioCmd c;
  while (q.pop(c)) {
    switch (c.t) {
      case AudioCmd::Play: startVoice(c); break;
      case AudioCmd::Stop: for (auto& v : voices) if (v.on && v.handle == c.handle) v.fadeStep = 1.f / (0.01f * SR); break;
      case AudioCmd::Wind: wind.target = c.a; wind.ft = c.b; break;
      case AudioCmd::Verb: roomT = c.a; caveT = c.b; break;
      default: if (music) music->command(c); break;
    }
  }
  // ambience's level is used the original's way (how often things sound, how loud the wind), not again here
  float master = vMaster.load(std::memory_order_relaxed), catG[3] = {master * vSfx.load(std::memory_order_relaxed), master * vMobs.load(std::memory_order_relaxed), master};
  static thread_local std::vector<float> dry, bus; dry.assign((size_t)n * 2, 0.f); bus.assign((size_t)n * 2, 0.f);
  // buffers
  for (auto& v : voices) {
    if (!v.on) continue;
    const float* d = v.b->d.data(); const size_t L = v.b->d.size();
    float gl = v.gL * catG[v.cat], gr = v.gR * catG[v.cat];
    for (int i = 0; i < n; i++) {
      if (v.wait > 0) { v.wait--; continue; }
      size_t k = (size_t)v.pos;
      if (k + 1 >= L) { v.on = false; break; }
      float fr = (float)(v.pos - k), s = d[k] + (d[k + 1] - d[k]) * fr;
      if (v.lpOn) s = v.lpL.run(s);
      if (v.fadeStep > 0) { v.fade -= v.fadeStep; if (v.fade <= 0) { v.on = false; break; } }
      s *= v.fade;
      bus[2 * i] += s * gl; bus[2 * i + 1] += s * gr;
      v.pos += v.rate;
    }
  }
  // the wind: a looping band of noise (ambience.js)
  if (wind.g > 1e-5f || wind.target > 1e-5f) {
    for (int i = 0; i < n; i++) {
      if ((i & 63) == 0) { wind.f += (wind.ft - wind.f) * 0.02f; wind.bp.setWA("bandpass", wind.f, 0.6); }
      wind.g += (wind.target - wind.g) * 0.002f;
      float s = wind.bp.run(wind.r.pm()) * wind.g * master;
      bus[2 * i] += s; bus[2 * i + 1] += s;
    }
  }
  // sound3.js: everything through one bus, with a small room and a cave on sends
  float rs = 1.f / (0.6f * SR);
  bool verb = roomG > 1e-4f || caveG > 1e-4f || roomT > 1e-4f || caveT > 1e-4f;
  for (int i = 0; i < n; i++) {
    float L = bus[2 * i], R = bus[2 * i + 1];
    if (verb) {
      roomG += (roomT - roomG) * rs; caveG += (caveT - caveG) * rs;
      float a, b, x, y;
      room.process(L * roomG, R * roomG, a, b); cave.process(L * caveG, R * caveG, x, y);
      L += a + x; R += b + y;
    }
    dry[2 * i] = L; dry[2 * i + 1] = R;
  }
  // the music bus (its own volume, straight out like music.js)
  if (music) music->render(dry.data(), n, frames.load(std::memory_order_relaxed) / (double)SR, vMusic.load(std::memory_order_relaxed));
  for (int i = 0; i < 2 * n; i++) {   // a soft knee above 0.8 instead of hard clipping
    float x = dry[i], a = std::abs(x);
    out[i] = a < 0.8f ? x : std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
  }
  frames.fetch_add((u64)n, std::memory_order_relaxed);
}

static void dataCb(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  static_cast<AudioEngine::Impl*>(dev->pUserData)->mix((float*)out, (int)frames);
}

// ---- worker ------------------------------------------------------------------------------------------------------
void AudioEngine::Impl::post(std::function<void()> f) {
  { std::lock_guard<std::mutex> l(jmx); jobs.push_back(std::move(f)); }
  jcv.notify_one();
}
std::shared_ptr<AudioBuffer> AudioEngine::Impl::cached(const std::string& key) {
  std::lock_guard<std::mutex> l(cmx);
  auto it = cache.find(key);
  return it == cache.end() ? nullptr : it->second;
}
void AudioEngine::Impl::requestRender(const std::string& key, const SoundRef& r, int take, u32 seed) {
  {
    std::lock_guard<std::mutex> l(cmx);
    if (cache.count(key) || rendering.count(key)) return;
    rendering[key] = true;
  }
  post([this, key, r, take, seed] {
    auto b = std::make_shared<AudioBuffer>();
    float p = 1, v = 1;
    b->d = lib.render(r, take, seed, &p, &v);
    if (b->d.empty()) b->d.assign(2, 0.f);
    nRenders++;
    std::lock_guard<std::mutex> l(cmx);
    cacheBytes += b->d.size() * 4;
    cache[key] = b; rendering.erase(key);
  });
}

static std::string cacheKey(const SoundRef& r, int take) { return std::to_string((int)r.kind) + ":" + r.key + "#" + std::to_string(take); }

bool AudioEngine::init(const Settings& s) {
  auto* dev = new ma_device();
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = SR;
  cfg.periodSizeInFrames = 512;
  cfg.dataCallback = dataCb; cfg.pUserData = impl.get();
  if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) { delete dev; DQ_WARN("audio: no output device; continuing without sound"); return false; }
  impl->device = dev;
  if (!initOffline(s)) return false;
  impl->offline = false;
  if (ma_device_start(dev) != MA_SUCCESS) { DQ_WARN("audio: the output device would not start; continuing without sound"); shutdown(); return false; }
  return true;
}

bool AudioEngine::initOffline(const Settings& s) {
  Impl& m = *impl;
  m.offline = true; m.ok = true; m.now = 0;
  m.room.init(0.7f, 0.5f); m.cave.init(2.5f, 0.28f);
  m.music = std::make_unique<MusicSynth>();
  m.director = std::make_unique<MusicDirector>(m);
  m.ambience = std::make_unique<Ambience>(m);
  setVolumes(s);
  m.worker = std::thread([&m] {
    m.libReady = m.lib.load(dataDir());
    // the footsteps and the clicks are ready before anyone needs them (sfxlib.js renders everything once sound is on)
    if (m.libReady) for (const char* mat : {"stone", "grass", "dirt", "wood", "sand", "gravel", "log", "leaves"}) {
      SoundRef r = m.lib.material(mat, "step", false);
      for (int t = 0; t < r.takes; t++) m.requestRender(cacheKey(r, t), r, t, (u32)t);
    }
    if (m.libReady) { SoundRef r = m.lib.resolve("click"); for (int t = 0; t < r.takes; t++) m.requestRender(cacheKey(r, t), r, t, (u32)t); }
    for (;;) {
      std::function<void()> job;
      { std::unique_lock<std::mutex> l(m.jmx); m.jcv.wait(l, [&] { return m.quit || !m.jobs.empty(); }); if (m.quit) return; job = std::move(m.jobs.front()); m.jobs.pop_front(); }
      try { job(); } catch (const std::exception& e) { DQ_WARN("audio: %s", e.what()); }
    }
  });
  return true;
}

void AudioEngine::shutdown() {
  if (!impl) return;
  Impl& m = *impl;
  if (m.device) { ma_device_uninit((ma_device*)m.device); delete (ma_device*)m.device; m.device = nullptr; }
  if (m.worker.joinable()) { { std::lock_guard<std::mutex> l(m.jmx); m.quit = true; } m.jcv.notify_all(); m.worker.join(); }
  if (m.ok && !m.offline) DQ_LOG("audio: %d sounds played, %d takes rendered (%.1f MB cached)", m.nPlays, m.nRenders.load(), m.cacheBytes / 1048576.0);
  m.ok = false;
}

void AudioEngine::setListener(vec3 pos, vec3 forward, vec3) { impl->lpos = pos; impl->lfwd = forward; }
void AudioEngine::setVolumes(const Settings& s) {
  impl->vMaster = s.volume; impl->vMobs = s.mobVol; impl->vMusic = s.music; impl->vAmb = s.ambience;
}
void AudioEngine::setCategoryVolume(const std::string& cat, float v) {
  if (cat == "master") impl->vMaster = v; else if (cat == "sfx") impl->vSfx = v; else if (cat == "mobs") impl->vMobs = v;
  else if (cat == "music") impl->vMusic = v; else if (cat == "ambience") impl->vAmb = v;
}
bool AudioEngine::ok() const { return impl && impl->ok; }
void AudioEngine::setScene(const AudioScene& s) { impl->scene = s; }

// ---- playing ---------------------------------------------------------------------------------------------------------
bool AudioEngine::Impl::gapOk(const std::string& key, float gap) {
  auto it = last.find(key);
  if (it != last.end() && now - it->second < gap) return false;
  last[key] = now; return true;
}

int AudioEngine::Impl::start(const SoundRef& r, AudioCmd c, float pitchMul) {
  if (!r || !ok || !libReady) return 0;
  if (vMaster.load() <= 0) return 0;
  int take = r.takes > 1 ? (int)(rnd() * r.takes) % r.takes : 0;
  // Sfx.sample: a take, a touch higher or lower each time, a touch louder or softer
  float pv = r.pv;
  c.t = AudioCmd::Play;
  c.handle = c.handle ? c.handle : nextHandle++;
  c.rate = pitchMul * (1 + (rnd() * 2 - 1) * pv);
  float g = r.gain * (r.jitter ? 0.9f + rnd() * 0.15f : 1.f);
  if (r.kind == SoundRef::Pack) {   // Minecraft's own per-entry pitch and volume
    if (auto v = lib.packs().event(r.key)) { auto& var = (*v)[take % v->size()]; c.rate *= var.pitch; g *= var.vol; }
  }
  c.gL *= g; c.gR *= g;
  c.cat = r.mob ? CAT_MOBS : c.cat;
  std::string key = cacheKey(r, take);
  nPlays++;
  if (auto b = cached(key)) { b->lastStart = now; c.buf = b.get(); send(c); return c.handle; }
  requestRender(key, r, take, (u32)take);
  pending.push_back({key, r, take, c, now});
  return c.handle;
}

// sound3.js placeNode: how far it travels, what's in the way, where it comes from
bool AudioEngine::Impl::place(vec3 p, float range, float vol, AudioCmd& c) {
  vec3 L = lpos; float best = glm::distance(L, p);
  if (best > range + 2) return false;
  float d = best, wall = 0;
  if (ambience && scene.inWorld && scene.world) ambience->reach(L, p, d, wall);
  if (d > range) return false;
  float gain = std::clamp(1 - (d - 3) / (range - 3), 0.f, 1.f) * vol;
  if (wall > 0) gain *= std::pow(0.42f, std::min(wall, 6.f));
  if (gain < 0.015f) return false;
  c.lp = wall > 0 ? std::max(220.f, 1400.f / wall) : 0;
  // direction: right/left from the listener's facing; behind sounds a little duller (the HRTF panner's cue)
  float gl = 1, gr = 1;
  if (best > 0.6f) {
    vec3 f = lfwd; f.y = 0; if (glm::length(f) < 1e-4f) f = vec3(0, 0, -1); f = glm::normalize(f);
    vec3 right(-f.z, 0, f.x);
    vec3 dv = p - L; float px = glm::dot(dv, right), pf = glm::dot(dv, f), n = std::sqrt(px * px + dv.y * dv.y * 0.36f + pf * pf);
    if (n < 1e-4f) n = 1;
    float pan = std::clamp(px / n, -1.f, 1.f) * 0.8f, th = (pan + 1) * 0.25f * PI;   // the far ear still hears some (as through the HRTF)
    gl = std::min(1.f, 1.41421f * std::cos(th)); gr = std::min(1.f, 1.41421f * std::sin(th));
    if (pf < 0) { float behind = -pf / n; float cut = 16000 - 10000 * behind; c.lp = c.lp > 0 ? std::min(c.lp, cut) : cut; }
  }
  c.gL = gain * gl; c.gR = gain * gr;
  return true;
}

int AudioEngine::play(const std::string& name, float volume, float pitch) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return 0;
  SoundRef r = m.lib.resolve(name);
  if (!r) return 0;
  if (r.mob && m.vMobs.load() <= 0) return 0;
  if (!m.gapOk(name, r.gap)) return 0;
  AudioCmd c; c.gL = c.gR = volume;
  return m.start(r, c, pitch);
}

int AudioEngine::playAt(const std::string& name, vec3 pos, float range, float volume, float pitch) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return 0;
  SoundRef r = m.lib.resolve(name);
  if (!r) return 0;
  if (r.mob && m.vMobs.load() <= 0) return 0;
  AudioCmd c;
  if (!m.place(pos, range, volume, c)) return 0;
  if (!m.gapOk(name, r.gap)) return 0;
  return m.start(r, c, pitch);
}

void AudioEngine::material(const std::string& mat, const std::string& kind) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  SoundRef r = m.lib.material(mat, kind, false);
  if (!r || !m.gapOk(kind + mat, r.gap)) return;
  AudioCmd c; m.start(r, c, 1);
}

void AudioEngine::materialAt(const std::string& mat, const std::string& kind, vec3 pos, float range, float volume) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  SoundRef r = m.lib.material(mat, kind, true);
  if (!r) return;
  AudioCmd c;
  if (!m.place(pos, range, volume, c)) return;
  m.start(r, c, 1);
}

void AudioEngine::note(const std::string& instrument, int pitch, vec3 pos) {
  Impl& m = *impl;
  if (!m.ok || !m.libReady) return;
  SoundRef r = m.lib.note(pitch, instrument);
  if (!m.gapOk("note" + std::to_string(pitch), r.gap)) return;
  AudioCmd c;
  if (!m.place(pos, 48, 1, c)) return;
  m.start(r, c, 1);
}

void AudioEngine::voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) {
  std::string n = kind.empty() || kind == "say" ? creature : kind == "step" ? "mstep_" + creature : creature + "_" + kind;
  playAt(n, pos, kind == "step" ? 16.f : 28.f, 1, pitch);
}

void AudioEngine::stop(int handle) { AudioCmd c; c.t = AudioCmd::Stop; c.handle = handle; impl->send(c); }

void AudioEngine::update(double dt) {
  Impl& m = *impl;
  if (!m.ok) return;
  m.now += dt;
  // sounds that were waiting for their take to be rendered
  for (size_t i = 0; i < m.pending.size();) {
    auto& p = m.pending[i];
    if (auto b = m.cached(p.key)) { b->lastStart = m.now; p.cmd.buf = b.get(); m.send(p.cmd); m.pending.erase(m.pending.begin() + i); continue; }
    if (m.now - p.t > 0.3) { m.pending.erase(m.pending.begin() + i); continue; }
    i++;
  }
  if (!m.libReady) return;
  if (m.ambience) m.ambience->update(dt, m.scene);
  if (m.director) m.director->update(dt, m.scene);
  // keep the cache small: buffers not started for a while (longer than any of them lasts) go
  static double sweep = 0;
  if ((sweep += dt) > 10) {
    sweep = 0;
    std::lock_guard<std::mutex> l(m.cmx);
    if (m.cacheBytes > (48u << 20))
      for (auto it = m.cache.begin(); it != m.cache.end();) {
        double len = it->second->d.size() / (double)SR;
        if (m.now - it->second->lastStart > 30 + len) { m.cacheBytes -= it->second->d.size() * 4; it = m.cache.erase(it); } else ++it;
      }
  }
}

// ---- tools ----------------------------------------------------------------------------------------------------------
void AudioEngine::mixOffline(float* out, int frames) { if (impl->ok) impl->mix(out, frames); }
std::vector<float> AudioEngine::renderSound(const std::string& name, int take, u32 seed) {
  Impl& m = *impl;
  preloadWait(5);
  if (name.rfind("orig:", 0) == 0) {   // the original's own synthesis, even where the pack has the sound
    bool u = m.lib.usePack; m.lib.usePack = false;
    auto d = renderSound(name.substr(5), take, seed);
    m.lib.usePack = u; return d;
  }
  if (name.rfind("recipe:", 0) == 0) { SoundRef r; r.kind = SoundRef::Recipe; r.key = name.substr(7); return m.lib.render(r, take, seed); }
  SoundRef r;
  auto dot = name.find('.');
  if (name.rfind("pack:", 0) == 0) { const Profile* p = m.lib.packs().profile(name.substr(5)); return p ? m.lib.packs().render(*p, seed) : std::vector<float>{}; }
  if (name.rfind("note:", 0) == 0) r = m.lib.note(std::atoi(name.c_str() + 5), "wood");
  else if (dot != std::string::npos && name.find('.', dot + 1) == std::string::npos && name.rfind("fx.", 0) != 0 && name.rfind("vx.", 0) != 0 && name.rfind("mob.", 0) != 0)
    r = m.lib.material(name.substr(0, dot), name.substr(dot + 1), false);
  else r = m.lib.resolve(name);
  if (!r) return {};
  return m.lib.render(r, take, seed);
}
std::vector<std::string> AudioEngine::soundNames() const { return impl->lib.names(); }
void AudioEngine::preloadWait(double seconds) {
  double t0 = steadyNow();
  while (!impl->libReady && steadyNow() - t0 < seconds) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  for (;;) {
    bool idle; { std::lock_guard<std::mutex> l(impl->jmx); idle = impl->jobs.empty(); }
    if (idle) { std::lock_guard<std::mutex> l(impl->cmx); idle = impl->rendering.empty(); }
    if (idle || steadyNow() - t0 > seconds) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

} // namespace dq
