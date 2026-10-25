// launcher/server_views.cpp — the server dashboard's live World map and Isometric view (game/server.js serverDrawMap,
// serverDrawIso, serverChunkImage): every loaded chunk seen from above with spawn and everyone on it, and the blocks
// round a player as little cubes. The columns come from the server thread (server/server_view.h); colours are the
// paper maps' (render/map_colors.h) from the same tiles the game uses, so a resource pack recolours these too.
// Mouse: wheel zooms the map, drag pans it, double-click recentres. Controller: right stick pans, R3 zooms, L3 recentres.
#include "launcher/server_ui.h"
#include "render/map_colors.h"
#include "render/gl.h"
#include <imgui_internal.h>
#include <thread>
#include <atomic>

namespace dq {

namespace {
// ---- the tiles (loaded once, off the UI thread: a resource pack takes a moment) ----
struct Tiles { TileSet ts; std::atomic<int> state{0}; std::thread th; std::string stamp; };   // 0 not yet, 1 loading, 2 ready
Tiles& tiles() { static Tiles* t = new Tiles(); return *t; }   // never destroyed: a launcher closed mid-load just ends
bool tilesReady(Launcher& L) {
  Tiles& T = tiles();
  if (T.state == 0) {
    T.state = 1;
    std::string pack = L.settings.resourcePack;
    T.th = std::thread([pack] {
      Tiles& t = tiles();
      loadBuiltinTiles(t.ts);
      t.ts.packName = applyResourcePack(t.ts, pack);
      t.state = 2;
    });
  }
  if (T.state == 2 && T.th.joinable()) { T.th.join(); T.stamp = mapTileStamp(T.ts); DQ_LOG("server map: colours from %s", T.ts.packName.empty() ? "the Doku tiles" : T.ts.packName.c_str()); }
  return T.state == 2;
}

// colour of (block, climate) before shading, cached (mapRGB is a little work per call)
std::unordered_map<u32, std::array<float, 3>> g_rgb;
const std::array<float, 3>& blockRGB(BlockId id, u8 cl) {
  u32 k = ((u32)id << 8) | cl;
  auto it = g_rgb.find(k);
  if (it != g_rgb.end()) return it->second;
  std::array<float, 3> c; mapRGB(tiles().ts, id, cl, c.data());
  return g_rgb.emplace(k, c).first->second;
}
ImU32 shadeCol(const std::array<float, 3>& c, float k, int a = 255) {
  return IM_COL32(clampv((int)(c[0] * k), 0, 255), clampv((int)(c[1] * k), 0, 255), clampv((int)(c[2] * k), 0, 255), a);
}
int bandBase(const ServerView& v) { return v.realm == REALM_MAIN ? dimBase(v.dim) : dimBase(DIM_OVERWORLD); }
const char* worldName(const ServerView& v) { return v.realm == REALM_POCKET ? "Pocket dimension" : v.realm != REALM_MAIN ? "A side realm" : dimName(v.dim); }
bool sameWorld(const ServerView& v, const ViewPlayer& p) { return p.realm == v.realm && (p.realm != REALM_MAIN || p.dim == v.dim); }

void label(ImDrawList* dl, ImFont* f, ImVec2 at, const std::string& t) {   // centred, black shadow (fillText twice)
  ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, t.c_str());
  ImVec2 p(std::floor(at.x - ts.x * 0.5f), std::floor(at.y - ts.y));
  dl->AddText(f, f->FontSize, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 255), t.c_str());
  dl->AddText(f, f->FontSize, p, C_WHITE, t.c_str());
}
void footer(ImDrawList* dl, ImFont* f, ImVec2 a, ImVec2 b, const std::string& t) {
  dl->AddRectFilled(ImVec2(a.x, b.y - 20), b, IM_COL32(0, 0, 0, 128));
  dl->AddText(f, f->FontSize, ImVec2(a.x + 8, b.y - 10 - f->FontSize * 0.5f), IM_COL32(232, 220, 196, 255), t.c_str());
}
std::vector<std::string> followNames(const ServerView* v) {
  std::vector<std::string> n;
  if (v) for (auto& p : v->players) n.push_back(p.name);
  return n;
}

// ---- the map picture: the chunks under the canvas, one pixel per block, as a texture ----
struct MapTex {
  GLuint tex = 0; int w = 0, h = 0;
  u64 chunksSeq = ~0ull; int cx0 = 0, cz0 = 0, nx = 0, nz = 0; std::string stamp;
  std::shared_ptr<const std::vector<ViewChunk>> held; std::unordered_map<i64, const ViewChunk*> index;   // held: what index points into
};
MapTex g_map;
i64 ckey(int cx, int cz) { return ((i64)cx << 32) ^ (u32)cz; }
void buildMapTex(const ServerView& v, int cx0, int cz0, int nx, int nz) {
  MapTex& M = g_map;
  if (M.held != v.chunks) {
    M.held = v.chunks; M.index.clear();
    for (auto& c : *M.held) M.index[ckey(c.cx, c.cz)] = &c;
  }
  int W = nx * 16, H = nz * 16, base = bandBase(v);
  std::vector<u32> px((size_t)W * H, 0);
  for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++) {
    auto it = M.index.find(ckey(cx0 + i, cz0 + j));
    if (it == M.index.end()) continue;
    const ViewChunk& c = *it->second;
    for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
      const ViewCol& col = c.col[(lz << 4) | lx];
      if (col.y < 0) continue;
      // serverChunkImage: shade = 0.75 + min(1, y / 110) * 0.4 (y in the original's 128-tall band; ours is 256 with the sea at 64, not 48)
      float yl = (float)(col.y - base) * 0.75f, shade = 0.75f + std::min(1.f, yl / 110.f) * 0.4f;
      px[(size_t)(j * 16 + lz) * W + i * 16 + lx] = shadeCol(blockRGB(col.id, col.cl), shade);
    }
  }
  if (!M.tex || M.w != W || M.h != H) {
    if (M.tex) glDeleteTextures(1, &M.tex);
    M.tex = makeTexture2D(W, H, px.data(), true, false, false); M.w = W; M.h = H;
  } else {
    glBindTexture(GL_TEXTURE_2D, M.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  }
  M.chunksSeq = v.chunksSeq; M.cx0 = cx0; M.cz0 = cz0; M.nx = nx; M.nz = nz; M.stamp = tiles().stamp;
}
const ViewCol* mapColAt(int x, int z) {
  auto it = g_map.index.find(ckey(x >> 4, z >> 4));
  return it == g_map.index.end() ? nullptr : &it->second->col[((z & 15) << 4) | (x & 15)];
}

float stick(ImGuiKey neg, ImGuiKey pos) { return ImGui::GetKeyData(pos)->AnalogValue - ImGui::GetKeyData(neg)->AnalogValue; }
} // namespace

bool followCombo(Launcher& L, const char* id, const char* text, const std::vector<std::string>& names, float width) {
  ServerDash& D = serverDash();
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(text); ImGui::SameLine();
  ImGui::SetNextItemWidth(width);
  bool changed = false;
  std::string cur = D.follow.empty() ? "(spawn)" : D.follow;
  bool here = D.follow.empty(); for (auto& n : names) if (n == D.follow) here = true;
  if (!here) cur += " (gone)";
  if (ImGui::BeginCombo(id, cur.c_str())) {
    if (ImGui::Selectable("(spawn)", D.follow.empty())) { D.follow.clear(); changed = true; }
    for (auto& n : names) if (ImGui::Selectable(n.c_str(), n == D.follow)) { D.follow = n; changed = true; }
    ImGui::EndCombo();
  }
  if (changed) D.mapPan = ImVec2(0, 0);
  (void)L;
  return changed;
}

// ---- the World map tab (serverDrawMap) ----
void drawServerMap(Launcher& L, DedicatedServer& srv, ImVec2 size) {
  ServerDash& D = serverDash();
  auto vp = srv.view();
  const ServerView* v = vp.get();
  // tools: Zoom, Centre on
  ImVec2 top = ImGui::GetCursorScreenPos();
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Zoom"); ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  const char* zooms[] = {"1×", "2×", "3×", "4×"};
  int zi = clampv(D.mapZoom, 1, 4) - 1;
  if (ImGui::Combo("##zoom", &zi, zooms, 4)) D.mapZoom = zi + 1;
  ImGui::SameLine(0, 18);
  followCombo(L, "##follow", "Centre on", followNames(v), 200);
  bool panned = D.mapPan.x != 0 || D.mapPan.y != 0;
  ImGui::SameLine(0, 18);
  if (mcButton(L, "Back to the centre", ImVec2(0, ImGui::GetFrameHeight()), BtnKind::Grey, panned, L.fSmall)) D.mapPan = ImVec2(0, 0);
  ImGui::SameLine(0, 18);
  ImGui::PushFont(L.fSmall); ImGui::AlignTextToFramePadding(); dimText("Wheel zooms, drag to look round (right stick on a controller)"); ImGui::PopFont();
  float used = ImGui::GetCursorScreenPos().y - top.y;
  ImVec2 cs(size.x, std::max(120.f, size.y - used));
  ImGui::InvisibleButton("##mapcanvas", cs, ImGuiButtonFlags_MouseButtonLeft);
  bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, IM_COL32(13, 19, 17, 255));   // #0d1311
  dl->AddRect(a, b, IM_COL32(77, 63, 51, 255));
  float Z = (float)clampv(D.mapZoom, 1, 4);
  // input: wheel, drag, double-click; the right stick pans, R3 zooms, L3 recentres
  ImGuiIO& io = ImGui::GetIO();
  if (hovered && io.MouseWheel != 0) D.mapZoom = clampv(D.mapZoom + (io.MouseWheel > 0 ? 1 : -1), 1, 4);
  if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) { D.mapPan.x -= io.MouseDelta.x / Z; D.mapPan.y -= io.MouseDelta.y / Z; }
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) D.mapPan = ImVec2(0, 0);
  if (!io.WantTextInput) {
    float sx = stick(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight), sy = stick(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown);
    if (std::abs(sx) > 0.15f || std::abs(sy) > 0.15f) { D.mapPan.x += sx * io.DeltaTime * 400 / Z; D.mapPan.y += sy * io.DeltaTime * 400 / Z; }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3)) D.mapZoom = D.mapZoom % 4 + 1;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL3)) D.mapPan = ImVec2(0, 0);
  }
  ImFont* f = L.fSmall;
  auto centreText = [&](const char* t) { ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, t); dl->AddText(f, f->FontSize, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), C_DIM, t); };
  if (!v || !v->chunks) { centreText("Looking at the world…"); return; }
  if (!tilesReady(L)) { centreText("Getting the colours ready…"); return; }
  float W = cs.x, H = cs.y;
  double cx = v->centre.x + D.mapPan.x, cz = v->centre.z + D.mapPan.y;
  int cx0 = ifloor((cx - W / 2 / Z) / 16), cz0 = ifloor((cz - H / 2 / Z) / 16);
  int cx1 = ifloor((cx + W / 2 / Z) / 16), cz1 = ifloor((cz + H / 2 / Z) / 16);
  int nx = cx1 - cx0 + 1, nz = cz1 - cz0 + 1;
  MapTex& M = g_map;
  if (M.chunksSeq != v->chunksSeq || M.cx0 != cx0 || M.cz0 != cz0 || M.nx != nx || M.nz != nz || M.stamp != tiles().stamp) buildMapTex(*v, cx0, cz0, nx, nz);
  auto toScreen = [&](double x, double z) { return ImVec2((float)((x - cx) * Z + W / 2 + a.x), (float)((z - cz) * Z + H / 2 + a.y)); };
  dl->PushClipRect(a, b, true);
  ImVec2 ia = toScreen(cx0 * 16.0, cz0 * 16.0);
  dl->AddImage((ImTextureID)(intptr_t)M.tex, ia, ImVec2(ia.x + nx * 16 * Z, ia.y + nz * 16 * Z));
  // spawn (a yellow square), then the players: a red dot with a white line for where they look, and their name
  if (v->realm == REALM_MAIN && dimOfY(ifloor(v->spawn.y)) == v->dim) {
    ImVec2 s = toScreen(v->spawn.x, v->spawn.z);
    dl->AddRect(ImVec2(s.x - 4, s.y - 4), ImVec2(s.x + 4, s.y + 4), IM_COL32(255, 224, 122, 255));
  }
  for (auto& p : v->players) {
    if (!sameWorld(*v, p)) continue;
    ImVec2 q = toScreen(p.pos.x, p.pos.z);
    dl->AddCircleFilled(q, 5, IM_COL32(0, 0, 0, 255));
    dl->AddCircleFilled(q, 4, p.dead ? IM_COL32(136, 136, 136, 255) : IM_COL32(255, 90, 74, 255));
    dl->AddLine(q, ImVec2(q.x - std::sin(p.yaw) * 9, q.y - std::cos(p.yaw) * 9), C_WHITE);
    label(dl, f, ImVec2(q.x, q.y - 7), p.name);
  }
  // what's under the pointer
  if (hovered && !active) {
    ImVec2 m = io.MousePos;
    int bx = ifloor(cx + (m.x - a.x - W / 2) / Z), bz = ifloor(cz + (m.y - a.y - H / 2) / Z);
    if (const ViewCol* c = mapColAt(bx, bz); c && c->y >= 0)
      ImGui::SetTooltip("%s\n%d, %d, %d", reg().block(c->id).name.c_str(), bx, (int)c->y, bz);
  }
  int here = (int)v->chunks->size();
  footer(dl, f, a, b, strFormat("Centre %d, %d · %d chunks loaded (%d here) · %s%s", (int)std::round(cx), (int)std::round(cz), v->chunksTotal, here, worldName(*v),
                               v->ceiling ? " · the floor round the centre" : ""));
  dl->PopClipRect();
}

// ---- the Isometric tab (serverDrawIso): the blocks round a player, as little cubes ----
void drawServerIso(Launcher& L, DedicatedServer& srv, ImVec2 size) {
  ServerDash& D = serverDash();
  auto vp = srv.view();
  const ServerView* v = vp.get();
  ImVec2 top = ImGui::GetCursorScreenPos();
  followCombo(L, "##isofollow", "Round", followNames(v), 200);
  ImGui::SameLine(0, 18);
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Size"); ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  const int sizes[] = {16, 26, 36};
  const char* sizeNames[] = {"32 blocks", "52 blocks", "72 blocks"};
  int si = D.isoR <= 16 ? 0 : D.isoR <= 26 ? 1 : 2;
  if (ImGui::Combo("##isor", &si, sizeNames, 3)) D.isoR = sizes[si];
  float used = ImGui::GetCursorScreenPos().y - top.y;
  ImVec2 cs(size.x, std::max(120.f, size.y - used));
  ImGui::InvisibleButton("##isocanvas", cs);
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, IM_COL32(16, 22, 26, 255));   // #10161a
  dl->AddRect(a, b, IM_COL32(77, 63, 51, 255));
  ImFont* f = L.fSmall;
  auto centreText = [&](const char* t) { ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, t); dl->AddText(f, f->FontSize, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), C_DIM, t); };
  if (!v || v->iso.empty() || v->isoR <= 0) { centreText("Looking at the world…"); return; }
  if (!tilesReady(L)) { centreText("Getting the colours ready…"); return; }
  int R = v->isoR, n = 2 * R + 1;
  float W = cs.x, H = cs.y;
  float T = std::max(4.f, std::floor(std::min(W / (R * 2 * 1.05f), H / (R * 1.6f)))), hw = T / 2, hh = T / 4, vh = T / 2;
  int base = v->isoBase, band = bandBase(*v);
  float ox = a.x + W / 2, oy = a.y + H / 2 - R * hh * 0.2f;
  dl->PushClipRect(a, b, true);
  ImDrawListFlags fl = dl->Flags; dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;   // crisp cubes, and a quarter of the vertices
  int shown = 0;
  for (int s = 0; s <= 4 * R; s++) for (int i = 0; i <= s; i++) {
    int dx = i, dz = s - i;
    if (dx > 2 * R || dz > 2 * R) continue;
    const ViewCol& c = v->iso[(size_t)dz * n + dx];
    if (c.y < 0) continue;
    const auto& rgb = blockRGB(c.id, c.cl);
    // the column down to its lower neighbours, so cliffs show their sides
    int y = c.y, yl = y - band;
    float rel = (float)(y - base), px = ox + (dx - dz) * hw, py = oy + (dx + dz - 2 * R) * hh - rel * vh;
    int below = std::min(yl, std::max(0, yl - 6));
    float sideH = (yl - below + 1) * vh;
    if (py - hh > b.y || py + hh + sideH < a.y || px + hw < a.x || px - hw > b.x) continue;
    dl->AddQuadFilled(ImVec2(px - hw, py), ImVec2(px, py + hh), ImVec2(px, py + hh + sideH), ImVec2(px - hw, py + sideH), shadeCol(rgb, 0.62f));
    dl->AddQuadFilled(ImVec2(px + hw, py), ImVec2(px, py + hh), ImVec2(px, py + hh + sideH), ImVec2(px + hw, py + sideH), shadeCol(rgb, 0.8f));
    dl->AddQuadFilled(ImVec2(px, py - hh), ImVec2(px + hw, py), ImVec2(px, py + hh), ImVec2(px - hw, py), shadeCol(rgb, 1.05f));
    shown++;
  }
  dl->Flags = fl;
  // the players, standing where they are
  for (auto& p : v->players) {
    if (!sameWorld(*v, p)) continue;
    float dx = p.pos.x - v->isoX0, dz = p.pos.z - v->isoZ0;
    if (dx < 0 || dz < 0 || dx > 2 * R || dz > 2 * R) continue;
    float px = ox + (dx - dz) * hw, py = oy + (dx + dz - 2 * R) * hh - (p.pos.y - base) * vh;
    dl->AddRectFilled(ImVec2(px - hw * 0.3f, py - vh * 2), ImVec2(px + hw * 0.3f, py), p.dead ? IM_COL32(136, 136, 136, 255) : IM_COL32(255, 90, 74, 255));
    dl->AddRectFilled(ImVec2(px - hw * 0.3f, py - vh * 2.6f), ImVec2(px + hw * 0.3f, py - vh * 2), IM_COL32(255, 216, 176, 255));
    label(dl, f, ImVec2(px, py - vh * 2.8f), p.name);
  }
  footer(dl, f, a, b, strFormat("%s · %d×%d blocks · %s · %s", v->follow.empty() ? "Spawn" : v->follow.c_str(), R * 2, R * 2, worldName(*v),
                               shown ? "live" : "nothing loaded here yet"));
  dl->PopClipRect();
}

} // namespace dq
