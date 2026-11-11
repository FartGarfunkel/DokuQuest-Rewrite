// sim/husbandry.cpp — see husbandry.h
#include "sim/husbandry.h"
#include "sim/creature.h"
#include "sim/rules.h"

namespace dq {

namespace {
json& D(Creature& c) { if (!c.data.is_object()) c.data = json::object(); return c.data; }
const json& Dc(const Creature& c) { static const json none = json::object(); return c.data.is_object() ? c.data : none; }
}

bool isMale(const Creature& c) {
  const json& d = Dc(c);
  if (d.contains("sex")) return d["sex"] == "m";
  return ((c.id * 2654435761u) >> 7) & 1;   // (decided once, by who it is)
}
void setMale(Creature& c, bool male) { D(c)["sex"] = male ? "m" : "f"; }
float familiarity(const Creature& c) { return Dc(c).value("fam", 0.f); }

bool husbandryFeed(Level& lv, Creature& c) {
  json& d = D(c);
  if (d.value("famDay", (i64)-99) == lv.info.day) return false;
  d["famDay"] = lv.info.day; d["famSeen"] = lv.info.day;
  d["fam"] = std::min(1.f, d.value("fam", 0.f) + FAM_FEED);
  return true;
}
void husbandryDaily(Level& lv, Creature& c) {
  json& d = D(c);
  if (!d.contains("fam")) return;
  const i64 seen = d.value("famSeen", lv.info.day), last = d.value("famDay", lv.info.day);
  if (seen == lv.info.day) return;
  d["famSeen"] = lv.info.day;
  const i64 missed = lv.info.day - last - 1;   // a day's grace
  if (missed > 0) d["fam"] = std::max(0.f, d.value("fam", 0.f) - FAM_FORGET * (float)std::min<i64>(missed, lv.info.day - seen));
}
bool canBreed(Level& lv, const Creature& a, const Creature& b) {
  if (!rule(lv, "immersive")) return true;
  return isMale(a) != isMale(b) && familiarity(a) >= FAM_BREED && familiarity(b) >= FAM_BREED;
}
std::string husbandryLabel(const Creature& c) {
  static const std::map<std::string, std::pair<const char*, const char*>> N = {{"cow", {"Bull", "Cow"}}, {"sheep", {"Ram", "Ewe"}}, {"pig", {"Boar", "Sow"}}, {"chicken", {"Rooster", "Hen"}},
    {"horse", {"Stallion", "Mare"}}, {"goat", {"Billy", "Nanny"}}, {"donkey", {"Jack", "Jenny"}}, {"llama", {"Male", "Female"}}, {"turkey", {"Tom", "Hen"}}, {"duck", {"Drake", "Duck"}}};
  auto it = N.find(c.type);
  const std::string sex = it != N.end() ? (isMale(c) ? it->second.first : it->second.second) : (isMale(c) ? "Male" : "Female");
  return sex + " \xC2\xB7 familiarity " + std::to_string((int)std::lround(familiarity(c) * 100)) + "%";
}

} // namespace dq
