# Structures and settlements (src/gen/structures_*.cpp)

Everything the original builds at generation time, through the hook in `gen/structures.h` (docs/port/gen.md).
Registered from `registerStructures()` (gen/structures_register.cpp, called by `registerWorldTypes()`), in the
original's order: that order is the priority when two would overlap (structures.js keeps every STRUCTS kind 24 from
villages, 8 from dungeons, 6 from dragon lairs and 6 from the STRUCTS kinds before it) and the build order.

| File | Original | Kinds (band) |
|---|---|---|
| `structures_villages.cpp` | towns.js planVillage / buildVillageInto / townWall, villageflat.js, towns3.js townGatesBuild, townlife.js | `village` (village / town / city tiers) (OW) |
| `structures_townbld.cpp` | villages.js vStamp, towns.js vStamp helpers, vHouse, TOWN_VT | the 18 buildings |
| `structures_dungeons.cpp` | dungeons.js, dragons.js planLair/buildLair | `dungeon` (OW), `dragon_lair` (OW, Depths, Skylands) |
| `structures_places.cpp` | structures.js | `ruin`, `tower`, `desert_temple`, `ocean_temple`, `wreck`, `mineshaft`, `labyrinth` (OW) |
| `structures_deep.cpp` | stronghold.js, deepdark.js, deephalls.js, seas.js | `stronghold` (3 per world, global), `ancient_city`, `deep_halls`, `twin_vault`, `pirate_cove` (OW) |
| `structures_far.cpp` | places.js, nether2.js, batche.js, theend.js | `sky_temple`, `sky_shrine`, `skyship`, `moa_nest`, `sky_sanctum` (Skylands); `ember_bastion`, `ember_spire`, `ruined_forge`, `bone_pit`, `nether_fortress` (Depths); `end_city` (End) |
| `structures_common.*` | structures.js sPut, structLoot, sLand; dungeons.js dungeonLoot; villages.js vLoot | builder `SP`, loot, block lookups |
| `structures_data.cpp` | GENERATED (`tools/port/structdata.py`): STRUCT_LOOT, DUNGEON_LOOT, VSTYLE, TOWN_PAVE, TOWN_DECOR, DTHEME, DH_THEMES, TOWN_TIERS, LORE_KEYS... after every module loaded | — |

paths.js has nothing built at generation time (it is character paths and schools of magic); riftarc.js and
haunt.js only change the world at run time (his realm's terrain is gen/realms.cpp).

## Placement and heights
- Seeds, regions, candidates, ids are the original's: STRUCTS kinds `s<k>_<jsDim>_<rx>_<rz>` with `jsIndex` = the
  original's STRUCTS index; villages `t<rx>_<rz>` (hash32(seed, rx, rz, 4242), 352 regions); dungeons `d<rx>_<rz>`;
  lairs `L<jsDim>_<rx>_<rz>`; strongholds `sh_0..2`; End cities `e<rx>_<rz>` (the original had no id).
- Hook additions (gen/structures.h, additive): `StructureType::place` (own per-region placement), `global`
  (strongholds), `jsIndex`, `treeOff`/`treePad` (each kind's own tree filter), `StructurePlan::hashDy`, `surface`,
  `ext` (the placer's C++ data); `StructureIndex::nearest`, `typeList`, a per-thread chunk memo for levelling and the
  tree filter; `setStructuresEnabled(false)` for tools. `SkylandsGen::islandMask/hillTop` added for places.js.
- Plans decide with the original's heights (`TerrainColumn::jsY`), so they accept exactly the original's places;
  they build at band heights. Overworld surface kinds stand on the band surface; underground kinds go to
  `vmap::owUnder(original y)` (mineshafts, labyrinth, dungeons, strongholds, ancient cities, deep halls, twin vault);
  Skylands + `SKY_DY`; End + `END_DY`; Depths `vmap::netherY`, with pillars down into the lava (fortress, bastion)
  stretched by NETHER_K so they still reach it; the obsidian spire's top is the original's top row mapped (it is taller).
- `hashDy` (original base y − band base y) is added to y in the per-block hashes that used a world y and in chest loot
  seeds, so cracked/mossy picks and loot match the original at the same place in the structure (`SP::hw/pickw`).
- Villages level the land (`flatten`, villageflat.js weights over 10 blocks); every building, street and the wall
  stand on the square's height.

## Fidelity (`tools/port/struct_compare.py <js-root> build <work> hello --R 1500 --span 2`)
Plans: every kind in all four bands, within 1500 blocks of the origin — 100% same ids, footprints and base heights
(ruins 59, villages 7, dungeons 55, lairs 60, Depths 258, Skylands 174, End cities 31...). Blocks of the nearest one of
each kind (positions that changed to a non-terrain block, relative to the base): the original's blocks found in the
C++ 98–100% for every kind (towns 99.8%, deep halls 97.9%: per-column 98.5%); the C++ has extra where the land is
stretched (footings, shafts, the labyrinth stair, stronghold ladder) and where pillars were lengthened (Depths).

## Differences (deliberate)
- The labyrinth's stair runs until it reaches the band surface, so it is longer and its footprint's z1 is larger.
- The stronghold's ladder shaft stops at most `owUnder(32)` above it (the original's 32 rows), short of the surface.
- Monster cages get a block entity `{type:'spawner', mob, structure}` (the original kept the mob only in the plan).
- The "land that's been played on keeps what it has" books (structBook, villageBook, dungeonBook, lairBook,
  villageFlat) are not kept: every C++ world is new, so everything is placed (that belongs to Level if ever needed).
- Old-terrain (version < 5) villages (villages.js templates) aren't built; every world uses towns.js.
- towns.js's apothecary decor and building signs (`b.decor`, `b.sign`) are never set in the original — not ported.
- Gate signs name no destinations (the original's game/roads.js is not part of 0.9.0.90).
- Shop signs are placed at the lot's height before levelling, like the original (so some go missing on slopes).

## Data for other workstreams (StructurePlan::data, band-local y — add dimBase(dim) for World coordinates)
Get plans with `structureIndexOf(gen)->near(x, z, pad, out[, type])`, `->at(type, rx, rz)`, `->nearest(type, x, z)`,
or `locateStructure(wg, dim, kind, x, z)` (gen/structures_api.h). Shapes follow the original's plan objects:
- `residents: [{at:[x,y,z], mob}]`, `spawners: [{at, mob}]`, `boss: {at, type, box:[x0,y0,z0,x1,y1,z1]}` — structTick
  spawns residents once, keeps cages alive, raises the boss (mage tower, labyrinth, temples, coves, Depths places...).
- village: `{kind:'village', id, name, style, tier, x, z, y, P, hw, r, board:[x,y,z], bell, stalls:[{x,z,f,wares|deco}],
  seats:[[x,y,z,f] | [x,y,z,f,[ax,az],held]], folk:[{home:{at,f}, job, work:{at,face}|null}], guards, golems,
  blds:[{type,r,ox,oz,W,D,H,y,x0..z1,door,job,work,beds:[{at,f}],animals,mid,station}], roads:[{d,L,hw}],
  gates:[{key,d,stone,name,cells:[[x,y,z]...],center,h0,inX,inZ,hw,span,cx,cz,posts,lever}], wall, waySpot}`.
  Beds are homes (folk[i].home), jobs from the hall (elder, innkeeper), stalls (merchant, baker) and workplaces.
  Gates are built shut (`gate_barrier` in their cells) — towns3.js opens them at dawn (gameplay).
- dungeon: `{theme, ent:[x,z], surf, boss:{at,type}, spawners}`; deep_halls: `floors[{f,y,doors[{kind,x0..,key}]}],
  traps[{plates,shooters,room}], captain, keyroom[{at,key,mural}], puzzle{levers,combo,door}, ladders, boss, vault2, shaft`;
  twin_vault: `plates, door, vdoor, wardens, vault, arena, shaft`; stronghold: `spawners, portal:[x,y,z], top`;
  dragon_lair: `{type, egg:[x,y,z], at}`; end_city: `{roofY, ship, dir, floors}`.
- Block entities (the original's JSON): chest `{type:'chest', items:[27 × null|{id,n}], mimic?}`, lectern
  `{type:'lectern', book:true, lore}`, pile `{type:'pile', items:[4], rot:[4]}`, shelf `{type:'shelf', items}`,
  bookcase `{type:'bookcase', items:[6]}`, frame `{type:'frame', item:{id,n}, rot}`, sign `{type:'sign', lines:[4]}`.
- `villageStartPoint(wg, pos, yaw)`: villages.js placeAtSpawn (used by client/session.cpp for a new world's spawn).
- `/locate <kind>` (op): the nearest of a kind in the player's band. `DQ_SPAWN_AT="x,y,z" | "<kind>" | "<kind>!"`
  spawns the player there (tests).

## Left for later
- Runtime systems of these places belong to other areas: residents/bosses/cages ticking (creatures), gates
  opening/closing, dart traps, keys and ward stones, the lever puzzle (gameplay), pirate raids and treasure maps
  (seas.js), villagers' lives (villagers).
- The exact-block comparison covers one structure per kind for one seed; more seeds: pass them to struct_compare.py.
