// sim/gamecommands.cpp — port of game/commands.js (the game's chat commands and WorldEdit) and /weather
// (game/weather.js), registered into the chat command registry (sim/commands.h).
#include "sim/gamecommands.h"
#include "sim/gameplay.h"
#include "sim/combat.h"
#include "sim/rules.h"
#include "sim/worldsys.h"
#include "sim/magic.h"
#include "sim/weather.h"
#include "gen/structures_api.h"
#include "gen/generator.h"
#include <map>
#include <random>
#include <regex>
#include <set>

namespace dq {
bool setCreatureLook(Entity& e, const std::string& look);   // sim/creature.cpp (creature.h clashes with gameplay.h here)
void keepSummoned(Entity& e);
int spawnLookRow(Level& lv, int realm, vec3 at, float yaw, const std::string& type);
std::string spawnLineup(Level& lv, int realm, vec3 at, float yaw, int page, int per);
std::string driftTravel(Level& lv, Player& p);   // sim/drift.cpp

static double rnd() { static thread_local std::mt19937 g{std::random_device{}()}; return std::uniform_real_distribution<double>(0, 1)(g); }

struct CommandCtx { Level& lv; Player& p; };
struct CommandUsage {};   // thrown by a command to print its usage line
using GameCmd = std::function<std::string(CommandCtx&, std::vector<std::string>&)>;
// A game command in the registry: a player runs it; usage lines and errors come back as text.
// A creature named the way a player types it: our key, "@e[type=x]", or a Minecraft name for ours (zombie is a ghoul,
// creeper a fuseling). "" when there's none.
static std::string creatureKey(std::string t) {
  t = toLower(t);
  if (t.rfind("@e[type=", 0) == 0) { t = t.substr(8); if (!t.empty() && t.back() == ']') t.pop_back(); }
  if (t.rfind("minecraft:", 0) == 0) t = t.substr(10);
  static const std::map<std::string, std::string> mc = {{"zombie", "ghoul"}, {"creeper", "fuseling"}, {"spider_jockey", "spider"},
    {"husk", "ghoul"}, {"drowned", "ghoul"}, {"zombie_villager", "ghoul"}, {"stray", "skeleton"}};
  const json& M = reg().mobs;
  if (!M.is_object()) return "";
  if (M.contains(t)) return t;
  if (auto it = mc.find(t); it != mc.end() && M.contains(it->second)) return it->second;
  return "";
}
// what to say for a name that isn't a creature: a few that look like it, not all hundred and twenty
static std::string noCreature(const std::string& typed) {
  const json& M = reg().mobs; std::string near; int n = 0;
  const std::string t = toLower(typed);
  if (M.is_object()) for (auto& [k, v] : M.items()) if (!t.empty() && (k.find(t) != std::string::npos || (t.size() >= 3 && k.rfind(t.substr(0, 3), 0) == 0)) && n < 8) { near += (n ? ", " : "") + k; n++; }
  return "No creature called " + typed + (n ? ". Did you mean: " + near + "?" : ". /lineup shows them all.");
}

static void addCommand(const std::string& name, const std::string& usage, const std::string& desc, bool cheat, GameCmd f) {
  registerChatCommand(name, desc, [name, usage, f](CommandSource& src, const std::vector<std::string>& args) -> std::string {
    // the server console has no player (the original parked a pawn for it): commands that only change the world
    // (time, weather, rules) run with a stand-in that isn't in the world
    static const std::set<std::string> worldOnly = {"time", "weather", "gamerule", "daylength"};
    if (!src.level || (!src.player && !worldOnly.count(name))) return "Only a player in the world can use /" + name;
    static thread_local Player console;
    CommandCtx ctx{*src.level, src.player ? *src.player : console};
    std::vector<std::string> a = args;
    try { return f(ctx, a); }
    catch (CommandUsage&) { return "Usage: /" + name + " " + usage; }
    catch (std::exception& e) { return std::string("Error: ") + e.what(); }
  }, cheat, usage);
}

static std::string thousands(long long n) {
  std::string s = std::to_string(n < 0 ? -n : n), out;
  for (size_t i = 0; i < s.size(); i++) { if (i && (s.size() - i) % 3 == 0) out += ','; out += s[i]; }
  return n < 0 ? "-" + out : out;
}
static std::string cleanName(std::string n) {
  n = toLower(n);
  static const std::regex pre("^minecraft:|^mcl_[a-z]+:|^default:");
  n = std::regex_replace(n, pre, "");
  size_t a = n.find_first_not_of(" \t"), b = n.find_last_not_of(" \t");
  n = a == std::string::npos ? "" : n.substr(a, b - a + 1);
  std::string o; bool sp = false;
  for (char c : n) { if (c == ' ' || c == '\t') { if (!sp) o += '_'; sp = true; } else { o += c; sp = false; } }
  return o;
}
static const std::unordered_map<std::string, std::string>& nameAliases() {
  static const std::unordered_map<std::string, std::string> a = {
    {"oak_planks", "planks"}, {"oak_log", "log"}, {"oak_leaves", "leaves"}, {"oak_sapling", "sapling"}, {"grass_block", "grass"}, {"short_grass", "tall_grass"},
    {"dandelion", "flower_yellow"}, {"poppy", "flower_red"}, {"deepslate", "slate"}, {"cobbled_deepslate", "cobbled_slate"}, {"polished_deepslate", "polished_slate"},
    {"deepslate_bricks", "slate_bricks"}, {"glowstone", "glowstone"}, {"white_wool", "wool"}, {"hay_block", "hay_bale"}, {"sweet_berry_bush", "berry_bush"},
    {"oak_slab", "oak_slab"}, {"smooth_stone_slab", "smooth_stone_slab"}, {"air", "air"}};
  return a;
}
int resolveBlock(const std::string& name) {
  if (name.empty()) return -1;
  const Registry& R = reg();
  std::string n = cleanName(name);
  if (n == "air" || n == "0") return 0;
  if (R.blockIds.count(n) || R.blockAliases.count(n)) return R.blockId(n);
  auto al = nameAliases().find(n);
  if (al != nameAliases().end() && R.blockIds.count(al->second)) return R.blockId(al->second);
  std::string words = n; std::replace(words.begin(), words.end(), '_', ' ');
  for (auto& b : R.blocks) if (toLower(b.name) == words) return b.id;
  for (auto& b : R.blocks) if (b.key.find(n) != std::string::npos && R.item(b.key)) return b.id;
  return -1;
}
std::string resolveItem(const std::string& name) {
  if (name.empty()) return "";
  const Registry& R = reg();
  std::string n = cleanName(name);
  if (R.item(n)) return n;
  auto al = nameAliases().find(n);
  if (al != nameAliases().end() && R.item(al->second)) return al->second;
  std::string words = n; std::replace(words.begin(), words.end(), '_', ' ');
  for (auto& d : R.items) if (!d.key.empty() && toLower(d.name) == words) return d.key;
  std::string best;
  for (auto& d : R.items) if (!d.key.empty() && d.key.find(n) != std::string::npos && (best.empty() || d.id < R.item(best)->id)) best = d.key;
  return best;
}

// A block pattern: "stone", "stone:2" (with meta), or a mix "60%stone,40%andesite".
struct PatternPart { BlockId id; u8 meta; double w; };
struct Pattern {
  std::vector<PatternPart> list; double total = 0; std::string error;
  PatternPart pick() const { if (list.size() == 1) return list[0]; double r = rnd() * total; for (auto& o : list) { r -= o.w; if (r <= 0) return o; } return list.back(); }
};
static bool parsePattern(const std::string& text, Pattern& out) {
  if (text.empty()) return false;
  static const std::regex re("^(?:(\\d+(?:\\.\\d+)?)%)?([^:]+)(?::(\\d+))?$");
  for (auto& part : splitStr(text, ',')) {
    std::string p = part; p.erase(0, std::min(p.find_first_not_of(' '), p.size())); while (!p.empty() && p.back() == ' ') p.pop_back();
    if (p.empty()) continue;
    std::smatch m;
    if (!std::regex_match(p, m, re)) return false;
    int id = resolveBlock(m[2]);
    if (id < 0) { out.error = "Unknown block: " + m[2].str(); return true; }
    double w = m[1].matched ? std::stod(m[1]) : 1;
    out.list.push_back({(BlockId)id, (u8)(m[3].matched ? std::stoi(m[3]) & 255 : 0), w}); out.total += w;
  }
  return !out.list.empty();
}
// "~" means where you are; "~3" three blocks off.
static int coord(Player& p, const std::vector<std::string>& a, size_t i, int axis) {
  if (i >= a.size()) throw CommandUsage{};
  const std::string& s = a[i];
  float base = axis == 0 ? p.pos.x : axis == 1 ? p.pos.y : p.pos.z;
  try {
    if (!s.empty() && s[0] == '~') return (int)std::floor(base + (s.size() > 1 ? std::stod(s.substr(1)) : 0));
    size_t used = 0; double v = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(v)) throw CommandUsage{};
    return (int)std::floor(v);
  } catch (std::invalid_argument&) { throw CommandUsage{}; } catch (std::out_of_range&) { throw CommandUsage{}; }
}
static double num(const std::vector<std::string>& a, size_t i, double def = NAN) {
  if (i >= a.size()) return def;
  try { size_t u = 0; double v = std::stod(a[i], &u); return u == a[i].size() ? v : NAN; } catch (...) { return NAN; }
}

std::string completeCommand(const std::string& v, std::string& hint) {
  hint.clear();
  if (v.empty() || v[0] != '/') return v;
  std::vector<std::string> parts = splitStr(v, ' ');
  if (parts.size() == 1) {
    bool we = v.size() > 1 && v[1] == '/';
    std::string want = v.substr(std::min(v.find_first_not_of('/'), v.size())), slash = we ? "//" : "/";
    std::vector<std::string> names;
    for (auto& c : allChatCommands()) if (startsWith(c.name, want) && c.name != "herobrine") names.push_back(c.name);
    if (names.size() == 1) return slash + names[0] + " ";
    for (auto& n : names) hint += slash + n + "  ";
    return v;
  }
  std::string last = toLower(parts.back());
  if (last.empty()) return v;
  std::vector<std::string> keys;
  for (auto& d : reg().items) if (!d.key.empty() && startsWith(d.key, last)) { keys.push_back(d.key); if (keys.size() >= 12) break; }
  if (keys.size() == 1) { parts.back() = keys[0]; std::string o; for (auto& s : parts) o += s + " "; return o; }
  for (auto& k : keys) hint += k + "  ";
  return v;
}

// ---- the commands ----------------------------------------------------------------------------
static void registerBuiltinCommands() {
  auto C = [](const std::string& n, const std::string& usage, const std::string& desc, bool cheat, GameCmd f) { addCommand(n, usage, desc, cheat, std::move(f)); };
  // (/help is the registry's own: it lists every command, the game's, WorldEdit's, the server's and mods')
  C("herobrine", "[trace]", "\xE2\x80\xA6", false, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (c.lv.events) c.lv.events->emit("herobrine", json::array({c.p.name, a.empty() ? "" : a[0]}));
    return "Removed Herobrine.";
  });
  C("say", "<message>", "Say something", false, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    std::string m; for (auto& s : a) m += (m.empty() ? "" : " ") + s;
    c.lv.fx->message(0, "[" + c.p.name + "] " + m, "chat");
    return "";
  });
  C("spell", "learn <spell|all> | right <spell> | left <spell|none> | cast", "Learn, ready or cast spells", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    const std::string op = a.empty() ? "" : toLower(a[0]), k = a.size() > 1 ? toLower(a[1]) : "";
    const MagicData& md = magicData();
    if (op == "learn") {
      json& sp = c.p.stats["spells"];
      if (!sp.is_array()) sp = json::array();
      int n = 0;
      for (auto& s : md.spellOrder) {
        if (k != "all" && s != k) continue;
        bool has = false; for (auto& x : sp) has |= x == s;
        if (!has) { sp.push_back(s); n++; }
      }
      if (!n && k != "all") return "No such spell (or already known): " + k;
      return "Learned " + std::to_string(n) + " spell" + (n == 1 ? "" : "s");
    }
    if (op == "right") { if (!pickSpell(c.lv, c.p, k)) return "You don't know " + k; return "Right hand: " + k; }
    if (op == "left") { setOffSpell(c.lv, c.p, k == "none" ? "" : k); return "Left hand: " + offSpell(c.p); }
    if (op == "cast") { MagicState& m = magicState(c.lv, c.p); m.castCd = 0; m.bare = !heldStaff(c.p); castSpell(c.lv, c.p); m.bare = false; return "Cast"; }
    throw CommandUsage{};
  });
  C("locate", "<structure, e.g. end_city, village, nether_fortress>", "Find the nearest structure of a kind", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.empty() || !c.lv.gen) throw CommandUsage{};
    const std::string kind = toLower(a[0]);
    const int here = dimOfY(ifloor(c.p.pos.y));
    for (int dim : {here, (int)DIM_OVERWORLD, (int)DIM_NETHER, (int)DIM_END, (int)DIM_AETHER}) {
      if (const StructurePlan* s = locateStructure(*c.lv.gen, dim, kind, ifloor(c.p.pos.x), ifloor(c.p.pos.z), 16))
        // y as /tp takes it (the stacked world's y, not the land's own); another land is named, not measured
        return strFormat("%s at %d, %d, %d (%s)", s->name.empty() ? kind.c_str() : s->name.c_str(), s->cx, dimBase(dim) + s->y, s->cz,
                         dim == here ? strFormat("%d blocks away", (int)std::lround(std::hypot(s->cx - c.p.pos.x, s->cz - c.p.pos.z))).c_str()
                                     : (std::string("in ") + dimName(dim)).c_str());
    }
    return "No " + kind + " found nearby";
  });
  C("seed", "", "Show the world seed", false, [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    return "Seed: " + (c.lv.info.seedText.empty() ? std::to_string(c.lv.info.seed) : c.lv.info.seedText);
  });
  C("gamemode", "<survival|creative|adventure|spectator>", "Change game mode", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    std::string m = a.empty() ? "" : toLower(a[0]);
    std::string mode = startsWith(m, "c") || m == "1" ? "creative" : m == "sp" || startsWith(m, "spec") || m == "3" ? "spectator" :
                       startsWith(m, "s") || m == "0" ? "survival" : startsWith(m, "a") || m == "2" ? "adventure" : "";
    if (mode.empty()) throw CommandUsage{};
    c.lv.info.mode = mode; c.p.mode = mode;
    if (mode == "survival" || mode == "adventure") c.p.flying = false;
    if (mode == "spectator") c.p.flying = true;
    return "Game mode: " + mode;
  });
  C("time", "set <day|noon|night|midnight|0-1200> | add <n> | season <spring|summer|autumn|winter> | query", "Set the time of day", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    LevelInfo& I = c.lv.info;
    if (!a.empty() && a[0] == "query") return "Time " + std::to_string((int)std::round(I.time)) + " of " + std::to_string((int)DAY_LEN) + " (day " + std::to_string(I.day) + (seasonOf(c.lv) >= 0 ? std::string(", ") + seasonName(seasonOf(c.lv)) : std::string()) + ")";
    if (!a.empty() && a[0] == "season" && a.size() > 1) {   // /time season spring|summer|autumn|winter: jump to the start of one
      static const char* N[4] = {"spring", "summer", "autumn", "winter"};
      for (int k = 0; k < 4; k++) if (toLower(a[1]) == N[k]) { I.day = (I.day / 28) * 28 + k * 7; return std::string(seasonName(k)) + " it is"; }
      throw CommandUsage{};
    }
    static const std::unordered_map<std::string, double> named = {{"day", 60}, {"morning", 30}, {"noon", DAY_LEN * 0.25}, {"sunset", DAY_LEN * 0.48}, {"night", DAY_LEN * 0.55}, {"midnight", DAY_LEN * 0.75}};
    if (!a.empty() && a[0] == "set") {
      double v = a.size() > 1 && named.count(a[1]) ? named.at(a[1]) : num(a, 1);
      if (!std::isfinite(v)) throw CommandUsage{};
      I.time = std::fmod(std::fmod(v, DAY_LEN) + DAY_LEN, DAY_LEN); return "Time set";
    }
    if (!a.empty() && a[0] == "add") {
      double v = num(a, 1); if (!std::isfinite(v)) throw CommandUsage{};
      I.time = std::fmod(std::fmod(I.time + v, DAY_LEN) + DAY_LEN, DAY_LEN); return "Time moved on";
    }
    throw CommandUsage{};
  });
  C("daylength", "<day minutes> <night minutes>", "How long days and nights last", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double d = num(a, 0), n = a.size() > 1 ? num(a, 1) : d;
    if (!(d > 0) || !(n > 0)) throw CommandUsage{};
    json& r = rules(c.lv); r["dayMinutes"] = clampv(d, 1.0, 240.0); r["nightMinutes"] = clampv(n, 1.0, 240.0);
    char buf[96]; snprintf(buf, sizeof buf, "Days last %g min, nights %g min", r["dayMinutes"].get<double>(), r["nightMinutes"].get<double>()); return buf;
  });
  C("gamerule", "[rule] [true|false|number]", "Show or change a game rule", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    json& r = rules(c.lv);
    const json& D = ruleDefaults();
    auto show = [](const json& v) { return v.is_boolean() ? (v.get<bool>() ? std::string("true") : std::string("false")) : v.dump(); };
    if (a.empty()) { std::string s; for (auto& [k, v] : D.items()) s += k + "=" + show(r.value(k, v)) + "  "; return s; }
    if (!D.contains(a[0])) { std::string s = "Unknown rule. Rules: "; bool f = true; for (auto& [k, v] : D.items()) { s += (f ? "" : ", ") + k; f = false; } return s; }
    if (a.size() < 2) return a[0] + " = " + show(r.value(a[0], D[a[0]]));
    if (D[a[0]].is_number()) { double v = num(a, 1); if (!std::isfinite(v)) throw CommandUsage{}; r[a[0]] = v; }
    else { static const std::regex yes("^(true|on|1|yes)$", std::regex::icase); r[a[0]] = std::regex_match(a[1], yes); }
    return a[0] + " = " + show(r[a[0]]);
  });
  C("tp", "<x y z> | spawn | <player> | <overworld|skylands|nether|end|drift> [x z]", "Teleport", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    Player& p = c.p;
    if (!a.empty() && a[0] == "spawn") { placeAtSpawn(c.lv, p, false); return "To spawn"; }
    // BTA's Drift (sim/drift.cpp): in as its portal takes you; from there any dimension's name brings you back
    if (!a.empty() && toLower(a[0]) == "drift") return p.realm == REALM_DRIFT ? "You're in the Drift already (/tp overworld to go back)" : driftTravel(c.lv, p);
    // BTA: /tp to a dimension (here a band of the stacked world): onto its ground above x, z
    if (!a.empty()) {
      const std::string d = toLower(a[0]);
      const int dim = d == "overworld" || d == "world" ? DIM_OVERWORLD : d == "skylands" || d == "aether" || d == "sky" ? DIM_AETHER
                    : d == "nether" || d == "depths" || d == "ember" ? DIM_NETHER : d == "end" || d == "the_end" ? DIM_END : -1;
      if (dim >= 0) {
        if (p.realm == REALM_DRIFT) return driftTravel(c.lv, p);
        if (p.realm != REALM_MAIN) return "Only from the main world";
        int x = a.size() >= 3 ? coord(p, a, 1, 0) : ifloor(p.pos.x), z = a.size() >= 3 ? coord(p, a, 2, 2) : ifloor(p.pos.z);
        const DimGenerator* g = c.lv.gen ? c.lv.gen->forDim(dim) : nullptr;
        int y = g ? g->spawnHeight(x, z) : -1;
        // no ground just there (the End's void, a gap between Skylands islands, the Nether's lava sea): the nearest
        // ground out to 640 blocks, rings of 8 (96 when a place was asked for), instead of dropping you from high up
        const int reachR = a.size() < 3 ? 640 : 96;
        if (g && !(y > 0 && y < DIM_H - 2))
          for (int r = 8; r <= reachR && !(y > 0 && y < DIM_H - 2); r += 8)
            for (int i = 0; i < 8 * r / 8 && !(y > 0 && y < DIM_H - 2); i++) {
              const double ang = i * 6.2831853 / (8 * r / 8);
              const int tx = x + (int)std::lround(std::cos(ang) * r), tz = z + (int)std::lround(std::sin(ang) * r);
              const int ty = g->spawnHeight(tx, tz);
              if (ty > 0 && ty < DIM_H - 2) { x = tx; z = tz; y = ty; }
            }
        y = y > 0 && y < DIM_H - 2 ? dimBase(dim) + y + 1 : dimBase(dim) + 160;   // no ground there (open sky, the void): high up
        World& w = c.lv.world(p.realm);
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) w.requestChunk(dim, (x >> 4) + dx, (z >> 4) + dz, 1000);
        p.pos = vec3(x + 0.5f, y, z + 0.5f); p.vel = vec3(0); p.hasFallStart = false; p.prevPos = p.pos; p.fallDist = 0;
        static const char* N[DIM_COUNT] = {"the End", "the Nether", "the Overworld", "the Skylands"};
        return std::string("To ") + N[dim] + " at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z);
      }
    }
    if (a.size() == 1) {
      for (auto& e : c.lv.entities()) if (e->isPlayer() && toLower(static_cast<Player*>(e.get())->name) == toLower(a[0])) { p.pos = e->pos; p.realm = e->realm; return "To " + static_cast<Player*>(e.get())->name; }
      return "No player called " + a[0];
    }
    int x = coord(p, a, 0, 0), y = coord(p, a, 1, 1), z = coord(p, a, 2, 2);
    World& w = c.lv.world(p.realm);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (inWorldY(y)) w.requestChunk(dimOfY(y), (x >> 4) + dx, (z >> 4) + dz, 1000);
    p.pos = vec3(x + 0.5f, y, z + 0.5f); p.vel = vec3(0); p.hasFallStart = false; p.prevPos = p.pos;
    return "Teleported to " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(z);
  });
  C("playsound", "<sound> [volume] [pitch] [x y z]", "Play one of the game's sounds (BTA)", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.empty()) throw CommandUsage{};
    const double v = num(a, 1, 1), pt = num(a, 2, 1);
    vec3 at = c.p.pos + vec3(0, 1.6f, 0);
    if (a.size() >= 6) at = vec3(coord(c.p, a, 3, 0) + 0.5f, coord(c.p, a, 4, 1) + 0.5f, coord(c.p, a, 5, 2) + 0.5f);
    c.lv.fx->sound(a[0], at, 32, std::isfinite(v) ? clampv((float)v, 0.f, 4.f) : 1.f, std::isfinite(pt) ? clampv((float)pt, 0.25f, 4.f) : 1.f);
    return "Playing " + a[0];
  });
  C("particle", "<kind> [count] [x y z]", "Make particles (smoke, flame, heart, firework...) (BTA)", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.empty()) throw CommandUsage{};
    const double n = num(a, 1, 12);
    vec3 at = c.p.pos + vec3(0, 1, 0) + vec3(-std::sin(c.p.yaw), 0, -std::cos(c.p.yaw)) * 2.f;
    if (a.size() >= 5) at = vec3(coord(c.p, a, 2, 0) + 0.5f, coord(c.p, a, 3, 1) + 0.5f, coord(c.p, a, 4, 2) + 0.5f);
    c.lv.fx->particles(a[0], at, std::isfinite(n) ? clampv((int)n, 1, 500) : 12);
    return "Particles: " + a[0];
  });
  C("give", "<item> [count]", "Get items", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.empty()) throw CommandUsage{};
    std::string k = resolveItem(a[0]);
    if (k.empty()) return "Unknown item: " + a[0];
    double nn = num(a, 1, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 1, 1, 64 * 36);
    int left = n;
    while (left > 0) {
      int t = std::min(left, maxStackOf(k));
      int r = giveItem(c.lv, c.p, ItemStack(k, t));
      if (r > 0) dropItem(c.lv, c.p.realm, ItemStack(k, r), c.p.pos + vec3(0, 1, 0), false);
      left -= t;
    }
    return "Gave " + std::to_string(n) + " " + reg().item(k)->name;
  });
  C("clear", "", "Empty your inventory", true, [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    for (auto* v : {&c.p.inv, &c.p.armor, &c.p.offhand, &c.p.hook, &c.p.charm}) for (auto& s : *v) s.clear();
    return "Inventory cleared";
  });
  C("kill", "[creature | @e[type=creature] | @e]", "No name: respawn the hard way. A creature's name: every one of them nearby", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    const std::string arg = a.empty() ? "" : toLower(a[0]);
    if (arg.empty() || arg == "@s" || arg == "@p" || arg == "me" || arg == toLower(c.p.name)) {
      DamageSource s; s.type = "void"; s.bypass = s.bypassArmor = true; damagePlayer(c.lv, c.p, 9999, s); return "";
    }
    const bool all = arg == "@e";
    const std::string t = all ? "" : creatureKey(arg);
    if (!all && t.empty()) return noCreature(a[0]);
    int n = 0;
    for (auto& e : c.lv.entities()) {   // creatures only (never a player, a dropped item or a cart), within 128 blocks
      if (!e || e->removed || e->isPlayer() || e->realm != c.p.realm || !reg().mobs.contains(e->type)) continue;
      if (!all && e->type != t) continue;
      if (glm::distance(e->pos, c.p.pos) > 128) continue;
      DamageSource s; s.type = "void"; s.bypass = s.bypassArmor = true;
      damageEntity(c.lv, *e, 1e6f, s); n++;
    }
    return n ? "Killed " + std::to_string(n) + (all ? " creatures" : " " + t) : "No " + (all ? std::string("creatures") : t) + " nearby";
  });
  C("heal", "", "Full health and breath", true, [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    c.p.hp = c.p.maxHp; c.p.air = AIR_MAX; c.p.burn = 0; return "Healed";
  });
  C("xp", "<amount>", "Gain experience", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double n = num(a, 0); if (!(n > 0)) throw CommandUsage{};
    grantXP(c.lv, c.p, (int)std::floor(n)); return "Level " + std::to_string(c.p.level);
  });
  C("spawnpoint", "", "Respawn here", true, [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    c.p.spawnPoint = c.p.pos; c.p.spawnRealm = c.p.realm; return "Spawn point set";
  });
  C("setworldspawn", "", "Make here the world spawn", true, [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    c.lv.info.spawn = c.p.pos; return "World spawn set";
  });
  C("summon", "<creature> [count] [look]", "Summon creatures", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    std::string t = a.empty() ? "" : creatureKey(a[0]);
    const json& M = reg().mobs;
    if (!a.empty() && t.empty()) return noCreature(a[0]);
    if (t.empty() || !M.is_object() || !M.contains(t)) { std::string s = "Creatures: "; bool f = true; if (M.is_object()) for (auto& [k, v] : M.items()) { s += (f ? "" : ", ") + k; f = false; } return s; }
    double nn = num(a, 1, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 1, 1, 20);
    vec3 L = lookVector(c.p.yaw, c.p.pitch);
    for (int i = 0; i < n; i++) {
      auto e = createEntity(t);
      e->realm = c.p.realm;
      e->pos = c.p.pos + vec3(L.x * 3 + (float)(rnd() - 0.5) * 2, 0.5f, L.z * 3 + (float)(rnd() - 0.5) * 2);
      const json& T = M[t];
      e->hp = e->maxHp = T.value("hp", 10.0f); e->w = T.value("w", e->w); e->h = T.value("h", e->h);
      if (a.size() >= 3) setCreatureLook(*e, toLower(a[2]));   // a look by name: "miner" or "skeleton_miner"
      keepSummoned(*e);
      c.lv.spawn(std::move(e));
    }
    return "Summoned " + std::to_string(n) + " " + t;
  });
  C("looks", "<creature>", "Every look a creature comes in, side by side", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.empty()) throw CommandUsage{};
    int n = spawnLookRow(c.lv, c.p.realm, c.p.pos, c.p.yaw, toLower(a[0]));
    return n ? std::to_string(n) + " looks of " + toLower(a[0]) : "No such creature";
  });
  C("lineup", "<page> [per page]", "Every creature, a page at a time, side by side", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double pg = num(a, 0, 1), per = num(a, 1, 8);
    return spawnLineup(c.lv, c.p.realm, c.p.pos, c.p.yaw, std::isfinite(pg) ? (int)pg : 1, std::isfinite(per) ? (int)per : 8);
  });
  C("setblock", "<x y z> <block>", "Place one block", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    int x = coord(c.p, a, 0, 0), y = coord(c.p, a, 1, 1), z = coord(c.p, a, 2, 2);
    Pattern pat; if (a.size() < 4 || !parsePattern(a[3], pat)) throw CommandUsage{};
    if (!pat.error.empty()) return pat.error;
    PatternPart o = pat.pick();
    c.lv.world(c.p.realm).setBlock(x, y, z, o.id, o.meta, SetFlags::Urgent);
    return "Block placed";
  });
  C("weather", "<clear|rain|thunder> [seconds]", "Change the weather", true, [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    std::string k = a.empty() ? "" : toLower(a[0]);
    std::string kind = startsWith(k, "c") ? "clear" : startsWith(k, "r") ? "rain" : startsWith(k, "t") ? "thunder" : "";
    if (kind.empty()) throw CommandUsage{};
    double secs = num(a, 1);
    if (!(secs > 0)) secs = kind == "clear" ? 900 + rnd() * 1200 : 300 + rnd() * 360;
    if (WeatherSystem* W = weatherOf(c.lv)) { W->set(kind, secs); W->save(c.lv); }   // now (it only read meta.weather when the world opened)
    else c.lv.info.data["weather"] = {{"kind", kind}, {"left", secs}};
    if (c.lv.events) c.lv.events->emit("weather", json::array({kind, secs}));
    return "Weather: " + kind;
  });
}

// ---- WorldEdit ---------------------------------------------------------------------------------
constexpr long long WE_MAX = 1500000;
struct WeEdit { std::string label; std::vector<std::array<int, 5>> data; };   // x,y,z,id,meta (what was there)
struct WeState { WeSelection sel; std::vector<std::array<int, 5>> clip; bool hasClip = false; std::vector<WeEdit> undo, redo; };
static WeState& weState(Level& lv, const Player& p) { return levelSlot<std::map<std::string, WeState>>(lv, "worldedit")[p.name]; }
WeSelection weSelection(Level& lv, const Player& p) { return weState(lv, p).sel; }
void weSetCorner(Level& lv, const Player& p, int which, ivec3 at) { WeState& s = weState(lv, p); if (which == 1) { s.sel.pos1 = at; s.sel.has1 = true; } else { s.sel.pos2 = at; s.sel.has2 = true; } }

struct Box { int x0, y0, z0, x1, y1, z1; };
static Box boxOf(ivec3 a, ivec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z), std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
static long long volOf(const Box& b) { return (long long)(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1) * (b.z1 - b.z0 + 1); }
static Box weSel(Level& lv, const Player& p) {
  WeState& s = weState(lv, p);
  if (!s.sel.has1 || !s.sel.has2) throw std::runtime_error("Select an area first: //wand, then left and right click two corners (or //pos1 and //pos2)");
  return boxOf(s.sel.pos1, s.sel.pos2);
}
std::string weSizeText(Level& lv, const Player& p) { WeState& s = weState(lv, p); if (!s.sel.has1 || !s.sel.has2) return ""; return " (" + thousands(volOf(boxOf(s.sel.pos1, s.sel.pos2))) + " blocks selected)"; }
// Write many blocks quickly (no physics), remembering what was there for //undo.
static long long weWrite(Level& lv, Player& p, const std::vector<std::array<int, 5>>& list, const std::string& label) {
  World& w = lv.world(p.realm);
  QuietEdits quiet;   // no physics (the original wrote the chunks directly), but saved and synced
  WeEdit undo{label, {}};
  for (auto& [x, y, z, id, meta] : list) {
    if (!inWorldY(y) || !w.isLoaded(x, y, z)) continue;
    BlockId oid = w.getBlock(x, y, z); u8 om = w.getMeta(x, y, z);
    if (oid == id && om == meta) continue;
    undo.data.push_back({x, y, z, oid, om});
    w.setBlock(x, y, z, (BlockId)id, (u8)meta);
  }
  WeState& s = weState(lv, p);
  long long n = (long long)undo.data.size();
  if (n) { s.undo.push_back(std::move(undo)); if (s.undo.size() > 12) s.undo.erase(s.undo.begin()); s.redo.clear(); }
  return n;
}
static std::string weFill(Level& lv, Player& p, const Box& b, const std::function<bool(int, int, int, PatternPart&)>& pick, const std::string& label, const std::function<bool(int, int, int)>& test = nullptr) {
  if (volOf(b) > WE_MAX) throw std::runtime_error("That's " + thousands(volOf(b)) + " blocks \xE2\x80\x94 the limit is " + thousands(WE_MAX));
  std::vector<std::array<int, 5>> list;
  for (int y = b.y0; y <= b.y1; y++) for (int z = b.z0; z <= b.z1; z++) for (int x = b.x0; x <= b.x1; x++) {
    if (test && !test(x, y, z)) continue;
    PatternPart o; if (!pick(x, y, z, o)) continue;
    list.push_back({x, y, z, o.id, o.meta});
  }
  long long n = weWrite(lv, p, list, label);
  return thousands(n) + " block" + (n == 1 ? "" : "s") + " changed";
}
static ivec3 weDir(Player& p, const std::string& word) {
  std::string w = toLower(word.empty() ? "me" : word);
  static const std::unordered_map<std::string, ivec3> m = {{"north", {0, 0, -1}}, {"n", {0, 0, -1}}, {"south", {0, 0, 1}}, {"s", {0, 0, 1}}, {"east", {1, 0, 0}}, {"e", {1, 0, 0}},
                                                          {"west", {-1, 0, 0}}, {"w", {-1, 0, 0}}, {"up", {0, 1, 0}}, {"u", {0, 1, 0}}, {"down", {0, -1, 0}}, {"d", {0, -1, 0}}};
  if (m.count(w)) return m.at(w);
  vec3 L = lookVector(p.yaw, p.pitch);
  auto sgn = [](float v) { return v > 0 ? 1 : v < 0 ? -1 : 0; };
  if (std::fabs(L.y) > 0.7f) return {0, sgn(L.y), 0};
  return std::fabs(L.x) > std::fabs(L.z) ? ivec3(sgn(L.x), 0, 0) : ivec3(0, 0, sgn(L.z));
}
// Turning a block a quarter turn clockwise (seen from above): facing bits for the common kinds.
static u8 rotMeta(BlockId id, u8 m, int q) {
  const Registry& R = reg(); const BlockDef& b = R.block(id); int k = R.tShape[id];
  static const int H2D6[4] = {2, 5, 3, 4}; static const int D62H[6] = {-1, -1, 0, 2, 3, 1};
  for (int i = 0; i < q; i++) {
    if (k == ShapeKind::Stairs || k == ShapeKind::Gate || k == ShapeKind::Door || k == ShapeKind::Trapdoor || k == ShapeKind::Ladder || k == ShapeKind::Bed || k == ShapeKind::Frame || b.has("facingModel") || (bhas(id, BF::FacingFront) && !b.has("f6"))) m = (u8)((m & ~3) | ((m + 1) & 3));
    else if (k == ShapeKind::Slab && (m & 7) >= 2 && (m & 7) <= 5) m = (u8)((m & ~7) | (2 + (((m & 7) - 2 + 1) & 3)));
    else if (b.has("axisBlock") || k == ShapeKind::Rod) { int a = m & 3; if (a == 1) m = (u8)((m & ~3) | 2); else if (a == 2) m = (u8)((m & ~3) | 1); }
    else if (b.render == Render::Torch && (m & 7)) { static const int t[5] = {0, 3, 4, 2, 1}; m = (u8)((m & ~7) | t[m & 7]); }
    else if (b.has("f6")) { int d = m & 7; if (d >= 2) m = (u8)((m & ~7) | H2D6[(D62H[d] + 1) & 3]); }
  }
  return m;
}
static WeEdit weReplay(Level& lv, Player& p, const WeEdit& e) {
  World& w = lv.world(p.realm);
  QuietEdits quiet;
  WeEdit rev{e.label, {}};
  for (auto& [x, y, z, id, meta] : e.data) {
    if (!w.isLoaded(x, y, z)) continue;
    rev.data.push_back({x, y, z, w.getBlock(x, y, z), w.getMeta(x, y, z)});
    w.setBlock(x, y, z, (BlockId)id, (u8)meta);
  }
  return rev;
}
static Pattern patOf(const std::string& t) { Pattern p; if (!parsePattern(t, p)) throw CommandUsage{}; if (!p.error.empty()) throw std::runtime_error(p.error); return p; }
static std::string joined(const std::vector<std::string>& a) { std::string s; for (auto& x : a) s += x; return s; }

static void registerWorldEditCommands() {
  // //set stone (the registry strips the slashes, so /set works as well)
  auto W = [](const std::string& n, const std::string& usage, const std::string& desc, GameCmd f) { addCommand(n, usage, "WorldEdit: " + desc, true, std::move(f)); };
  auto pick = [](const Pattern& pat) { return [pat](int, int, int, PatternPart& o) { o = pat.pick(); return true; }; };
  auto air = [](int, int, int, PatternPart& o) { o = {0, 0, 1}; return true; };
  W("wand", "", "Get the selection wand (left click: corner 1, right click: corner 2)", [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    giveItem(c.lv, c.p, ItemStack("we_wand", 1)); return "Left click a block for corner 1, right click for corner 2";
  });
  W("pos1", "", "Corner 1 where you stand", [](CommandCtx& c, std::vector<std::string>&) -> std::string { weSetCorner(c.lv, c.p, 1, ivec3(glm::floor(c.p.pos))); return "Corner 1 set" + weSizeText(c.lv, c.p); });
  W("pos2", "", "Corner 2 where you stand", [](CommandCtx& c, std::vector<std::string>&) -> std::string { weSetCorner(c.lv, c.p, 2, ivec3(glm::floor(c.p.pos))); return "Corner 2 set" + weSizeText(c.lv, c.p); });
  for (int which : {1, 2}) W("hpos" + std::to_string(which), "", "Corner " + std::to_string(which) + " at the block you look at", [which](CommandCtx& c, std::vector<std::string>&) -> std::string {
    RayHit t = raycastBlocks(c.lv.world(c.p.realm), glm::dvec3(c.p.pos.x, c.p.pos.y + c.p.eye, c.p.pos.z), lookVector(c.p.yaw, c.p.pitch), 5);
    if (!t.hit) return "Look at a block";
    weSetCorner(c.lv, c.p, which, t.block); return "Corner " + std::to_string(which) + " set" + weSizeText(c.lv, c.p);
  });
  W("sel", "", "Clear the selection", [](CommandCtx& c, std::vector<std::string>&) -> std::string { weState(c.lv, c.p).sel = WeSelection{}; return "Selection cleared"; });
  W("size", "", "Size of the selection", [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    Box b = weSel(c.lv, c.p);
    return std::to_string(b.x1 - b.x0 + 1) + " \xC3\x97 " + std::to_string(b.y1 - b.y0 + 1) + " \xC3\x97 " + std::to_string(b.z1 - b.z0 + 1) + " = " + thousands(volOf(b)) + " blocks";
  });
  W("count", "<block>", "Count a block in the selection", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    Box b = weSel(c.lv, c.p); int id = a.empty() ? -1 : resolveBlock(a[0]); if (id < 0) throw CommandUsage{};
    World& w = c.lv.world(c.p.realm); long long n = 0;
    for (int y = b.y0; y <= b.y1; y++) for (int z = b.z0; z <= b.z1; z++) for (int x = b.x0; x <= b.x1; x++) if (w.getBlock(x, y, z) == id) n++;
    return thousands(n) + " " + reg().block((BlockId)id).name;
  });
  W("set", "<pattern>", "Fill the selection (e.g. stone, or 70%stone,30%andesite)", [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string { Pattern pat = patOf(joined(a)); return weFill(c.lv, c.p, weSel(c.lv, c.p), pick(pat), "set"); });
  W("replace", "<from> <to>", "Swap one block for another in the selection", [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    int from = a.empty() ? -1 : resolveBlock(a[0]); if (from < 0 || a.size() < 2) throw CommandUsage{};
    Pattern pat = patOf(a[1]); World& w = c.lv.world(c.p.realm);
    return weFill(c.lv, c.p, weSel(c.lv, c.p), pick(pat), "replace", [&w, from](int x, int y, int z) { return w.getBlock(x, y, z) == from; });
  });
  W("walls", "<pattern>", "Four walls round the selection", [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    Pattern pat = patOf(joined(a)); Box b = weSel(c.lv, c.p);
    return weFill(c.lv, c.p, b, pick(pat), "walls", [b](int x, int, int z) { return x == b.x0 || x == b.x1 || z == b.z0 || z == b.z1; });
  });
  W("faces", "<pattern>", "Every outside face of the selection (a hollow box)", [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    Pattern pat = patOf(joined(a)); Box b = weSel(c.lv, c.p);
    return weFill(c.lv, c.p, b, pick(pat), "faces", [b](int x, int y, int z) { return x == b.x0 || x == b.x1 || y == b.y0 || y == b.y1 || z == b.z0 || z == b.z1; });
  });
  W("hollow", "", "Empty the inside of the selection, keeping a one-block shell", [air](CommandCtx& c, std::vector<std::string>&) -> std::string {
    Box b = weSel(c.lv, c.p);
    return weFill(c.lv, c.p, b, air, "hollow", [b](int x, int y, int z) { return x > b.x0 && x < b.x1 && y > b.y0 && y < b.y1 && z > b.z0 && z < b.z1; });
  });
  W("drain", "<radius>", "Remove water and lava around you", [air](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double rr = num(a, 0, 8); int r = clampv(std::isfinite(rr) ? (int)rr : 8, 1, 64);
    ivec3 o = ivec3(glm::floor(c.p.pos)); World& w = c.lv.world(c.p.realm);
    return weFill(c.lv, c.p, {o.x - r, o.y - r, o.z - r, o.x + r, o.y + r, o.z + r}, air, "drain", [&w, o, r](int x, int y, int z) { return reg().tLiquid[w.getBlock(x, y, z)] && glm::length(vec3(x - o.x, y - o.y, z - o.z)) <= r; });
  });
  auto ball = [pick](bool hollow) {
    return [pick, hollow](CommandCtx& c, std::vector<std::string>& a) -> std::string {
      if (a.size() < 2) throw CommandUsage{};
      Pattern pat = patOf(a[0]); double r = num(a, 1); if (!(r > 0) || r > 60) throw CommandUsage{};
      ivec3 o = ivec3(glm::floor(c.p.pos)); int R = (int)std::ceil(r);
      return weFill(c.lv, c.p, {o.x - R, o.y - R, o.z - R, o.x + R, o.y + R, o.z + R}, pick(pat), hollow ? "hsphere" : "sphere",
                    [o, r, hollow](int x, int y, int z) { double d = glm::length(glm::dvec3(x - o.x, y - o.y, z - o.z)); return d <= r + 0.5 && (!hollow || d > r - 0.5); });
    };
  };
  W("sphere", "<pattern> <radius>", "A ball centred on you", ball(false));
  W("hsphere", "<pattern> <radius>", "A hollow ball", ball(true));
  auto cyl = [pick](bool hollow) {
    return [pick, hollow](CommandCtx& c, std::vector<std::string>& a) -> std::string {
      if (a.size() < 2) throw CommandUsage{};
      Pattern pat = patOf(a[0]); double r = num(a, 1); double hh = num(a, 2, 1); int h = clampv(std::isfinite(hh) ? (int)hh : 1, 1, WORLD_H);
      if (!(r > 0) || r > 100) throw CommandUsage{};
      ivec3 o = ivec3(glm::floor(c.p.pos)); int R = (int)std::ceil(r);
      return weFill(c.lv, c.p, {o.x - R, o.y, o.z - R, o.x + R, o.y + h - 1, o.z + R}, pick(pat), hollow ? "hcyl" : "cyl",
                    [o, r, hollow](int x, int, int z) { double d = std::hypot(x - o.x, z - o.z); return d <= r + 0.5 && (!hollow || d > r - 0.5); });
    };
  };
  W("cyl", "<pattern> <radius> [height]", "A cylinder standing on you", cyl(false));
  W("hcyl", "<pattern> <radius> [height]", "A hollow cylinder", cyl(true));
  W("pyramid", "<pattern> <size>", "A pyramid standing on you", [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    if (a.size() < 2) throw CommandUsage{};
    Pattern pat = patOf(a[0]); double nn = num(a, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 0, 0, 100); if (!n) throw CommandUsage{};
    ivec3 o = ivec3(glm::floor(c.p.pos));
    return weFill(c.lv, c.p, {o.x - n, o.y, o.z - n, o.x + n, o.y + n, o.z + n}, pick(pat), "pyramid", [o, n](int x, int y, int z) { return std::max(std::abs(x - o.x), std::abs(z - o.z)) <= n - (y - o.y); });
  });
  GameCmd copyCmd = [](CommandCtx& c, std::vector<std::string>&) -> std::string {
    Box b = weSel(c.lv, c.p); if (volOf(b) > WE_MAX) return "Too big to copy";
    World& w = c.lv.world(c.p.realm); ivec3 o = ivec3(glm::floor(c.p.pos)); WeState& s = weState(c.lv, c.p);
    s.clip.clear();
    for (int y = b.y0; y <= b.y1; y++) for (int z = b.z0; z <= b.z1; z++) for (int x = b.x0; x <= b.x1; x++) s.clip.push_back({x - o.x, y - o.y, z - o.z, w.getBlock(x, y, z), w.getMeta(x, y, z)});
    s.hasClip = true;
    return thousands((long long)s.clip.size()) + " blocks copied";
  };
  W("copy", "", "Copy the selection (relative to where you stand)", copyCmd);
  W("cut", "", "Copy, then clear the selection", [air, copyCmd](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    copyCmd(c, a);
    return weFill(c.lv, c.p, weSel(c.lv, c.p), air, "cut") + " (cut)";
  });
  W("paste", "[-a]", "Paste where you stand (-a: leave out air)", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    WeState& s = weState(c.lv, c.p); if (!s.hasClip) return "Nothing copied yet";
    ivec3 o = ivec3(glm::floor(c.p.pos)); bool skipAir = std::find(a.begin(), a.end(), "-a") != a.end();
    std::vector<std::array<int, 5>> list;
    for (auto& d : s.clip) { if (skipAir && !d[3]) continue; list.push_back({o.x + d[0], o.y + d[1], o.z + d[2], d[3], d[4]}); }
    return thousands(weWrite(c.lv, c.p, list, "paste")) + " blocks pasted";
  });
  W("rotate", "<90|180|270>", "Turn what you copied (round the up axis)", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    WeState& s = weState(c.lv, c.p); if (!s.hasClip) return "Nothing copied yet";
    double deg = num(a, 0, 0); int q = (((int)(deg / 90)) % 4 + 4) % 4;
    for (auto& d : s.clip) { for (int k = 0; k < q; k++) { int x = d[0], z = d[2]; d[0] = -z; d[2] = x; } d[4] = rotMeta((BlockId)d[3], (u8)d[4], q); }
    return "Turned " + std::to_string(q * 90) + "\xC2\xB0";
  });
  W("flip", "[x|y|z]", "Mirror what you copied", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    WeState& s = weState(c.lv, c.p); if (!s.hasClip) return "Nothing copied yet";
    std::string ax = toLower(a.empty() ? "x" : a[0]); int i = ax == "y" ? 1 : ax == "z" ? 2 : 0;
    for (auto& d : s.clip) d[i] = -d[i];
    return "Flipped";
  });
  W("move", "<distance> [direction]", "Shift the selection", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double nn = num(a, 0, 0); int n = std::isfinite(nn) ? (int)nn : 0; if (!n) throw CommandUsage{};
    ivec3 d = weDir(c.p, a.size() > 1 ? a[1] : ""); Box b = weSel(c.lv, c.p); World& w = c.lv.world(c.p.realm); WeState& s = weState(c.lv, c.p);
    if (volOf(b) > WE_MAX / 2) return "Too big to move";
    std::vector<std::array<int, 5>> keep, list;
    for (int y = b.y0; y <= b.y1; y++) for (int z = b.z0; z <= b.z1; z++) for (int x = b.x0; x <= b.x1; x++) { keep.push_back({x, y, z, w.getBlock(x, y, z), w.getMeta(x, y, z)}); list.push_back({x, y, z, 0, 0}); }
    for (auto& k : keep) list.push_back({k[0] + d.x * n, k[1] + d.y * n, k[2] + d.z * n, k[3], k[4]});
    long long r = weWrite(c.lv, c.p, list, "move");
    s.sel.pos1 += d * n; s.sel.pos2 += d * n;
    return thousands(r) + " blocks changed";
  });
  W("stack", "<times> [direction]", "Repeat the selection", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    double nn = num(a, 0, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 1, 1, 64);
    ivec3 d = weDir(c.p, a.size() > 1 ? a[1] : ""); Box b = weSel(c.lv, c.p); World& w = c.lv.world(c.p.realm);
    ivec3 sz((b.x1 - b.x0 + 1) * d.x, (b.y1 - b.y0 + 1) * d.y, (b.z1 - b.z0 + 1) * d.z);
    if (volOf(b) * n > WE_MAX) return "Too big";
    std::vector<std::array<int, 5>> list;
    for (int y = b.y0; y <= b.y1; y++) for (int z = b.z0; z <= b.z1; z++) for (int x = b.x0; x <= b.x1; x++) {
      BlockId id = w.getBlock(x, y, z); u8 m = w.getMeta(x, y, z);
      for (int k = 1; k <= n; k++) list.push_back({x + sz.x * k, y + sz.y * k, z + sz.z * k, id, m});
    }
    return thousands(weWrite(c.lv, c.p, list, "stack")) + " blocks changed";
  });
  auto resize = [](int sign) {
    return [sign](CommandCtx& c, std::vector<std::string>& a) -> std::string {
      double nn = num(a, 0, 0); int n = std::isfinite(nn) ? (int)nn : 0; if (!n) throw CommandUsage{};
      Box b = weSel(c.lv, c.p); WeState& s = weState(c.lv, c.p);
      if (a.size() > 1 && toLower(a[1]) == "vert") { int base = dimBase(dimOfY(ifloor(c.p.pos.y))); b.y0 = base; b.y1 = base + DIM_H - 1; }
      else {
        ivec3 d = weDir(c.p, a.size() > 1 ? a[1] : "");
        int dir = d.x ? d.x : d.y ? d.y : d.z;
        int* lo = d.x ? &b.x0 : d.y ? &b.y0 : &b.z0; int* hi = d.x ? &b.x1 : d.y ? &b.y1 : &b.z1;
        if (dir > 0) *hi += n * sign; else *lo -= n * sign;
      }
      s.sel.pos1 = {b.x0, b.y0, b.z0}; s.sel.pos2 = {b.x1, b.y1, b.z1};
      return "Selection: " + thousands(volOf(b)) + " blocks";
    };
  };
  W("expand", "<amount> [direction|vert]", "Grow the selection", resize(1));
  W("contract", "<amount> [direction]", "Shrink the selection", resize(-1));
  W("undo", "[times]", "Take back the last edit", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    WeState& s = weState(c.lv, c.p); double nn = num(a, 0, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 1, 1, 12), done = 0;
    for (int k = 0; k < n && !s.undo.empty(); k++) { WeEdit e = std::move(s.undo.back()); s.undo.pop_back(); s.redo.push_back(weReplay(c.lv, c.p, e)); done++; }
    return done ? "Undid " + std::to_string(done) + " edit" + (done > 1 ? "s" : "") : "Nothing to undo";
  });
  W("redo", "[times]", "Put an undone edit back", [](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    WeState& s = weState(c.lv, c.p); double nn = num(a, 0, 1); int n = clampv(std::isfinite(nn) ? (int)nn : 1, 1, 12), done = 0;
    for (int k = 0; k < n && !s.redo.empty(); k++) { WeEdit e = std::move(s.redo.back()); s.redo.pop_back(); s.undo.push_back(weReplay(c.lv, c.p, e)); done++; }
    return done ? "Redid " + std::to_string(done) + " edit" + (done > 1 ? "s" : "") : "Nothing to redo";
  });
  // /fill uses the same writer
  addCommand("fill", "<x1 y1 z1> <x2 y2 z2> <block>", "Fill a box", true, [pick](CommandCtx& c, std::vector<std::string>& a) -> std::string {
    int v[6]; for (int i = 0; i < 6; i++) v[i] = coord(c.p, a, i, i % 3);
    if (a.size() < 7) throw CommandUsage{};
    Pattern pat = patOf(a[6]);
    return weFill(c.lv, c.p, boxOf({v[0], v[1], v[2]}, {v[3], v[4], v[5]}), pick(pat), "fill");
  });
}

void registerGameCommands() {
  registerBuiltinCommands();
  registerWorldEditCommands();
}

} // namespace dq
