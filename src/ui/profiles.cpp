// ui/profiles.cpp — see profiles.h.
#include "ui/profiles.h"
#include "assets/cape_tex.h"
#include "client/app.h"
#include "client/session.h"
#include "common/settings.h"

namespace dq {

namespace {
int g_editing = 0;   // whose profile sits in the usual settings right now

json& slots(Settings& s) {
  if (!s.extra.is_object()) s.extra = json::object();
  json& p = s.extra["profiles"];
  if (!p.is_array()) p = json::array();
  while (p.size() < 4) p.push_back(json::object());
  for (size_t i = 1; i < 4; i++) {
    if (!p[i].is_object()) p[i] = json::object();
    if (!p[i].contains("name") || !p[i]["name"].is_string()) p[i]["name"] = "Player " + std::to_string(i + 1);
    if (!p[i].contains("skin") || !p[i]["skin"].is_string()) p[i]["skin"] = "";
    if (!p[i].contains("cape") || !p[i]["cape"].is_string()) p[i]["cape"] = "";
  }
  return p;
}
ProfileLook fromSlot(const json& o) { return {o.value("name", std::string()), o.value("skin", std::string()), o.value("cape", std::string())}; }
ProfileLook fromSettings(const Settings& s) { return {s.playerName, s.skin, wornCapeName(s)}; }
} // namespace

int editingProfile() { return g_editing; }

// the game closed while someone else's profile was being edited: put player 1 back where they belong
static void recover(Settings& s) {
  static bool done = false;
  if (done) return;
  done = true;
  int e = s.extra.is_object() ? s.extra.value("profileEditing", 0) : 0;
  if (e > 0 && e < 4 && g_editing == 0) { g_editing = e; editProfile(s, 0); s.save(); }
}

ProfileLook profileLook(Settings& s, int i) {
  recover(s);
  json& p = slots(s);
  if (i <= 0) return g_editing == 0 ? fromSettings(s) : fromSlot(p[g_editing]);   // player 1 is parked in the slot being edited
  if (i > 3) i = 3;
  return i == g_editing ? fromSettings(s) : fromSlot(p[i]);
}

void editProfile(Settings& s, int i) {
  recover(s);
  i = i < 0 ? 0 : i > 3 ? 3 : i;
  if (i == g_editing) return;
  json& p = slots(s);
  auto swapIn = [&](int k) {   // exchange slot k and the usual settings
    ProfileLook was = fromSettings(s), slot = fromSlot(p[k]);
    s.playerName = slot.name.empty() ? "Player " + std::to_string(k + 1) : slot.name; s.skin = slot.skin;
    p[k] = json{{"name", was.name}, {"skin", was.skin}, {"cape", was.cape}};
    setWornCapeName(s, slot.cape);
  };
  if (g_editing != 0) { swapIn(g_editing); g_editing = 0; }   // player 1 back first
  if (i != 0) { swapIn(i); g_editing = i; }
  s.extra["profileEditing"] = g_editing;
}

void applyProfiles(App& a) {
  GameSession* s = a.session.get();
  if (!s) return;
  for (auto& L : s->locals) {
    if (!L.player) continue;
    ProfileLook pl = profileLook(a.settings, L.index);
    if (!pl.name.empty()) L.player->name = pl.name;
    if (L.player->skin != pl.skin) L.player->skin = pl.skin;
    if (pl.cape.empty()) L.player->data.erase("customCape"); else L.player->data["customCape"] = pl.cape;
  }
}

} // namespace dq
