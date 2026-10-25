// sim/weapon_rack.h — batche.js (0.9.0.82) weapon racks. Where item k rests on a rack, relative to the block's
// corner (shared by the use code, which picks the slot nearest the pointer, and the renderer).
#pragma once
#include "common/common.h"
#include <glm/gtc/matrix_transform.hpp>

namespace dq {

inline mat4 rackSlotMat(int f, bool wall, int k) {
  static const float ROT[4] = {0, -PI / 2, PI, PI / 2};
  static const float WALL_Y[3] = {3.5f, 8, 12.5f};
  mat4 m(1);
  k = k < 0 ? 0 : k > 2 ? 2 : k;
  if (wall) {
    m = glm::translate(m, vec3(0.5f, WALL_Y[k] / 16, 0.5f)); m = glm::rotate(m, ROT[f & 3], vec3(0, 1, 0));
    m = glm::translate(m, vec3(0, 0, -0.5f + 3.1f / 16)); m = glm::rotate(m, -PI / 4, vec3(0, 0, 1));
  } else {
    m = glm::translate(m, vec3(0.5f, 0, 0.5f)); m = glm::rotate(m, ROT[f & 3], vec3(0, 1, 0));
    m = glm::translate(m, vec3((k - 1) * 4.2f / 16, 0.53f, 2.4f / 16)); m = glm::rotate(m, PI / 4, vec3(0, 0, 1));
  }
  return glm::scale(m, vec3(0.66f));
}

void registerWeaponRack();

} // namespace dq
