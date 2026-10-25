// net/server_host.h — serving a Level over the network (the original net/net.js host side + game/server.js).
// Used by the dedicated server (server/server.h, launcher + headless) and by "Open to LAN" for a single-player
// world (client/netplay.h). The server runs the world: blocks, creatures, items, time, weather, fluids and
// saving. Each joined player is a Player entity in the level (a "mirror": ghost = true, moved by its owner's
// game); everything the server's own systems do to a mirror (damage, knockback, teleport, items given,
// game mode) is noticed and passed on to that player's game, so gameplay code treats remote players like local ones.
#pragma once
#include "net/protocol.h"
#include "net/lan.h"
#include "sim/level.h"
#include "sim/player.h"
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace dq::net {

struct HostOptions {
  std::string name = "DokuQuest Server";   // shown in LAN lists
  std::string hostName = "Server";         // who hosts ("Server" for a dedicated server, else the host player's name)
  int port = DEFAULT_PORT;                 // tries the next 9 ports when taken, like the original
  int maxPlayers = 8;
  std::string motd;                        // said to each player soon after they arrive
  std::string password;                    // "" = anyone may join
  bool lan = true;                         // announce on the home network
  int viewDistance = 8;                    // chunks; a player's own setting is capped to this
  bool pvp = true;
  bool dedicated = true;                   // false = "Open to LAN" (the host plays in the same level)
};

struct RCKey {
  int realm, dim, cx, cz;
  bool operator==(const RCKey& o) const { return realm == o.realm && dim == o.dim && cx == o.cx && cz == o.cz; }
};
struct RCKeyHash { size_t operator()(const RCKey& k) const { return ChunkKeyHash()({k.dim + k.realm * 8, k.cx, k.cz}); } };

class ServerHost : public LevelNet {
public:
  ServerHost(Level& lv, HostOptions o);
  ~ServerHost() override;
  bool start(std::string* err = nullptr);
  void stop(const std::string& why = "The host closed the world.");   // saves everyone, tells them, closes the port
  bool running() const { return host != nullptr; }
  int port() const { return boundPort; }

  // LevelNet
  bool isClient() const override { return false; }
  void update(Level& lv, double dt) override;
  void streamAnchors(int realm, std::vector<StreamAnchor>& out) override;
  void portal(Player& who, int realm, vec3 to) override;
  std::vector<std::string> playerNames() const override;

  // management (console, chat commands, the launcher's Server tab)
  struct PlayerInfo { std::string name, address; u32 entity = 0; int realm = 0; vec3 pos{0}; float hp = 20; bool op = false; u32 rttMs = 0; };
  std::vector<PlayerInfo> players() const;
  Player* player(const std::string& name) const;          // remote players and the host's own
  bool kick(const std::string& name, const std::string& why = "");
  bool ban(const std::string& name, const std::string& why = "");
  bool pardon(const std::string& nameOrIp);
  bool banIp(const std::string& ip);
  void setOp(const std::string& name, bool on);
  bool isOp(const std::string& name) const;
  void say(const std::string& text, const std::string& from = "Server");   // "<from> text" to everyone (the original's serverSay)
  bool tell(const std::string& name, const std::string& text, const std::string& kind = "");
  bool teleport(const std::string& name, vec3 pos, int realm = -1);
  void flushSaves();                                       // ask everyone for their latest bags now (before stopping)
  void saveAll();                                          // remote players into the world (players/<name>.json)
  void serverLog(const std::string& text, const std::string& kind = "");   // console line + the mods' "log" event
  std::function<void(const std::string& text, const std::string& kind)> onLog;   // the launcher's console view
  json lanInfo() const;

  HostOptions opts;
  Level& level;

  // ---- internals shared with the sync code (server_sync.cpp) ----
  struct SentEnt { size_t hash = 0, dataHash = 0; double at = 0; };
  struct Snap { json s; size_t hash = 0, dataHash = 0; };
  struct CkQ { RCKey k; bool force; int d; };
  struct Peer {
    u32 id = 0; struct _ENetPeer* enet = nullptr; std::string addr;
    bool hello = false, ready = false, left = false;
    std::string name, skin; int R = 6;
    Player* pl = nullptr; u32 eid = 0;
    // what the player's own game last told us (anything else on the mirror was done by the server)
    vec3 aPos{0}, aVel{0}; float aHp = 20; std::string aInv, aMode; int aXp = 0, aLevel = 0, aRealm = 0; bool aFly = false, aDead = false;
    int invSeq = 0;
    double allowJumpUntil = 0, lastMeAt = 0, savedAt = 0, joinedAt = 0, motdAt = -1, chunkBytes = 0;
    std::unordered_set<RCKey, RCKeyHash> inRange;
    std::vector<CkQ> ckQ;
    std::unordered_map<u32, SentEnt> sent;
    std::vector<std::pair<std::string, json>> fxOut;
  };

private:
  friend class NetWatch; friend class NetFx;
  struct _ENetHost* host = nullptr;
  int boundPort = 0;
  u32 nextPeerId = 1;
  std::map<u32, Peer> peers;
  class NetFx* fx = nullptr; LevelFx* innerFx = nullptr;
  std::shared_ptr<class NetWatch> watch;
  std::set<int> watched;
  LanAnnouncer lanAnn;
  double t = 0, tEnts = 0, tEnv = 0, tChunks = 0, tSave = 0;
  bool logging = false;
  // queued for everyone
  struct BlkQ { int realm, x, y, z; BlockId id; u8 meta; };
  std::vector<BlkQ> blockQ;
  std::map<std::tuple<int, int, int, int>, json> beQ;                       // realm,x,y,z -> e
  std::map<std::tuple<int, int, int, int>, std::map<u32, int>> beAck;      // which client changes each copy includes
  struct Pending { int realm, x, y, z; BlockId id; u8 meta; double until; u32 from; };
  std::vector<Pending> pendingBlocks;                                       // changes to chunks still loading here
  std::vector<json> fxS, fxM, fxV, fxP;
  json bans, ops;
  std::vector<std::string> palette;

  Peer* peerOf(struct _ENetPeer* e);
  Peer* peerByName(const std::string& name);
  Peer* peerByEntity(u32 eid);
  void receive(Peer& p, u8 t, json& m);
  void welcome(Peer& p, const json& m);
  void refuse(Peer& p, const std::string& why);
  void peerLeft(Peer& p, bool quiet = false);
  void applyMe(Peer& p, const json& m);
  void applySave(Peer& p, const json& m);
  void applyBlocks(Peer& p, const json& m);
  void applyBlockEntity(Peer& p, const json& m);
  void hostChat(Peer& p, const std::string& text);
  void diffMirrors();
  void toAll(Msg t, const json& m, u32 except = 0, int channel = 0);
  vec3 spawnFor(const std::string& name);
  void loadLists();
  void saveLists();
  void ensureWatchers();
  // server_sync.cpp
  void updateRange(Peer& p, bool first);
  void flushChunks(Peer& p, int maxRecords);
  void sendEntities(Peer& p, const std::unordered_map<u32, Snap>& snaps);
  void flushBlocks();
  void flushEffects();
  void sendEnv();
  void queueFx(char kind, json item);
  void sendFxTo(Peer& p, const char* key, json item);
};

std::string playerInvSig(const Player& p);   // what a player carries, in short (netInvSig)
json playerInvJson(const Player& p);         // inv/armor/offhand/selected/xp/level
void applyPlayerInv(Player& p, const json& j);
json entitySnapshot(const Entity& e);        // what clients see of an entity (the original's netSnap)

void registerNetCommands();                  // /list /kick /ban /pardon /op /deop /msg (mod/register.cpp calls it)

} // namespace dq::net
