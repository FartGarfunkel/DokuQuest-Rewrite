// ui/photo_mode.cpp — Better than Adventure 8.0.1's Photo Mode (its GuiPhotoMode: "Photo Mode..." in the pause menu, or
// the photo mode key, F6). The camera leaves you and flies on its own, up to 48 blocks off; the world stands still in a
// world of your own (a shared one keeps going); the HUD is put away. The panel down the left side: Take Screenshot (the
// panel hides for it), Center Camera, Show Player, Time of Day, Season, Fog, Tilt, Field of View, Shader and its
// Intensity — all of it only the picture: the world's own time and season are untouched.
//   Mouse: "Click and drag to move and rotate" — the left button turns the camera, the right one slides it, the wheel
//   moves it in and out; W A S D fly, Space / Shift rise and sink, Sprint goes faster.
//   Controller: the panel as any menu; X puts it away to fly the camera (left stick, right stick to look, the triggers
//   down and up, the bumpers tilt, A takes the picture), X or B brings it back. B on the panel (or Esc) leaves.
#include "client/photo_mode.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "render/frame.h"
#include "sim/player.h"
#include "sim/level.h"
#include "input/input.h"
#include <imgui.h>
#include <cstdlib>

namespace dq {
using namespace ui;

PhotoMode& photoMode() { static PhotoMode m; return m; }

double photoDayTime() { return std::fmod(photoMode().hour - 6.0 + 48.0, 24.0) / 24.0 * DAY_LEN; }   // 06:00 is sunrise, time 0

void photoFrame(FrameCtx& f) {
  const PhotoMode& m = photoMode();
  if (m.season > 0) {   // render/sky.cpp's seasons: spring fresh, summer as it is, autumn gold and rust, winter dull
    static const vec4 S[4] = {vec4(0.62f, 1.0f, 0.42f, 0.18f), vec4(1, 1, 1, 0), vec4(1.0f, 0.56f, 0.18f, 0.6f), vec4(0.72f, 0.68f, 0.56f, 0.5f)};
    f.season = S[(m.season - 1) & 3];
  }
  if (!m.fog) { f.fogStart = 1.0e6f; f.fogEnd = 2.0e6f; f.nearFog = vec3(0); }
}

namespace {
// the filters (assets/shaders/colour_filter.glsl uPhoto)
const char* const SHADERS[] = {"None", "Black & White", "Sepia", "Vivid", "Faded Film", "Warm", "Cool", "Retro", "Dreamy", "Vignette"};
constexpr int N_SHADERS = 10;
const char* const SEASONS[] = {"The World's", "Spring", "Summer", "Autumn", "Winter"};
constexpr float REACH = 48;   // how far the camera may go from you

class PhotoModeScreen : public Screen {
public:
  const char* id() const override { return "photo_mode"; }
  bool onBack(App&) override { if (flying) { flying = false; focusPending = true; return true; } return false; }

  void onOpen(App& a) override {
    if (!a.session || !a.session->player) return;
    GameSession& s = *a.session;
    PhotoMode& m = photoMode();
    m.active = true;
    m.pos = s.frame.cam.pos; m.yaw = s.frame.cam.yaw; m.pitch = s.frame.cam.pitch; m.tilt = 0;
    if (!m.setTime) m.hour = (float)std::fmod(6.0 + s.level->info.time / DAY_LEN * 24.0 + 48.0, 24.0);
    hudWas = s.hudHidden; s.hudHidden = true;
    if (const char* t = std::getenv("DQ_PHOTO")) {   // tests: "shader=2,season=4,fog=0,hour=18,tilt=10,back=6"
      std::string v = t; size_t i = 0;
      while (i < v.size()) {
        size_t c = v.find(',', i); if (c == std::string::npos) c = v.size();
        const std::string kv = v.substr(i, c - i); i = c + 1;
        const size_t e = kv.find('='); if (e == std::string::npos) continue;
        const std::string k = kv.substr(0, e); const float n = (float)std::atof(kv.c_str() + e + 1);
        if (k == "shader") m.shader = clampv((int)n, 0, N_SHADERS - 1); else if (k == "season") m.season = clampv((int)n, 0, 4);
        else if (k == "fog") m.fog = n != 0; else if (k == "hour") { m.setTime = true; m.hour = n; } else if (k == "tilt") m.tilt = n;
        else if (k == "back") m.pos -= glm::dvec3(-std::sin(m.yaw) * n, -2.0, -std::cos(m.yaw) * n);   // step the camera back (and up) to see you
        else if (k == "up") m.pos.y += n;
        else if (k == "pitch") m.pitch = n * (float)PI / 180.f;
        else if (k == "front") { m.pos += glm::dvec3(-std::sin(m.yaw) * n, -0.4, -std::cos(m.yaw) * n); m.yaw += PI; m.pitch = -0.1f; }   // round in front, looking back at you
      }
    }
  }
  void onClose(App& a) override {
    photoMode().active = false;
    if (a.session) a.session->hudHidden = hudWas;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  }

  void draw(App& a) override {
    if (a.topScreen() != this) return;
    if (!a.session || !a.session->player) { a.popScreen(); return; }
    PhotoMode& m = photoMode();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;   // Space flies up: it mustn't press the button last clicked
    const float dt = std::min(0.1f, io.DeltaTime);
    openT += dt;
    if (openT > 0.25 && a.input.pressed(Act::PhotoMode)) { a.popScreen(); return; }
    fly(a, dt);
    if (shoot) { a.takeScreenshot(); shoot = false; saidShot = true; return; }   // this frame without the panel: that's the picture
    if (saidShot) { a.toast("Screenshot saved", "good"); saidShot = false; }
    const float z = Z(), W = (float)a.width, H = (float)a.height;
    const bool pad = a.input.usingPad();
    // the hint along the bottom
    {
      std::string hint = pad ? (flying ? "Left stick to fly, right stick to look, triggers down / up, bumpers tilt. " + padButtonName(0, a.settings.padLayout) + ": take the picture, " + padButtonName(2, a.settings.padLayout) + ": the panel"
                                       : padButtonName(2, a.settings.padLayout) + ": put the panel away and fly the camera")
                             : "Click and drag to move and rotate. W A S D to fly, Space / Shift up and down, the wheel to move in.";
      ImVec2 ts = textSize(F_BODY, hint.c_str());
      textShadow(ImGui::GetForegroundDrawList(), F_BODY, ImVec2((W - ts.x) * 0.5f, H - ts.y - 14 * z), col::text, hint.c_str(), ImVec2(1, 1) * z, col::black);
    }
    if (flying) return;
    const float bw = 236 * z, bh = 30 * z, gap = 6 * z;
    ImGui::SetNextWindowPos(ImVec2(16 * z, H * 0.5f), ImGuiCond_Always, ImVec2(0, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(W, H - 40 * z));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * z, 10 * z));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 150));
    ImGui::Begin("##photo", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    padScrollHere();
    { ImVec2 ts = textSize(F_BIG, "Photo Mode"); ImVec2 p = ImGui::GetCursorScreenPos();
      textShadow(dl, F_BIG, ImVec2(p.x + (bw - ts.x) * 0.5f, p.y), col::white, "Photo Mode", ImVec2(2, 2) * z, col::rgb(0x3f3f3f)); ImGui::Dummy(ImVec2(bw, ts.y + 6 * z)); }
    if (focusPending && a.input.padCount()) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    if (a.input.padCount() > 0) focusPending = false;
    char b[64];
    const ImVec2 B(bw, bh);
    if (mcButton("Take Screenshot##shot", B)) shoot = true;
    if (mcButton("Center Camera##centre", B)) centre(a);
    if (mcButton(m.showPlayer ? "Show Player: On##show" : "Show Player: Off##show", B)) m.showPlayer = !m.showPlayer;
    if (mcButton(m.setTime ? "Time of Day: Set##tod" : "Time of Day: The World's##tod", B)) m.setTime = !m.setTime;
    if (m.setTime) {
      const int mins = (int)std::lround(m.hour * 60) % (24 * 60);
      std::snprintf(b, sizeof b, "%02d:%02d", mins / 60, mins % 60);
      mcSlider("##hour", &m.hour, 0, 23.75f, 0.25f, b, B);
    }
    std::snprintf(b, sizeof b, "Season: %s##season", SEASONS[m.season]);
    if (mcButton(b, B)) m.season = (m.season + 1) % 5;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m.season = (m.season + 4) % 5;
    if (mcButton(m.fog ? "Fog: On##fog" : "Fog: Off##fog", B)) m.fog = !m.fog;
    std::snprintf(b, sizeof b, "Tilt: %+d", (int)std::lround(m.tilt));
    mcSlider("##tilt", &m.tilt, -45, 45, 1, m.tilt == 0 ? "Tilt: None" : b, B);
    std::snprintf(b, sizeof b, "Field of View: %d", (int)std::lround(clampv(a.settings.fov + m.fovAdd, 20.f, 120.f)));
    mcSlider("##fov", &m.fovAdd, -50, 40, 1, b, B);
    std::snprintf(b, sizeof b, "Shader: %s##shader", SHADERS[m.shader]);
    if (mcButton(b, B)) m.shader = (m.shader + 1) % N_SHADERS;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m.shader = (m.shader + N_SHADERS - 1) % N_SHADERS;
    if (m.shader > 0) {
      std::snprintf(b, sizeof b, "Intensity: %d%%", (int)std::lround(m.intensity * 100));
      mcSlider("##amt", &m.intensity, 0, 1, 0.05f, b, B);
    }
    ImGui::Dummy(ImVec2(bw, 2 * z));
    if (mcButton("Done##done", B)) { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); a.popScreen(); return; }
    overPanel = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsAnyItemActive();
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
  }

private:
  bool flying = false, shoot = false, saidShot = false, hudWas = false, focusPending = true, overPanel = false;
  double openT = 0;
  int drag = -1;   // the mouse button dragging the camera

  void centre(App& a) {
    PhotoMode& m = photoMode(); const Player& p = *a.session->player;
    m.pos = glm::dvec3(p.pos.x, p.pos.y + p.eye, p.pos.z); m.yaw = p.yaw; m.pitch = p.pitch; m.tilt = 0;
  }
  void fly(App& a, float dt) {
    PhotoMode& m = photoMode(); Input& in = a.input; const Player& p = *a.session->player;
    ImGuiIO& io = ImGui::GetIO();
    const vec3 fwd(-std::sin(m.yaw) * std::cos(m.pitch), std::sin(m.pitch), -std::cos(m.yaw) * std::cos(m.pitch));
    const vec3 right(std::cos(m.yaw), 0, -std::sin(m.yaw)), flat(-std::sin(m.yaw), 0, -std::cos(m.yaw));
    vec3 move(0);
    float speed = 6.f;
    // the mouse, off the panel: left drag turns, right drag slides, the wheel moves in and out
    if (!overPanel && !flying) {
      for (int k = 0; k < 2; k++) if (ImGui::IsMouseClicked(k)) drag = k;
      if (io.MouseWheel != 0) m.pos += glm::dvec3(fwd * (io.MouseWheel * 1.5f));
    }
    if (drag >= 0 && !ImGui::IsMouseDown(drag)) drag = -1;
    if (drag == 0) { m.yaw -= io.MouseDelta.x * 0.006f; m.pitch -= io.MouseDelta.y * 0.006f; }
    if (drag == 1) m.pos += glm::dvec3(right * (-io.MouseDelta.x * 0.03f) + vec3(0, io.MouseDelta.y * 0.03f, 0));
    if (!in.usingPad()) {
      if (in.down(Act::Forward)) move += flat;
      if (in.down(Act::Back)) move -= flat;
      if (in.down(Act::Right)) move += right;
      if (in.down(Act::Left)) move -= right;
      if (in.down(Act::Jump)) move.y += 1;
      if (in.down(Act::Sneak)) move.y -= 1;
      if (in.down(Act::Sprint)) speed *= 3;
    } else {
      if (in.padButtonPressed(2)) { flying = !flying; focusPending = true; }
      if (flying) {
        for (auto& pd : in.pads()) {
          if (!pd.active) continue;
          move += right * pd.lstick.x - flat * pd.lstick.y;
          move.y += pd.rt - pd.lt;
          const float k = a.settings.padSens * 2.4f * dt;
          m.yaw -= pd.rstick.x * std::fabs(pd.rstick.x) * k;
          m.pitch -= pd.rstick.y * std::fabs(pd.rstick.y) * k * (a.settings.invertY ? -1.f : 1.f);
        }
        if (in.padButtonDown(4)) m.tilt = std::max(-45.f, m.tilt - 40 * dt);
        if (in.padButtonDown(5)) m.tilt = std::min(45.f, m.tilt + 40 * dt);
        if (in.padButtonPressed(0)) shoot = true;
        if (in.padButtonDown(10)) speed *= 3;   // the left stick pressed in
      }
    }
    if (glm::length(move) > 1) move = glm::normalize(move);
    m.pos += glm::dvec3(move * speed * dt);
    m.pitch = clampv(m.pitch, -1.55f, 1.55f);
    // no further than REACH from you
    const glm::dvec3 eye(p.pos.x, p.pos.y + p.eye, p.pos.z), off = m.pos - eye;
    const double d = glm::length(off);
    if (d > REACH) m.pos = eye + off * (REACH / d);
  }
};
}

std::unique_ptr<Screen> makePhotoModeScreen() { return std::make_unique<PhotoModeScreen>(); }

} // namespace dq
