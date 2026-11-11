// render/height_fog.cpp — see height_fog.h
#include "render/height_fog.h"
#include "world/dims.h"
#include <cmath>

namespace dq {

vec4 heightFogParams(float camY, int dim, float mist, float rain, bool on, float open) {
  if (!on || dim != DIM_OVERWORLD) return vec4(0);   // the Nether, the End and the skies keep their own fog
  mist = std::clamp(mist, 0.f, 1.f); rain = std::clamp(rain, 0.f, 1.f);
  const float base = (float)dimBase(DIM_OVERWORLD) + 64;    // the valley floor: sea level
  const float H = 6 + 10 * (1 - mist);                      // a mist lies lower and thicker
  const float d0 = 0.004f + 0.03f * mist + 0.008f * rain;   // per block at the floor
  // at the camera: thinner above the floor; below it (deep in a ravine) no thicker than a little over the floor's
  const float e = std::min((base - camY) / H, 1.5f);
  return vec4(d0 * std::exp(e) * std::clamp(open, 0.f, 1.f), H, 1, 1.5f - e);
}

} // namespace dq
