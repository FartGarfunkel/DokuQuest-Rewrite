// sim/world_spawn.cpp — see world_spawn.h.
#include "sim/world_spawn.h"
#include "sim/rules.h"
#include "gen/structures_api.h"
#include "gen/generator.h"
#include "world/dims.h"
#include <cmath>

namespace dq {

vec3 chooseWorldSpawn(Level& lv, float* yawOut) {
  vec3 sp = lv.info.spawn;
  if (sp.y >= 0 || !lv.gen) return sp;
  float yaw = 0;
  if (!rule(lv, "startInWild") && lv.info.worldType != "floating" && villageStartPoint(*lv.gen, sp, yaw)) { lv.info.spawn = sp; if (yawOut) *yawOut = yaw; return sp; }
  if (rule(lv, "startInWild") && wildStartPoint(*lv.gen, sp)) { lv.info.spawn = sp; return sp; }
  DimGenerator* g = lv.gen->forDim(DIM_OVERWORLD);
  int sx = 0, sz = 0, h = g ? g->spawnHeight(0, 0) : 100;
  bool found = false;
  // dry land out from the middle: every way round (one line out could be all sea)
  for (int r = 0; r < 4000 && g && !found; r += 32)
    for (int a = 0; a < (r ? 16 : 1) && !found; a++) {
      const int x = (int)std::lround(r * std::cos(a * 0.3927f)), z = (int)std::lround(r * std::sin(a * 0.3927f));
      ColumnSample s; g->sample(x, z, s);
      if (s.height > 0 && s.water < 0) { sx = x; sz = z; h = s.height + 1; found = true; }
    }
  if (lv.info.worldType == "floating" && g) {   // the middle of an island: every way round has ground under it
    found = false;
    for (int r = 0; r < 1600 && !found; r += 24) for (int a = 0; a < 8 && !found; a++) {
      const int x = (int)(std::cos(a * 0.785f) * r), z = (int)(std::sin(a * 0.785f) * r);
      ColumnSample s; g->sample(x, z, s); if (s.height <= 0 || s.floatBottom < 0) continue;
      bool ok = true; for (int d = 0; d < 4 && ok; d++) { ColumnSample q; g->sample(x + (d == 0 ? 8 : d == 1 ? -8 : 0), z + (d == 2 ? 8 : d == 3 ? -8 : 0), q); ok = q.height > 0 && q.floatBottom >= 0; }
      if (ok) { sx = x; sz = z; h = s.height + 1; found = true; }
    }
  }
  sp = vec3(sx + 0.5f, dimBase(DIM_OVERWORLD) + h + 1, sz + 0.5f);
  lv.info.spawn = sp;
  return sp;
}

} // namespace dq
