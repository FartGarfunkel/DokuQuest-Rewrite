// client/netplay.h — multiplayer for the game's screens (net/netui.js): joining a world over the network,
// "Open to LAN" for the open single-player world, and the list of worlds announced on the home network.
// The network itself is net/ (NetClient, ServerHost); the open level's `net` says which role it plays.
#pragma once
#include "net/lan.h"

namespace dq {

class App;

// Join someone's world ("host[:port]"). Waits up to ~12 s while connecting and fetching the changed chunks
// (TODO for the UI: a "Joining…" screen can drive net::NetClient::begin()/poll() instead). On failure the
// reason is toasted and false returned. App::joinServer() calls this.
bool netJoin(App& app, const std::string& address, const std::string& password = "");

// Pause menu → Open to LAN: serve the open single-player world (the world keeps running while the menu is up).
struct LanOpenOptions { int port = 25580; int maxPlayers = 8; std::string motd, password; bool announce = true, pvp = true; int viewDistance = 12; };
bool openToLan(App& app, const LanOpenOptions& o = {}, std::string* err = nullptr);
void closeLan(App& app);
struct LanStatus { bool open = false; int port = 0; std::string address; std::vector<std::string> players; };
LanStatus lanStatus(App& app);          // the pause menu's "open on 192.168.1.20:25580, playing: ..." line
bool lanKick(App& app, const std::string& name);

bool netIsGuest(App& app);              // playing in someone else's world
bool netIsHosting(App& app);            // this world is open to LAN

// Title → Multiplayer: worlds open on the home network (starts listening on first call; call every frame).
std::vector<net::LanGame> lanScan();
void lanScanStop();

// App hooks: after the session's update each frame (leaves a world whose host went away, with the reason),
// and before a world is left (says goodbye / stops serving).
void netAfterFrame(App& app);
void netBeforeLeave(App& app);

} // namespace dq
