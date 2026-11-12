#include "render/camera.h"

namespace dq {

void Frustum::fromMatrix(const mat4& m) {
  // rows of the (column-major) matrix
  auto row = [&](int r) { return vec4(m[0][r], m[1][r], m[2][r], m[3][r]); };
  vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  planes[0] = r3 + r0; planes[1] = r3 - r0; planes[2] = r3 + r1; planes[3] = r3 - r1; planes[4] = r3 + r2; planes[5] = r3 - r2;
  for (auto& p : planes) p /= glm::length(vec3(p));
}
bool Frustum::box(vec3 lo, vec3 hi) const {
  for (auto& p : planes) {
    vec3 v(p.x > 0 ? hi.x : lo.x, p.y > 0 ? hi.y : lo.y, p.z > 0 ? hi.z : lo.z);
    if (glm::dot(vec3(p), v) + p.w < 0) return false;
  }
  return true;
}
bool Frustum::sphere(vec3 c, float r) const {
  for (auto& p : planes) if (glm::dot(vec3(p), c) + p.w < -r) return false;
  return true;
}

void Camera::update() {
  origin = glm::floor(pos);
  vec3 eye = vec3(pos - origin);
  mat4 rot(1);
  rot = glm::rotate(rot, -roll, vec3(0, 0, 1));
  rot = glm::rotate(rot, -pitch, vec3(1, 0, 0));
  rot = glm::rotate(rot, -yaw, vec3(0, 1, 0));
  view = rot * glm::translate(mat4(1), -eye);
  proj = glm::perspective(glm::radians(fov), aspect, nearZ, farZ);
  viewProj = proj * view;
  frustum.fromMatrix(viewProj);
}

} // namespace dq
