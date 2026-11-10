// assets/quiver3d.h — the quiver's shape, the same held, dropped and worn (assets/item_parts.cpp, assets/char_model.cpp):
// a leather tube a touch wider at its mouth, a dark rim and base cap, a band round its middle, a strap down its back with
// a buckle, an open mouth dark inside, and (when there are arrows in it) their shafts standing out of the mouth with white
// fletchings, more of them the fuller it is. Its own space, in px: the tube runs up y from 0 to 12.6, centred on
// x = z = 0, the strap on its +z side. fill: 0 (empty) .. 4 (full).
#pragma once
#include "common/common.h"
#include <array>

namespace dq {

struct QuiverTiles { int leather, dark, strap, metal, shaft, fletch, hollow; };   // hollow: the dark inside of its mouth

template <class BoxFn>
void quiverShape(BoxFn boxRaw, const QuiverTiles& t, int fill) {
  auto box = [&](vec3 a, vec3 c, std::array<int, 6> k) { boxRaw(glm::min(a, c), glm::max(a, c), k); };   // corners in any order
  auto all = [](int k) { return std::array<int, 6>{k, k, k, k, k, k}; };
  box(vec3(-1.8f, 0.8f, -1.8f), vec3(1.8f, 11.4f, 1.8f), all(t.leather));     // the tube
  box(vec3(-1.6f, 10.9f, -1.6f), vec3(1.6f, 11.4f, 1.6f), all(t.hollow));      // the dark inside, seen down its open mouth
  for (int s : {-1, 1}) {                                                        // its mouth: a rolled-over rim, open
    box(vec3(-2.05f, 11.0f, s * 1.6f), vec3(2.05f, 12.6f, s * 2.05f), all(t.dark));
    box(vec3(s * 1.6f, 11.0f, -2.05f), vec3(s * 2.05f, 12.6f, 2.05f), all(t.dark));
  }
  box(vec3(-2.0f, 0.0f, -2.0f), vec3(2.0f, 1.0f, 2.0f), all(t.dark));          // the base cap
  box(vec3(-1.95f, 5.4f, -1.95f), vec3(1.95f, 6.2f, 1.95f), all(t.dark));      // a band round it
  box(vec3(-0.6f, 1.6f, 1.8f), vec3(0.6f, 11.2f, 2.3f), all(t.strap));         // the strap down its back
  box(vec3(-0.45f, 8.2f, 2.25f), vec3(0.45f, 9.0f, 2.55f), all(t.metal));      // and its buckle
  // the arrows: two to a quarter of fullness, each standing its own height out of the mouth
  static const std::array<std::array<float, 3>, 8> A = {{{-0.8f, -0.7f, 3.2f}, {0.7f, 0.6f, 3.8f}, {0.5f, -0.9f, 2.6f}, {-0.6f, 0.8f, 3.5f},
                                                          {0.0f, 0.0f, 4.2f}, {1.0f, -0.1f, 3.0f}, {-1.0f, 0.1f, 2.8f}, {0.1f, 1.0f, 3.4f}}};
  const int n = std::min(8, std::max(0, fill) * 2);
  for (int i = 0; i < n; i++) {
    const float x = A[i][0], z = A[i][1], top = 12.6f + A[i][2];
    box(vec3(x - 0.22f, 10.4f, z - 0.22f), vec3(x + 0.22f, top - 1.1f, z + 0.22f), all(t.shaft));      // the shaft
    box(vec3(x - 0.5f, top - 1.5f, z - 0.12f), vec3(x + 0.5f, top, z + 0.12f), all(t.fletch));        // fletching, both ways
    box(vec3(x - 0.12f, top - 1.5f, z - 0.5f), vec3(x + 0.12f, top, z + 0.5f), all(t.fletch));
  }
}

} // namespace dq
