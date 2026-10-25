// sim/places.cpp — finding places; your houses; the list for the journal of places.
#include "sim/places.h"
#include "gen/structures_api.h"
#include <regex>

namespace dq {

std::string foundText(const std::string& name) {
  static const std::regex art("^(the|an?) ", std::regex::icase);
  std::string n = name;
  if (std::regex_search(n, art)) { size_t sp = n.find(' '); for (size_t i = 0; i < sp; i++) n[i] = (char)tolower((unsigned char)n[i]); }
  else n = "a " + n;
  return "You have found " + n + "!";
}

void placesTick(Level& lv, double dt) {
  static std::unordered_map<const Level*, float> timers;
  float& t = timers[&lv];
  if ((t -= (float)dt) > 0) return;
  t = 1;
  auto& hook = contentHooks().placesNear;
  if (!hook) return;
  for (Player* p : levelPlayers(lv)) {
    if (p->dead) continue;
    for (const PlaceInfo& S : hook(lv, p->realm, p->pos, 28)) {
      if (S.id.empty() || S.realm != p->realm) continue;
      json& st = p->stats;
      if (!st.contains("sseen") || !st["sseen"].is_object()) st["sseen"] = json::object();
      if (st["sseen"].contains(S.id)) continue;
      float pad = 6;
      bool inside = p->pos.x >= S.x0 - pad && p->pos.x <= S.x1 + pad && p->pos.z >= S.z0 - pad && p->pos.z <= S.z1 + pad;
      if (!inside || (S.y >= 0 && std::fabs(p->pos.y - S.y) >= 24)) continue;
      st["sseen"][S.id] = json::array({(int)std::lround((S.x0 + S.x1) / 2.0), (int)std::lround((S.z0 + S.z1) / 2.0), S.name, jsDimOf(*p)});
      toast(lv, *p, foundText(S.name), "good");
      grantXp(lv, *p, 25, p->pos + vec3(0, 2, 0));
      if (lv.events) lv.events->emit("placeFound", json::array({p->name, S.id, S.name}));
    }
  }
}

void registerPlacesNearHook() {
  ContentHooks& C = contentHooks();
  if (C.placesNear) return;
  C.placesNear = [](Level& lv, int realm, vec3 pos, float pad) {
    std::vector<PlaceInfo> out;
    if (realm != REALM_MAIN || !lv.gen) return out;
    const int dim = dimOfY(ifloor(pos.y));
    const StructureIndex* idx = structureIndexOf(lv.gen->forDim(dim));
    if (!idx || !idx->active()) return out;
    std::vector<const StructurePlan*> plans;
    idx->near(ifloor(pos.x), ifloor(pos.z), (int)pad, plans);
    const auto& T = structureTypes();
    for (auto* P : plans) {
      if (!P || P->id.empty()) continue;
      bool village = (P->type >= 0 && P->type < (int)T.size() && T[P->type].key == "village") || (P->data.is_object() && P->data.value("kind", std::string()) == "village");
      if (village) continue;
      PlaceInfo o; o.id = P->id; o.name = P->name; o.realm = realm; o.x0 = P->x0; o.z0 = P->z0; o.x1 = P->x1; o.z1 = P->z1; o.y = P->y + dimBase(dim);
      out.push_back(o);
    }
    return out;
  };
}

void homeSettle(Level& lv, Player& p, const std::string& vid, ivec3 bed, ivec3 door, const std::string& name) {
  if (!p.stats.contains("homes") || !p.stats["homes"].is_object()) p.stats["homes"] = json::object();
  p.stats["homes"][vid] = {{"bed", {bed.x, bed.y, bed.z}}, {"door", {door.x, door.y, door.z}}, {"name", name}};
  p.spawnPoint = vec3(bed.x + 0.5f, bed.y + 0.6f, bed.z + 0.5f); p.spawnRealm = REALM_MAIN;
  toast(lv, p, "Your house in " + name + " is ready! Its bed is your spawn now, and there's a chest of gifts inside.", "epic");
}

std::vector<FoundPlace> foundPlaces(Level& lv, const Player& p) {
  std::vector<FoundPlace> out;
  const json& S = p.stats;
  const json& D = lv.info.data;
  if (!S.is_object()) return out;
  auto& H = contentHooks();
  if (S.contains("sseen") && S["sseen"].is_object())
    for (auto& [id, v] : S["sseen"].items()) {
      if (!v.is_array() || v.size() < 3) continue;
      FoundPlace f; f.id = id; f.kind = "place"; f.x = v[0].get<float>(); f.z = v[1].get<float>(); f.name = v[2].is_string() ? v[2].get<std::string>() : id;
      f.dim = v.size() > 3 ? v[3].get<int>() : 0;
      f.cleared = D.contains("structs") && D["structs"].contains(id) && D["structs"][id].value("cleared", 0) != 0;
      out.push_back(f);
    }
  if (S.contains("vseen") && S["vseen"].is_object())
    for (auto& [id, _] : S["vseen"].items()) {
      FoundPlace f; f.id = id; f.kind = "village"; f.name = "A village";
      if (H.villageById) if (auto v = H.villageById(lv, id)) { f.name = v->name; f.x = (float)v->x; f.z = (float)v->z; }
      out.push_back(f);
    }
  if (S.contains("dseen") && S["dseen"].is_object())
    for (auto& [id, _] : S["dseen"].items()) {
      FoundPlace f; f.id = id; f.kind = "dungeon"; f.name = "A dungeon";
      if (H.dungeonById) if (auto d = H.dungeonById(lv, id)) { f.name = d->name; f.x = (d->x0 + d->x1) * 0.5f; f.z = (d->z0 + d->z1) * 0.5f; }
      f.cleared = D.contains("dungeons") && D["dungeons"].contains(id) && D["dungeons"][id].value("cleared", false);
      out.push_back(f);
    }
  if (S.contains("ways") && S["ways"].is_object())
    for (auto& [id, r] : S["ways"].items()) {
      FoundPlace f; f.id = id; f.kind = "waystone"; f.name = r.value("name", std::string("A waystone")); f.x = r.value("x", 0.f); f.z = r.value("z", 0.f); f.dim = r.value("dim", 0);
      out.push_back(f);
    }
  if (S.contains("homes") && S["homes"].is_object())
    for (auto& [vid, h] : S["homes"].items()) {
      if (!h.contains("door")) continue;
      FoundPlace f; f.id = vid; f.kind = "home"; f.name = "Your house in " + h.value("name", std::string("the village")); f.x = h["door"][0].get<float>(); f.z = h["door"][2].get<float>();
      out.push_back(f);
    }
  return out;
}

} // namespace dq
