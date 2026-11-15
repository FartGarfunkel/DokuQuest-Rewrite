# Tactile Blocks, brought in whole

The user's ask: every Tactile Blocks station as Tactile has it (its model, the items where Tactile puts them, its
interactions, its animations), worked in our own immersive first person (no scene, no screen). The reference is the
decompiled mod (CFR, scratch only, never committed) and its models/textures, imported locally and git-ignored
(tools/port/tactile_import.py -> data/local_models/tactile). Our own simulation (smelting, brewing, enchanting...)
stays; Tactile's layout, zones and handling go over it. Sneak + use keeps our scene; Options -> Tactile Stations off
keeps our own blocks.

## Shared base
- [x] importer: every Tactile block (model + its parts: doors, lids, wheel, arm; textures; blockstate turns)
- [x] local models for Model-render blocks too (anvil, grindstone, brewing stand, enchanting table, lectern), not only cubes
- [x] render kit (render/tactile_kit.h): Minecraft's PoseStack in block space, the item "FIXED" draw (items a block wide, blocks half), the
      facing turn (north 0, east -90, south 180, west 90), animated parts (a door, a lid, a wheel) about their hinges
- [x] interaction kit (sim/tactile.h): the hit point in the block's north frame (worldToNorthLocal), zones -> slots, put/take

## Blocks (Tactile -> ours)
- [x] table_de_forge -> smithing table: base/template/material zones (x<0.3 material, >0.7 template), Tactile spots and
      tilts, forge hammer 3 blows (0.5 s apart, a level), result left on the table, preview at the back (armour worn)
- [ ] smithing templates: the cinderite upgrade (diamond -> cinderite), the 18 armour trims; trims drawn on worn armour
- [x] haut_fourneau -> blast furnace: the door, four raw spots on the crucible, the mould and its ingot, loose fuel
- [x] four -> furnace, fumoir -> smoker (ours light themselves: no flint and steel needed; one stack a slot, shown as Tactile's 16-a-spot): their chambers, food on the grill spots, fuel
- [x] meule -> grindstone: the round wheel turning, grinding held to it (repair for experience, wear; ten seconds to disenchant)
- [ ] the grindstone's first-person pose (MeuleFirstPersonAnimator: the piece held to the wheel)
- [x] enclume -> anvil (work + material on its face, hammer blows, the result left on it)
- [ ] armour trims shown on worn armour (every armour, the procedural ones too)
- [x] coffre -> chest (lid on its inner hinge, tray of 15 + heap, buttons sort/swap, the item looked at written inside the lid; a large chest stays ours)
- [ ] coffre_ender -> ender chest
- [ ] (was) coffre / coffre_ender -> chests: the lid (kept off the wall behind), a note of paper on the lid naming it, the
      contents in Tactile's layers (large items, stacks)
- [x] cartographe -> cartography table (ink, quill, board, pile, compass; draw, copy, enlarge with 8 blanks or paper)
- [x] alambic -> brewing stand (rack of our 3D bottles, dome ingredient, burner powder, water while brewing; the recipe book not yet)
- [ ] (rest) -> brewing stand, enclume -> anvil, table_enchantement -> enchanting
      table, jukebox, bibliotheque -> bookshelf, pupitre -> lectern, etabli_gravure -> engraving
