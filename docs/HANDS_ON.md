# Hands-on Stations

The goal: every hands-on station with its own model, the items set where they belong on it, its interactions and
its animations, worked in our own immersive first person (no scene, no screen). The station models and textures are
imported locally and git-ignored (tools/local/station_import.py -> data/local_models/hands). Our own simulation
(smelting, brewing, enchanting...) stays; the hands-on layout, zones and handling go over it. Sneak + use keeps our
scene; Options -> Hands-on Stations off keeps our own blocks.

## Shared base
- [x] importer: every hands-on block (model + its parts: doors, lids, wheel, arm; textures; blockstate turns)
- [x] local models for Model-render blocks too (anvil, grindstone, brewing stand, enchanting table, lectern), not only cubes
- [x] render kit (render/hands_kit.h): a transform stack in block space, the item "FIXED" draw (items a block wide, blocks half), the
      facing turn (north 0, east -90, south 180, west 90), animated parts (a door, a lid, a wheel) about their hinges
- [x] interaction kit (sim/hands.h): the hit point in the block's north frame (worldToNorthLocal), zones -> slots, put/take

## Blocks (imported model name -> ours)
- [x] table_de_forge -> smithing table (three spots, hammer blows, templates, trims shown on worn armour)
- [x] haut_fourneau, four, fumoir -> blast furnace, furnace, smoker (four spots each, doors, the melt down the trough)
- [x] meule -> grindstone (round wheel, grinding held to it, your arms holding the piece)
- [x] enclume -> anvil (work + material, hammer blows, result left on it)
- [x] chest -> every chest (plain, painted, ender, party; each its own pictures; lid, buttons, tray, heap, note, outline)
- [x] barrels, large chests, cabinets: the same open-by-hand put/take (our own models)
- [x] cartographe -> cartography table (ink, quill, board, pile, compass)
- [x] alambic -> brewing stand (rack of our 3D bottles, dome, burner, water, bubbles, recipe book of our brews)
- [x] jukebox (platter, needle arm)
- [x] table_enchantement -> enchanting table: its model, the socket and the lapis reservoir by hand; our offers kept
- [ ] (not ported, on purpose) a separate enchanting system (feeding XP into the table, nether-star antennas, the grimoire's
      runes and knowledge books): it would replace our enchanting, not show it
- [ ] (not ported, on purpose) bibliotheque: a four-book shelf; our bookshelf is a full shelf that powers enchanting, and
      our chiseled bookshelf already takes books one at a time
- [ ] pupitre: a plain lectern and book, which ours already is
- [ ] etabli_gravure (the engraving bench, renaming): a new block of ours, and typing a name needs the on-screen keyboard
      for controllers first (the controller audit)
