// render/camera.h — view/projection with camera-relative rendering (world positions can be large,
// so everything is drawn relative to the camera's integer block to keep float precision).
// Angles follow the original (core/util.js Mat4.viewRotation): yaw=0 looks down -Z, +yaw turns
// toward -X, +pitch looks up.
#pragma once
#include "common/common.h"

namespace dq {

struct Frustum {
  vec4 planes[6];
  void fromMatrix(const mat4& m);
  bool box(vec3 lo, vec3 hi) const;
  bool sphere(vec3 c, float r) const;
};

struct Camera {
  glm::dvec3 pos{0};       // world position (double)
  float yaw = 0, pitch = 0, roll = 0;
  float fov = 72;          // vertical degrees
  float aspect = 16.f / 9.f;
  float nearZ = 0.05f, farZ = 4096.f;
  int realm = 0;
  // derived (call update())
  mat4 view{1}, proj{1}, viewProj{1};   // view is relative to `origin`
  glm::dvec3 origin{0};                  // floor(pos): render offset
  Frustum frustum;                       // in origin-relative space
  vec3 forward() const { return vec3(-std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)); }
  vec3 right() const { return vec3(std::cos(yaw), 0, -std::sin(yaw)); }
  void update();
  vec3 rel(glm::dvec3 p) const { return vec3(p - origin); }   // world -> render space
};

} // namespace dq
