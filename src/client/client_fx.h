// client/client_fx.h — where the simulation's sounds (Level::fx) reach this device's speakers, and the
// session's audio layer: the local player's footsteps (game/local.js), and what the music and the
// ambience need to know each frame (AudioScene: place, hour, light, dimension, a boss nearby).
#pragma once
#include "sim/level.h"

namespace dq {

class GameSession;

class ClientFx : public LevelFx {
public:
  explicit ClientFx(GameSession& s) : s(s) {}
  void sound(const std::string& name, vec3 pos, float range, float volume, float pitch) override;
  void soundUi(u32 playerId, const std::string& name) override;
  void material(const std::string& mat, const std::string& kind, vec3 pos) override;
  void voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) override;
  void particles(const std::string& kind, vec3 pos, int count, const json& extra) override {}   // TODO(particles): hand to the particle renderer
  void message(u32 playerId, const std::string& text, const std::string& kind) override {}     // TODO(hud): toast / chat line
  void shake(u32 playerId, float strength) override {}                                       // TODO(camera): screen shake
private:
  GameSession& s;
};

void registerAudioLayers();   // the session's audio layer (call once at start-up)

} // namespace dq
