// render/activity_layer.cpp — What Are They Up To (Watut), our own way: in a game with others you see what each is doing.
//   * typing in chat: a speech bubble over their head with three dots bouncing, quicker the faster they type;
//   * in a menu (their bag, a chest, the crafting screen...): a glowing screen held up before them, the right hand
//     tapping at it (the pose: render/char_anim.cpp CharPose::gui);
//   * nothing pressed for five minutes: they doze off on their feet, the head dropped, "Zz" drifting up (CharPose::doze).
// Your own state is worked out here every frame (the screen on top, the keys you type, how long since you last touched
// anything) into Player::activity / typeRate, which the net carries to the others (net/client.cpp sendMe, server_sync).
#include "ui/book_ui.h"
#include "client/session.h"
#include "client/app.h"
#include "render/item_layers.h"
#include "sim/player.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include <cmath>

namespace dq {
namespace {

constexpr double DOZE_AFTER = 300;   // seconds without a touch (Watut's five minutes)

class ActivityLayer : public RenderLayer {
public:
  const char* name() const override { return "activity"; }
  int order() const override { return 123; }
  void update(GameSession& s, double dt) override {
    time += dt;
    Player* me = s.player;
    if (!me) return;
    Input& in = s.app.input;
    const Screen* top = s.app.topScreen();
    const bool typing = top && std::string(top->id()) == "chat";
    const std::string tid = top ? top->id() : "";
    const bool menu = top && !typing && isBookScreen(top);   // (the menus in the journal: not a photo, the HUD laid out, a station worked in the world)
    // how fast they type: characters a second, eased
    const float now = dt > 0 ? (float)in.typed.size() / (float)dt : 0.f;
    rate += (std::min(now, 40.f) - rate) * std::min(1.f, (float)dt * 1.5f);
    if (!typing) rate = 0;
    const bool touched = !in.typed.empty() || !in.lastCode.empty() || in.lastPadButton >= 0
                         || glm::length(in.lookDelta()) > 1e-4f || std::hypot(me->vel.x, me->vel.z) > 0.1f || top != lastTop;
    // another menu: the page turned (its way set by the tab switch). (Whether the last was a page is remembered, not asked of it:
    // it may be gone — a menu switch closes it — and asking a closed screen crashed the game.)
    if (menu && lastWasBook && top != lastTop) me->bookFlip++;
    lastWasBook = menu;
    if (!menu && !me->bookPic.empty()) { me->bookPic.clear(); me->bookPicV++; }
    (void)0;   // (Q / LB / RB switch the menu: the switch turns the page, above)
    lastTop = top;
    idle = touched ? 0 : idle + dt;
    if (const char* ds = std::getenv("DQ_DEV_STATION")) {   // (screenshots: you as the others see you working a station, in F5)
      const float yaw = me->yaw; me->stationKind = (i8)std::atoi(ds);
      me->workAt = me->pos + vec3(-std::sin(yaw), 0, -std::cos(yaw)) * 0.65f + vec3(std::cos(yaw), 0, -std::sin(yaw)) * 0.12f + vec3(0, me->stationKind >= 3 ? 0.45f : 0.95f, 0);
      static double tapT = 0; if ((tapT += dt) > 0.9) { tapT = 0; me->workTap++; }
    }
    if (const char* dev = std::getenv("DQ_DEV_ACTIVITY")) { me->activity = (u8)std::atoi(dev); me->typeRate = 6; return; }   // (screenshots)
    me->activity = (u8)((typing ? Player::ACT_TYPING : 0) | (menu ? Player::ACT_SCREEN : 0) | (!top && idle > DOZE_AFTER ? Player::ACT_DOZE : 0));
    me->typeRate = typing ? rate : 0;
  }
  void drawHud(GameSession& s) override {
    if (!s.player || s.hudHidden) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float W = (float)s.app.width, H = (float)s.app.height, z = ui::Z();
    Camera& cam = s.frame.cam;
    auto project = [&](vec3 wp, ImVec2& out) {
      vec4 c = cam.viewProj * vec4(cam.rel(glm::dvec3(wp)), 1);
      if (c.w <= 0.1f) return false;
      out = ImVec2((c.x / c.w * 0.5f + 0.5f) * W, (1 - (c.y / c.w * 0.5f + 0.5f)) * H); return true;
    };
    const float t = (float)time;
    for (auto& up : s.level->entities()) {
      auto* p = dynamic_cast<Player*>(up.get());
      if (!p || p->removed || p->dead || !p->activity || p->realm != s.player->realm) continue;
      if (p == s.player && s.viewMode == 0) continue;   // (your own only shows when you see yourself)
      const float d = glm::distance(p->pos, vec3(cam.pos));
      if (d > 32) continue;
      const float k = std::clamp(8.f / std::max(d, 1.f), 0.35f, 1.2f) * z;   // smaller further off
      const int alpha = (int)(255 * std::clamp((32.f - d) / 6.f, 0.f, 1.f));
      // (at a menu they hold their journal open in both hands: render/char_layer.cpp, render/book_model.h)
      ImVec2 head;
      if (!project(p->pos + vec3(0, p->sneaking ? 2.05f : 2.3f, 0), head)) continue;
      if (p->activity & Player::ACT_TYPING) {   // a bubble with three dots bouncing in turn
        const float bw = 34 * k, bh = 18 * k, sp = 4.f + std::min(p->typeRate, 20.f) * 0.6f;
        const ImVec2 a(head.x - bw / 2, head.y - bh), b(head.x + bw / 2, head.y);
        dl->AddRectFilled(a, b, IM_COL32(250, 250, 250, alpha * 230 / 255), bh / 2);
        dl->AddTriangleFilled(ImVec2(head.x - 5 * k, head.y - 1), ImVec2(head.x + 3 * k, head.y - 1), ImVec2(head.x - 6 * k, head.y + 6 * k), IM_COL32(250, 250, 250, alpha * 230 / 255));
        for (int i = 0; i < 3; i++) {
          const float hop = std::max(0.f, std::sin(t * sp - i * 0.9f)) * 3.5f * k;
          dl->AddCircleFilled(ImVec2(head.x + (i - 1) * 9 * k, head.y - bh / 2 - hop), 2.6f * k, IM_COL32(60, 60, 70, alpha));
        }
      } else if (p->activity & Player::ACT_DOZE) {   // Zz drifting up off them
        for (int i = 0; i < 3; i++) {
          const float ph = std::fmod(t * 0.45f + i / 3.f, 1.f);
          const ImVec2 at(head.x + (6 + ph * 14) * k, head.y - ph * 34 * k);
          const char* zz = i == 1 ? "z" : "Z";
          const int za = (int)(alpha * std::sin(ph * 3.14159f));
          ui::textShadow(dl, ui::F_MID, at, IM_COL32(230, 236, 255, za), zz, ImVec2(z, z), IM_COL32(0, 0, 0, za));
        }
      }
    }
  }

private:
  double time = 0, idle = 0;
  float rate = 0;
  bool lastWasBook = false; const Screen* lastTop = nullptr;
};

} // namespace

void registerActivityLayer() {
  static bool done = false;
  if (done) return;
  done = true;
  registerRenderLayer([](GameSession&) { return std::make_shared<ActivityLayer>(); });
}

} // namespace dq
