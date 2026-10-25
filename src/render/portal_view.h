// render/portal_view.h — portals you can see through (game/portalview.js), drawn as true portals:
// each visible window is a stencil mask; inside it the far side is drawn from a camera carried by the
// portal pair's transform, with an oblique near plane on the far portal's plane, then the window's depth
// is put back so the rest of the scene sorts against it. Depth 1 (2 on high settings), every visible
// portal at once (Settings::portalViews). Portals that aren't windows show their animated surface.
// The far ends are kept loaded (stream anchors) and, for side realms, meshed by their own WorldRenderer.
// Also the client glue: the local player's walk-through (PortalSystem::track right after moving).
//
// Smoke test hook: DQ_PORTAL_TEST=ember|sky|end builds and lights a portal in front of the player once
// the world around has loaded, waits for its far side, then walks through it. DQ_PORTAL_TEST_SHOT=<prefix>
// saves <prefix>-window.png, -crossed.png and -back.png.
#pragma once
#include "client/session.h"

namespace dq {

void registerPortalView();   // client start-up (App::run)

} // namespace dq
