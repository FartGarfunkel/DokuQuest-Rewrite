// ui/hud.cpp — one player's heads-up display (ui/hud.js): hotbar, hearts (poisoned, chilled, withered),
// armour, air bubbles, XP bar and level, held-item name, toasts and pick-ups, boss bar, dimension name,
// F3 debug / FPS / coordinates, screen effects, damage numbers, name tags; the chat log and chat line
// (game/commands.js chatEls/openChat/submitChat), the death screen. Also the in-game keys that open menus.
#include "sim/inventory.h"
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "sim/commands.h"
#include "audio/audio.h"
#include <imgui.h>
#include <deque>
#include <imgui_stdlib.h>

namespace dq {
using namespace ui;

namespace {
struct Toast { std::string text, kind; double t; double life; std::string pickId; int pickN = 0; };
struct ChatLine { std::string text, kind; double t; };
struct Floater { vec3 pos; std::string text; u32 rgb; float life; };
struct HudState {
  std::deque<Toast> toasts;
  std::deque<ChatLine> chat;
  std::vector<std::string> history;
  std::vector<Floater> floaters;
  double chatClosedAt = -100, chatShowUntil = -100;
  bool chatOpen = false;
  std::string dimName; double dimAt = -100;
  bool saving = false; double savingOff = -100;
  std::string heldId; double heldAt = -100; int lastSel = -1;
  double dbgT = 0; std::string dbgText;
  bool deathShown = false;
};
HudState& H() { static HudState h; return h; }
double now() { return app().time; }
}

void hudToast(const std::string& text, const std::string& kind) {
  auto& t = H().toasts;
  t.push_back({text, kind, now(), kind == "boss" ? 4.2 : kind == "whisper" ? 2.6 : 2.8});
  while (t.size() > 5) t.pop_front();
}
void hudPickup(const std::string& id, int n) {
  const ItemDef* d = reg().item(id); if (!d) return;
  auto& t = H().toasts;
  for (auto it = t.rbegin(); it != t.rend(); ++it) if (it->pickId == id && now() - it->t < 1.5) {
    it->pickN += n; it->t = now(); it->text = "+" + std::to_string(it->pickN) + " " + d->name; return;
  }
  Toast x{"+" + std::to_string(n) + " " + d->name, d->has("rarity") ? "rare" : "", now(), 2.2, id, n};
  t.push_back(x);
  while (t.size() > 5) t.pop_front();
}
void chatLine(const std::string& text, const std::string& kind) {
  auto& h = H();
  for (auto& line : splitStr(text, '\n')) h.chat.push_back({line, kind, now()});
  while (h.chat.size() > 60) h.chat.pop_front();
  h.chatShowUntil = now() + 9;
}
void hudDimName(const std::string& name) { H().dimName = name; H().dimAt = now(); }
void hudFloatText(vec3 pos, const std::string& text, u32 rgb) {
  auto& f = H().floaters; f.push_back({pos, text, rgb, 1.1f});
  if (f.size() > 24) f.erase(f.begin());
}
void hudSaving(bool on) { H().saving = on; if (!on) H().savingOff = now() + 0.6; }

void hudInitEvents(App& a) {
  auto str = [](const json& j, size_t i) { return i < j.size() && j[i].is_string() ? j[i].get<std::string>() : std::string(); };
  a.events.on("toast", [str](const json& j) { hudToast(str(j, 0), str(j, 1)); return true; }, -100);
  a.events.on("pickup", [str](const json& j) { if (j.size() > 1 && j[1].is_number()) hudPickup(str(j, 0), j[1].get<int>()); return true; }, -100);
  a.events.on("chatLine", [str](const json& j) { chatLine(str(j, 0), str(j, 1)); return true; }, -100);
  a.events.on("dimName", [str](const json& j) { hudDimName(str(j, 0)); return true; }, -100);
  a.events.on("saving", [](const json& j) { hudSaving(!j.empty() && j[0].is_boolean() && j[0].get<bool>()); return true; }, -100);
  a.events.on("floatText", [str](const json& j) {
    if (j.size() < 4) return true;
    std::string c = str(j, 4); u32 rgb = c.size() == 7 ? (u32)std::strtoul(c.c_str() + 1, nullptr, 16) : 0xffffff;
    hudFloatText(vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()), str(j, 3), rgb); return true; }, -100);
}

// ---- the chat line ----------------------------------------------------------------------------------------
class ChatScreen : public Screen {
public:
  explicit ChatScreen(std::string p) : buf(std::move(p)) {}
  std::string buf; int hist = -1; bool first = true;
  const char* id() const override { return "chat"; }
  bool pausesGame() const override { return false; }
  void onOpen(App& a) override { H().chatOpen = true; }
  void onClose(App& a) override { H().chatOpen = false; H().chatClosedAt = now(); H().chatShowUntil = now() + 9; }
  void submit(App& a, const std::string& t) {
    auto& h = H().history; h.push_back(t); if (h.size() > 40) h.erase(h.begin());
    if (!a.session || !a.session->player) return;
    // a /command runs (its reply comes back through Level::fx->message) or the line is said to everyone
    submitChat(*a.session->level, a.session->player, t);
  }
  // Tab: complete a command name, or an item name in its arguments (commands.js completeChat)
  static std::string complete(const std::string& v) {
    if (!startsWith(v, "/")) return v;
    auto parts = splitStr(v, ' ');
    if (parts.size() <= 1) {
      std::string slash = startsWith(v, "//") ? "//" : "/", want = toLower(v.substr(slash.size()));
      std::vector<std::string> names;
      for (auto& c : allChatCommands()) if (startsWith(c.name, want)) names.push_back(c.name);
      if (names.size() == 1) return slash + names[0] + " ";
      if (names.size() > 1) { std::string l; for (auto& n : names) l += (l.empty() ? "" : "  ") + slash + n; chatLine(l, "dim"); }
      return v;
    }
    std::string last = toLower(parts.back());
    if (last.empty()) return v;
    std::vector<std::string> keys;
    for (auto& d : reg().items) if (startsWith(d.key, last)) { keys.push_back(d.key); if (keys.size() >= 12) break; }
    if (keys.size() == 1) { parts.back() = keys[0]; std::string o; for (auto& x : parts) o += (o.empty() ? "" : " ") + x; return o + " "; }
    if (!keys.empty()) { std::string l; for (auto& k : keys) l += (l.empty() ? "" : "  ") + k; chatLine(l, "dim"); }
    return v;
  }
  void draw(App& a) override {
    float z = Z(), W = (float)a.width, H0 = (float)a.height;
    float vw = W / z, vh = H0 / z;
    float slot = std::floor(clampv(std::min({(vw - 24) / 9.6f, (vh - 140) / 9.5f, 46.f}), 24.f, 46.f));
    float w = std::min(520.f, vw * 0.7f) * z, x = 10 * z, bottom = H0 - (slot * 2.6f + 40) * z;
    float ih = textSize(F_BODY, "Ag").y + 12 * z + 4 * z;
    ImGui::SetNextWindowPos(ImVec2(x, bottom - ih)); ImGui::SetNextWindowSize(ImVec2(w, ih));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##chat", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(font(F_BODY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 184)); ImGui::PushStyleColor(ImGuiCol_Text, col::white);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * z, 6 * z)); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2 * z);
    if (first) { ImGui::SetKeyboardFocusHere(); ImGui::GetIO().InputQueueCharacters.resize(0); }   // the T or / that opened it isn't typed
    ImGui::SetNextItemWidth(w);
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
      auto* self = (ChatScreen*)d->UserData;
      if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        auto& h = H().history; if (h.empty()) return 0;
        int i = self->hist < 0 ? (int)h.size() : self->hist;
        i = clampv(i + (d->EventKey == ImGuiKey_UpArrow ? -1 : 1), 0, (int)h.size());
        self->hist = i;
        d->DeleteChars(0, d->BufTextLen); if (i < (int)h.size()) d->InsertChars(0, h[i].c_str());
      }
      if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        std::string v(d->Buf, d->BufTextLen), c = complete(v);
        if (c != v) { d->DeleteChars(0, d->BufTextLen); d->InsertChars(0, c.c_str()); }
      }
      if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways && self->first) { d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen; self->first = false; }
      return 0;
    };
    bool enter = ImGui::InputTextWithHint("##line", "Chat, or /command \xE2\x80\x94 /help for the list", &buf,
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackAlways, cb, this);
    if (buf.size() > 256) buf.resize(256);
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), col::line, 2 * z, 0, 2 * z);
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2); ImGui::PopFont();
    ImGui::End(); ImGui::PopStyleVar();
    if (enter) { std::string t = buf; a.popScreen(); if (!trimmed(t).empty()) submit(a, t); }
  }
  static std::string trimmed(const std::string& s) { size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t"); return b == std::string::npos ? "" : s.substr(b, e - b + 1); }
};
std::unique_ptr<Screen> makeChatScreen(const std::string& prefill) { return std::make_unique<ChatScreen>(prefill); }
void openChat(App& a, const std::string& prefill) { if (!a.topScreen()) a.pushScreen(makeChatScreen(prefill)); }

// ---- death ---------------------------------------------------------------------------------------------
class DeathScreen : public Screen {
public:
  DeathScreen(std::string c, int l) : cause(std::move(c)), level(l) {}
  std::string cause; int level; double openedAt = 0;
  const char* id() const override { return "death"; }
  bool pausesGame() const override { return false; }
  bool onBack(App&) override { return true; }
  void onOpen(App& a) override { openedAt = a.time; }
  void respawn(App& a) {
    if (!a.session || !a.session->player) { a.popScreen(); return; }
    Player& p = *a.session->player;
    // the gameplay side respawns the player (bed, rules) and returns false; this is the plain fallback
    if (a.events.emit("respawn", json::array({p.name}))) {
      p.dead = false; p.hp = p.maxHp; p.vel = vec3(0); p.fallDist = 0;
      p.pos = p.spawnPoint.y >= 0 ? p.spawnPoint : a.session->level->info.spawn;
    }
    H().deathShown = false;
    a.popScreen();
  }
  void update(App& a, double) override {
    if (a.session && a.session->player && !a.session->player->dead) { H().deathShown = false; a.popScreen(); return; }
    // controller players respawn with A after a short pause
    if (a.time - openedAt > 1.2 && a.input.usingPad() && a.input.pressed(Act::MenuOk)) respawn(a);
  }
  void draw(App& a) override {
    float z = Z(), W = (float)a.width, H0 = (float)a.height;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(W, H0), IM_COL32(90, 0, 0, 115));
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H0 * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##death", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float big = clampv(9 * W / z / 100.f, 40.f, 84.f) / fontPx(F_HUGE);
    std::string note = "Your items were dropped where you fell. You keep level " + std::to_string(level) + ".";
    float colW = std::max({textSize(F_HUGE, "You died", 0, big).x, textSize(F_BODY, cause.c_str()).x, textSize(F_MID, note.c_str()).x, 160 * z}) + 32 * z;
    auto centered = [&](FontId f, const char* t, ImU32 c, float k, ImU32 sh, ImVec2 off) {
      ImVec2 s = textSize(f, t, 0, k); ImVec2 p = ImGui::GetCursorScreenPos();
      ImVec2 q(p.x + (colW - s.x) * 0.5f, p.y);
      if (sh) text(dl, f, q + off, sh, t, 0, k);
      text(dl, f, q, c, t, 0, k);
      ImGui::Dummy(ImVec2(colW, s.y + 10 * z));
    };
    centered(F_HUGE, "You died", col::rgb(0xffd9d4), big, col::rgb(0x3a0000), ImVec2(0, 4 * z));
    centered(F_BODY, cause.c_str(), col::text, 1, 0, ImVec2());
    centered(F_MID, note.c_str(), col::muted, 1, 0, ImVec2());
    float bw = textSize(F_BODY, "Respawn").x + 40 * z;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (colW - bw) * 0.5f);
    if (a.input.padCount() && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    if (button("Respawn", BTN_PRIMARY, false, bw)) respawn(a);
    ImGui::End();
  }
};
std::unique_ptr<Screen> makeDeathScreen(const std::string& cause, int level) { return std::make_unique<DeathScreen>(cause, level); }

// ---- the HUD layer ------------------------------------------------------------------------------------------
namespace {
int xpForLevel(int level) { return 12 + level * 6; }

// Level::fx messages (command replies, chat lines, server notices) reach this player's chat and toasts; every
// other effect goes on to whatever sink was there before (sounds, particles).
class HudFx : public LevelFx {
public:
  LevelFx* next = nullptr; u32 me = 0;
  void sound(const std::string& n, vec3 p, float r, float v, float pi) override { next->sound(n, p, r, v, pi); }
  void soundUi(u32 id, const std::string& n) override { next->soundUi(id, n); }
  void material(const std::string& m, const std::string& k, vec3 p) override { next->material(m, k, p); }
  void voice(const std::string& c, const std::string& k, vec3 p, float pi) override { next->voice(c, k, p, pi); }
  void particles(const std::string& k, vec3 p, int n, const json& e) override { next->particles(k, p, n, e); }
  void shake(u32 id, float s) override { next->shake(id, s); }
  void message(u32 id, const std::string& text, const std::string& kind) override {
    next->message(id, text, kind);
    if (id && id != me) return;
    if (kind.empty() || kind == "chat" || kind == "cmd" || kind == "ok" || kind == "err" || kind == "dim") chatLine(text, kind == "chat" || kind == "cmd" ? "" : kind);
    else hudToast(text, kind);
  }
};

class HudLayer : public RenderLayer {
public:
  explicit HudLayer(GameSession& s) : session(&s) {
    fx.next = s.level->fx; fx.me = s.player ? s.player->id : 0;
    s.level->fx = &fx;
  }
  ~HudLayer() override { if (session && session->level && session->level->fx == &fx) session->level->fx = fx.next; }
  GameSession* session; HudFx fx;
  const char* name() const override { return "hud"; }
  int order() const override { return 1000; }

  void update(GameSession& s, double dt) override {
    App& a = s.app; auto& h = H();
    if (!s.player) return;
    Player& p = *s.player;
    // the toasts and floaters age
    while (!h.toasts.empty() && now() - h.toasts.front().t > h.toasts.front().life + 0.45) h.toasts.pop_front();
    for (auto& f : h.floaters) { f.life -= (float)dt; f.pos.y += (float)dt * 0.8f; }
    h.floaters.erase(std::remove_if(h.floaters.begin(), h.floaters.end(), [](const Floater& f) { return f.life <= 0; }), h.floaters.end());
    // held-item name when the selection changes
    const ItemStack& held = p.inv[clampv(p.selected, 0, 8)];
    std::string hid = held.empty() ? "" : held.id;
    if (hid != h.heldId) { if (!hid.empty()) h.heldAt = now(); h.heldId = hid; }
    // death
    if (p.dead && !h.deathShown) {
      h.deathShown = true;
      while (a.topScreen()) a.popScreen();
      a.pushScreen(makeDeathScreen(p.data.value("deathCause", std::string("You died")), p.level));
      return;
    }
    if (a.topScreen() || p.dead) return;
    // keys that open menus (local.js update)
    Input& in = a.input;
    if (in.pressed(Act::Inventory)) { openInventory(a); return; }
    if (in.pressed(Act::Command)) { openChat(a, "/"); return; }
    if (in.pressed(Act::Chat)) { openChat(a, ""); return; }
    struct K { Act act; const char* tab; };
    for (K k : {K{Act::Map, "map"}, K{Act::Journal, "journal"}, K{Act::Skills, "skills"}, K{Act::Spellbook, "spells"}, K{Act::Stable, "stable"}})
      if (in.pressed(k.act)) for (auto& t : menuTabs()) if (t.key == k.tab && (!t.can || t.can(a))) { t.open(a); return; }
  }

  void drawHud(GameSession& s) override {
    App& a = s.app; auto& h = H();
    if (!s.player) return;
    Player& p = *s.player;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float z = Z(), W = (float)a.width, H0 = (float)a.height, vw = W / z, vh = H0 / z;
    bool hidden = s.hudHidden;
    World& w = s.world();
    vec3 eye = p.pos + vec3(0, p.sneaking ? 1.5f : 1.62f, 0);
    BlockId eyeB = w.getBlock(ifloor(eye.x), ifloor(eye.y), ifloor(eye.z));
    bool eyeWater = reg().tLiquid[eyeB] == 1, eyeLava = reg().tLiquid[eyeB] == 2;
    // ---- screen effects (.fx) ----
    if (eyeWater) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H0), IM_COL32(30, 70, 170, 97));
    if (eyeLava) dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H0), IM_COL32(255, 90, 10, 191));
    if (p.data.value("burn", 0.0) > 0) {
      float fl = 0.75f + 0.25f * (std::sin((float)a.time * 35.f) * 0.5f + 0.5f);
      dl->AddRectFilledMultiColor(ImVec2(0, H0 * 0.45f), ImVec2(W, H0), 0, 0, IM_COL32(255, 120, 20, (int)(140 * fl)), IM_COL32(255, 120, 20, (int)(140 * fl)));
    }
    float hurt = clampv(p.hurtTime * 2.f, 0.f, 1.f) * 0.9f + (p.hp <= 4 && !p.dead ? 0.25f + std::sin((float)a.time * 4) * 0.1f : 0);
    if (hurt > 0.01f) vignette(dl, W, H0, IM_COL32(200, 0, 0, (int)(140 * std::min(1.f, hurt))));
    float portal = p.data.value("portalTime", 0.0f);
    if (portal > 0) vignette(dl, W, H0, IM_COL32(120, 40, 160, (int)(153 * std::min(1.f, portal / 1.6f))));
    // ---- floaters and name tags ----
    Camera& cam = s.frame.cam;
    auto project = [&](vec3 wp, ImVec2& out) {
      vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
      if (c.w <= 0.1f) return false;
      out = ImVec2((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H0); return true;
    };
    if (!hidden) {
      for (auto& f : h.floaters) {
        ImVec2 sp; if (!project(f.pos, sp)) continue;
        ImVec2 ts = textSize(F_H3, f.text.c_str());
        float al = std::min(1.f, f.life * 2);
        textShadow(dl, F_H3, sp - ts * 0.5f, withAlpha(col::rgb(f.rgb), al), f.text.c_str(), ImVec2(2, 2) * z, withAlpha(col::black, al));
      }
      for (auto& e : s.level->entities()) {
        if (e.get() == &p || !e->isPlayer() || e->removed || e->realm != p.realm) continue;
        if (std::hypot(e->pos.x - (float)cam.pos.x, e->pos.z - (float)cam.pos.z) > 64) continue;
        ImVec2 sp; if (!project(e->pos + vec3(0, 2.1f, 0), sp)) continue;
        std::string nm = static_cast<Player*>(e.get())->name;
        ImVec2 ts = textSize(F_MID, nm.c_str());
        ImVec2 p0(sp.x - ts.x * 0.5f - 6 * z, sp.y - ts.y - 2 * z);
        dl->AddRectFilled(p0, p0 + ImVec2(ts.x + 12 * z, ts.y + 2 * z), IM_COL32(0, 0, 0, 115), 2 * z);
        text(dl, F_MID, p0 + ImVec2(6 * z, 1 * z), col::text, nm.c_str());
      }
    }
    // ---- crosshair ----
    if (!hidden && !a.topScreen()) {
      ImVec2 c(std::round(W * 0.5f), std::round(H0 * 0.5f)); float r = 9 * z, t = 1 * z;
      dl->AddRectFilled(ImVec2(c.x - t - 1, c.y - r - 1), ImVec2(c.x + t + 1, c.y + r + 1), IM_COL32(0, 0, 0, 70));
      dl->AddRectFilled(ImVec2(c.x - r - 1, c.y - t - 1), ImVec2(c.x + r + 1, c.y + t + 1), IM_COL32(0, 0, 0, 70));
      dl->AddRectFilled(ImVec2(c.x - t, c.y - r), ImVec2(c.x + t, c.y + r), IM_COL32(255, 255, 255, 230));
      dl->AddRectFilled(ImVec2(c.x - r, c.y - t), ImVec2(c.x + r, c.y + t), IM_COL32(255, 255, 255, 230));
    }
    if (!hidden) { drawBoss(s, dl, z, W); drawBottom(s, dl, z, W, H0, vw, vh, eyeWater); drawDebug(s, dl, z); }
    // dimension name (dimIn 3.2 s)
    double dt = now() - h.dimAt;
    if (!hidden && dt < 3.3 && !h.dimName.empty()) {
      float k = (float)(dt / 3.2), al = k < 0.15f ? k / 0.15f : k < 0.7f ? 1 : std::max(0.f, 1 - (k - 0.7f) / 0.3f), dy = k < 0.15f ? (1 - k / 0.15f) * 8 * z : 0;
      float sc = clampv(7 * vw / 100.f * 100.f / 100.f, 30.f, 64.f) / fontPx(F_TITLE);
      ImVec2 ts = textSize(F_TITLE, h.dimName.c_str(), 0, sc);
      ImVec2 p0(W * 0.5f - ts.x * 0.5f, H0 * 0.28f + dy);
      text(dl, F_TITLE, p0 + ImVec2(0, 4 * z), withAlpha(col::black, al), h.dimName.c_str(), 0, sc);
      text(dl, F_TITLE, p0, withAlpha(col::text, al), h.dimName.c_str(), 0, sc);
    }
    drawToasts(dl, z, vw);
    drawChat(dl, z, W, H0, vw, vh);
    // saving badge
    if (h.saving || now() < h.savingOff) {
      const char* t = "Saving\xE2\x80\xA6"; ImVec2 ts = textSize(F_SMALL, t, 0, 12 / 12.5f);
      ImVec2 p1(W - 12 * z, H0 - 12 * z), p0 = p1 - ts - ImVec2(16, 6) * z;
      dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 128), 3 * z); text(dl, F_SMALL, p0 + ImVec2(8, 3) * z, col::muted, t, 0, 12 / 12.5f);
    }
  }

  static void vignette(ImDrawList* dl, float W, float H0, ImU32 c) {
    // radial-gradient(circle, transparent 45%, colour) — the circle reaches the far corner
    float R = std::hypot(W, H0) * 0.5f;
    radialVignette(dl, ImVec2(W * 0.5f, H0 * 0.5f), ImVec2(R, R), 0.45f, c, 1.01f);
  }

  void drawBoss(GameSession& s, ImDrawList* dl, float z, float W) {
    Player& p = *s.player;
    Entity* boss = nullptr; float bd = 48 * 48;
    const json& mobs = reg().mobs;
    for (auto& e : s.level->entities()) {
      if (e->removed || e->hp <= 0 || e->realm != p.realm || !mobs.contains(e->type) || !mobs[e->type].value("boss", false)) continue;
      vec3 d = e->pos - p.pos; float d2 = glm::dot(d, d);
      if (d2 < bd) { bd = d2; boss = e.get(); }
    }
    if (!boss) return;
    // dungeons.js: the bar names whichever keeper you're fighting
    bool golem = boss->type == "cinder_golem";
    std::string nm = golem ? "Cinder Golem" : mobs[boss->type].value("label", boss->type);
    if (startsWith(nm, "the ")) nm = "The " + nm.substr(4);
    float bw = std::min(520 * z, W * 0.8f), x = (W - bw) * 0.5f, y = 14 * z;
    float k = 26.f / fontPx(F_DISP);
    ImVec2 ts = textSize(F_DISP, nm.c_str(), 0, k), es = golem ? textSize(F_SMALL, "FIRE", 0, 11 / 12.5f) + ImVec2(12, 4) * z : ImVec2(0, 0);
    float tw = ts.x + (golem ? es.x + 8 * z : 0), tx = W * 0.5f - tw * 0.5f;
    textShadow(dl, F_DISP, ImVec2(tx, y), col::text, nm.c_str(), ImVec2(0, 2 * z), col::black, 0, k);
    if (golem) {
      ImVec2 e0(tx + ts.x + 8 * z, y + (ts.y - es.y) * 0.5f);
      dl->AddRectFilled(e0, e0 + es, col::rgb(0x5a1e0a), 3 * z); dl->AddRect(e0, e0 + es, col::emberDeep, 3 * z);
      text(dl, F_SMALL, e0 + ImVec2(6, 2) * z, col::rgb(0xffb07a), "FIRE", 0, 11 / 12.5f);
    }
    y += ts.y + 4 * z;
    ImVec2 t0(x, y), t1(x + bw, y + 12 * z);
    dl->AddRectFilled(t0, t1, col::rgb(0x2a0f08), 2 * z);
    float f = clampv(boss->hp / std::max(1.f, boss->maxHp), 0.f, 1.f);
    dl->AddRectFilledMultiColor(t0 + ImVec2(2, 2) * z, ImVec2(t0.x + 2 * z + (bw - 4 * z) * f, t1.y - 2 * z), col::rgb(0xff9a52), col::rgb(0xff9a52), col::rgb(0xc4321b), col::rgb(0xc4321b));
    dl->AddRect(t0, t1, col::black, 2 * z, 0, 2 * z);
  }

  void drawBottom(GameSession& s, ImDrawList* dl, float z, float W, float H0, float vw, float vh, bool eyeWater) {
    App& a = s.app; auto& h = H(); Player& p = *s.player;
    float slot = std::floor(clampv(std::min({(vw - 24) / 9.6f, (vh - 140) / 9.5f, 46.f}), 24.f, 46.f)) * z;
    float hs = clampv(std::min(vw / 900.f, vh / 560.f), 0.65f, 1.f), sp = 16 * hs * z;
    float rowW = slot * 9 + 16 * z, x0 = std::round((W - rowW) * 0.5f);
    float y = H0 - 8 * z;
    // hotbar (.hotbar: padding 4, gap 2, 2px border)
    float hbW = slot * 9 + 2 * z * 8 + 8 * z + 4 * z, hbH = slot + 12 * z;
    ImVec2 hb0(std::round((W - hbW) * 0.5f), y - hbH), hb1 = hb0 + ImVec2(hbW, hbH);
    dl->AddRectFilled(hb0, hb1, IM_COL32(8, 12, 10, 179), 4 * z);
    dl->AddRect(hb0, hb1, IM_COL32(0, 0, 0, 153), 4 * z, 0, 2 * z);
    for (int i = 0; i < 9; i++) {
      ImVec2 sp0(hb0.x + 6 * z + i * (slot + 2 * z), hb0.y + 6 * z);
      SlotLook lk; lk.inv = false; lk.sel = i == p.selected;
      drawSlot(dl, sp0, slot, lk);
      drawItem(dl, p.inv[i], sp0, slot);
    }
    if (!p.offhand.empty() && !p.offhand[0].empty()) {
      ImVec2 o0(hb0.x - 8 * z - slot - 4 * z, hb1.y - 6 * z - slot - 4 * z);
      dl->AddRectFilled(o0, o0 + ImVec2(slot + 4 * z, slot + 4 * z), IM_COL32(8, 12, 10, 179), 3 * z);
      SlotLook lk; lk.inv = false; drawSlot(dl, o0 + ImVec2(2, 2) * z, slot, lk); drawItem(dl, p.offhand[0], o0 + ImVec2(2, 2) * z, slot);
    }
    y = hb0.y - 4 * z;
    // XP row (height 10: bar inset 2px, level number above its middle)
    ImVec2 xa(x0, y - 8 * z), xb(x0 + rowW, y - 2 * z);
    dl->AddRectFilled(xa, xb, IM_COL32(0, 0, 0, 153), 2 * z);
    float xf = clampv((float)p.xp / xpForLevel(p.level), 0.f, 1.f);
    if (xf > 0) dl->AddRectFilledMultiColor(xa + ImVec2(1, 1) * z, ImVec2(xa.x + 1 * z + (rowW - 2 * z) * xf, xb.y - 1 * z), col::xp, col::xp, col::rgb(0x5f9a20), col::rgb(0x5f9a20));
    dl->AddRect(xa, xb, col::black, 2 * z, 0, 1 * z);
    if (p.level > 0) {
      std::string lv = std::to_string(p.level); float k = 22.f / fontPx(F_H3);
      ImVec2 ts = textSize(F_H3, lv.c_str(), 0, k);
      textOutline(dl, F_H3, ImVec2(W * 0.5f - ts.x * 0.5f, y - 10 * z - 12 * z), col::xp, lv.c_str(), 2 * z, col::black, k);
    }
    y -= 10 * z + 4 * z;
    // hearts (wrap upward, ten to a row) and armour (right, glowing past twenty)
    int hearts = (int)std::ceil(p.maxHp / 2), hp = (int)std::ceil(p.hp);
    int tint = p.data.value("wither", 0.0) > 0 ? HT_WITHER : p.data.value("poison", 0.0) > 0 ? HT_POISON : p.data.value("chill", 0.0) > 0 ? HT_CHILL : HT_NONE;
    int rows = (hearts + 9) / 10;
    float shake = p.hp <= 4 && !p.dead ? (std::sin((float)a.time * 30) > 0 ? -1.f : 1.f) * z : 0;
    for (int i = 0; i < hearts; i++) {
      int r = i / 10, c = i % 10;
      int spr = tint + (hp >= i * 2 + 2 ? SP_HEART : hp >= i * 2 + 1 ? SP_HEART_HALF : SP_HEART_EMPTY);
      drawSprite(dl, spr, ImVec2(x0 + c * (sp + 1 * z), y - (r + 1) * sp - r * 1 * z + shake), sp);
    }
    int ap = armorPoints(p);
    if (ap) for (int i = 0; i < 10; i++) {
      // reversed: the first point is the rightmost shield
      ImVec2 q(x0 + rowW - (i + 1) * sp - i * 1 * z, y - sp);
      drawSprite(dl, ap - 20 > i ? SP_SHIELD_GLOW : SP_SHIELD, q, sp, ap >= i * 2 + 1 ? 1.f : 0.25f);
    }
    y -= std::max(rows, 1) * (sp + 1 * z) + 4 * z;
    // air bubbles while your head is under water (and for a moment after)
    float airMax = std::max(0.01f, p.maxAir), air = p.air;
    if (air < airMax - 0.05f || (eyeWater && !p.creative())) {
      float tenths = air / airMax * 10;
      int n = (int)std::ceil(tenths - 0.001f); bool popping = tenths - std::floor(tenths) < 0.15f && n > 0;
      for (int i = 0; i < n && i < 10; i++) drawSprite(dl, i == n - 1 && popping ? SP_BUBBLE_POP : SP_BUBBLE, ImVec2(x0 + rowW - (n - i) * (sp + 1 * z), y - sp), sp);
      y -= sp + 4 * z;
    }
    // held-item name (shown 1.6 s after a change)
    if (!h.heldId.empty() && now() - h.heldAt < 1.6) {
      const ItemDef* d = reg().item(h.heldId);
      std::string nm = d ? d->name : h.heldId; std::string rar = d ? d->str("rarity") : "";
      ImU32 c = rar == "rare" ? col::frost : rar == "epic" ? col::epic : col::text;
      ImVec2 ts = textSize(F_BODY, nm.c_str());
      ImVec2 q(W * 0.5f - ts.x * 0.5f, y - ts.y - 4 * z);
      for (ImVec2 o : {ImVec2(0, 2), ImVec2(1, 1), ImVec2(-1, 1), ImVec2(0, 0)}) text(dl, F_BODY, q + o * z, IM_COL32(0, 0, 0, 160), nm.c_str());
      text(dl, F_BODY, q, c, nm.c_str());
    }
    // carry hint
    if (p.data.contains("carrying") && p.data["carrying"].is_object()) {
      std::string t = "Carrying " + p.data["carrying"].value("name", std::string("something")) + " \xC2\xB7 use to set down";
      ImVec2 ts = textSize(F_MID, t.c_str()); ImVec2 q(W * 0.5f - ts.x * 0.5f - 10 * z, H0 - slot - 76 * z - ts.y);
      dl->AddRectFilled(q, q + ts + ImVec2(20, 8) * z, IM_COL32(0, 0, 0, 140), 3 * z); text(dl, F_MID, q + ImVec2(10, 4) * z, col::text, t.c_str());
    }
  }

  void drawDebug(GameSession& s, ImDrawList* dl, float z) {
    App& a = s.app; auto& h = H(); Player& p = *s.player;
    bool coords = s.level->info.rules.is_object() && s.level->info.rules.value("showCoords", false);
    if (!s.debugOverlay && !a.settings.showFps && !coords) return;
    h.dbgT += a.dt;
    if (h.dbgT > 0.25 || h.dbgText.empty()) {
      h.dbgT = 0;
      int fps = (int)std::lround(1.0 / std::max(1e-3, a.dt));
      if (!s.debugOverlay && coords) {
        static const char* fac[] = {"north", "west", "south", "east"};
        float yw = std::fmod(std::fmod(p.yaw, TAU) + TAU, TAU);
        h.dbgText = (a.settings.showFps ? std::to_string(fps) + " fps\n" : "") +
          strFormat("XYZ %d %d %d \xC2\xB7 facing %s", ifloor(p.pos.x), ifloor(p.pos.y) - dimBase(dimOfY(ifloor(p.pos.y))), ifloor(p.pos.z), fac[(int)std::lround(yw / (PI / 2)) & 3]);
      } else if (s.debugOverlay) {
        World& w = s.world();
        int d = dimOfY(ifloor(p.pos.y));
        u8 l = w.getLight(ifloor(p.pos.x), ifloor(p.pos.y + 0.5f), ifloor(p.pos.z));
        h.dbgText = strFormat("%d fps\nXYZ %.1f %.1f %.1f\n%s \xC2\xB7 world y %d\nlight sky %d block %d\nchunks %zu \xC2\xB7 sections %d/%d \xC2\xB7 mesh jobs %d\nentities %zu\nstorage game folder\n%s GL %d.%d \xC2\xB7 %s",
          fps, p.pos.x, p.pos.y - dimBase(d), p.pos.z, dimName(d), ifloor(p.pos.y), l >> 4, l & 15, w.chunks().size(), s.worldRenderer.drawnSections,
          s.worldRenderer.totalSections, s.worldRenderer.pendingJobs, s.level->entities().size(), gfx.modern ? "Modern" : "Legacy", gfx.major, gfx.minor, gfx.renderer.c_str());
        h.dbgText += strFormat("\nmouse %s", a.input.captured() ? "held" : "loose");
      } else h.dbgText = std::to_string(fps) + " fps";
    }
    ImVec2 ts = textSize(F_MONO, h.dbgText.c_str());
    ImVec2 p0(10 * z, 50 * z);
    dl->AddRectFilled(p0, p0 + ts + ImVec2(16, 12) * z, IM_COL32(0, 0, 0, 140), 3 * z);
    text(dl, F_MONO, p0 + ImVec2(8, 6) * z, col::text, h.dbgText.c_str());
  }

  void drawToasts(ImDrawList* dl, float z, float vw) {
    auto& h = H();
    float y = 12 * z, maxW = std::min(360.f, vw * 0.6f) * z - 23 * z;
    for (auto& t : h.toasts) {
      double age = now() - t.t;
      float al = age < 0.2 ? (float)(age / 0.2) : age > t.life ? std::max(0.f, 1 - (float)((age - t.life) / 0.4)) : 1.f;
      float slide = age < 0.2 ? (1 - (float)(age / 0.2)) * -8 * z : 0;
      bool whisper = t.kind == "whisper";
      FontId f = F_BODY; float k = 14.f / 15.f;
      ImVec2 ts = textSize(f, t.text.c_str(), maxW, k);
      ImVec2 p0(12 * z + slide, y), p1 = p0 + ts + ImVec2(23, 10) * z;
      ImU32 border = t.kind == "good" ? col::xp : t.kind == "boss" ? col::ember : t.kind == "warn" || t.kind == "bad" ? col::bad : t.kind == "rare" ? col::frost : t.kind == "epic" ? col::epic : col::lineHi;
      if (!whisper) {
        dl->AddRectFilled(p0, p1, withAlpha(IM_COL32(10, 15, 13, 209), al), 2 * z);
        dl->AddRectFilled(p0, ImVec2(p0.x + 3 * z, p1.y), withAlpha(border, al));
      }
      ImU32 tc = whisper ? IM_COL32(220, 220, 220, 140) : t.kind == "boss" ? col::rgb(0xffd0b0) : col::text;
      text(dl, f, p0 + ImVec2(13, 5) * z, withAlpha(tc, al), t.text.c_str(), maxW, k);
      y = p1.y + 4 * z;
    }
  }

  void drawChat(ImDrawList* dl, float z, float W, float H0, float vw, float vh) {
    auto& h = H();
    if (h.chat.empty()) return;
    bool open = h.chatOpen;
    if (!open && now() > h.chatShowUntil) return;
    float slot = std::floor(clampv(std::min({(vw - 24) / 9.6f, (vh - 140) / 9.5f, 46.f}), 24.f, 46.f));
    float w = std::min(520.f, vw * 0.7f) * z, x = 10 * z, bottom = H0 - (slot * 2.6f + 40) * z;
    if (open) bottom -= textSize(F_BODY, "Ag").y + 16 * z + 4 * z;
    float maxH = 0.4f * H0, y = bottom, k = 14.f / 15.f;
    dl->PushClipRect(ImVec2(x, bottom - maxH), ImVec2(x + w, bottom), true);
    for (auto it = h.chat.rbegin(); it != h.chat.rend() && y > bottom - maxH; ++it) {
      double age = now() - std::max(it->t, h.chatClosedAt);
      float al = open ? 1.f : age < 7.2 ? 1.f : std::max(0.f, 1 - (float)((age - 7.2) / 1.8));
      if (al <= 0) continue;
      FontId f = it->kind == "dim" ? F_SMALL : F_BODY; float kk = it->kind == "dim" ? 12.f / 12.5f : k;
      ImVec2 ts = textSize(f, it->text.c_str(), w - 14 * z, kk);
      y -= ts.y + 4 * z;
      dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ts.y + 4 * z), withAlpha(IM_COL32(0, 0, 0, 115), al));
      ImU32 c = it->kind == "ok" ? col::rgb(0xc8f0a0) : it->kind == "err" ? col::rgb(0xff9a8a) : it->kind == "dim" ? col::rgb(0xb8b0a4) : col::rgb(0xf0ece4);
      text(dl, f, ImVec2(x + 8 * z, y + 3 * z), withAlpha(col::black, al), it->text.c_str(), w - 14 * z, kk);
      text(dl, f, ImVec2(x + 7 * z, y + 2 * z), withAlpha(c, al), it->text.c_str(), w - 14 * z, kk);
      y -= 1 * z;
    }
    dl->PopClipRect();
  }
};
}

void registerHudLayer() { registerRenderLayer([](GameSession& s) { return std::make_shared<HudLayer>(s); }); }

} // namespace dq
