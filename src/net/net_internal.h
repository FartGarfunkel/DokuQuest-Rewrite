// net/net_internal.h — pieces the server host plugs into the level (not for use outside net/).
#pragma once
#include "net/server_host.h"

namespace dq::net {

// Every block / block-entity change in a served realm, for everyone (net.js: World.setBlock reports each change).
class NetWatch : public WorldSystem {
public:
  explicit NetWatch(ServerHost& h) : host(h) {}
  const char* name() const override { return "net"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId oldId, BlockId newId) override;
  void onBlockEntityChanged(World& w, int x, int y, int z) override;
  ServerHost& host;
};

// The level's sounds, puffs and messages: played here as before, and passed on to the players near them.
class NetFx : public LevelFx {
public:
  NetFx(ServerHost& h, LevelFx* inner) : host(h), inner(inner) {}
  void sound(const std::string& name, vec3 pos, float range, float volume, float pitch) override;
  void soundUi(u32 playerId, const std::string& name) override;
  void material(const std::string& mat, const std::string& kind, vec3 pos) override;
  void voice(const std::string& creature, const std::string& kind, vec3 pos, float pitch) override;
  void particles(const std::string& kind, vec3 pos, int count, const json& extra) override;
  void message(u32 playerId, const std::string& text, const std::string& kind) override;
  void shake(u32 playerId, float strength) override;
  ServerHost& host;
  LevelFx* inner;
};

} // namespace dq::net
