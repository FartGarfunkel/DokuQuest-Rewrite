// ui/multiplayer.cpp — net/netui.js: the Multiplayer screen, the Joining… screen (the original's loading bar)
// and Open to LAN for the world being played. The network itself is client/netplay + net/.
// Select Server is laid out like BTA 8.0's server browser (ui/paged.h). There is no Internet page and no server ping.
#include "ui/ui.h"
#include "ui/theme.h"
#include "ui/paged.h"
#include "client/app.h"
#include "client/session.h"
#include "client/netplay.h"
#include "net/client.h"
#include "net/lan.h"
#include "common/fs.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace dq {
using namespace ui;
std::string agoText(i64 ms);   // worlds.cpp

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

// ---- Saved servers: userDir()/servers.json (favorites, plus a history of joins) ------------------------------
// An older file is just {servers:[{name,address}], last}. showIp and history are optional.
struct SavedServer { std::string name, address; bool showIp = true; i64 played = 0; };
static constexpr int kHistoryCap = 50;
static fs::path serversFile() { return userDir() / "servers.json"; }
static std::string trimmed(std::string s) {
  while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
  size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
  return s.substr(i);
}
static bool jbool(const json& s, const char* k, bool d) {
  if (!s.contains(k)) return d;
  const json& v = s[k];
  if (v.is_boolean()) return v.get<bool>();
  if (v.is_number()) return v.get<double>() != 0;
  return d;
}
static i64 jint(const json& s, const char* k) {
  if (!s.contains(k) || !s[k].is_number()) return 0;
  try { return s[k].get<i64>(); } catch (...) { return 0; }
}
static SavedServer readServer(const json& s) {
  SavedServer r;
  r.address = s.contains("address") && s["address"].is_string() ? s["address"].get<std::string>() : std::string();
  r.name = s.contains("name") && s["name"].is_string() ? s["name"].get<std::string>() : std::string();   // a blank name is shown as the address
  r.showIp = jbool(s, "showIp", true);
  r.played = jint(s, "played");
  return r;
}
struct ServerLists { std::vector<SavedServer> favorites, history; std::string last; };
static ServerLists loadServerLists() {
  ServerLists o;
  auto j = readJson(serversFile());
  if (!j || !j->is_object()) return o;
  o.last = j->value("last", std::string());
  auto take = [](const json& arr, std::vector<SavedServer>& out) {
    if (!arr.is_array()) return;
    for (auto& s : arr) if (s.is_object()) { SavedServer r = readServer(s); if (!r.address.empty()) out.push_back(std::move(r)); }
  };
  if (auto it = j->find("servers"); it != j->end()) take(*it, o.favorites);
  if (auto it = j->find("history"); it != j->end()) take(*it, o.history);
  std::sort(o.history.begin(), o.history.end(), [](const SavedServer& a, const SavedServer& b) { return a.played > b.played; });
  if ((int)o.history.size() > kHistoryCap) o.history.resize(kHistoryCap);
  return o;
}
static void saveServerLists(const ServerLists& o) {
  json fav = json::array(), hist = json::array();
  for (auto& s : o.favorites) fav.push_back({{"name", s.name}, {"address", s.address}, {"showIp", s.showIp}});
  for (auto& s : o.history) hist.push_back({{"name", s.name}, {"address", s.address}, {"showIp", s.showIp}, {"played", s.played}});
  writeJson(serversFile(), {{"servers", fav}, {"history", hist}, {"last", o.last}});
}

// ---- Select Server (BTA 8.0's ScreenSelectServer) --------------------------------------------------------------
// Pages Favorites, History, Local Network and Direct Connect; a quarter of the width kept on the right for Join
// Server / Edit / Delete (and the password and the network help above them); Cancel / Refresh / Add Server at the
// bottom; Add Server, Edit Server and Delete Server are screens of their own like BTA's. No Internet page, no ping.
class MultiplayerScreen : public Screen {
public:
  const char* id() const override { return "lanjoin"; }
  enum View { LIST, EDIT, DELETE };
  std::string addr, name, password, msg, last, formAddr;
  std::vector<SavedServer> favorites, history;
  bool formShowIp = true;
  int editing = -1, selected = -1;   // editing: the favourite being edited, -1 adding
  View view = LIST;
  std::string lanPick;
  bool lanWas = false;
  Paged kit, form;
  // Windows: "Let DokuQuest through Windows Firewall" runs on a worker (Windows' own Yes/No question waits on it)
  struct FwJob { std::mutex mx; bool busy = false, done = false, ok = false; std::string err; };
  std::shared_ptr<FwJob> fw = std::make_shared<FwJob>();
  bool onBack(App&) override {
    if (backConsumedByText()) return true;
    if (view != LIST) { view = LIST; return true; }
    return false;
  }
  void onOpen(App&) override {
    ServerLists o = loadServerLists();
    favorites = std::move(o.favorites); history = std::move(o.history); last = std::move(o.last);
    addr = last;
    kit.page = favorites.empty() && !history.empty() ? 1 : 0;
  }
  void onClose(App&) override { lanScanStop(); }
  void persist() { saveServerLists(ServerLists{favorites, history, last}); }
  void touchHistory(const std::string& address, const std::string& label) {
    bool show = true; std::string nm = trimmed(label); bool fromFav = false;
    for (auto& f : favorites) if (f.address == address) { show = f.showIp; fromFav = true; if (nm.empty()) nm = f.name; break; }
    if (!fromFav) for (auto& h : history) if (h.address == address) { show = h.showIp; if (nm.empty()) nm = h.name; break; }
    history.erase(std::remove_if(history.begin(), history.end(), [&](const SavedServer& h) { return h.address == address; }), history.end());
    history.insert(history.begin(), SavedServer{nm, address, show, unixMillis()});
    if ((int)history.size() > kHistoryCap) history.resize(kHistoryCap);
    last = address;
    persist();
  }
  void join(App& a, const std::string& addressIn, const std::string& label) {
    std::string address = trimmed(addressIn);
    if (address.empty()) { msg = "Type the address the host's screen shows, like 192.168.1.23:25580"; return; }
    touchHistory(address, label);   // one history row per address, newest first
    a.pushScreen(makeJoiningScreen(address, label, password));
  }
  std::vector<SavedServer>* rows() { return kit.page == 0 ? &favorites : kit.page == 1 ? &history : nullptr; }
  void joinSelected(App& a, const std::vector<net::LanGame>& found) {
    if (auto* r = rows()) {
      if (selected < 0 || selected >= (int)r->size()) { msg = "Select a server first."; return; }
      join(a, (*r)[selected].address, (*r)[selected].name);
    } else if (kit.page == 2) {
      for (auto& g : found) {
        const std::string key = g.address + ":" + std::to_string(g.port);
        if (key == lanPick) { join(a, key, g.world); return; }
      }
      msg = "Select a world on your network first.";
    } else {
      std::string ad = trimmed(addr), lbl;
      for (auto& s : favorites) if (s.address == ad) { lbl = s.name; break; }
      join(a, addr, lbl);
    }
  }
  void beginAdd() { editing = -1; name.clear(); formAddr.clear(); formShowIp = true; view = EDIT; }
  void beginEdit() {
    auto* r = rows();
    if (!r || selected < 0 || selected >= (int)r->size()) return;
    const SavedServer& s = (*r)[selected];
    editing = kit.page == 0 ? selected : -1;   // editing a history row saves it as a favourite
    name = s.name; formAddr = s.address; formShowIp = s.showIp; view = EDIT;
  }
  void saveForm() {
    std::string ad = trimmed(formAddr), nm = trimmed(name);
    if (ad.empty()) return;
    SavedServer row{nm, ad, formShowIp, 0};
    if (editing >= 0 && editing < (int)favorites.size()) { favorites[editing] = row; selected = editing; msg = "Saved the changes to " + (nm.empty() ? ad : nm) + "."; }
    else { favorites.push_back(row); kit.page = 0; selected = (int)favorites.size() - 1; msg = "Saved " + (nm.empty() ? ad : nm) + "."; }
    persist(); view = LIST;
  }

  void draw(App& a) override {
    if (a.topScreen() != this) return;
    if (view == EDIT) { drawEdit(a); return; }
    if (view == DELETE) { drawDelete(a); return; }
    static const std::vector<PagedPage> PAGES = {{"Favorites", "item/name_tag"}, {"History", "item/writable_book"}, {"Local Network", "item/oak_door"}, {"Direct Connect", "item/arrow"}};
    const int pageBefore = kit.page;
    const bool direct = kit.page == 3;
    kit.begin(a, "##servers", "Select Server", PAGES, direct ? 0.f : 0.25f);
    if (kit.page != pageBefore) { selected = -1; lanPick.clear(); }
    const float U = kit.u();
    if (kit.page != 2) { if (lanWas) lanScanStop(); lanWas = false; } else lanWas = true;
    std::vector<net::LanGame> found;
    if (kit.page == 2) found = lanScan();

    kit.beginContent();
    auto entry = [&](const std::string& id, const std::string& title, const std::string& sub, const std::string& right, const char* icon, bool on) {
      bool dbl = false;
      bool hit = kit.listRow(id.c_str(), 24, on, &dbl);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p = kit.rowMin(), q = kit.rowMax();
      pageIcon(dl, icon, p + ImVec2(2, 2) * U, 20 * U);
      float tw = q.x - p.x - 30 * U - (right.empty() ? 0 : textSize(F_BODY, right.c_str()).x + 6 * U);
      kit.textAt(ellipsize(F_MID, title, tw), p + ImVec2(27, 2) * U, IM_COL32(255, 255, 255, 255));
      if (!sub.empty()) kit.textAt(ellipsize(F_BODY, sub, tw), p + ImVec2(27, 13) * U, IM_COL32(128, 128, 128, 255), F_BODY);
      if (!right.empty()) { ImVec2 rs = textSize(F_BODY, right.c_str()); kit.textAt(right, ImVec2(q.x - rs.x - 3 * U, p.y + 2 * U), IM_COL32(255, 255, 85, 255), F_BODY); }
      return std::make_pair(hit, dbl);
    };
    if (auto* r = rows()) {
      if (selected >= (int)r->size()) selected = (int)r->size() - 1;
      if (r->empty()) kit.note(kit.page == 0 ? "No saved servers yet. Add Server keeps one here." : "Servers you join show up here.");
      for (int i = 0; i < (int)r->size(); i++) {
        const SavedServer& s = (*r)[i];
        const std::string title = s.name.empty() ? s.address : s.name;
        std::string sub = s.showIp && s.address != title ? s.address : std::string();
        if (kit.page == 1 && s.played) sub += (sub.empty() ? "" : "  \xC2\xB7  ") + std::string("joined ") + agoText(s.played);
        auto [hit, dbl] = entry((kit.page == 0 ? "f" : "h") + std::to_string(i), title, sub, "", kit.page == 0 ? "item/name_tag" : "item/writable_book", selected == i);
        if (hit) { selected = i; if (dbl) join(a, s.address, s.name); }
      }
    } else if (kit.page == 2) {
      if (found.empty()) kit.note("Looking for worlds on your network\xE2\x80\xA6 (on the host: pause \xE2\x86\x92 Open to LAN)");
      bool still = false;
      for (auto& g : found) {
        const std::string key = g.address + ":" + std::to_string(g.port);
        std::string sub = "hosted by " + g.host + "  \xC2\xB7  " + g.address + (g.self ? " (this computer)" : "") + (g.password ? "  \xC2\xB7  password" : "");
        auto [hit, dbl] = entry(key, g.world.empty() ? key : g.world, sub, std::to_string(g.players) + " playing", "item/oak_door", key == lanPick);
        if (hit) { lanPick = key; if (dbl) join(a, key, g.world); }
        still |= key == lanPick;
      }
      if (!still) lanPick.clear();
    } else {
      // Direct Connect (BTA's SeverDirectConnectComponent): the address and Connect
      kit.category("Direct Connect");
      kit.textField("IP Address", &addr, "192.168.1.23:25580", "The address the host's screen shows. Over the internet: the host's internet address, like 203.0.113.7:25580.", 180, 0, 80);
      kit.textField("Password", &password, nullptr, "Only if the host set one.", 180, ImGuiInputTextFlags_Password, 64);
      if (kit.action("Connect", "Connect", nullptr, trimmed(addr).empty())) joinSelected(a, found);
      if (!msg.empty()) kit.note(msg, IM_COL32(255, 255, 255, 255));
      kit.note("Worlds opened to LAN on your home network show up under Local Network. If that list stays empty, connect here: the firewall may be hiding the list (sudo firewall-cmd --add-port=25581/udp lets it through). Split screen works too: plug in a controller and press Start.");
    }
    kit.endContent();

    // the right column
    if (!direct) {
      const float x = kit.W - kit.rightW + 10 * U, w = kit.rightW - 12 * U;
      float y = kit.top + 6 * U;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      kit.textAt("Password", ImVec2(x, y)); y += 12 * U;
      kit.fieldAt("##pw", &password, ImVec2(x, y), w / U, nullptr, ImGuiInputTextFlags_Password, 64); y += 24 * U;
      auto para = [&](const std::string& t, ImU32 c) {
        float lim = kit.bottom - 70 * U;
        if (y >= lim) return;
        ImVec2 ts = textSize(F_BODY, t.c_str(), w);
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + w, lim), true);
        ui::text(dl, F_BODY, ImVec2(x, y), c, t.c_str(), w);
        dl->PopClipRect();
        y += ts.y + 4 * U;
      };
      if (!msg.empty()) para(msg, IM_COL32(255, 255, 255, 255));
      para("Only if the host set one.", IM_COL32(128, 128, 128, 255));
#ifdef _WIN32
      {
        bool busy, done, ok; std::string err;
        { std::lock_guard<std::mutex> lk(fw->mx); busy = fw->busy; done = fw->done; ok = fw->ok; err = fw->err; }
        if (busy) para("Waiting for Windows\xE2\x80\xA6 (answer Yes to its question)", IM_COL32(160, 160, 160, 255));
        else if (y + 24 * U < kit.bottom - 70 * U && kit.buttonAt("Allow Through Firewall", ImVec2(x, y), w / U, 20)) {
          { std::lock_guard<std::mutex> lk(fw->mx); fw->busy = true; fw->done = false; }
          std::thread([j = fw] { std::string e; bool r = net::firewallAllowSelf(&e); std::lock_guard<std::mutex> lk(j->mx); j->busy = false; j->done = true; j->ok = r; j->err = e; }).detach();
        }
        if (!busy) { if (ImGui::IsItemHovered()) kit.tooltip("Can't reach a server others can? Windows may be blocking the game (if its question about networks was closed or answered Cancel), or an antivirus may be."); y += 24 * U; }
        if (done) para(ok ? "Windows Firewall lets DokuQuest through now. Try joining again." : err, ok ? IM_COL32(85, 255, 85, 255) : IM_COL32(255, 85, 85, 255));
      }
#endif
      const bool listPage = rows() != nullptr;
      const bool canJoin = listPage ? selected >= 0 : !lanPick.empty();
      if (kit.buttonAt("Join Server", ImVec2(x, kit.bottom - 66 * U), w / U, 20, !canJoin)) joinSelected(a, found);
      if (kit.buttonAt("Edit", ImVec2(x, kit.bottom - 44 * U), w / U, 20, !listPage || selected < 0)) beginEdit();
      if (kit.buttonAt("Delete", ImVec2(x, kit.bottom - 22 * U), w / U, 20, !listPage || selected < 0)) view = DELETE;
    }
    // the bottom row
    const float cx = kit.W * 0.5f, yb = kit.H - 24 * U;
    bool cancel;
    if (direct) cancel = kit.buttonAt("Cancel", ImVec2(std::round((kit.W - 100 * U) * 0.5f), yb), 100, 20);
    else {
      cancel = kit.buttonAt("Cancel", ImVec2(cx - 154 * U, yb), 100, 20);
      if (kit.buttonAt("Refresh", ImVec2(cx - 50 * U, yb), 100, 20)) {
        ServerLists o = loadServerLists(); favorites = std::move(o.favorites); history = std::move(o.history);
        lanScanStop(); lanPick.clear(); msg.clear();
      }
      if (kit.buttonAt("Add Server", ImVec2(cx + 54 * U, yb), 100, 20)) beginAdd();
    }
    kit.end();
    if (cancel) a.popScreen();
  }

  // BTA's Add Server / Edit Server
  void drawEdit(App& a) {
    form.beginPlain(a, "##addserver", editing >= 0 ? "Edit Server" : "Add Server");
    const float U = form.u(), cx = a.width * 0.5f;
    form.textAt("Server Name", ImVec2(cx - 100 * U, 47 * U));
    form.fieldAt("##sname", &name, ImVec2(cx - 100 * U, 60 * U), 200, "Leave blank to show the address", 0, 60);
    form.textAt("IP Address", ImVec2(cx - 100 * U, 88 * U));
    bool enter = form.fieldAt("##saddr", &formAddr, ImVec2(cx - 100 * U, 100 * U), 200, "192.168.1.23:25580", ImGuiInputTextFlags_EnterReturnsTrue, 80);
    if (form.buttonAt(formShowIp ? "Show IP: ON###showip" : "Show IP: OFF###showip", ImVec2(cx - 100 * U, 124 * U), 200, 20)) formShowIp = !formShowIp;
    float yb = std::max(a.height / 4.f + 108 * U, 152 * U);
    bool save = form.buttonAt("Save", ImVec2(cx - 100 * U, yb), 200, 20, trimmed(formAddr).empty()) || (enter && !trimmed(formAddr).empty());
    bool cancel = form.buttonAt("Cancel##edit", ImVec2(cx - 100 * U, yb + 24 * U), 200, 20);
    form.end();
    if (save) saveForm();
    else if (cancel) view = LIST;
  }

  // BTA's Delete Server
  void drawDelete(App& a) {
    auto* r = rows();
    if (!r || selected < 0 || selected >= (int)r->size()) { view = LIST; return; }
    const SavedServer& s = (*r)[selected];
    form.beginPlain(a, "##deleteserver", "Delete Server");
    const float U = form.u(), cx = a.width * 0.5f;
    form.centerText("Are you sure you want to delete this server?", a.height / 2.f - 30 * U);
    form.centerText("'" + (s.name.empty() ? s.address : s.name) + "' will be lost forever! (A long time!)", a.height / 2.f - 16 * U, IM_COL32(160, 160, 160, 255));
    bool del = form.buttonAt("Delete##yes", ImVec2(cx - 154 * U, a.height / 2.f + 16 * U), 150, 20);
    bool cancel = form.buttonAt("Cancel##no", ImVec2(cx + 4 * U, a.height / 2.f + 16 * U), 150, 20);
    form.end();
    if (del) {
      msg = "Removed " + (s.name.empty() ? s.address : s.name) + ".";
      r->erase(r->begin() + selected);
      if (selected >= (int)r->size()) selected = (int)r->size() - 1;
      persist(); view = LIST;
    } else if (cancel) view = LIST;
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
