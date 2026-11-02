// net/client.h — playing in someone else's world (the guest side of net/net.js). The game builds the same
// terrain from the seed and is sent the chunks that were changed; it is told about every block that changes and
// sees the server's creatures, items and players as ghosts that glide to where the server says they are. Each
// player moves their own body and keeps their own bags; the server keeps a copy (by name) so you come back
// with them, and passes on what its world does to you (damage, knockback, teleports, items given...).
#pragma once
#include "net/protocol.h"
#include "sim/level.h"
#include "sim/player.h"
#include <unordered_map>
#include <unordered_set>

namespace dq::net {

struct JoinOptions {
  std::string address;       // "host[:port]"
  std::string name = "Player", skin, password;
  json skinData;             // the skin itself (netSkinHook): sent so the others see it
  int viewDistance = 8;      // chunks; the server may cap it
  double timeout = 15;       // seconds to reach the host; once it answers, joining goes on as long as the world keeps
                             // coming (silence for `quiet` seconds gives up): over a home internet upload the first
                             // download of a well-played world takes far longer than on a home network
  double quiet = 25, joinCap = 300;
};

class NetClient : public LevelNet {
public:
  NetClient();
  ~NetClient() override;
  enum class State { Idle, Connecting, Joining, Ready, Failed, Closed };

  // Joining: begin() + poll() every frame until Ready/Failed (for a "Joining…" screen), or connect() which waits.
  bool begin(const JoinOptions& o);
  State poll();
  bool connect(const JoinOptions& o);
  State state() const { return st; }
  const std::string& error() const { return err; }
  float progress() const;

  // After Ready: the level to build (seed, world type, rules, time... remote = true), our saved body, and then
  // attach() the level made from it (before the GameSession is created, so the local player gets a client id).
  LevelInfo levelInfo() const;
  json playerRecord() const { return welcomeMsg.value("pl", json()); }
  void attach(Level& lv);

  // LevelNet
  bool isClient() const override { return true; }
  void update(Level& lv, double dt) override;
  bool chat(Player& from, const std::string& text) override;
  void hitEntity(Player& by, u32 entityId, float amount, const json& src) override;
  void action(Player& by, const std::string& name, const json& args) override;
  void portal(Player& who, int realm, vec3 to) override;
  std::vector<std::string> playerNames() const override;

  // Couch players (split screen in someone else's world): each one joins the host on a light link of its own (HELLO
  // "sub"), so the host has a body for them with their own bags and health; the world itself still comes over ours.
  // Their hits, chat and actions go out on their link, and their bodies on the host are hidden here (they're local).
  bool addCouch(Player& p, const json& skinData);
  void removeCouch(Player& p);

  bool lost() const { return st == State::Closed || st == State::Failed; }
  std::string lostReason;                  // why the server let us go (kicked, closed, connection lost)
  std::string serverName, hostName;
  bool op = false, dedicated = true;
  int viewDistance = 8;
  void close(const std::string& why = "");   // leave politely (sends our latest bags)

  // for tests and the debug overlay
  struct Stats { u64 msgsIn = 0, msgsOut = 0, records = 0, sumsIn = 0, sums = 0, needs = 0, mismatches = 0, blocksIn = 0, blocksOut = 0; } stats;   // needs: asked again (a mismatch, or a chunk the host changed before it loaded here)
  size_t sumsWaiting() const { return sums.size(); }
  Player* localPlayer() const { return me; }

private:
  friend class ClientWatch;
  struct _ENetHost* host = nullptr;
  struct _ENetPeer* peer = nullptr;
  State st = State::Idle;
  std::string err;
  JoinOptions opts;
  double startedAt = 0, t = 0, heardAt = 0;
  json welcomeMsg;
  int expectRecords = -1, gotRecords = 0;
  Level* lv = nullptr;
  Player* me = nullptr;
  std::shared_ptr<class ClientWatch> watch;
  std::unordered_set<int> watched;
  std::vector<BlockId> s2l, l2s;            // server block ids <-> ours (by key)
  struct Early { int realm, dim, cx, cz; std::vector<u8> rec; };
  std::vector<Early> early;                 // records that came before the level existed
  std::unordered_map<u64, u32> sums;        // chunk -> the server's checksum, waiting for ours
  std::unordered_set<u64> stale;            // changed on the server while we didn't have it: ask when it loads
  std::unordered_set<u64> fresh;            // a record came while the chunk may have been generating
  std::vector<u64> replaceQ;
  std::unordered_map<std::string, int> bePend;   // our block-entity changes the server hasn't taken in yet
  int beSeq = 0, invSeq = 0, myId = 0;
  u32 myEid = 0;   // our player's entity id on the host
  json blkOut = json::object();             // realm -> [[x,y,z,id,meta]...]
  double tMe = 0, tSave = 0, tInv = 0, saveDue = 0, tSums = 0;
  std::string invSig; json lastSave;
  int lastRealm = 0;
  bool applying = false, applyingBE = false;
  // couch players' links (see addCouch); a link of that kind only carries its player's own news
  bool subMode = false; Player* subMe = nullptr; bool subReady = false;
  std::vector<std::unique_ptr<NetClient>> subs;
  std::unordered_set<u32> hideEids;
  NetClient* subFor(const Player& p) const;
  void updateSub(Level& level, double dt);

  void handle(u8 ty, json& m);
  void onWelcome(json& m);
  void applyEntities(const json& m);
  void applySelf(const json& m);
  void applyEnv(const json& m);
  void applyFx(const json& m);
  void storeRecord(int realm, int dim, int cx, int cz, std::vector<u8>&& rec, bool early);
  void sendMe();
  void sendSave();
  void findMe();
  void sendTo(Msg t, const json& m, int channel = 0, bool reliable = true);
  void disconnected(u32 code);
  BlockId toLocal(u32 sid) const { return sid < s2l.size() ? s2l[sid] : AIR; }
  u32 toServer(BlockId lid) const { return lid < l2s.size() ? l2s[lid] : 0; }
};

u64 packChunkKey(int realm, int dim, int cx, int cz);

} // namespace dq::net
