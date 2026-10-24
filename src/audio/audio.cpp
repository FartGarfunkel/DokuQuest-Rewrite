// audio/audio.cpp — miniaudio device; the synth engine (audio/synth*.cpp) fills the mix.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>
#include "audio/audio.h"
#include <mutex>

namespace dq {

struct AudioEngine::Impl {
  ma_device device{};
  bool ok = false;
  std::mutex mx;
  float master = 0.6f;
};

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

static void dataCb(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  // silence until the synth engine is plugged in
  std::memset(out, 0, (size_t)frames * 2 * sizeof(float));
}

bool AudioEngine::init(const Settings& s) {
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32; cfg.playback.channels = 2; cfg.sampleRate = 48000;
  cfg.dataCallback = dataCb; cfg.pUserData = impl.get();
  if (ma_device_init(nullptr, &cfg, &impl->device) != MA_SUCCESS) { DQ_WARN("audio: no output device; continuing without sound"); return false; }
  ma_device_start(&impl->device);
  impl->ok = true;
  setVolumes(s);
  return true;
}
void AudioEngine::shutdown() { if (impl && impl->ok) { ma_device_uninit(&impl->device); impl->ok = false; } }
void AudioEngine::setListener(vec3, vec3, vec3) {}
void AudioEngine::setVolumes(const Settings& s) { impl->master = s.volume; }
int AudioEngine::play(const std::string&, float, float) { return 0; }
int AudioEngine::playAt(const std::string&, vec3, float, float, float) { return 0; }
void AudioEngine::material(const std::string&, const std::string&) {}
void AudioEngine::materialAt(const std::string&, const std::string&, vec3) {}
void AudioEngine::note(const std::string&, int, vec3) {}
void AudioEngine::voice(const std::string&, const std::string&, vec3, float) {}
void AudioEngine::setCategoryVolume(const std::string&, float) {}
void AudioEngine::stop(int) {}
void AudioEngine::update(double) {}
bool AudioEngine::ok() const { return impl && impl->ok; }

} // namespace dq
