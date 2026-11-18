// client/play.h — the local player's glue: turns this device's keyboard/mouse/controllers into a
// PlayerInput for the sim's PlayerController (sim/controller.h), and places the camera (first
// person with view bob and smooth eye height, over the shoulder, behind, in front — local.js camera()).
#pragma once
#include "client/session.h"
#include "sim/controller.h"

namespace dq {

PlayerController* localController(GameSession& s);   // the local player's controller (null before spawn)
bool localSitOn(GameSession& s, int x, int y, int z);  // sat on that seat as the use does (client/test_script.cpp)
void clientTickLocalPlayer(GameSession& s, double dt);
void clientPlayerCamera(GameSession& s, Camera& cam, double dt);
// a screen leans your eyes in toward a point, this frame (first person only; ui/lectern_screen.cpp): up to 0.85 of a
// block along your line of sight, stopping `keep` short of it and short of any wall, the view narrowing a little more
// if that isn't near enough; it eases in, and back out once nothing asks for it
void cameraLeanTo(GameSession& s, glm::dvec3 target, float keep = 0.9f);

} // namespace dq
