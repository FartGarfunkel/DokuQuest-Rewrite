// ui/multiplayer.cpp — net/netui.js: the Multiplayer screen (worlds announced on the home network + an address
// to type), the Joining… screen (the original's loading bar) and Open to LAN for the world being played.
// The network itself is client/netplay + net/.
#include "ui/ui.h"
#include "ui/theme.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "net/client.h"
#include "net/lan.h"
#include "common/fs.h"
#include <imgui.h>
#include <cctype>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace dq {
using namespace ui;

// the game talks over UDP (ENet): the game's ports 25580-25589 and the home-network announcements on 25581. (It used to say
// TCP here, which let nobody in: the firewall still dropped the game's UDP.)
static const char* FIREWALL_CMD = "sudo firewall-cmd --permanent --add-port=25580-25589/udp && sudo firewall-cmd --reload";

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
      if (std::string v = net::vpnOn(); !v.empty() && (lower.find("connect") != std::string::npos || lower.find("answer") != std::string::npos))
        e += " A VPN is on here (" + v + "): try again with it off, or turn it off for DokuQuest (split tunnelling).";
#ifdef _WIN32
      if (lower.find("answer") != std::string::npos) e += " On Windows: Multiplayer \xE2\x86\x92 Let DokuQuest through Windows Firewall.";
#endif
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

// ---- Saved servers (like Minecraft's server list): userDir()/servers.json ------------------------------------
struct SavedServer { std::string name, address; };
static fs::path serversFile() { return userDir() / "servers.json"; }
static std::vector<SavedServer> loadServers(std::string* last) {
  std::vector<SavedServer> v;
  auto j = readJson(serversFile());
  if (!j || !j->is_object()) return v;
  if (last) *last = j->value("last", std::string());
  if (auto it = j->find("servers"); it != j->end() && it->is_array())
    for (auto& s : *it)
      if (s.is_object() && !s.value("address", std::string()).empty()) v.push_back({s.value("name", std::string()), s.value("address", std::string())});
  return v;
}
static void saveServers(const std::vector<SavedServer>& v, const std::string& last) {
  json a = json::array();
  for (auto& s : v) a.push_back({{"name", s.name}, {"address", s.address}});
  writeJson(serversFile(), {{"servers", a}, {"last", last}});
}
static std::string trimmed(std::string s) {
  while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
  size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
  return s.substr(i);
}

// ---- Multiplayer ------------------------------------------------------------------------------------------
class MultiplayerScreen : public Screen {
public:
  const char* id() const override { return "lanjoin"; }
  std::string addr, name, password, msg, last;
  std::vector<SavedServer> servers;
  int editing = -1, removing = -1;
  // Windows: "Let DokuQuest through Windows Firewall" runs on a worker (Windows' own Yes/No question waits on it)
  struct FwJob { std::mutex mx; bool busy = false, done = false, ok = false; std::string err; };
  std::shared_ptr<FwJob> fw = std::make_shared<FwJob>();
  bool onBack(App&) override { return backConsumedByText(); }
  void onOpen(App&) override { servers = loadServers(&last); addr = last; }
  void onClose(App&) override { lanScanStop(); }
  void join(App& a, const std::string& addressIn, const std::string& label) {
    std::string address = trimmed(addressIn);
    if (address.empty()) { msg = "Type the address the host's screen shows, like 192.168.1.23:25580"; return; }
    last = address; saveServers(servers, last);   // the next visit starts with it typed in
    a.pushScreen(makeJoiningScreen(address, label, password));
  }
  void saveTyped() {
    std::string ad = trimmed(addr), nm = trimmed(name);
    if (ad.empty()) { msg = "Type the server's address first, then a name for it."; return; }
    if (nm.empty()) nm = ad;
    if (editing >= 0 && editing < (int)servers.size()) servers[editing] = {nm, ad};
    else servers.push_back({nm, ad});
    msg = (editing >= 0 ? "Saved the changes to " : "Saved ") + nm + ".";
    editing = -1; name.clear();
    saveServers(servers, last);
  }
  void drawSaved(App& a, float w, float z) {
    if (servers.empty()) return;
    label(F_BODY, col::white, "Your servers");
    for (int i = 0; i < (int)servers.size(); i++) {
      const SavedServer& s = servers[i];
      ImGui::PushID(1000 + i);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p0 = ImGui::GetCursorScreenPos(); float h = 54 * z;
      dl->AddRectFilled(p0, p0 + ImVec2(w, h), IM_COL32(255, 255, 255, editing == i ? 30 : 15), 4 * z); dl->AddRect(p0, p0 + ImVec2(w, h), IM_COL32(255, 255, 255, 31), 4 * z);
      const char* rm = removing == i ? "Remove it?" : "Remove";
      float jw = textSize(F_BODY, "Join").x + 40 * z, ew = textSize(F_BODY, "Edit").x + 30 * z, rw = textSize(F_BODY, rm).x + 30 * z;
      float tw = w - jw - ew - rw - 40 * z;
      dl->PushClipRect(p0, p0 + ImVec2(10 * z + tw, h), true);   // a long name stops short of the buttons
      text(dl, F_BODY, p0 + ImVec2(10, 8) * z, col::white, s.name.c_str());
      text(dl, F_SMALL, p0 + ImVec2(10, 30) * z, withAlpha(col::text, 0.8f), s.address.c_str(), 0, 12 / 12.5f);
      dl->PopClipRect();
      ImGui::SetCursorScreenPos(ImVec2(p0.x + w - jw - ew - rw - 24 * z, p0.y + (h - 44 * z) * 0.5f));
      if (button("Join", BTN_PRIMARY, false, jw)) { removing = -1; join(a, s.address, s.name); }
      ImGui::SameLine(0, 8 * z);
      if (button("Edit", BTN_NORMAL, false, ew)) { editing = i; removing = -1; addr = s.address; name = s.name; msg = "Change the name or address below, then Save changes."; }
      ImGui::SameLine(0, 8 * z);
      if (button(rm, BTN_DANGER, false, rw)) {
        if (removing == i) {
          msg = "Removed " + s.name + ".";
          servers.erase(servers.begin() + i); removing = -1;
          if (editing == i) { editing = -1; name.clear(); } else if (editing > i) editing--;
          saveServers(servers, last);
          ImGui::PopID();
          break;
        }
        removing = i;
      }
      ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + h + 6 * z));
      ImGui::Dummy(ImVec2(w, 0));
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1, 8 * z));
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
    drawSaved(a, w, z);
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
    label(F_MID, col::muted, "Or type the address the host's screen shows (over the internet: the host's internet address, like 203.0.113.7:25580)", w);
    float jw = textSize(F_BODY, "Join").x + 40 * z;
    bool enter = false;
    { inputText("##addr", &addr, "192.168.1.23:25580", 80, w - jw - 8 * z, ImGuiInputTextFlags_EnterReturnsTrue); enter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter); }
    ImGui::SameLine(0, 8 * z);
    if (button("Join##addr", BTN_PRIMARY, false, jw) || enter) {
      std::string lbl; for (auto& s : servers) if (s.address == trimmed(addr)) lbl = s.name;
      join(a, addr, lbl);
    }
    // save the typed address to the list (or the changes to the one being edited)
    label(F_MID, col::muted, editing >= 0 ? "Name of this server" : "Save it to your servers: give it a name");
    const char* sv = editing >= 0 ? "Save changes" : "Save server";
    float sw = textSize(F_BODY, sv).x + 40 * z, cw = editing >= 0 ? textSize(F_BODY, "Cancel").x + 30 * z : 0;
    bool senter = false;
    { inputText("##name", &name, "Our server", 60, w - sw - (cw > 0 ? cw + 8 * z : 0) - 8 * z, ImGuiInputTextFlags_EnterReturnsTrue); senter = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter); }
    ImGui::SameLine(0, 8 * z);
    if (button(sv, BTN_NORMAL, false, sw) || senter) saveTyped();
    if (editing >= 0) { ImGui::SameLine(0, 8 * z); if (button("Cancel##edit", BTN_GHOST, false, cw)) { editing = -1; name.clear(); msg.clear(); } }
    label(F_MID, col::muted, "Password (only if the host set one)");
    inputText("##pw", &password, nullptr, 64, std::min(w, 260 * z), ImGuiInputTextFlags_Password);
    if (!msg.empty()) label(F_BODY, col::text, msg.c_str(), w);
    ImGui::Dummy(ImVec2(1, 4 * z));
    label(F_MID, col::muted, "Worlds opened to LAN on your home network show up here by themselves. If the list stays empty, type the address the host's screen shows \xE2\x80\x94 the firewall may be hiding the list (sudo firewall-cmd --add-port=25581/udp lets it through).", w);
#ifdef _WIN32
    {
      bool busy, done, ok; std::string err;
      { std::lock_guard<std::mutex> lk(fw->mx); busy = fw->busy; done = fw->done; ok = fw->ok; err = fw->err; }
      label(F_MID, col::muted, "Can't reach a server that others can? Windows may be blocking the game (if its question about networks was closed or answered Cancel), or an antivirus may be.", w);
      if (busy) label(F_MID, col::muted, "Waiting for Windows\xE2\x80\xA6 (answer Yes to its question)", w);
      else if (button("Let DokuQuest through Windows Firewall", BTN_NORMAL, true)) {
        { std::lock_guard<std::mutex> lk(fw->mx); fw->busy = true; fw->done = false; }
        std::thread([j = fw] { std::string e; bool r = net::firewallAllowSelf(&e); std::lock_guard<std::mutex> lk(j->mx); j->busy = false; j->done = true; j->ok = r; j->err = e; }).detach();
      }
      if (done) label(F_MID, ok ? col::text : col::ember, ok ? "Done: Windows Firewall lets DokuQuest through now. Try joining again." : err.c_str(), w);
    }
#endif
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
  std::string msg; bool fwOpen = false, netOpen = false;
  // this computer's address on the internet, looked up on a worker when asked (net::internetAddress)
  struct Lookup { std::mutex mx; std::string addr; bool busy = false, done = false; };
  std::shared_ptr<Lookup> look = std::make_shared<Lookup>();
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
        label(F_MID, col::text, "openSUSE's firewall blocks people joining until you allow it once (on the internet as well as at home). Open a terminal and run:", w);
        label(F_MONO, col::text, FIREWALL_CMD, w);
        if (button("Copy", BTN_NORMAL, true)) { SDL_SetClipboardText(FIREWALL_CMD); msg = "Copied"; }
        label(F_MID, col::muted, "It asks for your password. Then try joining again. Other Linux systems, and Windows: allow UDP ports 25580\xE2\x80\x93" "25589. (UDP, not TCP: the game doesn't use TCP at all.)", w);
      }
      ImGui::PushStyleColor(ImGuiCol_Text, col::gold); ImGui::PushStyleColor(ImGuiCol_Header, 0);
      ImGui::SetNextItemOpen(netOpen); netOpen = ImGui::CollapsingHeader("Playing over the internet");
      ImGui::PopStyleColor(2);
      if (netOpen) {
        const std::string port = std::to_string(st.port);
        if (std::string v = net::vpnAdvice(st.port); !v.empty()) label(F_MID, col::ember, v.c_str(), w);
        label(F_MID, col::text, ("1. On your router, forward UDP port " + port + " (UDP, not TCP) to this computer" + (st.address.empty() ? std::string() : " (" + st.address + ")") + ".").c_str(), w);
        label(F_MID, col::text, "2. Let it through this computer's firewall too (above): a port open on the router isn't enough.", w);
        label(F_MID, col::text, "3. They type your internet address, not the home one above:", w);
        std::string addr; bool busy, done;
        { std::lock_guard<std::mutex> lk(look->mx); addr = look->addr; busy = look->busy; done = look->done; }
        if (!addr.empty()) {
          const std::string full = addr + ":" + port;
          label(F_BIG, col::ember, full.c_str());
          if (button("Copy##net", BTN_NORMAL, true)) { SDL_SetClipboardText(full.c_str()); msg = "Copied"; }
        } else if (busy) label(F_MID, col::muted, "Looking it up\xE2\x80\xA6", w);
        else {
          if (button("Find my internet address", BTN_NORMAL, true)) {
            { std::lock_guard<std::mutex> lk(look->mx); look->busy = true; }
            std::thread([l = look] { std::string a = net::internetAddress(); std::lock_guard<std::mutex> lk(l->mx); l->addr = a; l->busy = false; l->done = true; }).detach();
          }
          if (done) label(F_MID, col::muted, "Couldn't find it out (no curl, or offline). Search \"what is my IP\" in a browser instead.", w);
        }
        label(F_MID, col::muted, "Still nothing? If your router's own internet (WAN) address isn't the one above, your provider shares one address among many homes (CGNAT) and nobody can reach you from outside: ask them for a public IPv4 address. And you both need the same DokuQuest version.", w);
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
