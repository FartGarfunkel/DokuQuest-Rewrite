// client/netplay.cpp — joining, hosting and finding worlds for the game (see client/netplay.h).
#include "client/netplay.h"
#include "client/app.h"
#include "client/session.h"
#include "net/client.h"
#include "net/server_host.h"
#include "mod/modloader.h"
#include "ui/screen.h"

namespace dq {

static net::NetClient* clientOf(App& app) { return app.session && app.session->level->net ? dynamic_cast<net::NetClient*>(app.session->level->net.get()) : nullptr; }
static net::ServerHost* hostOf(App& app) { return app.session && app.session->level->net ? dynamic_cast<net::ServerHost*>(app.session->level->net.get()) : nullptr; }
bool netIsGuest(App& app) { return clientOf(app) != nullptr; }
bool netIsHosting(App& app) { return hostOf(app) != nullptr; }

bool netJoin(App& app, const std::string& address, const std::string& password) {
  app.leaveWorld(false);
  auto nc = std::make_shared<net::NetClient>();
  net::JoinOptions o;
  o.address = address; o.name = app.settings.playerName; o.skin = app.settings.skin; o.password = password; o.viewDistance = app.settings.renderDist;
  if (!nc->connect(o)) { app.toast(nc->error(), "bad"); return false; }
  return netJoinFinish(app, nc, address);
}

std::shared_ptr<net::NetClient> netJoinBegin(App& app, const std::string& address, const std::string& password) {
  auto nc = std::make_shared<net::NetClient>();
  net::JoinOptions o;
  o.address = address; o.name = app.settings.playerName; o.skin = app.settings.skin; o.password = password; o.viewDistance = app.settings.renderDist;
  if (!nc->begin(o)) return nc;   // nc->error() says why; state() is Failed
  return nc;
}

bool netJoinFinish(App& app, std::shared_ptr<net::NetClient> nc, const std::string& address) {
  // a copy of the host's world on this computer: made from the seed, plus the chunks they changed
  std::string tag;
  for (char c : address) tag += isalnum((unsigned char)c) ? c : '_';
  fs::path dir = userSub("cache") / "mp" / tag;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir / "players", ec);
  json pl = nc->playerRecord();
  if (pl.is_object()) writeJson(dir / "players" / (app.settings.playerName + ".json"), pl);
  auto lv = std::make_unique<Level>(nc->levelInfo(), dir, app.pool.get(), &app.events);
  nc->attach(*lv);
  lv->net = nc;
  app.session = std::make_unique<GameSession>(app, std::move(lv));
  app.setScreen(nullptr);
  app.toast("Joined " + nc->serverName + " (hosted by " + nc->hostName + ")", "good");
  return true;
}

bool openToLan(App& app, const LanOpenOptions& o, std::string* err) {
  if (!app.session) { if (err) *err = "No world is open"; return false; }
  Level& lv = *app.session->level;
  if (lv.net) { if (hostOf(app)) return true; if (err) *err = "This is someone else's world"; return false; }
  net::HostOptions h;
  h.name = lv.info.name; h.hostName = app.settings.playerName; h.port = o.port; h.maxPlayers = o.maxPlayers; h.motd = o.motd;
  h.password = o.password; h.lan = o.announce; h.pvp = o.pvp; h.viewDistance = o.viewDistance; h.dedicated = false;
  auto host = std::make_shared<net::ServerHost>(lv, h);
  std::string e;
  if (!host->start(&e)) { if (err) *err = e; app.toast("Could not open the world to LAN: " + e, "bad"); return false; }
  lv.net = host;
  if (app.mods) app.mods->setServing(true);   // plugins run while this world is hosted
  std::string addr = net::localAddress();
  app.toast(strFormat("Open to LAN: %s:%d", addr.empty() ? "this computer" : addr.c_str(), host->port()), "good");
  return true;
}

void closeLan(App& app) {
  net::ServerHost* h = hostOf(app);
  if (!h) return;
  h->stop("The host closed the world.");
  app.session->level->net.reset();
  if (app.mods) app.mods->setServing(false);
  app.toast("The LAN server stopped; your world is just yours again", "");
}

LanStatus lanStatus(App& app) {
  LanStatus s;
  if (net::ServerHost* h = hostOf(app)) {
    s.open = true; s.port = h->port(); s.address = net::localAddress();
    for (auto& p : h->players()) s.players.push_back(p.name);
  }
  return s;
}
bool lanKick(App& app, const std::string& name) { net::ServerHost* h = hostOf(app); return h && h->kick(name); }

static net::LanScanner& scanner() { static net::LanScanner s; return s; }
std::vector<net::LanGame> lanScan() {
  if (!scanner().running()) scanner().start();
  scanner().poll();
  return scanner().list();
}
void lanScanStop() { scanner().stop(); }

void netBeforeLeave(App& app) {
  if (!app.session || !app.session->level->net) return;
  if (net::NetClient* c = clientOf(app)) c->close();
  else if (net::ServerHost* h = hostOf(app)) { h->stop("The host closed the world."); if (app.mods) app.mods->setServing(false); }
  app.session->level->net.reset();
}

void netAfterFrame(App& app) {
  net::NetClient* c = clientOf(app);
  if (!c || !c->lost()) return;
  std::string why = c->lostReason.empty() ? "Lost the connection to the host." : c->lostReason;
  app.leaveWorld(true);   // never left playing a copy of the host's world
  app.toast(why, "bad");
}

} // namespace dq
