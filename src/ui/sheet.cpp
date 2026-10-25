// ui/sheet.cpp — item tooltips (invui.js itemInfoHTML with the dyeing.js and sheet.js additions: what a tool
// or armour piece does in your hands and against what you have now) and the character sheet beside the doll
// (sheet.js: skills, combat level, blow, protection, mana), the 3D doll (sheet.js drawDoll: the real voxel
// character, turned by dragging or the right stick) and the show/hide buttons for what you wear (charparts.js).
#include "sim/villagers.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "sim/crafting.h"
#include "ui/char_preview.h"
#include "render/char_layer.h"
#include <imgui.h>

namespace dq {
using namespace ui;

// ---- skills (game/skills.js) --------------------------------------------------------------------------------
namespace {
const std::vector<std::array<const char*, 4>> SKILLS = {
  {"mining", "Mining", "Min", "#9aa4b0"}, {"woodcutting", "Woodcutting", "Wood", "#8a6a3a"}, {"excavation", "Excavation", "Dig", "#b89a6a"},
  {"farming", "Farming", "Farm", "#6ab04a"}, {"fishing", "Fishing", "Fish", "#4a8ad8"}, {"melee", "Melee", "Melee", "#d84a3a"},
  {"archery", "Archery", "Arch", "#6ac08a"}, {"defence", "Defence", "Def", "#7a8ad0"}, {"smithing", "Smithing", "Smith", "#d8a04a"},
  {"cooking", "Cooking", "Cook", "#e07a3a"}, {"taming", "Taming", "Tame", "#d88ab0"}, {"magic", "Magic", "Magic", "#9a6ad8"}};
const std::vector<double>& skillXp() {
  static std::vector<double> t;
  if (t.empty()) { t = {0, 0}; double pts = 0; for (int L = 1; L < 99; L++) { pts += std::floor(L + 300 * std::pow(2.0, L / 7.0)); t.push_back(std::floor(pts / 4)); } }
  return t;
}
int skillLevel(double xp) { const auto& T = skillXp(); int L = 1; while (L < 99 && xp >= T[L + 1]) L++; return L; }
double skillXpOf(const Player& p, const char* k) { const json& s = p.stats; return s.is_object() && s.contains("skills") && s["skills"].is_object() ? s["skills"].value(k, 0.0) : 0.0; }
int skillLv(const Player& p, const char* k) { return skillLevel(skillXpOf(p, k)); }
double skillBonus(const Player& p, const char* k, double max) { return (skillLv(p, k) - 1) / 98.0 * max; }
double gearBonus(const ItemStack& s, double max) {
  int gx = s.tag.is_object() ? s.tag.value("gx", 0) : 0;
  if (!gx) return 0;
  int lv = std::min(50, 1 + (int)std::floor(std::sqrt(gx / 30.0)));
  return (lv - 1) / 49.0 * max;
}
int armorPts(const Player& p) { int n = 0; for (auto& s : p.armor) if (!s.empty()) if (const ItemDef* d = s.def()) if (d->has("armor")) n += d->props["armor"].value("points", 0); return n; }
// combatLevel (sheet.js): sim/villagers.h (one definition; the paths use it too)
// how much of a blow your armour, your Defence and its wear turn aside (0..1)
double protection(const Player& p) {
  int worn = 0; double gb = 0;
  for (auto& s : p.armor) if (!s.empty()) { worn++; gb += gearBonus(s, 0.03); }
  double arm = std::min(0.8, armorPts(p) * 0.032);
  double sk = worn ? skillBonus(p, "defence", 0.15) + gb : 0;
  return 1 - (1 - arm) * (1 - sk);
}
// a weapon's blow in your hands (hearts): its damage, sharpened, times your Melee and its level
double blowOf(const Player& p, const ItemStack& s) {
  const ItemDef* d = s.empty() ? nullptr : s.def();
  bool tool = d && d->hasTool;
  double dmg = tool ? d->tool.damage : 1;
  int sh = s.tag.is_object() && s.tag.contains("en") && s.tag["en"].is_object() ? s.tag["en"].value("sharp", 0) : 0;
  if (tool && sh) dmg += 0.5 * sh + 0.5;
  return dmg * (1 + skillBonus(p, "melee", 0.2) + (tool ? gearBonus(s, 0.25) : 0)) / 2;
}
double manaMax(const Player& p) {
  double m = 20 + std::floor(skillLv(p, "magic") / 2.0);
  for (auto& s : p.armor) if (!s.empty()) if (const ItemDef* d = s.def()) if (d->has("armor")) m += d->props["armor"].value("mana", 0.0);
  return m;
}
std::string fmt1(double v) { double r = std::round(v * 10) / 10; char b[32]; snprintf(b, sizeof b, "%g", r); return b; }
std::string num(double v) { char b[32]; snprintf(b, sizeof b, "%g", v); return b; }
u32 hexCol(const char* h) { return (u32)std::strtoul(h + 1, nullptr, 16); }
}

// itemInfoHTML: lines of (text, colour); returns the first line (the name)
std::string itemInfoText(const ItemStack& s, const Player* p, std::vector<std::pair<std::string, u32>>* out) {
  std::vector<std::pair<std::string, u32>> L;
  const u32 TN = 0xfff6e0, TS = 0x9fd0ff, TD = 0xb8a98f;
  const ItemDef* d = s.def();
  if (!d) { L.push_back({s.id, TN}); if (out) *out = L; return s.id; }
  std::string rar = d->str("rarity");
  L.push_back({d->name + (s.n > 1 ? " \xC3\x97" + std::to_string(s.n) : ""), rar == "rare" ? 0x8fe3ff : rar == "epic" ? 0xc88cff : TN});
  { std::vector<std::pair<std::string, u32>> en; if (tooltipEnchantLines(s, en)) { L[0].second = 0x8fe8ff; L.insert(L.end(), en.begin(), en.end()); } }   // .tn.enchanted
  tooltipEffectLines(s, L);
  if (d->has("food") && d->props["food"].is_object()) { double h = d->props["food"].value("heal", 0.0); L.push_back({"Heals " + num(h / 2) + (h == 2 ? " heart" : " hearts") + " instantly", TS}); }
  if (d->hasTool) {
    const ToolInfo& t = d->tool;
    static const std::map<std::string, std::string> EL = {{"fire", "Fire"}, {"frost", "Frost"}, {"physical", "Physical"}};
    std::string el = d->props.contains("tool") && d->props["tool"].is_object() ? d->props["tool"].value("element", std::string()) : "";
    auto e = EL.find(el);
    L.push_back({(t.type == "sword" ? std::string("Attack") : "Mining speed " + num(t.speed) + "\xC3\x97 \xC2\xB7 attack") + " " + num(t.damage / 2) + " \xE2\x99\xA5" + (e != EL.end() ? " \xC2\xB7 " + e->second + " element" : ""), TS});
    L.push_back({"Durability " + std::to_string(t.durability - s.d) + " / " + std::to_string(t.durability), TD});
  }
  if (d->has("armor")) {
    const json& a = d->props["armor"];
    L.push_back({"Armor +" + num(a.value("points", 0.0)) + (a.value("fireResist", 0.0) ? " \xC2\xB7 fire resist" : ""), TS});
    int dur = a.value("durability", 0); L.push_back({"Durability " + std::to_string(dur - s.d) + " / " + std::to_string(dur), TD});
  }
  if (d->has("backpack")) L.push_back({num(d->num("backpack")) + " extra slots when worn", TS});
  int light = d->props.contains("light") && d->props["light"].is_number() ? d->props["light"].get<int>() : 0;
  if (!d->props.contains("light") && !d->block.empty()) { auto bi = reg().blockIds.find(d->block); if (bi != reg().blockIds.end()) light = reg().block(bi->second).light; }
  if (light) L.push_back({std::string("Gives light when carried") + (d->has("hook") ? " \xC2\xB7 hangs on your belt hook" : ""), TS});
  if (s.tag.is_object() && s.tag.contains("c") && s.tag["c"].is_array()) { int n = 0; for (auto& x : s.tag["c"]) if (!x.is_null()) n++; L.push_back({"Holds " + std::to_string(n) + " stack" + (n == 1 ? "" : "s"), TS}); }
  if (int f = fuelValue(s.id)) L.push_back({"Fuel: smelts " + std::to_string(f) + " item" + (f == 1 ? "" : "s"), TD});
  { auto it = reg().smelting.find(s.id); if (it != reg().smelting.end()) { const ItemDef* od = reg().item(it->second); L.push_back({"Smelts into " + (od ? od->name : it->second), TD}); } }
  if (d->has("desc")) L.push_back({d->str("desc"), TD});
  if (s.tag.is_object() && s.tag.contains("col") && s.tag["col"].is_string()) L.push_back({"\x01" + s.tag["col"].get<std::string>() + " Dyed", TS});
  // sheet.js: what it does in your hands, and against what you have now
  if (p) {
    auto cmp = [](double a, double b, const char* unit) -> std::string { double dv = std::round((a - b) * 10) / 10; if (!dv) return ""; return dv > 0 ? " \xE2\x96\xB2 +" + fmt1(dv) + unit : " \xE2\x96\xBC " + fmt1(dv) + unit; };
    if (d->hasTool && d->tool.type != "bow" && d->tool.type != "igniter" && d->tool.type != "shears") {
      const ItemStack& held = p->inv[clampv(p->selected, 0, 8)];
      double mine = blowOf(*p, s);
      std::string line = "In your hands: " + fmt1(mine) + " \xE2\x99\xA5 a blow";
      if (!held.empty() && &held != &s) line += cmp(mine, blowOf(*p, held), "");
      L.push_back({line, TS});
      static const char* TIER[] = {"", "wood", "stone", "iron", "diamond", "mithril", "adamantite"};
      int tier = d->tool.tier;
      if (d->tool.type != "sword" && tier) L.push_back({"Tier " + std::to_string(tier) + (tier < 7 && *TIER[tier] ? std::string(" (") + TIER[tier] + ")" : "") + " \xC2\xB7 digs " + fmt1(d->tool.speed * (1 + gearBonus(s, 0.35))) + "\xC3\x97 as fast as a bare hand", TD});
    }
    if (d->has("armor")) {
      int slot = d->props["armor"].value("slot", 0);
      if (slot >= 0 && slot < (int)p->armor.size() && &p->armor[slot] != &s) {
        const ItemStack& worn = p->armor[slot]; const ItemDef* wd = worn.empty() ? nullptr : worn.def();
        double wp = wd && wd->has("armor") ? wd->props["armor"].value("points", 0.0) : 0;
        L.push_back({"Instead of " + (wd ? wd->name : std::string("nothing")) + ":" + cmp(d->props["armor"].value("points", 0.0), wp, " armour"), TD});
      }
    }
  }
  tooltipGearLines(s, L);   // skills.js: Level N and its progress bar
  if (out) *out = L;
  return L[0].first;
}

// the character sheet (sheet.js sheetHTML), drawn into a box at p of width w; returns its height
float drawSheet(ImDrawList* dl, ImVec2 p, float w, float is, const Player& pl) {
  float fs = is * 0.24f / Z() / fontPx(F_BODY);
  float lh = textSize(F_BODY, "Ag", 0, fs).y;
  int total = 0; for (auto& s : SKILLS) total += skillLevel(skillXpOf(pl, s[0]));
  float y = p.y;
  // sh-head: Combat N · Total N (numbers bright)
  { std::string c = std::to_string(combatLevel(pl)), t = std::to_string(total);
    float cw = textSize(F_BODY, "Combat ", 0, fs).x; text(dl, F_BODY, ImVec2(p.x, y), col::dim, "Combat ", 0, fs); text(dl, F_BODY, ImVec2(p.x + cw, y), col::rgb(0xfff3d8), c.c_str(), 0, fs);
    float tw = textSize(F_BODY, t.c_str(), 0, fs).x, tl = textSize(F_BODY, "Total ", 0, fs).x;
    text(dl, F_BODY, ImVec2(p.x + w - tw - tl, y), col::dim, "Total ", 0, fs); text(dl, F_BODY, ImVec2(p.x + w - tw, y), col::rgb(0xfff3d8), t.c_str(), 0, fs); }
  y += lh + is * 0.08f;
  // sh-grid: two columns of skills with a progress line under each
  float gap = 6 * Z() * is / 40, cw = (w - gap) / 2, rh = lh + 4 * Z() * is / 40;
  const auto& T = skillXp();
  for (size_t i = 0; i < SKILLS.size(); i++) {
    float x = p.x + (i % 2) * (cw + gap), yy = y + (i / 2) * (rh + 2 * Z() * is / 40);
    double xp = skillXpOf(pl, SKILLS[i][0]); int L = skillLevel(xp);
    double pr = L >= 99 ? 1 : (xp - T[L]) / std::max(1.0, T[std::min(99, L + 1)] - T[L]);
    dl->AddRectFilled(ImVec2(x, yy), ImVec2(x + cw, yy + rh), IM_COL32(0, 0, 0, 71), 2 * Z());
    ImU32 c = col::rgb(hexCol(SKILLS[i][3]));
    text(dl, F_BODY, ImVec2(x + 3 * Z(), yy + 1 * Z()), c, SKILLS[i][2], 0, fs);
    std::string lv = std::to_string(L); ImVec2 ls = textSize(F_BODY, lv.c_str(), 0, fs);
    text(dl, F_BODY, ImVec2(x + cw - 3 * Z() - ls.x, yy + 1 * Z()), col::rgb(0xfff3d8), lv.c_str(), 0, fs);
    dl->AddRectFilled(ImVec2(x, yy + rh - 2 * Z()), ImVec2(x + cw * (float)clampv(pr, 0.0, 1.0), yy + rh), withAlpha(c, 0.85f));
    if (ImGui::IsMouseHoveringRect(ImVec2(x, yy), ImVec2(x + cw, yy + rh))) {
      std::string tip = std::string(SKILLS[i][1]) + " level " + lv + " \xC2\xB7 " + std::to_string((long long)xp) + " xp";
      if (L < 99) tip += " \xC2\xB7 " + std::to_string((long long)std::ceil(T[L + 1] - xp)) + " to " + std::to_string(L + 1);
      ImGui::SetTooltip("%s", tip.c_str());
    }
  }
  y += ((SKILLS.size() + 1) / 2) * (rh + 2 * Z() * is / 40) + is * 0.04f;
  // sh-foot: ⚔ blow · shield protection · ✦ mana
  double mana = manaMax(pl);
  std::string blow = "\xE2\x9A\x94 " + fmt1(blowOf(pl, pl.inv[clampv(pl.selected, 0, 8)])), prot = std::to_string((int)std::lround(protection(pl) * 100)) + "%";
  float fx = p.x, sp = lh * 0.8f, gapx = 8 * Z() * is / 40;
  text(dl, F_BODY, ImVec2(fx, y), col::dim, blow.c_str(), 0, fs); fx += textSize(F_BODY, blow.c_str(), 0, fs).x + gapx;
  drawSprite(dl, SP_SHIELD, ImVec2(fx, y + (lh - sp) * 0.5f), sp, 0.8f); fx += sp + 2 * Z();
  text(dl, F_BODY, ImVec2(fx, y), col::dim, prot.c_str(), 0, fs); fx += textSize(F_BODY, prot.c_str(), 0, fs).x + gapx;
  if (mana > 0) text(dl, F_BODY, ImVec2(fx, y), col::dim, ("\xE2\x9C\xA6 " + std::to_string((int)std::lround(mana))).c_str(), 0, fs);
  y += lh;
  return y - p.y;
}

// ---- the doll (sheet.js): you, in 3D — your skin, your armour, what's in your hands and on your back, standing
// there breathing. It turns slowly this way and that; drag it (or push the right stick) to turn it round.
namespace {
struct DollState { CharPreview* view = nullptr; float spin = 0.35f, base = 0.35f; bool drag = false; float dragX = 0, dragS = 0; };
DollState& doll() { static DollState d; if (!d.view) d.view = new CharPreview(); return d; }   // kept past exit (GL objects)
}
void drawDoll(ImDrawList* dl, ImVec2 a, ImVec2 b, const Player& pl, double t) {
  // canvas: radial-gradient(ellipse at 50% 80%, #2c241c, #0a0806) with a sunk border
  dl->AddRectFilledMultiColor(a, b, col::rgb(0x0a0806), col::rgb(0x0a0806), col::rgb(0x2c241c), col::rgb(0x2c241c));
  bevel(dl, a, b, col::slLo, col::slHi, 2 * Z());
  DollState& D = doll();
  ImGuiIO& io = ImGui::GetIO();
  // dragging turns it; let go and it carries on turning a little this way and that from there
  bool over = io.MousePos.x >= a.x && io.MousePos.y >= a.y && io.MousePos.x < b.x && io.MousePos.y < b.y;
  if (over && ImGui::IsMouseClicked(0) && pl.cursor.empty()) { D.drag = true; D.dragX = io.MousePos.x; D.dragS = D.spin; }
  if (D.drag && !ImGui::IsMouseDown(0)) { D.drag = false; D.base = D.spin - std::sin((float)t / 2.6f) * 0.45f; }
  float stick = padAxis(ImGuiKey_GamepadRStickRight) - padAxis(ImGuiKey_GamepadRStickLeft);
  if (std::fabs(stick) > 0.15f) D.base += stick * io.DeltaTime * 2.6f;
  if (D.drag) D.spin = D.dragS + (io.MousePos.x - D.dragX) * 0.02f / Z();
  else D.spin = D.base + std::sin((float)t / 2.6f) * 0.45f;
  if (over) ImGui::SetTooltip("Drag to turn");
  // you as the doll: standing still, facing out, what you wear and hold
  auto spec = resolveCharacter(pl.skin);
  CharWear wear = charWearOf(pl, *spec, true);
  CharPose ps;
  ps.time = (float)t; ps.headPitch = std::sin((float)t / 2.6f) * 0.08f;
  ps.held = !wear.held.empty(); ps.off = !wear.off.empty(); ps.capeA = 0.1f; ps.capeSway = std::sin((float)t * 3.1f) * 0.02f;
  CharPreviewOpts o;
  o.wear = &wear; o.pose = &ps; o.yaw = PI + D.spin; o.elev = 0.035f; o.dist = 66; o.target = 15.5f; o.time = (float)t;
  float fb = io.DisplayFramebufferScale.x;
  ImVec2 in(2 * Z(), 2 * Z());
  ImVec2 sz = (b - in) - (a + in);
  GLuint tex = D.view->render(*spec, (int)(sz.x * fb), (int)(sz.y * fb), o);
  if (tex) dl->AddImage((ImTextureID)(intptr_t)tex, a + in, b - in, ImVec2(0, 1), ImVec2(1, 0));
}

// charparts.js GEAR_VIS: show or hide each thing you wear (hidden gear still protects you and still works)
static const char* GEAR_VIS[6][2] = {{"Helmet", "iron_helmet"}, {"Chestplate", "iron_chestplate"}, {"Leggings", "iron_leggings"}, {"Boots", "iron_boots"}, {"Backpack", "leather_backpack"}, {"Cape", "ember_cape"}};
void toggleGearVis(Player& p, int k) {
  if (k < 0 || k >= 6) return;
  json h = p.data.contains("hideGear") && p.data["hideGear"].is_array() ? p.data["hideGear"] : json::array();
  while (h.size() < 6) h.push_back(0);
  h[k] = (h[k].is_number() && h[k].get<int>() != 0) ? 0 : 1;
  p.data["hideGear"] = h;
  playClick();
}
void drawGearVis(ImDrawList* dl, ImVec2 a, ImVec2 b, const Player& p, int k, bool focus, bool hover) {
  if (k < 0 || k >= 6) return;
  bool off = gearHidden(p, k);
  float z = Z(), w = b.x - a.x, h = b.y - a.y;
  // .eqvis: rgba(0,0,0,.35) with a --sl-lo border; .off: the icon greyed, a red stroke across
  dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, hover || focus ? 150 : 90));
  dl->AddRect(a, b, focus ? col::gold : col::slLo, 0, 0, (focus ? 2.f : 1.f) * z);
  float ic = std::min(w, h) * 0.8f;
  drawIcon(dl, GEAR_VIS[k][1], ImVec2(a.x + (w - ic) * 0.5f, a.y + (h - ic) * 0.5f), ic, off ? 0.3f : 1.f);
  if (off) {
    ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f); float r = w * 0.38f, dx = std::cos(0.61f) * r, dy = std::sin(0.61f) * r;
    dl->AddLine(ImVec2(c.x - dx, c.y + dy), ImVec2(c.x + dx, c.y - dy), col::rgb(0xe0584a), 2 * z);
  }
  if (hover) ImGui::SetTooltip("%s %s", off ? "Show" : "Hide", toLower(GEAR_VIS[k][0]).c_str());
}

} // namespace dq
