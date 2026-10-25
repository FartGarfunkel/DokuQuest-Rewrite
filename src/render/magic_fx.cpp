// render/magic_fx.cpp — the client side of magic: spell sparks and bolts (game/magic.js spellFx/spellSpark/
// spellRing/spellSpiral/castCircle, SpellBolt/ArcBolt trails and glows), the auras (ward, witchlight, levitation,
// stoneskin, effect motes, the staff's motes), the enchanting table's floating book and its glyphs, the brewing
// stand's bottles and bubbles, thrown potions; the HUD (mana bar, effect chips, skill pops, floating numbers);
// the keys G / B / Z / K / J; opening the enchanting table and brewing stand. Sparks arrive through LevelFx
// (sim/magic_fx.h): this layer wraps the level's fx sink and takes the kinds it draws.
#include "ui/magic_screens.h"
#include "client/app.h"
#include "client/session.h"
#include "render/item_layers.h"
#include "assets/item_models.h"
#include "assets/voxel.h"
#include "audio/audio.h"
#include "sim/magic.h"
#include "sim/effects.h"
#include "sim/brewing.h"
#include "sim/enchanting.h"
#include "sim/commands.h"
#include "world/world.h"
#include <imgui.h>
#include <deque>

namespace dq {

static float frand() { return (float)mrand(); }
static float jv(const json& o, const char* k, float d) { return o.is_object() && o.contains(k) && o[k].is_number() ? o[k].get<float>() : d; }

namespace {

struct Spark { vec3 pos{0}, vel{0}; float life = 0.5f, max = 0.5f, size = 0.08f, size0 = 0.08f, grav = 0, grow = 0, flutter = 0, ph = 0; bool shrink = true, flat = false; vec3 col{1}; int shape = 0; };
struct VBolt { std::string spell, fx; vec3 pos{0}, vel{0}; float age = 0; u32 target = 0; int realm = 0; };
struct FloatText { vec3 pos{0}; std::string text; u32 col = 0xffffff; float t = 0; };
struct Pop { std::string k; double xp = 0; double t = 0; };
struct BookAnim { float yaw = 0, open = 0, flip = 0, leaf = -1, t = 0; std::vector<ivec3> shelves; float shelfT = 0; };
struct QV { float x, y, z, cx, cy, size; u8 r, g, b, a; float shape, flat; };

static vec3 tileColor(const std::string& k) {
  static const std::unordered_map<std::string, std::string> M = {{"mana_fire", "#ff8a2a"}, {"mana_frost", "#7ad8ff"}, {"mana_heal", "#5aff6a"}, {"mana_arcane", "#b87aff"},
    {"mana_light", "#ffe08a"}, {"mana_stone", "#b8a080"}, {"flame", "#ff9a30"}, {"white", "#f0f0f0"}, {"cloud", "#e8e8f0"}, {"happy", "#6ad850"}, {"end_mote", "#c8a0ff"}, {"sparkle", "#fff0a0"}};
  auto it = M.find(k);
  if (it != M.end()) return hexRGBf(it->second);
  if (startsWith(k, "fxmote_")) { const EffectDef* E = magicData().effect(k.substr(7)); return hexRGBf(E ? E->col : "#ffffff"); }
  if (startsWith(k, "ench_glyph_")) return hexRGBf("#e8e0ff");
  return vec3(0.9f);
}
static int tileShape(const std::string& k) {
  if (startsWith(k, "fxmote_")) return 1;
  if (k == "brew_bubble") return 2;
  if (startsWith(k, "ench_glyph_")) return 10 + (k.back() - '0') % 6;
  if (k == "flame") return 4;
  return 0;
}
static bool ourKind(const std::string& k) { return startsWith(k, "mana_") || startsWith(k, "ench_glyph_") || startsWith(k, "fxmote_") || k == "brew_bubble"; }

class MagicLayer;

// wraps the level's fx sink: the spell kinds are drawn here, everything goes on to the sink underneath
class MagicFxSink : public LevelFx {
public:
  LevelFx* inner; MagicLayer* layer; bool generic;
  MagicFxSink(LevelFx* in, MagicLayer* l, bool g) : inner(in), layer(l), generic(g) {}
  void sound(const std::string& n, vec3 p, float r, float v, float pi) override;
  void soundUi(u32 id, const std::string& n) override;
  void material(const std::string& m, const std::string& k, vec3 p) override { inner->material(m, k, p); }
  void voice(const std::string& c, const std::string& k, vec3 p, float pi) override { inner->voice(c, k, p, pi); }
  void particles(const std::string& kind, vec3 pos, int count, const json& extra) override;
  void message(u32 id, const std::string& t, const std::string& k) override;
  void shake(u32 id, float s) override { inner->shake(id, s); }
};

class MagicLayer : public RenderLayer {
public:
  explicit MagicLayer(GameSession& gs) : s(gs) {
    Level& lv = *s.level;
    bool generic = lv.fx == &lv.nullFx;   // no game particle system under us: draw flames/bursts/texts too
    sink = std::make_unique<MagicFxSink>(lv.fx, this, generic);
    lv.fx = sink.get();
    hUse = s.app.events.on("itemUse", [this](const json& a) { return onUse(a); }, 10);
    hXp = s.app.events.on("skillXp", [this](const json& a) { onSkillXp(a); return true; });
    if (const char* t = std::getenv("DQ_MAGIC_TEST")) test = t;
  }
  ~MagicLayer() override {
    if (s.level && s.level->fx == sink.get()) s.level->fx = sink->inner;
    s.app.events.off(hUse); s.app.events.off(hXp);
  }
  const char* name() const override { return "magic-fx"; }
  int order() const override { return 500; }
  void update(GameSession& gs, double dt) override;
  void drawOpaque(GameSession& gs, const FrameCtx& f) override;
  void drawTranslucent(GameSession& gs, const FrameCtx& f) override;
  void drawHud(GameSession& gs) override;

  // ---- spark makers (the original's helpers) ----
  void add(const std::string& kind, vec3 p, vec3 v, float life, float size, const json& o = json()) {
    if (sparks.size() > 6000) return;
    Spark k; k.pos = p; k.vel = v; k.life = k.max = std::max(0.02f, life); k.size = k.size0 = size;
    k.grav = jv(o, "grav", 0); k.grow = jv(o, "grow", 0); k.shrink = k.grow == 0; k.flutter = jv(o, "flutter", 0); k.ph = frand() * 6;
    k.flat = o.is_object() && o.value("flat", false); k.col = tileColor(kind); k.shape = tileShape(kind);
    sparks.push_back(k);
  }
  void fx(const std::string& t, vec3 p, int n, float sp, const json& o) {
    float spread = jv(o, "spread", 0.3f);
    for (int i = 0; i < n; i++)
      add(t, p + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * spread,
          vec3((frand() - 0.5f) * 2 * sp + jv(o, "vx", 0), (frand() - 0.5f) * 2 * sp + jv(o, "vy", 0), (frand() - 0.5f) * 2 * sp + jv(o, "vz", 0)),
          jv(o, "life", 0.5f) * (0.6f + frand() * 0.8f), jv(o, "size", 0.08f) * (0.7f + frand() * 0.6f), o);
  }
  void spark(const std::string& t, vec3 p, const json& o) { add(t, p, vec3(jv(o, "vx", 0), jv(o, "vy", 0), jv(o, "vz", 0)), jv(o, "life", 0.5f), jv(o, "size", 0.08f), o); }
  void ring(const std::string& t, vec3 p, float r, int n, const json& o) {
    float sp = jv(o, "speed", 1.2f);
    for (int i = 0; i < n; i++) { float a = i / (float)n * TAU, c = std::cos(a), sn = std::sin(a);
      add(t, p + vec3(c * r, 0, sn * r), vec3(c * sp, jv(o, "vy", 0), sn * sp), jv(o, "life", 0.6f), jv(o, "size", 0.09f), json{{"flat", o.is_object() && o.value("flat", false)}}); }
  }
  void spiral(const std::string& t, vec3 p, int n, const json& o) {
    for (int i = 0; i < n; i++) {
      float a = i / (float)n * TAU * jv(o, "turns", 3), r = jv(o, "r", 0.7f), h = i / (float)n * jv(o, "h", 2);
      add(t, p + vec3(std::cos(a) * r, h, std::sin(a) * r), vec3(-std::sin(a) * 0.8f, jv(o, "vy", 0.9f), std::cos(a) * 0.8f), jv(o, "life", 0.9f) * (0.7f + frand() * 0.6f), jv(o, "size", 0.08f));
    }
  }
  void beam(const std::string& t, vec3 a, vec3 b) {
    float L = std::max(0.01f, glm::distance(a, b));
    for (float d = 0; d <= L; d += 0.3f) { float k = d / L; add(t, a + (b - a) * k + vec3((frand() - 0.5f) * 0.15f, (frand() - 0.5f) * 0.15f, 0), vec3(0), 0.35f, 0.07f); }
  }
  void circle(const std::string& fxT, vec3 p) {
    ring(fxT, p + vec3(0, 0.06f, 0), 0.95f, 28, json{{"speed", 0.25}, {"life", 0.7}, {"size", 0.1}, {"flat", true}});
    ring("mana_arcane", p + vec3(0, 0.06f, 0), 0.55f, 14, json{{"speed", -0.2}, {"life", 0.6}, {"size", 0.07}, {"flat", true}});
    for (int i = 0; i < 10; i++) { float a = frand() * TAU; add(fxT, p + vec3(std::cos(a) * 0.9f, 0.1f, std::sin(a) * 0.9f), vec3(0, 1.6f + frand(), 0), 0.6f, 0.06f); }
  }
  void boltHit(VBolt& b) {
    const SpellDef* S = magicData().spell(b.spell);
    std::string t = S ? S->fx : "mana_arcane";
    if (b.spell == "missiles") { fx("mana_arcane", b.pos, 18, 1.8f, json{{"life", 0.4}, {"size", 0.09}}); return; }
    if (b.spell == "fireball") {
      fx("mana_fire", b.pos, 70, 5, json{{"life", 0.7}, {"size", 0.14}});
      ring("mana_fire", b.pos, 0.3f, 30, json{{"speed", 6}, {"life", 0.5}, {"size", 0.1}});
      for (int i = 0; i < 14; i++) add("flame", b.pos + vec3((frand() - 0.5f) * 3, frand() - 0.5f, (frand() - 0.5f) * 3), vec3(0, 0.8f, 0), 0.7f, 0.12f);
      return;
    }
    fx(t, b.pos, 34, 2.6f, json{{"life", 0.55}, {"size", 0.11}});
    fx("mana_arcane", b.pos, 12, 1.2f, json{{"life", 0.8}, {"size", 0.07}});
    ring(t, b.pos, 0.2f, 18, json{{"speed", 3}, {"life", 0.4}, {"size", 0.08}});
    for (int i = 0; i < 8; i++) add(t, b.pos + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f), vec3(0, 0.3f, 0), 1.4f, 0.05f, json{{"flutter", 0.8}});
    if (b.spell == "firebolt") for (int i = 0; i < 6; i++) add("flame", b.pos + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * 0.6f, vec3(0, 0.8f, 0), 0.6f, 0.1f);
  }

  void onFx(const std::string& kind, vec3 p, int n, const json& o) {
    std::string m = o.is_object() ? o.value("m", "") : "";
    if (m == "cast") {
      std::string caster = o.value("caster", "");
      bool mine = s.player && caster == s.player->name;
      if (o.value("relay", false) && mine) return;
      if (mine) if (auto* h = s.layer<HeldItemLayer>()) h->swingArm();
      if (o.contains("b") && o["b"].is_array()) for (auto& b : o["b"]) if (b.is_array() && b.size() == 6) {
        VBolt v; v.spell = o.value("s", ""); v.pos = vec3(b[0], b[1], b[2]); v.vel = vec3(b[3], b[4], b[5]); v.target = o.value("t", 0u);
        v.realm = s.player ? s.player->realm : 0;
        const SpellDef* S = magicData().spell(v.spell); v.fx = S ? S->fx : "mana_arcane";
        bolts.push_back(v);
      }
      if (!mine) circle(kind, p);
      return;
    }
    if (m == "fx") fx(kind, p, n, jv(o, "sp", 1), o);
    else if (m == "spark") spark(kind, p, o);
    else if (m == "ring") ring(kind, p, jv(o, "r", 1), n, o);
    else if (m == "spiral") spiral(kind, p, n, o);
    else if (m == "beam" && o.contains("to")) beam(kind, p, vec3(o["to"][0], o["to"][1], o["to"][2]));
    else if (m == "circle") circle(kind, p);
    else if (m == "vortex") { float r = jv(o, "r", 1.1f); for (int i = 0; i < n; i++) { float a = i / (float)n * TAU, hh = frand() * 2; add(kind, p + vec3(std::cos(a) * r, hh, std::sin(a) * r), vec3(-std::cos(a) * 2 - std::sin(a) * 2, 0.2f, -std::sin(a) * 2 + std::cos(a) * 2), 0.5f, 0.08f); } }
    else if (m == "implode") {
      float r = jv(o, "r", 0.9f), h = jv(o, "h", 1.9f); bool up = o.contains("vy");
      for (int i = 0; i < n; i++) { float a = frand() * TAU; vec3 q = p + vec3(std::cos(a) * r, frand() * h, std::sin(a) * r);
        if (up) add(kind, q, vec3(0, jv(o, "vy", 1.2f), 0), 0.8f, 0.07f); else add(kind, q, vec3(-std::cos(a) * 1.4f, 0, -std::sin(a) * 1.4f), 0.45f, 0.08f); }
    } else if (m == "sphere") {
      float r = jv(o, "r", 1.1f);
      for (int i = 0; i < n; i++) { float a = frand() * TAU, b = std::acos(frand() * 2 - 1); vec3 d(std::sin(b) * std::cos(a), std::cos(b), std::sin(b) * std::sin(a)); add(kind, p + d * r, d * 0.4f, 0.9f, 0.07f); }
    } else if (m == "cone" && o.contains("dir")) {
      vec3 d(o["dir"][0], o["dir"][1], o["dir"][2]);
      for (int i = 0; i < n; i++) { float sp = 4 + frand() * 5; auto j = [] { return (frand() - 0.5f) * 2.2f; }; add(kind, p, vec3(d.x * sp + j(), d.y * sp + j() * 0.5f, d.z * sp + j()), 0.55f, 0.12f, json{{"grow", 0.2}}); }
    } else if (m == "splash") {
      for (int i = 0; i < n; i++) { float a = frand() * TAU, sp = 1 + frand() * 3; add(kind, p, vec3(std::cos(a) * sp, 1 + frand() * 2, std::sin(a) * sp), 0.7f + frand() * 0.5f, 0.08f, json{{"grav", 3}}); }
    } else fx(kind, p, std::max(1, n), 0.5f, o);
  }
  void onGeneric(const std::string& kind, vec3 p, int n, const json& o) {
    if (kind == "flame") { for (int i = 0; i < std::max(1, n); i++) add("flame", p + vec3(frand() - 0.5f, 0, frand() - 0.5f) * 0.2f, vec3(0, 0.6f + frand() * 0.4f, 0), 0.5f + frand() * 0.3f, 0.09f); }
    else if (kind == "text" && o.is_object()) texts.push_back({p, o.value("text", ""), hexColor(o.value("col", "#ffffff")), 0});
    else if (kind == "flash") flashT = std::max(flashT, jv(o, "t", 0.25f));
    else if (kind == "burst" || kind == "happy" || kind == "white" || kind == "cloud" || kind == "end_mote") {
      std::string tile = o.is_object() ? o.value("tile", kind) : kind;
      float sp = jv(o, "speed", 1);
      for (int i = 0; i < std::max(1, n); i++) {
        Spark k; k.pos = p + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * 0.6f; k.vel = vec3((frand() - 0.5f) * 2 * sp, frand() * 2 * sp, (frand() - 0.5f) * 2 * sp);
        k.life = k.max = 0.6f + frand() * 0.4f; k.size = k.size0 = 0.06f; k.grav = kind == "burst" ? 9 : 0;
        BlockId b = reg().blockId(tile);
        k.col = b ? blockColor(b) : tileColor(tile); k.shape = kind == "burst" ? 5 : 0;
        sparks.push_back(k);
      }
    }
  }
  vec3 blockColor(BlockId b) {
    const std::string& k = reg().block(b).key;
    if (k.find("leaves") != std::string::npos) return hexRGBf("#4a7a2a");
    if (k.find("stone") != std::string::npos || k.find("cobble") != std::string::npos) return hexRGBf("#8a8a8a");
    return vec3(0.6f);
  }
  void onSkillXp(const json& a) {
    if (!s.player || !a.is_array() || a.size() < 3 || a[0] != s.player->name) return;
    std::string k = a[1]; double xp = a[2];
    if (!pops.empty() && pops.back().k == k && s.app.time - pops.back().t < 1.2) { pops.back().xp += xp; pops.back().t = s.app.time; return; }
    pops.push_back({k, xp, s.app.time});
    while (pops.size() > 4) pops.pop_front();
  }
  bool onUse(const json& a) {   // the enchanting table and the brewing stand open their panels
    if (!s.player || !a.is_array() || a.size() < 6 || a[0] != s.player->name || !a[5].is_number() || a[5].get<int>() < 0 || s.player->sneaking) return true;
    ivec3 at(a[2].get<int>(), a[3].get<int>(), a[4].get<int>());
    const BlockDef& b = reg().block(s.world().getBlock(at.x, at.y, at.z));
    if (b.key == "enchanting_table") { s.app.pushScreen(makeEnchantScreen(at)); return false; }
    if (b.key == "brewing_stand") { s.app.pushScreen(makeBrewScreen(at)); return false; }
    return true;
  }

  GameSession& s;
  std::unique_ptr<MagicFxSink> sink;
  int hUse = 0, hXp = 0;
  std::vector<Spark> sparks;
  std::vector<VBolt> bolts;
  std::vector<FloatText> texts;
  std::deque<Pop> pops;
  float flashT = 0;
  double scanT = 0, time = 0;
  std::vector<ivec3> tables, stands;
  std::unordered_map<i64, BookAnim> books;
  std::string test; int testFrame = 0; ivec3 testAt{0};
  // GL
  std::unique_ptr<Shader> sh; bool shTried = false;
  Mesh meshGlow, meshSolid;
  std::vector<QV> vGlow, vSolid;
  VoxelMeshGL cover, pages; bool bookBuilt = false;
  void ambient(double dt);
  void runTest();
  void buildBook();
  void drawBooks(const FrameCtx& f);
  void quad(std::vector<QV>& out, vec3 rel, float size, vec3 col, float alpha, int shape, bool flat) {
    QV v{rel.x, rel.y, rel.z, 0, 0, size, (u8)clampv(col.r * 255, 0.f, 255.f), (u8)clampv(col.g * 255, 0.f, 255.f), (u8)clampv(col.b * 255, 0.f, 255.f), (u8)clampv(alpha * 255, 0.f, 255.f), (float)shape, flat ? 1.f : 0.f};
    static const float C[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (auto& c : C) { v.cx = c[0]; v.cy = c[1]; out.push_back(v); }
  }
};

void MagicFxSink::sound(const std::string& n, vec3 p, float r, float v, float pi) {
  inner->sound(n, p, r, v, pi);
  if (generic && layer->s.app.audio) layer->s.app.audio->playAt(n, p, r, v, pi);
}
void MagicFxSink::soundUi(u32 id, const std::string& n) {
  inner->soundUi(id, n);
  if (generic && layer->s.app.audio && layer->s.player && (id == 0 || id == layer->s.player->id)) layer->s.app.audio->play(n);
}
void MagicFxSink::particles(const std::string& kind, vec3 pos, int count, const json& extra) {
  inner->particles(kind, pos, count, extra);
  if (ourKind(kind) || (extra.is_object() && extra.value("m", "") == "cast")) layer->onFx(kind, pos, count, extra);
  else if (generic) layer->onGeneric(kind, pos, count, extra);
}
void MagicFxSink::message(u32 id, const std::string& t, const std::string& k) {
  inner->message(id, t, k);
  if (generic && layer->s.player && (id == 0 || id == layer->s.player->id)) layer->s.app.toast(t, k);
}

// ---- per frame -------------------------------------------------------------------------------------------------
void MagicLayer::update(GameSession& gs, double dt) {
  time += dt;
  Player* p = s.player;
  if (!p) return;
  Level& lv = *s.level;
  App& app = s.app;
  // the keys (when no menu is open)
  if (!app.topScreen() && !p->dead) {
    Input& in = app.input;
    if (in.pressed(Act::Spell)) cycleSpell(lv, *p);
    if (in.pressed(Act::CastQuick)) castQuick(lv, *p);
    bool padBook = in.usingPad() && in.down(Act::Sneak) && in.pressed(Act::MenuUp) && p->held().empty() && !s.target.hit;
    if (in.pressed(Act::Spellbook) || padBook) app.pushScreen(makeSpellbookScreen());
    else if (in.pressed(Act::Skills)) app.pushScreen(makeSkillsScreen());
    else if (in.pressed(Act::Journal)) app.pushScreen(makeJournalScreen());
    magicState(lv, *p).jumpHeld = in.down(Act::Jump);
  }
  if (!test.empty()) runTest();
  // sparks
  float fdt = (float)dt;
  for (auto& k : sparks) {
    k.life -= fdt;
    k.vel.y -= k.grav * fdt;
    k.pos += k.vel * fdt;
    if (k.flutter > 0) { float a = (k.max - k.life) * 5 + k.ph; k.pos.x += std::sin(a) * k.flutter * fdt; k.pos.z += std::cos(a * 0.8f) * k.flutter * fdt; }
    float t = clampv(k.life / k.max, 0.f, 1.f);
    k.size = k.shrink ? k.size0 * t : k.size0 + k.grow * (1 - t);
  }
  sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [](const Spark& k) { return k.life <= 0; }), sparks.end());
  // bolts drawn on every machine
  World& w = s.world();
  for (auto& b : bolts) {
    b.age += fdt;
    if (b.age > 2.4f) { b.age = 99; continue; }
    if (b.spell == "missiles" && b.target) if (Entity* t = lv.entity(b.target); t && isAlive(*t)) {
      float sp = std::max(1.f, glm::length(b.vel)); vec3 d = t->pos + vec3(0, t->h * 0.6f, 0) - b.pos; float l = std::max(1e-3f, glm::length(d)), k = std::min(1.f, fdt * 7);
      b.vel += (d / l * sp - b.vel) * k;
    }
    bool hit = false;
    for (int st = 0; st < 4 && !hit; st++) {
      b.pos += b.vel * fdt / 4.f;
      for (auto& e : lv.entities()) if (e->realm == b.realm && isSpellTarget(*e) && std::abs(e->pos.x - b.pos.x) < e->w + 0.25f && std::abs(e->pos.z - b.pos.z) < e->w + 0.25f && b.pos.y > e->pos.y - 0.1f && b.pos.y < e->pos.y + e->h + 0.1f) { hit = true; break; }
      if (!hit && reg().tSolid[w.getBlock(ifloor(b.pos.x), ifloor(b.pos.y), ifloor(b.pos.z))]) hit = true;
    }
    if (hit) { boltHit(b); b.age = 99; continue; }
    // the trail: a bright core, two strands twisting round it, and the spell's own leavings
    float v = std::max(1e-3f, glm::length(b.vel)); vec3 u = b.vel / v, px(-u.z, 0, u.x);
    float a = b.age * 26;
    fx(b.fx, b.pos, 3, 0.25f, json{{"life", 0.3}, {"size", 0.12}, {"spread", 0.12}});
    for (float k2 : {0.f, PI}) { float c = std::cos(a + k2) * 0.22f, sn = std::sin(a + k2) * 0.22f; add(k2 != 0 ? "mana_arcane" : b.fx, b.pos + vec3(px.x * c, sn, px.z * c), -u * 0.6f, 0.45f, 0.06f); }
    if (b.spell == "firebolt" || b.spell == "fireball") { if (frand() < 0.8f) add("mana_fire", b.pos, vec3((frand() - 0.5f) * 1.5f, frand() * 0.5f, (frand() - 0.5f) * 1.5f), 0.7f, 0.05f, json{{"grav", 4}}); if (frand() < 0.5f) add("flame", b.pos, vec3(0, 0.5f, 0), 0.5f, 0.08f); }
    else add("mana_frost", b.pos + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * 0.3f, vec3(0, -0.4f, 0), 1.1f, 0.05f, json{{"grav", 0.4}, {"flutter", 1.2}});
  }
  bolts.erase(std::remove_if(bolts.begin(), bolts.end(), [](const VBolt& b) { return b.age >= 99; }), bolts.end());
  for (auto& t : texts) { t.t += fdt; t.pos.y += fdt * 0.6f; }
  texts.erase(std::remove_if(texts.begin(), texts.end(), [](const FloatText& t) { return t.t > 1.2f; }), texts.end());
  if (flashT > 0) flashT -= fdt;
  if (!app.topScreen() || !app.topScreen()->pausesGame()) ambient(dt);
}

void MagicLayer::ambient(double dt) {
  Player& p = *s.player;
  Level& lv = *s.level;
  MagicState& m = magicState(lv, p);
  float fdt = (float)dt, t = (float)time;
  auto chance = [&](float rate) { return frand() < fdt * rate; };
  if (!p.dead) {
    if (m.ward > 0) for (int k = 0; k < 2; k++) { float a = t * 3 + k * PI + frand() * 0.3f, b = t * 1.7f + k; add("mana_light", p.pos + vec3(std::cos(a) * 0.8f, 1 + std::sin(b) * 0.8f, std::sin(a) * 0.8f), vec3(0), 0.35f, 0.05f); }
    if (heldStaff(p) && chance(14)) {
      std::string k = activeSpell(p); const SpellDef* S = magicData().spell(k);
      std::string tile = S ? S->fx : "mana_arcane";
      if (!s.viewMode) {
        const Camera& c = s.frame.cam; vec3 f = c.forward(), r = c.right(), up = glm::cross(r, f);
        float a = frand() * TAU, rr = 0.05f + frand() * 0.05f;
        vec3 tip = vec3(c.pos) + f * 0.75f + r * (0.36f + std::cos(a) * rr) + up * (0.12f + std::sin(a) * rr * 0.6f);
        add(tile, tip, vec3(0, 0.18f, 0), 0.55f, 0.022f, json{{"flutter", 0.25}});
      } else {
        float rx = std::cos(p.yaw), rz = -std::sin(p.yaw), fx2 = -std::sin(p.yaw), fz = -std::cos(p.yaw);
        auto j = [] { return (frand() - 0.5f) * 0.12f; };
        add(tile, p.pos + vec3(rx * 0.38f + fx2 * 0.3f + j(), (p.sneaking ? 1.3f : 1.6f) + j(), rz * 0.38f + fz * 0.3f + j()), vec3(0, 0.35f, 0), 0.45f, 0.035f, json{{"flutter", 0.6}});
      }
    }
    if (m.lightT > 0 && m.witchSet) {
      if (chance(14)) add("mana_light", m.witch + vec3(frand() - 0.5f, frand() - 0.5f, frand() - 0.5f) * 0.3f, vec3(0, -0.3f, 0), 0.6f, 0.04f, json{{"flutter", 1}});
    }
    if (m.levT > 0 && chance(24)) add(frand() < 0.5f ? "mana_frost" : "mana_arcane", p.pos + vec3((frand() - 0.5f) * 0.8f, -0.05f, (frand() - 0.5f) * 0.8f), vec3(0, -0.8f, 0), 0.5f, 0.05f);
    if (m.stoneT > 0 && chance(6)) add("mana_stone", p.pos + vec3((frand() - 0.5f) * 0.9f, frand() * 1.8f, (frand() - 0.5f) * 0.9f), vec3(0, -0.4f, 0), 0.5f, 0.05f);
    auto list = effectsOn(p);
    if (!list.empty() && !hasEffect(p, "invis") && chance(s.viewMode ? 6.f : 2.5f)) {
      auto& e = list[(size_t)(frand() * list.size()) % list.size()];
      float a = frand() * TAU, rr = 0.45f + frand() * 0.2f;
      add("fxmote_" + e.id, p.pos + vec3(std::cos(a) * rr, 0.2f + frand() * 1.5f, std::sin(a) * rr), vec3(0, 0.5f + frand() * 0.4f, 0), 0.9f, 0.06f);
    }
  }
  for (auto& e : lv.entities()) if (!e->removed && e->realm == p.realm && mobStatus(*e, "rootT") > 0 && chance(6))
    add("mana_heal", e->pos + vec3((frand() - 0.5f) * 0.8f, 0.1f, (frand() - 0.5f) * 0.8f), vec3(0, 0.6f, 0), 0.5f, 0.06f);
  // the tables and stands around you
  World& w = s.world();
  if ((scanT -= dt) <= 0) {
    scanT = 1;
    tables.clear(); stands.clear();
    BlockId et = reg().blockId("enchanting_table"), bs = reg().blockId("brewing_stand");
    ivec3 c(ifloor(p.pos.x), ifloor(p.pos.y), ifloor(p.pos.z));
    for (int y = -4; y <= 4; y++) for (int z = -12; z <= 12; z++) for (int x = -12; x <= 12; x++) {
      BlockId b = w.getBlock(c.x + x, c.y + y, c.z + z);
      if (b && b == et) tables.push_back(c + ivec3(x, y, z)); else if (b && b == bs) stands.push_back(c + ivec3(x, y, z));
    }
  }
  for (auto& T : tables) {
    i64 key = ((i64)T.x * 73856093) ^ ((i64)T.y * 19349663) ^ ((i64)T.z * 83492791);
    BookAnim& a = books[key];
    vec3 dd = p.pos - (vec3(T) + vec3(0.5f, 0, 0.5f)); float d2 = glm::dot(dd, dd);
    bool near = d2 < 49 && !p.dead;
    float want = near ? std::atan2(p.pos.x - T.x - 0.5f, p.pos.z - T.z - 0.5f) - PI / 2 : a.yaw + fdt * 0.3f;
    float dy = std::remainder(want - a.yaw, TAU);
    a.yaw += dy * std::min(1.f, fdt * 3);
    a.open = clampv(a.open + (near ? fdt * 1.5f : -fdt * 1.2f), 0.f, 1.f);
    a.flip -= fdt;
    if (a.open > 0.8f && a.flip <= 0) { a.flip = 0.6f + frand() * 2.2f; a.leaf = 0; if (frand() < 0.5f && s.app.audio) s.app.audio->playAt("book_page", vec3(T) + vec3(0.5f, 1, 0.5f), 8); }
    if (a.leaf >= 0) { a.leaf += fdt * 2.4f; if (a.leaf >= 1) a.leaf = -1; }
    // glyphs drift in from the shelves
    if (near && d2 < 36 && chance(6)) {
      if ((a.shelfT -= 1) <= 0 || a.shelves.empty()) {
        a.shelfT = 40; a.shelves.clear();
        BlockId shelf = reg().blockId("bookshelf");
        for (int yy = 0; yy <= 1; yy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++)
          if (std::max(std::abs(dx), std::abs(dz)) == 2 && w.getBlock(T.x + dx, T.y + yy, T.z + dz) == shelf) a.shelves.push_back(T + ivec3(dx, yy, dz));
      }
      if (!a.shelves.empty()) {
        ivec3 sp = a.shelves[(size_t)(frand() * a.shelves.size()) % a.shelves.size()];
        vec3 from(sp.x + 0.5f + (frand() - 0.5f) * 0.6f, sp.y + 0.6f + frand() * 0.8f, sp.z + 0.5f + (frand() - 0.5f) * 0.6f);
        float life = 1.6f;
        add("ench_glyph_" + std::to_string((int)(frand() * 6) % 6), from, vec3((T.x + 0.5f - from.x) / life, (T.y + 1.2f - from.y) / life + 0.3f, (T.z + 0.5f - from.z) / life), life, 0.1f, json{{"grav", 0.4}});
      }
    }
  }
  for (auto& S : stands) {
    json* e = w.blockEntity(S.x, S.y, S.z);
    if (!e || e->value("t", 0.0) <= 0) continue;
    static const float pads[3][2] = {{12.5f, 8}, {3.5f, 4}, {3.5f, 12}};
    for (int k = 0; k < 3; k++) if (frand() < 0.08f && (*e)["items"].is_array() && !(*e)["items"][k].is_null()) add("brew_bubble", vec3(S.x + pads[k][0] / 16, S.y + 0.45f, S.z + pads[k][1] / 16), vec3(0, 0.5f, 0), 0.6f, 0.04f);
    if (frand() < 0.05f) add("brew_bubble", vec3(S) + vec3(0.5f, 0.95f, 0.5f), vec3(0, 0.3f, 0), 0.8f, 0.05f);
  }
}

// ---- drawing -----------------------------------------------------------------------------------------------------
void MagicLayer::buildBook() {
  bookBuilt = true;
  auto box = [](VoxelMeshGL& gl, ivec3 n, u32 col, u32 edge, bool gem) {
    VoxelGrid g(n);
    for (int z = 0; z < n.z; z++) for (int y = 0; y < n.y; y++) for (int x = 0; x < n.x; x++) {
      bool rim = x == 0 || x == n.x - 1 || z == 0 || z == n.z - 1;
      u32 c = rim ? edge : col;
      if (gem && std::abs(x - n.x / 2) <= 2 && std::abs(z - n.z / 2) <= 3) c = voxRGBA(200, 160, 64);
      g.set(x, y, z, c);
    }
    VoxelMeshData d; buildVoxelMesh(g, d); gl.upload(d);
  };
  box(cover, ivec3(17, 1, 24), voxRGBA(106, 58, 26), voxRGBA(58, 30, 10), true);
  box(pages, ivec3(15, 3, 22), voxRGBA(236, 226, 196), voxRGBA(210, 200, 175), false);
}

void MagicLayer::drawBooks(const FrameCtx& f) {
  if (tables.empty()) return;
  if (!bookBuilt) buildBook();
  if (!cover.ready()) return;
  VoxelRenderer& VR = voxelRenderer();
  VR.begin(f, f.cam.viewProj);
  World& w = s.world();
  const float V = 1.f / 64;
  for (auto& T : tables) {
    i64 key = ((i64)T.x * 73856093) ^ ((i64)T.y * 19349663) ^ ((i64)T.z * 83492791);
    BookAnim& a = books[key];
    vec3 rel = f.cam.rel(glm::dvec3(T));
    u8 l = w.getLight(T.x, T.y + 1, T.z);
    VoxelLight L{(l >> 4) / 15.f, std::max(7, l & 15) / 15.f};
    float open = a.open * a.open * (3 - 2 * a.open), ang = 0.08f + open * 1.25f;
    mat4 base = glm::translate(mat4(1), rel + vec3(0.5f, 0.75f + 0.3f + std::sin((float)time * 1.4f + T.x) * 0.05f, 0.5f));
    base = glm::rotate(base, a.yaw, vec3(0, 1, 0));
    base = glm::rotate(base, 0.25f + 0.55f * open, vec3(0, 0, 1));
    // a cover lies along +x from the spine, 0.38 wide (z centred), 1/64 thick
    mat4 grid = glm::scale(glm::translate(mat4(1), vec3(0, -0.5f * V, -12 * V)), vec3(V));
    mat4 pgrid = glm::scale(glm::translate(mat4(1), vec3(0.005f, 0.5f * V, -11 * V)), vec3(V));
    for (int sd : {1, -1}) {
      mat4 c = glm::rotate(base, sd > 0 ? PI / 2 - ang : PI / 2 + ang, vec3(0, 0, 1));
      VR.draw(cover, c * grid, L);
      VR.draw(pages, glm::translate(c, vec3(0, sd > 0 ? -3.5f * V : 0.5f * V, 0)) * pgrid, L);
    }
    if (a.leaf >= 0) {
      mat4 lm = glm::rotate(base, PI / 2 - ang + a.leaf * 2 * ang, vec3(0, 0, 1));
      VR.draw(pages, glm::scale(glm::translate(lm, vec3(0, -0.5f * V, 0)) * pgrid, vec3(1, 0.34f, 1)), L);
    }
  }
  VR.end();
}

void MagicLayer::drawOpaque(GameSession& gs, const FrameCtx& f) {
  drawBooks(f);
  // potions on the stands, and thrown ones in flight
  VoxelRenderer& VR = voxelRenderer();
  bool begun = false;
  World& w = s.world();
  auto drawItem = [&](const std::string& key, vec3 at, float yaw, float sc) {
    const VoxelModel& im = itemModel(key);
    if (!im.ready()) return;
    if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
    u8 l = w.getLight(ifloor(at.x), ifloor(at.y), ifloor(at.z));
    mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(at)));
    m = glm::rotate(m, yaw, vec3(0, 1, 0)); m = glm::scale(m, vec3(sc));
    VR.draw(im.gl, m * im.matrix(), VoxelLight{(l >> 4) / 15.f, std::max(1, l & 15) / 15.f});
  };
  for (auto& S : stands) {
    json* e = w.blockEntity(S.x, S.y, S.z);
    if (!e || !(*e)["items"].is_array()) continue;
    static const float pads[3][3] = {{12.5f, 8, PI / 2}, {3.5f, 4, -PI / 4 * 3}, {3.5f, 12, -PI / 4}};
    for (int k = 0; k < 3; k++) { ItemStack st = ItemStack::fromJson((*e)["items"][k]); if (!st.empty()) drawItem(st.id, vec3(S) + vec3(pads[k][0] / 16, 2.f / 16 + 0.2f, pads[k][1] / 16), pads[k][2], 0.42f); }
  }
  for (auto& e : s.level->entities()) if (!e->removed && e->type == "potion_flask" && e->realm == s.player->realm)
    drawItem(e->data.value("potion", "splash_potion_water"), e->pos, e->age * 8, 0.36f);
  if (begun) VR.end();
}

void MagicLayer::drawTranslucent(GameSession& gs, const FrameCtx& f) {
  if (!shTried) {
    shTried = true;
    sh = std::make_unique<Shader>();
    if (!sh->load("spellfx.glsl", "spellfx.glsl")) { DQ_WARN("magic: spellfx shader failed"); sh.reset(); }
    std::vector<VertexAttrib> at = {{0, 3, GL_FLOAT, GL_FALSE, false, 0}, {1, 3, GL_FLOAT, GL_FALSE, false, 12}, {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, false, 24}, {3, 2, GL_FLOAT, GL_FALSE, false, 28}};
    meshGlow.setup(sizeof(QV), at); meshSolid.setup(sizeof(QV), at);
  }
  if (!sh) return;
  vGlow.clear(); vSolid.clear();
  for (auto& k : sparks) {
    if (k.size <= 0.002f) continue;
    vec3 rel = f.cam.rel(glm::dvec3(k.pos));
    float t = clampv(k.life / k.max, 0.f, 1.f), alpha = k.shrink ? 1.f : std::min(1.f, t * 3);
    quad(k.shape == 0 || k.shape == 4 ? vGlow : vSolid, rel, k.size, k.col, alpha, k.shape, k.flat);
  }
  for (auto& b : bolts) {
    vec3 rel = f.cam.rel(glm::dvec3(b.pos));
    quad(vGlow, rel, 0.34f + std::sin(b.age * 40) * 0.05f, tileColor(b.fx), 1, 0, false);
    quad(vGlow, rel, 0.55f, tileColor("mana_arcane"), 1, 0, false);
  }
  MagicState& m = magicState(*s.level, *s.player);
  if (m.lightT > 0 && m.witchSet && !s.player->dead) {
    vec3 rel = f.cam.rel(glm::dvec3(m.witch));
    quad(vGlow, rel, 0.3f, tileColor("mana_light"), 1, 0, false);
    quad(vGlow, rel, 0.14f, tileColor("mana_light"), 1, 0, false);
  }
  if (vGlow.empty() && vSolid.empty()) return;
  const Camera& c = f.cam;
  vec3 r = c.right(), up = glm::normalize(glm::cross(r, c.forward()));
  sh->use();
  f.apply(*sh);
  sh->set("uViewProj", c.viewProj); sh->set("uRight", r); sh->set("uUp", up);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
  if (!vSolid.empty()) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); meshSolid.uploadQuads(vSolid.data(), vSolid.size() * sizeof(QV), vSolid.size() / 4, GL_DYNAMIC_DRAW); meshSolid.draw(); }
  if (!vGlow.empty()) { glBlendFunc(GL_SRC_ALPHA, GL_ONE); meshGlow.uploadQuads(vGlow.data(), vGlow.size() * sizeof(QV), vGlow.size() / 4, GL_DYNAMIC_DRAW); meshGlow.draw(); }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}

// ---- the HUD ---------------------------------------------------------------------------------------------------
void MagicLayer::drawHud(GameSession& gs) {
  Player* p = s.player;
  if (!p || p->dead) return;
  App& app = s.app;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  float W = (float)app.width, H = (float)app.height, sc = app.uiScale > 0 ? app.uiScale : 1;
  MagicState& m = magicState(*s.level, *p);
  // the mana bar, shown once you know magic (under the experience row)
  if (!knownSpells(*p).empty() || heldStaff(*p)) {
    float max = manaMax(*p), mana = m.mana < 0 ? max : m.mana;
    std::string k = activeSpell(*p);
    const SpellDef* S = magicData().spell(k);
    std::string txt = (S ? S->name : std::string("No spells")) + " \xC2\xB7 " + std::to_string((int)std::floor(mana)) + "/" + std::to_string((int)max) + (m.ward > 0 ? " \xC2\xB7 ward " + std::to_string((int)std::round(m.ward)) : "");
    float bw = 364 * sc, bh = 6 * sc, x0 = W / 2 - bw / 2, y0 = H - 104 * sc;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw, y0 + bh), IM_COL32(10, 14, 40, 200), 3);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * clampv(mana / std::max(1.f, max), 0.f, 1.f), y0 + bh), IM_COL32(70, 120, 255, 255), 3);
    ImVec2 ts = ImGui::CalcTextSize(txt.c_str());
    u32 c = S ? hexColor(S->col) : 0xcccccc;
    dl->AddText(ImVec2(W / 2 - ts.x / 2 + 1, y0 - ts.y - 1), IM_COL32(0, 0, 0, 200), txt.c_str());
    dl->AddText(ImVec2(W / 2 - ts.x / 2, y0 - ts.y - 2), IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255), txt.c_str());
  }
  // what you're under, top right
  float y = 8;
  for (auto& e : effectsOn(*p)) {
    const EffectDef* E = magicData().effect(e.id);
    if (!E) continue;
    std::string t = E->name + fxRoman(e.lv) + "  " + fmtTime(e.secs);
    ImVec2 ts = ImGui::CalcTextSize(t.c_str());
    float x = W - ts.x - 34;
    bool blink = e.secs < 10 && std::fmod(time, 0.8) < 0.4;
    dl->AddRectFilled(ImVec2(x - 26, y), ImVec2(W - 8, y + ts.y + 6), E->bad ? IM_COL32(70, 20, 20, 190) : IM_COL32(20, 20, 30, 190), 4);
    u32 c = hexColor(E->col);
    dl->AddCircleFilled(ImVec2(x - 14, y + ts.y / 2 + 3), 6, IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255));
    dl->AddText(ImVec2(x, y + 3), blink ? IM_COL32(255, 255, 255, 120) : IM_COL32_WHITE, t.c_str());
    y += ts.y + 10;
  }
  // skill pops (right of the crosshair)
  float py = H * 0.42f;
  for (auto& pp : pops) {
    double age = app.time - pp.t;
    if (age > 2.4) continue;
    std::string t = "+" + std::to_string((long long)std::llround(pp.xp)) + " " + magicData().skillName(pp.k);
    u32 c = 0xffffff; for (auto& sk : magicData().skills) if (sk.key == pp.k) c = hexColor(sk.col);
    int a = (int)(255 * clampv((float)(2.4 - age) / 0.6f, 0.f, 1.f));
    dl->AddText(ImVec2(W * 0.5f + 60, py - (float)age * 6), IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, a), t.c_str());
    py += 18;
  }
  pops.erase(std::remove_if(pops.begin(), pops.end(), [&](const Pop& x) { return app.time - x.t > 2.4; }), pops.end());
  // floating numbers ("+5 XP", "Free!", "Mended")
  const Camera& c = s.frame.cam;
  for (auto& t : texts) {
    vec4 cl = c.viewProj * vec4(c.rel(glm::dvec3(t.pos)), 1);
    if (cl.w <= 0.1f) continue;
    ImVec2 sp((cl.x / cl.w * 0.5f + 0.5f) * W, (1 - (cl.y / cl.w * 0.5f + 0.5f)) * H);
    ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
    int a = (int)(255 * clampv((1.2f - t.t) / 0.4f, 0.f, 1.f));
    dl->AddText(ImVec2(sp.x - ts.x / 2, sp.y), IM_COL32((t.col >> 16) & 255, (t.col >> 8) & 255, t.col & 255, a), t.text.c_str());
  }
  if (flashT > 0) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(230, 240, 255, (int)(160 * clampv(flashT / 0.25f, 0.f, 1.f))));
}

// ---- DQ_MAGIC_TEST: a scripted look at the magic (smoke tests) ---------------------------------------------------------
// DQ_MAGIC_TEST=fire|spellbook|skills|journal|enchant|brew — sets a mage up, casts, opens that screen
void MagicLayer::runTest() {
  testFrame++;
  Player& p = *s.player;
  Level& lv = *s.level;
  auto use = [&](const std::string& key, ivec3 at, int face) { s.app.events.emit("itemUse", json::array({p.name, key, at.x, at.y, at.z, face})); };
  if (testFrame == 60) {
    CommandSource cs; cs.level = &lv; cs.player = &p; cs.name = p.name;
    runCommand(lv, cs, "/learn all");
    runCommand(lv, cs, "/skillxp magic 300000");
    runCommand(lv, cs, "/skillxp mining 20000");
    runCommand(lv, cs, "/skillxp exploring 5000");
    p.inv[0] = ItemStack("ruby_staff", 1); p.selected = 0;
    p.inv[1] = ItemStack("potion_swift", 1); p.inv[2] = ItemStack("lapis_lazuli", 32); p.inv[3] = ItemStack("diamond_sword", 1);
    p.inv[4] = ItemStack("potion_water", 3); p.inv[5] = ItemStack("nether_wart_item", 8); p.inv[6] = ItemStack("cinder_powder", 4);
    p.level = 30;
    giveEffect(lv, p, "speed", 95, 1); giveEffect(lv, p, "night", 8, 0); giveEffect(lv, p, "poison", 30, 0);
    p.pitch = -0.15f;
    DQ_LOG("magic test: set up");
  }
  if (testFrame == 90) { pickSpell(lv, p, "ward"); use("ruby_staff", ivec3(0), -1); }
  if (testFrame == 100) { pickSpell(lv, p, "light"); magicState(lv, p).castCd = 0; use("ruby_staff", ivec3(0), -1); }
  if (testFrame == 110) {
    vec3 f(-std::sin(p.yaw), 0, -std::cos(p.yaw));
    spawnCreature(lv, "skeleton", p.realm, p.pos + f * 7.f);
    World& w = s.world();
    ivec3 c(ifloor(p.pos.x + f.x * 4), ifloor(p.pos.y), ifloor(p.pos.z + f.z * 4));
    if (test == "enchant" || test == "fire") {
      w.setBlock(c.x, c.y, c.z, reg().blockId("enchanting_table"), 0, SetFlags::Urgent);
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (std::max(std::abs(dx), std::abs(dz)) == 2 && (dx + dz) % 2 == 0) w.setBlock(c.x + dx, c.y, c.z + dz, reg().blockId("bookshelf"), 0, SetFlags::Urgent);
    }
    if (test == "brew") {
      w.setBlock(c.x, c.y, c.z, reg().blockId("brewing_stand"), 0, SetFlags::Urgent);
      json& e = brewEntity(w, c.x, c.y, c.z);
      for (int i = 0; i < 3; i++) e["items"][i] = ItemStack("potion_water", 1).toJson();
      e["items"][3] = ItemStack("nether_wart_item", 4).toJson(); e["items"][4] = ItemStack("cinder_powder", 2).toJson();
    }
    testAt = c;
  }
  if (testFrame >= 140 && testFrame <= 250 && testFrame % 22 == 0 && test == "fire") {
    static const char* seq[] = {"firebolt", "fireball", "missiles", "frost", "icenova", "chainlight"};
    pickSpell(lv, p, seq[(testFrame / 22) % 6]); magicState(lv, p).castCd = 0; magicState(lv, p).mana = 200;
    use("ruby_staff", ivec3(0), -1);
  }
  if (testFrame == 200) {
    if (test == "spellbook") s.app.pushScreen(makeSpellbookScreen());
    else if (test == "skills") s.app.pushScreen(makeSkillsScreen());
    else if (test == "journal") s.app.pushScreen(makeJournalScreen());
    else if (test == "enchant" || test == "brew") { p.selected = 4; use(p.held().id, testAt, 2); }
  }
}

} // namespace

void registerMagicClient() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession& s) -> std::shared_ptr<RenderLayer> { return std::make_shared<MagicLayer>(s); });
}

} // namespace dq
