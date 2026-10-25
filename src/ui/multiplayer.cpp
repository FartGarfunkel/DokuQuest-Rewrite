// ui/multiplayer.cpp — net/netui.js: the Multiplayer screen (worlds announced on the home network + an address
// to type), the Joining… screen (the original's loading bar) and Open to LAN for the world being played.
// The network itself is client/netplay + net/.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "net/client.h"
#include <imgui.h>

namespace dq {
using namespace ui;

static const char* FIREWALL_CMD = "sudo firewall-cmd --permanent --add-port=25580-25589/tcp --add-port=25581/udp && sudo firewall-cmd --reload";

// ---- Joining… -------------------------------------------------------------------------------------------
class JoiningScreen : public Screen {
public:
  JoiningScreen(std::string addr, std::string label, std::string pw) : address(std::move(addr)), label(std::move(label)), password(std::move(pw)) {}
  std::string address, label, password, error;
  std::shared_ptr<net::NetClient> nc;
  const char* id() const override { return "loading"; }
  void onOpen(App& a) override { nc = netJoinBegin(a, address, password); }
  bool onBack(App& a) override { nc.reset(); return false; }   // B / Esc gives up
  void update(App& a, double) override {
    if (!nc) return;
    auto st = nc->poll();
    if (st == net::NetClient::State::Ready) {
      auto c = nc; std::string addr = address;
      nc.reset();
      uiDefer([c, addr](App& app) { app.leaveWorld(false); if (!netJoinFinish(app, c, addr)) app.setScreen(makeTitleScreen()); });
    } else if (st == net::NetClient::State::Failed || st == net::NetClient::State::Closed) {
      std::string e = nc->error().empty() ? "Could not join" : nc->error();
      nc.reset();
      std::string lower = toLower(e);
      if (lower.find("connect") != std::string::npos || lower.find("answer") != std::string::npos)
        e += " \xE2\x80\x94 check the address, that you're on the same network, and the host's firewall (see below).";
      a.toast(e, "warn");
      a.popScreen();
    }
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z(), W = (float)a.width, H = (float)a.height;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 6, 4, 179));
    std::string t = nc && nc->state() == net::NetClient::State::Joining ? "Joining " + (label.empty() ? address : label) + "\xE2\x80\xA6" : "Connecting to " + (label.empty() ? address : label) + "\xE2\x80\xA6";
    float bw = std::min(420 * z, W * 0.86f), k = 26.f / fontPx(F_H3);
    ImVec2 ts = textSize(F_H3, t.c_str(), bw, k);
    float y = H * 0.5f - (ts.y + 12 * z + 14 * z) * 0.5f;
    text(dl, F_H3, ImVec2(W * 0.5f - ts.x * 0.5f, y), col::text, t.c_str(), bw, k);
    y += ts.y + 12 * z;
    ImVec2 b0(W * 0.5f - bw * 0.5f, y), b1 = b0 + ImVec2(bw, 14 * z);
    dl->AddRectFilled(b0, b1, col::rgb(0x120e0b), 3 * z);
    float f = nc ? clampv(nc->progress(), 0.f, 1.f) : 1.f;
    if (f > 0) dl->AddRectFilledMultiColor(b0 + ImVec2(2, 2) * z, ImVec2(b0.x + 2 * z + (bw - 4 * z) * f, b1.y - 2 * z), col::emberDeep, col::ember, col::ember, col::emberDeep);
    dl->AddRect(b0, b1, col::line, 3 * z, 0, 2 * z);
  }
};
std::unique_ptr<Screen> makeJoiningScreen(const std::string& address, const std::string& label, const std::string& password) {
  return std::make_unique<JoiningScreen>(address, label, password);
}

// ---- Multiplayer ------------------------------------------------------------------------------------------
class MultiplayerScreen : public Screen {
public:
  const char* id() const override { return "lanjoin"; }
  std::string addr, password, msg;
  bool onBack(App&) override { return backConsumedByText(); }
  void onClose(App&) override { lanScanStop(); }
  void join(App& a, const std::string& address, const std::string& label) {
    if (address.empty()) { msg = "Type the address the host's screen shows, like 192.168.1.23:25580"; return; }
    a.pushScreen(makeJoiningScreen(address, label, password));
  }
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    float z = Z();
    Panel pn;
    pn.begin("##lanjoin", "Multiplayer", 860, "Back");
    pn.beginBody();
    float w = pn.width - 36 * z - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetCursorPos(ImVec2(18 * z, 14 * z));
    ImGui::BeginGroup();
    label(F_BODY, col::white, "Worlds on your network");
    auto found = lanScan();
    if (found.empty()) label(F_BODY, withAlpha(col::text, 0.75f), "Looking for worlds on your network\xE2\x80\xA6 (on the host: pause \xE2\x86\x92 Open to LAN)", w);
    for (auto& g : found) {
      ImGui::PushID((g.address + ":" + std::to_string(g.port)).c_str());
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p0 = ImGui::GetCursorScreenPos(); float h = 54 * z;
      dl->AddRectFilled(p0, p0 + ImVec2(w, h), IM_COL32(255, 255, 255, 15), 4 * z); dl->AddRect(p0, p0 + ImVec2(w, h), IM_COL32(255, 255, 255, 31), 4 * z);
      text(dl, F_BODY, p0 + ImVec2(10, 8) * z, col::white, g.world.c_str());
      std::string sub = "hosted by " + g.host + " \xC2\xB7 " + std::to_string(g.players) + " playing \xC2\xB7 " + g.address + (g.self ? " (this computer)" : "") + (g.password ? " \xC2\xB7 password" : "");
      text(dl, F_SMALL, p0 + ImVec2(10, 30) * z, withAlpha(col::text, 0.8f), sub.c_str(), 0, 12 / 12.5f);
      float bw = textSize(F_BODY, "Join").x + 40 * z;
      ImGui::SetCursorScreenPos(ImVec2(p0.x + w - bw - 8 * z, p0.y + (h - 44 * z) * 0.5f));
      if (button("Join", BTN_PRIMARY, false, bw)) join(a, g.address + ":" + std::to_string(g.port), g.world);
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6 * z));
      ImGui::Dummy(ImVec2(w, 0));
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 8 * z));
    label(F_MID, col::muted, "Or type the address the host's screen shows");
    float jw = textSize(F_BODY, "Join").x + 40 * z;
    bool enter = false;
    { inputText("##addr", &addr, "192.168.1.23:25580", 80, w - jw - 8 * z, ImGuiInputTextFlags_EnterReturnsTrue); enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter); }
    ImGui::SameLine(0, 8 * z);
    if (button("Join##addr", BTN_PRIMARY, false, jw) || enter) join(a, addr, "");
    label(F_MID, col::muted, "Password (only if the host set one)");
    inputText("##pw", &password, nullptr, 64, std::min(w, 260 * z), ImGuiInputTextFlags_Password);
    if (!msg.empty()) label(F_BODY, col::text, msg.c_str(), w);
    ImGui::Dummy(ImVec2(1, 4 * z));
    label(F_MID, col::muted, "Worlds opened to LAN on your home network show up here by themselves. If the list stays empty, type the address the host's screen shows \xE2\x80\x94 the firewall may be hiding the list (sudo firewall-cmd --add-port=25581/udp lets it through).", w);
    label(F_MID, col::muted, "Split screen still works too: plug in a controller and press Start.", w);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 16 * z));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
std::unique_ptr<Screen> makeMultiplayerScreen() { return std::make_unique<MultiplayerScreen>(); }

// ---- Open to LAN ------------------------------------------------------------------------------------------
class LanHostScreen : public Screen {
public:
  const char* id() const override { return "lanhost"; }
  std::string msg; bool fwOpen = false;
  void draw(App& a) override {
    if (a.topScreen() != this || !a.session) return;
    float z = Z();
    Panel pn;
    pn.begin("##lanhost", "Open to LAN", 860, "Back");
    pn.beginBody();
    float w = pn.width - 36 * z - ImGui::GetStyle().ScrollbarSize;
    ImGui::SetCursorPos(ImVec2(18 * z, 14 * z));
    ImGui::BeginGroup();
    const std::string& world = a.session->level->info.name;
    LanStatus st = lanStatus(a);
    if (!st.open) {
      label(F_BODY, col::text, ("Let others on your home network play in " + world + " with you. Your computer runs the world: creatures, time and saving. Everyone keeps their own things, and your world remembers them for next time.").c_str(), w);
      label(F_BODY, col::text, "They can join from DokuQuest on another computer (Multiplayer on the title screen).", w);
      if (button("Open to LAN##start", BTN_PRIMARY)) { std::string e; msg = openToLan(a, LanOpenOptions(), &e) ? "" : e; }
      if (!msg.empty()) label(F_MID, col::muted, msg.c_str(), w);
    } else {
      label(F_BODY, col::text, (world + " is open to your home network.").c_str(), w);
      label(F_BODY, col::text, "On another computer with DokuQuest: Multiplayer on the title screen \xE2\x80\x94 your world shows up there. Or type this address:", w);
      label(F_BIG, col::ember, ((st.address.empty() ? std::string("this computer") : st.address) + ":" + std::to_string(st.port)).c_str());
      ImGui::Dummy(ImVec2(1, 4 * z));
      label(F_MID, col::muted, "PLAYING WITH YOU");
      if (st.players.empty()) label(F_MID, col::rgb(0xd8d8d8), "Nobody has joined yet.");
      for (auto& n : st.players) {
        ImGui::PushID(n.c_str());
        ImGui::AlignTextToFramePadding(); label(F_BODY, col::text, n.c_str());
        ImGui::SameLine(w - 90 * z + 18 * z);
        if (button("Remove", BTN_NORMAL, true)) lanKick(a, n);
        ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(1, 6 * z));
      ImGui::PushStyleColor(ImGuiCol_Text, col::gold); ImGui::PushStyleColor(ImGuiCol_Header, 0);
      ImGui::SetNextItemOpen(fwOpen); fwOpen = ImGui::CollapsingHeader("They can't connect?");
      ImGui::PopStyleColor(2);
      if (fwOpen) {
        label(F_MID, col::text, "Be on the same Wi-Fi or network. openSUSE's firewall blocks people joining until you allow it once. Open a terminal and run:", w);
        label(F_MONO, col::text, FIREWALL_CMD, w);
        if (button("Copy", BTN_NORMAL, true)) { SDL_SetClipboardText(FIREWALL_CMD); msg = "Copied"; }
        label(F_MID, col::muted, "It asks for your password. Then try joining again. Other Linux systems: allow TCP ports 25580\xE2\x80\x93" "25589 and UDP 25581.", w);
      }
      ImGui::Dummy(ImVec2(1, 6 * z));
      if (button("Stop sharing this world")) { closeLan(a); a.toast("Your world is just yours again", ""); }
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(1, 16 * z));
    pn.endBody();
    pn.end();
    if (pn.closed) a.popScreen();
  }
};
std::unique_ptr<Screen> makeLanHostScreen() { return std::make_unique<LanHostScreen>(); }

} // namespace dq
