# Port notes: network, dedicated server, mods

JS sources: `net/net.js`, `net/netsync.js`, `net/netui.js` (logic only), `game/server.js` (non-UI parts),
`game/mods.js`, `launcher/desktop/lan.js`. Protocol: `docs/net-protocol.md`. Test: `build/test_net` (59 checks:
a dedicated server with JS/folder/native mods + two scripted clients, then an "Open to LAN" host + a friend).

## Ported
| Original | C++ | Notes |
|---|---|---|
| net.js `Net` host side, `welcome`, `peerLeft`, `hostTick`, `flushBlocks`, `updateRange/flushChunks`, `sendEntities`, `flushEffects`, env | `net/server_host.{h,cpp}`, `net/server_sync.cpp` | ENet instead of the WebSocket relay; one code path for the dedicated server and Open to LAN |
| net.js guest side (`join`, `guestMsg`, `guestTick`, `applyEntities`, `chunkLoaded`) | `net/client.{h,cpp}` | blocking `connect()` or `begin()`+`poll()` |
| netsync.js snapshots, block-change hook, guest-made entities, sounds/puffs, portals-take-everyone | `entitySnapshot`, `NetWatch`/`ClientWatch` world systems, `NetFx` (LevelFx decorator), SPAWN, `ServerHost::portal` | ghosts glide in `Level::tick` (`Entity::glide`) |
| lan.js announce / LanScanner | `net/lan.{h,cpp}` | ENet's socket calls; also `localAddress()` |
| server.js serverStart/Stop/Say/Command, player limit, motd, kick | `server/server.{h,cpp}`, `ServerHost` | + ban/banip/pardon/op/deop, tp, give, msg, motd, maxplayers, plugins; ops/bans in the world folder (`ops.json`, `bans.json`) |
| mods.js modApi / modsLoad / modEmit / modFault / MOD_EXAMPLE | `mod/modloader.cpp`, `mod/js_mod.cpp`, `mod/mod_api.cpp` | QuickJS-ng, one runtime per mod |
| — | `mod/dq_plugin.h`, `mod/native_mod.cpp`, `tools/sample_plugin.c` | native plugins (C ABI) |
| commands.js COMMANDS registry & dispatch | `sim/commands.h`, `sim/commands_registry.cpp` | registry only; the game's commands are the gameplay port's |

## DQ API (JS) — same as the original
`DQ.name`, `DQ.plugin`, `DQ.version`, `DQ.game` (a plain info object, not the engine), `DQ.on(ev, fn)` (return `false`
cancels chat/blockBreak/blockPlace/itemUse/craft), `DQ.command(name, desc, fn(player, args))`, `DQ.item(key, def)`,
`DQ.recipe(def)`, `DQ.world.getBlock/setBlock(x, y, z[, key, meta, realm])`, `DQ.players()`, `DQ.give(name, item, n)`,
`DQ.spawn(type, x, y, z)` (host only), `DQ.say`, `DQ.toast`, `DQ.time([fraction])`, `DQ.log(...)`.
Events: worldOpen, tick, blockBreak(player, x, y, z, key), blockPlace(player, key, x, y, z), mobDeath(info, killer),
chat/join/leave (host), serverStart/serverStop/log, plus every other EventBus event (args as JSON).
Additions: `DQ.block(key, def)` (data-pack block + its item), `DQ.emit(ev, ...args)`, `setTimeout/setInterval/clear*`,
`console.log/info/warn/error`. A file is the body of `function (DQ) { "use strict"; ... }` (line numbers match).
Each call has a budget (load 2 s, events 250 ms, tick 50 ms); a mod that runs over 3 times is switched off.

## Differences from the original
- Transport: ENet/UDP with MessagePack bodies instead of JSON over a WebSocket relay; no browser guests.
- y is global (0..1023, stacked dimensions); `DQ.players()[i].dim` uses the original numbering (`jsFromDim`), plus `realm`.
- Plugins on a server also hear `blockBreak`/`blockPlace` of remote players (the original host only heard its own).
- The server keeps a *mirror* Player per guest and passes on what its world does to it (`self`), instead of the
  RemotePawn proxy forwarding hurt/knock/give calls one by one.
- Portal trips: each player has their own realm/position (seamless world); "portals take everyone" moves the other
  players in the same land and band (world rule `portalsTakeEveryone`, default on).
- Riding hand-over (`ride`/`own`/`unown`) and `mobset`/`fell` are not ported yet (need the boats/carts/mobs ports).
- No UPnP (forward UDP 25580 for internet play).

## Shared files touched (additive)
`sim/entity.h` (`ghost`, `netLocal`, `netPos/netYaw`, `glide()`), `sim/level.h/.cpp` (`LevelNet` interface,
`Level::net`, `LevelInfo::remote`, ghosts skip `tick()`), `world/world.h/.cpp` (`SetFlags::FromNet`, `changeFlags`,
`blockEntityChanged()`, `WorldSystem::onBlockEntityChanged`, `regionStore()`), `client/app.cpp` (joinServer, two
hooks, `--connect` at start), `client/session.cpp` (3 lines: net update, stream anchors, no pause when shared),
`CMakeLists.txt` (test_net + dq_sample_plugin targets). New: `sim/commands.h`, `client/netplay.{h,cpp}`.

## APIs for other areas
- **Gameplay** (`sim/`): emit `blockBreak`/`blockPlace` before changing blocks (already done in session.cpp); call
  `world.blockEntityChanged(x,y,z)` after editing a block entity in place; on a remote level (`lv.isRemote()`) don't
  simulate the world (spawning, fluids, random ticks, AI) and skip `e->ghost` entities everywhere; to hurt an entity on a
  client call `lv.net->hitEntity(player, id, amount, src)` (server side: handle the `netHit` event, return false);
  other server-side actions: `lv.net->action(player, name, args)` → event `netAction`. Things a client creates
  (arrows, drops, boats) are handed to the server automatically; mark purely visual entities `netLocal = true`.
  World generation must write with `SetFlags::NoModify` (otherwise every generated chunk counts as changed and is sent).
  Chat commands: `registerChatCommand(name, desc, fn, cheat, usage)` / `runCommand` / `submitChat` (sim/commands.h).
- **Portals**: when a player goes through a portal call `lv.net->portal(player, realm, dest)` if `lv.net`.
- **UI**: chat input → `submitChat(level, player, text)`; show `LevelFx::message(id, text, kind)` lines (chat, cmd,
  good...) — on a client every server message arrives there. Pause menu → `openToLan(app, opts)`, `closeLan`,
  `lanStatus`, `lanKick` (client/netplay.h); Multiplayer → `lanScan()` each frame, `App::joinServer(address)`;
  Mods screen → `ModLoader::list/setEnabled/saveState/addMod/removeMod/exampleMod/checkSyntax`.
- **Launcher Server tab**: `DedicatedServer(cfg).start(&err)`, `status()` (players, tps, uptime...), `logSince(seq)`
  for the console view, `command(line)`, `modLoader()`; `ServerConfig::toJson/fromJson` (the headless server reads
  `<userdir>/servers/<world>.server.json`).
- **Assets**: `modTextures()` → append to the TileSet, set `reg().tileIndex[name]` and the faces of blocks whose `tex` is it.
- **Weather/sky**: on a client the server's weather arrives in `info.data["weather"]`; time in `info.time/day`.

## Left to do
- A non-blocking "Joining…" screen (NetClient::begin/poll are ready) and the Multiplayer / Open-to-LAN screens (UI).
- Riding hand-over, animal interactions from guests (`mobset`), falling trees, `hurt` sources with attacker info.
- Interest management by realm for `blk`/`be` broadcasts (now: to everyone, clients mark unseen chunks stale).
- Per-chunk checksum only for settled chunks; the edge of a client's view is not checked.
- UPnP, server-side resource-pack hints, chat history scroll-back for joiners.
