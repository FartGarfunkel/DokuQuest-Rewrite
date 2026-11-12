// client/play.h — the local player's glue: turns this device's keyboard/mouse/controllers into a
// PlayerInput for the sim's PlayerController (sim/controller.h), and places the camera (first
// person with view bob and smooth eye height, over the shoulder, behind, in front — local.js camera()).
#pragma once
#include "client/session.h"
#include "sim/controller.h"

namespace dq {

PlayerController* localController(GameSession& s);   // the local player's controller (null before spawn)
void clientTickLocalPlayer(GameSession& s, double dt);
void clientPlayerCamera(GameSession& s, Camera& cam, double dt);

} // namespace dq
