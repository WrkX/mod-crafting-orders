# mod-crafting-orders

Turtle/Vanilla crafting-order NPCs for this Tortoise fork. Players hand an NPC
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
config directory. Keep `CraftingOrders.Enable = 0` until migrations have been
applied on a staging copy.

Permit the module in `Database.AutoUpdate.AllowedModules` (or use `all`) so
`data/sql/world/` and `data/sql/character/` run through the auto-updater.

## Database

Migrations are additive and restart-safe:

- `data/sql/world/0001_crafting_orders_world.sql`
- `data/sql/character/0001_crafting_orders_character.sql`

They create mapping/unlock/cooldown tables and insert creature templates only
when those entries are free. Occupied IDs are left untouched; startup logs a
diagnostic and disables the colliding NPC.

Uninstall statements live in `data/sql/uninstall/` and are **not** auto-applied.
Do not drop unlock or cooldown tables during rollback.

## NPC placement

Templates are shipped without world spawns. After the world migration:

```
.npc add 5110001
```

See `docs/ID_ALLOCATION.md` for the full entry list. Place one NPC per service
you want live, then enable `CraftingOrders.Enable`.

## Addon

Install `addon/CraftingOrders/` into the client's `Interface/AddOns/` directory.
Interface version is 11200. The addon is the presentation layer only; the server
revalidates profession, spell, item, price, bags, and distance on every request.

The UI supports multi-page lists, makeable/tier filters, selectable quantities,
and confirmation before replacing a permanent enchant. Client requests use a
Vanilla-compatible guild addon packet that the Tortoise packet hook consumes
before normal guild routing.

## Rollback

1. Set `CraftingOrders.Enable = 0`.
2. Remove optional NPC spawns.
3. Leave unlock and cooldown tables in place.
4. Disable or delete the client addon. No DBC/MPQ rollback is required.
