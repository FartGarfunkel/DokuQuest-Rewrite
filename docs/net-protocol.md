# DokuQuest network protocol (C++ port) — version 2

Modelled on the original `net/net.js` messages (same names and short keys where they exist), carried over
**ENet** (reliable/unreliable UDP) instead of the desktop app's WebSocket relay. Code: `src/net/protocol.h`.

## Transport
- Server listens on UDP **25580** (tries the next 9 ports when taken, like `lan.js`). Clients connect with 3 channels.
- **Channel 0** – reliable, ordered: handshake, chunks, blocks, block entities, chat, inventory, control.
  Chunk records and block changes share this channel so a block change can never overtake the chunk it belongs to.
- **Channel 1** – reliable entity news (`es`: something new to the player, its data changed, gone; or an entry too big
  for one datagram), time (`env`), effects (`efx`, unreliable), a `me` carrying book pages.
- **Channel 2** – movement only, unreliable and unsequenced: `me` and `es` with `u:1`, each one datagram (under 1100
  bytes). Never a reliable packet on it (ENet holds a channel's unreliable packets behind its reliable ones). Both ends
  turn off ENet's throttle deceleration (`steadyPeer`): on a jittery link it dropped most unreliable packets at the sender.
- Times: `me.ct` is the guest's clock when it was there; `es.st` the host's clock when sent, and a player's `ts` the
  host-clock time its place was true (a guest's `ct` put on the host's clock). Each side estimates the other's clock
  offset from the quickest packets (`NetClock`) and draws replicas a little in the past between timed samples
  (`sim/net_interp.cpp`: buffer = sample gap + mean lateness + 2.5 × its spread, eased; up to 0.3 s carried on along
  the last heading; corrections eased out; a jump over 8 blocks snaps).
- Each packet = 1 byte message type + a **MessagePack** map (a JSON object; chunk records are MessagePack binary).
- ENet timeouts: a peer silent for 10–30 s is dropped (the original's `NET_SILENT` = 30 s).
- Disconnect data codes: `0` normal, `4000` kicked, `4001` host closed the world, `4002` refused.

## Compatibility rules
- `PROTO` (in `hello`/`welcome`) only changes when an old peer can no longer understand a new one.
  A server refuses a different `PROTO` with an "update so you both have the same one" message.
- Unknown message types and unknown fields **must be ignored** by both sides; add new things as new fields/types.
- Block ids are runtime-only: the server sends its palette (`pal`, key per id) in `welcome`; the client maps ids
  both ways by key (unknown keys become air). Chunk records use the save codec (palette of keys), so they never
  depend on ids. Entity `data` and block entities should use keys, not ids.
- Coordinates are global (y 0..1023 across the stacked dimensions). `rl` = realm (0 main, 1 pocket, 2 his realm),
  `d` = dimension band (0 End, 1 Nether, 2 Overworld, 3 Skylands).

## Joining
1. client → `hello {proto, version, name, skin, password, R}` (R = view distance in chunks)
2. server → `refuse {why}` (bad version/name/password, banned, full, name taken) and disconnects, or
   `welcome {proto, id, eid, info, pal, pl, name, host, vd, op, dedicated, version}`:
   `info` = the level (seed, seedText, worldType, genVersion, mode, difficulty, rules, time, day, spawn, data),
   `pl` = the player's saved body (`Player::save()`, kept by the server by name), `eid` = its entity id there.
3. server → `ck` records for every **changed** chunk in range (nearest first), `cs` checksums for untouched ones,
   then `ckdone {n}`. The client builds its level from `info` (`LevelInfo::remote = true`), stores the records
   in its region cache so those chunks load instead of generating, and generates everything else from the seed.
4. client → first `me`: the player is "ready"; others get `peer {n, j:1}`, the server's mods hear `join(name)`,
   and the welcome message (motd) follows 1.5 s later as a `msg`.

## Client → server
| type | id | body | what |
|---|---|---|---|
| hello | 1 | see above | |
| me | 2 | `x y z vx vy vz r p b rl f hp dead sel ct rd?` + pose keys | own body, 20/s, channel 2 (one older than the newest taken in only adds a drawing sample). `f` bits: 1 onGround 2 inWater 4 sneak 8 sprint 16 fly 32 swim 64 crawl. Light check: moves over 4 + 90 m/s are put back with a `self.tp` (except after a teleport/portal, a respawn near spawn or the bed, or a realm change). |
| save | 3 | `base, pl{inv ar of sel xp lv air stats spawnPoint spawnRealm}` | own bags: 0.7 s after they change, every 8 s, on `flush`. Ignored when `base` < the server's last inventory push (`self.seq`). |
| blk | 4 | `rl, l:[[x,y,z,id,meta]...]` | block changes made by this game (server ids). The server checks reach (24), emits `blockBreak`/`blockPlace` (a cancelling plugin → the true blocks are sent back), applies, and echoes to everyone. |
| be | 5 | `rl x y z e q` | a block entity (chest contents...); `q` numbers our changes |
| chat | 6 | `text` | a chat line or `/command` (runs with the player's op rights) |
| hit | 7 | `id a s` | hurt an entity (ghost); the server emits `netHit [name,id,a,src]` — a handler returning false has applied it, otherwise plain damage |
| act | 8 | `n a` | any other action for the server: event `netAction [name, n, a]` |
| spawn | 9 | `e` (Entity::save) | something made here (arrow, dropped item, boat...): the server makes the real one |
| portal | 10 | `rl to[3]` | used a portal: others in that land follow (rule `portalsTakeEveryone`, default on) |
| need | 11 | `rl d cx cz` | send this chunk's record (checksum mismatch, or it changed while we didn't have it) |
| bye | 12 | `pl?` | leaving (with the latest bags) |

Pose keys (in `me` and player snaps, `net/protocol.cpp playerPoseInto`): `sn` arm swings begun (a count: each change
plays a swing), `mn [x,y,z,percent]` the block being dug (its cracks drawn on every screen, dust and sound played),
`bw` bow draw 0..1, `lc` ledge-climb progress, `cy` what's carried in the arms, `xf` bits: 1 asleep (host's word only)
2 gliding 4 spyglass 8 hanging from a ledge 16 shimmying 32 on a ladder 64 in water 128 eyes under water 256 the host's
own player (its sounds already come as `efx`).

## Server → client
| type | id | body | what |
|---|---|---|---|
| refuse | 32 | `why` | |
| welcome | 33 | see above | |
| ckdone | 34 | `n` | the first records have all been sent |
| ck | 35 | `rl d cx cz r(binary)` | a chunk record (`encodeChunk`); replaces a loaded chunk's contents |
| cs | 36 | `rl d cx cz h` | checksum (FNV like `chunkHash` in net.js, over server ids + meta) of an untouched, populated chunk; a mismatch → `need` |
| blk | 4 | `rl, l:[[x,y,z,id,meta]...]` | every block change in the served world (generation excluded), 30/s batches, to everyone |
| be | 5 | `rl x y z e a` | block entity now; `a` = {peer id: last `q` included} so a client ignores older news of its own change |
| es | 38 | `rl, l:[snap...], g:[ids], st, u?` | entities within 96 blocks (players 20/s, the rest 10/s; changed ones, once more after they stop, + every 2 s). snap: `i t x y z r p b hp mh w h f? rd? d?` (+ players: `n sk md hd ar of pf ts` + pose keys); `d` (entity data) only when it changed. `g` = gone. `u:1`: the movement channel's — only things the client already has, and nothing older than a `g` for it. |
| env | 39 | `time day w rules` | 1/s; the client's clock runs on and is nudged (snapped only on a big jump) |
| efx | 40 | `s:[[name,x,y,z,range,vol,pitch]] m:[[mat,kind,x,y,z]] v:[[creature,kind,x,y,z,pitch]] p:[[kind,x,y,z,count,extra]] u:[name] k:[strength]` | the world's sounds/particles near the player (LevelFx), UI sounds and shakes for that player |
| msg | 41 | `text k` | chat / message / command reply (`k`: chat, cmd, good, warn, ...) |
| self | 42 | `tp rl kb hurt hp inv seq md fl xp lv` | what the server's world did to the player's own body: teleport, knockback (velocity delta), damage, heal, bags (+ sequence), game mode, flying, xp |
| peer | 43 | `n j` | someone joined (1) / left (0) |
| dim | 44 | — | reserved (portal follow is sent as `self.tp`) |
| kick | 45 | `why closed?` | about to be disconnected |
| flush | 46 | — | send your `save` now (the server is stopping / saving) |

## LAN discovery (`lan.js`)
Every 1.5 s a host broadcasts one UDP datagram to **25581** (255.255.255.255 and 127.0.0.1):
`{"dokuquest":1, "world", "host", "version", "proto", "players", "max", "port", "motd", "password":bool}`.
Scanners bind 25581 and list hosts heard in the last 5 s.

## Who decides what
- **Server**: blocks, fluids, creatures, items, time, weather, saving, other players' positions.
- **Each client**: its own movement (lightly checked), its own bags and health — like the original. The server keeps a
  *mirror* Player per client (`ghost = true`); anything the server's own systems do to a mirror (damage, knockback,
  teleport, items given, mode, xp) is noticed after each tick and sent as `self`. Gameplay code can therefore treat
  remote players like local ones — but **must not simulate ghosts** (skip `e.ghost` in physics/AI systems).
