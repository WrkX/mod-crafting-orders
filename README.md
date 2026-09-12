# mod-crafting-orders

Turtle/Vanilla crafting orders for this Tortoise fork. The module augments
existing profession trainers at runtime, so players can hand a trainer
materials and a gold fee; the server crafts, enchants, or disenchants without
the player knowing the recipe.

Ported from [WoWGreymane/mod-crafting-Orders](https://github.com/WoWGreymane/mod-crafting-Orders)
revision `81e1dc45e6750fd9ebe623a68a18486f47f76548`. See
`docs/PORTING_PLAN.md` and `docs/FEATURE_MATRIX.md`.

This module does not change core sources and does not ship replacement DBC or
MPQ files.

## Supported services

- Blacksmithing, Leatherworking, Alchemy, Tailoring, Engineering
- Enchanting (crafted formulas and direct item enchants)
- Turtle Jewelcrafting (skill 755, skill cap 300)
- Disenchanting
- Recipe and formula hand-ins

Inscription, milling, and prospecting are deferred.

## Build

Enable the module in CMake (`MODULES=static` is the supported first-release
mode) and rebuild. The native loader discovers `modules/mod-crafting-orders/src`.

On this VPS, compile single-threaded:

```sh
nice -n 15 ionice -c 3 cmake --build build -- -j1
```

Standalone domain tests:

```sh
./t/run_tests.sh
```

## Configuration

Copied from `conf/mod_crafting_orders.conf.dist` into the server `modules/`
config directory. Keep `CraftingOrders.Enable = 0` until the character
migration has been applied on a staging copy. The world migration is optional
and only provides compatibility storage for legacy dedicated-NPC bindings.

Permit the module in `Database.AutoUpdate.AllowedModules` (or use `all`) so
the character migration runs through the auto-updater. The world migration is
optional and only needs to be included when retaining legacy binding storage.

## Existing profession trainers

With `CraftingOrders.TrainerGossip.Enable = 1`, the module discovers existing
profession trainers from their trainer data and adds the appropriate gossip
options without changing `creature_template`, `creature`, `npc_trainer`, or
`script_name` rows:

- Blacksmithing, Leatherworking, Alchemy, Tailoring, Engineering, and
  Jewelcrafting trainers offer crafting and recipe hand-ins.
- Enchanting trainers offer crafting/direct enchanting, formula hand-ins, and
  disenchanting when `CraftingOrders.TrainerGossip.EnchantingDisenchant = 1`.

The server still validates the profession and service for every opened session
and request. A creature without recognized trade-skill trainer data is left
unchanged; recipe availability is validated when the list or hand-in is
requested. Disable `CraftingOrders.TrainerGossip.Enable` to keep the normal
trainer gossip only.

## Database

Migrations are additive and restart-safe:

- `data/sql/world/0001_crafting_orders_world.sql`
- `data/sql/character/0001_crafting_orders_character.sql`

The optional world migration creates the `crafting_order_npc` compatibility
table for older releases, but inserts no creature templates, spawns, gossip,
or script bindings. New installations need no World-DB change, dedicated NPC
data, or binding rows for trainer gossip. Apply it only when retaining the
legacy binding table is useful. The character migration creates the
recipe-unlock and cooldown tables and is required for those services.

Existing installations may still have the old reserved templates and binding
rows. They are legacy data and are intentionally neither deleted nor rewritten
by an upgrade; leave them in place until they have been audited separately.

Uninstall statements live in `data/sql/uninstall/` and are **not** auto-applied.
Do not drop unlock or cooldown tables during rollback.

## NPC placement

No module NPCs need to be spawned. Enable the module after the character
migration and use any supported in-world profession trainer. The optional
world migration is only relevant to legacy bindings. The legacy
dedicated entries from older releases remain documented in
`docs/ID_ALLOCATION.md` solely for upgrade and rollback safety.

## Addon

Install `addon/CraftingOrders/` into the client's `Interface/AddOns/` directory.
Interface version is 11200. The addon is the presentation layer only; the server
revalidates profession, spell, item, price, bags, and distance on every request.

The UI supports multi-page lists, makeable/tier filters, selectable quantities,
and confirmation before replacing a permanent enchant. Client requests use a
Vanilla-compatible guild addon packet that the Tortoise packet hook consumes
before normal guild routing.

## Rollback

1. Set `CraftingOrders.Enable = 0` (and optionally
   `CraftingOrders.TrainerGossip.Enable = 0`).
2. Leave legacy creature templates/bindings and the unlock/cooldown tables in
   place; the module ships no destructive world uninstall SQL.
3. Disable or delete the client addon. No DBC/MPQ rollback is required.
