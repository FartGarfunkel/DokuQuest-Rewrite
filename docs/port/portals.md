# Portals & the stacked worlds

Files: `src/sim/portals.{h,cpp}`, `src/sim/seamless.{h,cpp}`, `src/render/portal_view.{h,cpp}`,
`assets/shaders/portal.glsl`.

## Ported modules
| JS | C++ |
|---|---|
| game/portals.js: frames (any shape 2x3 … 21x21), lighting, collapse, `arrivePortal`, `findSafeY` ranges | `findPortalOpening`, `tryIgniteAt`, `tryIgnitePortal`, `collapsePortalNear`, `arrivePortal` (sim/portals.cpp) |
| game/portallinks.js: anchors, `portalStand`, links both ways, the forgiving Skylands lighting (water in the frame, any inside face) | `portalAnchor`, `portalStand`, `PortalSystem::link`, `findPortalOpening(skyRules)` |
| game/portalview.js: windows, `pvDest`, `pvMap`, prelinking a new portal, walking through mid-plane, standing-in-portal timer (local.js), rim glow + fade-in, rift tears' ragged edge | `PortalSystem::windows/dest/track/swap/prelink`, `portalXform`, render/portal_view.cpp, portal.glsl |
| game/seamless.js + worldupdate.js `stackedTick`/slow fall + worlds.js | `SeamlessSystem` (sim/seamless.cpp) |
| theend.js: eyes in the frames, the 3x3 pool, flat windows (stronghold → over the platform, exit → over spawn), `pvArrived`, void crossings (`void`/`unvoid`), void winds (`endDrift`), `endLanding` | `endPortalCheck`, `PortalSystem::flatDest`, `SeamlessSystem::arrived/endLanding` |
| riftarc.js / haunt.js (only the portal part) | `VirtualPortal` + `PortalSystem::setVirtual` (torn windows into another realm) |

## How it works
- **Index.** A `WorldSystem` hook per realm keeps every loaded `ember_portal` / `sky_portal` / `end_portal`
  cell (chunk-load scan + block changes). `windows(realm)` floods them into windows (anchor = lowest,
  then least x, then least z — the portallinks.js naming) and adds the virtual ones.
- **Destinations.** Ember: Overworld ↔ Ember Depths band, x/z ÷ 8 (`NETHER_SCALE`, game rule
  `portalScale` overrides). Skylands: Overworld ↔ Skylands band, same x/z. A portal with no link that a
  player comes within 28 blocks of gets its far end prepared (stream anchor), then an existing portal
  within 24 blocks of the target is used (index lookup) or one is built on safe ground (platform +
  4x5 frame sunk into the floor), and the two are linked both ways in `LevelInfo.data["portalLinks"]`
  (`"realm:x,y,z" -> {realm, anchor, pos}`). A link whose far portal is gone is dropped and redone.
  End pool in the Overworld band → 22 above the End platform (`endPlatform()`, built if missing);
  End pool within 8 blocks of the End's 0,0 → 24 above the world spawn.
- **Pair transform** (`portalXform`): rotate about the vertical by the difference of the two normals'
  yaws, centre onto centre, floor onto floor (pvMap). Rifts flip to the side you look from.
- **Walking through.** `PortalSystem::track(entity)` tests the segment moved since last time against
  each window's plane; crossing inside the window moves position, velocity, look and realm, plus the
  mount and its riders, with no pause. The client calls it for the local player every frame right after
  movement (before the camera), the level tick for everything else (creatures, items, other players).
  Standing in a portal: 0.25 s walking / 1.2 s still, then you're put out on the side you face.
- **Views** (render/portal_view.cpp): per visible window (≤ 3, nearest first; ≤ 1 nested), stencil-mark
  its plane → clear depth inside → draw the far sky + world (+ other layers when same realm) from the
  carried camera with an oblique near plane on the far plane → nested windows (depth 2 on preset
  high/ultra or auto+Modern GL; `DQ_PORTAL_DEPTH` env overrides) → rim glow/fade → restore the window's
  depth. While the camera is inside the portal slab the mask moves to the far face so the near plane
  never cuts it away mid-step. Same-realm views reuse the session's WorldRenderer
  (`pushView/popView` keep the main pass's list); side realms get their own WorldRenderer.
  Portals that aren't windows (views off, far side not ready, too many) draw the animated surface
  (doku.js painter colours; the End's starry deep; rift purple).
- **Stacked worlds** (`SeamlessSystem`, main realm only): crossing a band edge while moving gives the
  original's notices and effects — Skylands → Overworld: cloud-bank slow fall; Overworld top → Skylands:
  the updraft (along under the islands, up, across onto the nearest island from the Aether generator's
  `sample`); Overworld floor → Depths: a shaft is broken through the Depths' roof; Depths → End: void
  winds carry you to the nearest End island; End → Depths: a pocket opens in the Depths' floor (void
  crust). Void below y −32 (side realms: their band − 32) hurts 4 per tick, creative too (local.js).
  First visits set `data.flags.skyVisited/depthsVisited/endVisited` and add 40/40/120 xp.
  Preparing the neighbouring band near an edge is `World::stream`'s `nearDims` (already in world.cpp).

## APIs for other areas
- **itemUse contract** (gameplay must emit it when the player uses an item, before the default action):
  `itemUse [playerName, itemKey, x, y, z, face]` — x,y,z = block clicked (global y), face 0..5 =
  +x −x +y −y +z −z side clicked, −1 if nothing targeted. Returning false (cancelled) means "used up":
  skip the default (fire/water placement). The portal system lights portals (any item with
  `igniter` — flint and steel wears 1, the Ember Igniter shows the original hint when it misses; a
  water bucket becomes a bucket; an Eye of Ender goes into an `end_portal_frame`) and edits the held stack.
  Fire placed by any means inside an obsidian frame also lights it (WorldSystem hook).
- Events emitted: `portalEnter [entityId, fromRealm, toRealm, x, y, z, windowKey]`,
  `portalLit [x, y, z, "ember"|"sky"|"end", realm]`,
  `dimensionChange [entityId, playerName, fromDim, toDim, fromRealm, toRealm]` (C++ dim numbers).
- `portalSystem(level)`: `windows(realm)`, `dest(window)`, `track(entity, dt)`, `farAnchors(...)`
  (a dedicated server must add these anchors when it streams for its players), `setVirtual/removeVirtual`
  (riftarc port: the village gateway → `REALM_HIS`, and its exit), `hurt` (set it to gameplay's damage
  function), `links()`.
- `SeamlessSystem::cloudAt(y)` (sky/clouds may use it), `skyLanding`, `endLanding`.
- `endPlatform()` (sim/portals.h): where the End generator should put the obsidian platform.
- `RenderLayer::afterPlayerMove` / `streamAnchors` (session hooks), `FrameCtx::portalDepth` (> 0 while a
  layer is called for a portal view: don't glClear, don't keep per-frame state from that call).

## Differences from the original
- One world, no far-world copies: a portal pair is two places in the same World (side realms excepted),
  so nothing is loaded/saved/swapped on crossing; the far end is just kept streamed and meshed.
- True stencil portals (drawn in place, any depth) instead of render-to-texture pictures; the window is
  the portal's centre plane, the far side is cut at the far plane (not 0.52 beyond), so floors run on
  unbroken through it.
- Ember portals scale x/z by 8 (per the port spec; 0.9.0.90 kept x/z 1:1). Original height ranges for
  finding/building are doubled for the 256 band.
- Standing still in a portal puts you out on the side you face (the original could bounce you back).
- Particle names used: `white`, `flame`, `cloud`, `end_mote` (extra json: spread, ring, vel, life, size, grow).

## What's left / for other areas
- Mesher: `ember_portal`/`sky_portal` (render LIQUID, tLiquid 0) currently emit only faces next to water —
  they should emit nothing (this layer draws them). `end_portal` needs its 11.5–12/16 slab shape
  (theend.js SHAPES.end_portal); the placeholder full cube hides the End window.
- Gameplay: emit `itemUse`; death at hp ≤ 0 (void damage only lowers hp); xp levels; flint fire.
- Light engine: portal blocks emit 11 (the far platforms are dark until block light is ported).
- Crossing into a side realm remeshes the session renderer (a moment of pop-in); the session could hold a
  renderer per realm. Multiplayer guests: crossings/links are host-side; the replica needs the windows.
- Not ported: End gateways (theend.js), the realm's own exit rift placement (riftarc.js owns it),
  portal sounds on crossing (the original had none for windows).

## Test hook
`DQ_PORTAL_TEST=ember|sky|end|rift DQ_PORTAL_TEST_SHOT=/tmp/pt ./build/dokuquest --create T --seed hello
--smoke-frames 400` builds and lights a portal in front of the player once the area is loaded (emits
`itemUse` like the gameplay will), waits for the far side, then walks through it, saving
`/tmp/pt-window.png`, `-crossed.png`, `-back.png`. Progress goes to the log (`portal test: ...`).
