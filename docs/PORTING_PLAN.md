# Crafting Orders Module Porting Plan

## Status

- Target repository: Tortoise/Turtle WoW fork
- Target module: `mod-crafting-orders`
- Source module: [WoWGreymane/mod-crafting-Orders](https://github.com/WoWGreymane/mod-crafting-Orders)
- Reviewed source revision: `81e1dc45e6750fd9ebe623a68a18486f47f76548`
- Target branch: `codex/port-crafting-orders`
- Plan state: implementation in progress (phases 0–7 scaffolded)

## Objective

Port the AzerothCore crafting-orders module into a self-contained module for
this fork. The first release must not require core source changes or replacement
client DBC/MPQ files. It must preserve player items and currency when an order
fails, and it must treat every addon command as untrusted input.

## Recommended first-release scope

Include:

- Blacksmithing
- Leatherworking
- Alchemy
- Tailoring
- Engineering
- Enchanting orders and direct enchanting
- Turtle WoW Jewelcrafting (skill 755, capped at 300)
- Disenchanting
- Recipe and formula hand-ins
- Configurable fees
- Character-wide or account-wide recipe cooldowns
- A Turtle/Vanilla 1.12-compatible addon

Defer:

- Inscription and milling. This fork does not contain a complete Inscription
  implementation.
- Prospecting. The spell effect is declared but unused, and there is no native
  prospecting loot store. It can be added later using reviewed, module-owned
  loot data.
- The upstream `SkillLine.dbc` and `patch-I.MPQ`. These can overwrite or conflict
  with Turtle-specific client data and must not be distributed by the module.
- Northrend/Outland NPC spawns and level-80 profession assumptions.

## Target structure

```text
modules/mod-crafting-orders/
|-- src/
|   |-- CraftingOrders.cpp
|   |-- CraftingOrders.h
|   |-- CraftingOrdersConfig.cpp
|   |-- CraftingOrdersRepository.cpp
|   |-- CraftingOrdersProtocol.cpp
|   |-- CraftingOrdersScripts.cpp
|   `-- loader.cpp
|-- conf/
|   `-- mod_crafting_orders.conf.dist
|-- data/sql/
|   |-- world/
|   `-- character/
|-- addon/
|   `-- CraftingOrders/
|-- docs/
|   `-- PORTING_PLAN.md
`-- README.md
```

The native module loader discovers modules below `modules/` and generates the
normal static/dynamic build integration. Do not edit the core source list unless
a verified build limitation makes that necessary.

## Compatibility changes

The port must use this fork's APIs rather than carrying AzerothCore interfaces
into the core.

| AzerothCore API or behavior | Fork equivalent or required change |
|---|---|
| `SpellInfo` and effect objects | `SpellEntry` and its effect arrays |
| `sSpellMgr->GetSpellInfo()` | `sSpellMgr.GetSpellEntry()` |
| `ItemTemplate` | `ItemPrototype` |
| `sObjectMgr->GetItemTemplate()` | `sObjectMgr.GetItemPrototype()` |
| `sConfigMgr->GetOption()` | `sConfig.Get*Default()` |
| Formatted `Query`/`Execute` calls | `PQuery`, `PExecute`, and owned `QueryResult` |
| `Field::Get<T>()` | Typed getters such as `GetUInt32()` |
| Modern gossip helpers | `Player::ADD_GOSSIP_ITEM`, `SEND_GOSSIP_MENU`, etc. |
| `GetGUID().GetCounter()` | `GetGUIDLow()` or `GetObjectGuid()` as appropriate |
| `GameTime::GetGameTime()` | The fork's game time or `time(nullptr)` |
| Five-argument `SetEnchantment` | The fork's four-argument form |
| C++20 container `.contains()` | C++17 `find()` checks |
| Modern logging macros | Native `sLog` methods or a private module adapter |

Use a small, module-private compatibility adapter only where it genuinely keeps
the domain code clearer. Do not depend on compatibility headers belonging to an
unrelated module.

## Implementation phases

### Phase 0: Freeze behavior and data ownership

1. Record the exact upstream revision in the module README.
2. Create a feature matrix marking every upstream feature as retained, adapted,
   or deferred.
3. Confirm that profession tiers stop at Artisan/300.
4. Decide which NPC templates are included and how administrators place them.
   The recommended default is to ship templates/mappings without automatic
   world spawns.
5. Reserve collision-free creature entries, module string IDs, and any other
   module-owned numeric ranges after checking the target database.

Exit gate: the feature matrix and ID allocation are reviewed before SQL is
written.

### Phase 1: Create the minimal loadable module

1. Add `loader.cpp` with `Addmod_crafting_ordersScripts()`.
2. Add a configuration file under `conf/` using the server's normal section and
   `sConfig` access patterns.
3. Register only the hooks the module uses:
   - startup and elapsed-time update hooks;
   - creature gossip hooks;
   - the packet receive hook used for addon messages.
4. Add a module enable flag and clear startup logging.
5. Compile and boot with the module enabled before porting gameplay logic.

Exit gate: a no-op module builds, loads, logs its state, and cleanly disables.

### Phase 2: Port configuration and domain logic

1. Separate fee calculation, profession tiers, cooldown policy, and recipe
   eligibility from script hooks and database code.
2. Replace Master/Grand Master and level-80 assumptions with Turtle-supported
   data.
3. Preserve configurable fee overrides and add a nonzero minimum-fee policy for
   items with no vendor price.
4. Use checked arithmetic for quantities, reagent totals, output totals, and
   fees.
5. Add explicit recipe allow/deny overrides. Do not assume every row in
   `npc_trainer` is safe to expose.

Exit gate: pricing, tier selection, quantity bounds, and overflow behavior have
focused tests.

### Phase 3: Rewrite database integration

1. Place versioned updates in `data/sql/world/` and `data/sql/character/`.
2. Translate upstream schema assumptions to local column names, including:
   - `npc_trainer.spell`;
   - `npc_trainer.reqskill`;
   - `npc_trainer.reqskillvalue`;
   - `npc_trainer.reqlevel`;
   - the local `creature_template` and `creature` layouts.
3. Use additive, restart-safe migrations:
   - `CREATE TABLE IF NOT EXISTS`;
   - targeted inserts or updates;
   - versioned alterations when a schema evolves.
4. Never run `DROP TABLE`, broad `DELETE ... BETWEEN`, or range-based cleanup in
   an automatic update.
5. Keep uninstall/drop statements out of the auto-update directories.
6. Store cooldown scope explicitly so account-wide cooldowns do not depend on a
   surviving character row.
7. Detect occupied NPC/string identifiers and fail with a clear diagnostic
   instead of overwriting unrelated data.

Exit gate: migrations succeed against disposable copies of the world and
character databases, and a second startup makes no destructive changes.

### Phase 4: Recipe discovery and cache

1. Read recipes from the local trainer and spell data.
2. Support the five generic crafting professions, Enchanting, and Turtle
   Jewelcrafting.
3. Validate that each spell has a supported create-item or enchant effect.
4. Derive output count from local spell effects instead of hard-coding one item.
5. Exclude unsupported, triggered, test, or deny-listed recipes.
6. Refresh caches using elapsed milliseconds, not a count of world update calls.
7. If required tables or data are unavailable, disable the affected service and
   log the exact cause rather than starting in a partially valid state.

Exit gate: startup reports recipe counts by profession and representative
recipes have verified reagents, output, required skill, and fee.

### Phase 5: Implement an authenticated interaction protocol

Treat the addon as an untrusted presentation layer.

1. Opening an eligible NPC creates a short-lived server session tied to:
   - player GUID;
   - creature GUID;
   - map and distance;
   - selected profession/service;
   - creation and expiration time.
2. Revalidate that session for every state-changing request.
3. Never trust client-supplied professions, spell/enchant IDs, item entries,
   prices, or bag/slot ownership.
4. Use bounded, non-throwing parsing with limits on message size, field count,
   quantity, and numeric values.
5. Add per-player rate limits, a busy flag, request IDs, and replay rejection.
6. Chunk large responses on record boundaries and include:
   - protocol version;
   - request ID;
   - current part;
   - total parts.
7. Escape delimiter-bearing text or use a length-prefixed representation.
8. Remove interaction state on gossip close, logout, excessive distance, map
   change, and expiry.

Exit gate: commands sent without a live nearby NPC session cannot list private
state or mutate character state.

### Phase 6: Make operations transaction-safe

Every operation follows a preflight-then-commit design.

Preflight must:

1. Resolve current item GUIDs and validate ownership.
2. Recheck recipe authorization, cooldown, service, NPC, and distance.
3. Calculate complete reagent, fee, and output totals using checked arithmetic.
4. Validate all materials and money for the entire requested quantity.
5. Generate the complete result set and reserve inventory capacity.
6. Validate enchant target restrictions or disenchant eligibility.

Only after the full preflight succeeds may the module:

1. consume reagents or the hand-in item;
2. charge money;
3. grant outputs or apply the enchant;
4. persist the cooldown/unlock;
5. return a success response.

Additional rules:

- Store recipe hand-in item GUIDs, not merely item entries, and re-resolve them
  immediately before consumption.
- Define explicit behavior for permanent versus temporary enchants and replacing
  an existing enchant.
- Respect binding, quest-item, non-disenchantable, and item-level restrictions.
- Never destroy or charge before inventory output is known to be deliverable.
- Log any unexpected failure during commit with enough context for recovery,
  without logging sensitive account data.

Exit gate: every expected failure leaves money, materials, targets, cooldowns,
and unlocks unchanged.

### Phase 7: Rewrite the addon for the 1.12 client

The upstream addon targets interface 30300 and cannot be copied unchanged.

1. Create a standalone Vanilla/Turtle addon under `addon/CraftingOrders/`.
2. Convert event handlers from `self, event, ...` to the 1.12 `this`, `event`,
   and `arg1...` model.
3. Replace unsupported Lua constructs such as `#table`, modern varargs usage,
   and unavailable string functions with Lua 5.0-compatible forms.
4. Convert XML handlers using `self` or `button` to 1.12-compatible globals.
5. Remove the `Blizzard_TrainerUI` dependency unless it is verified in the
   target client and genuinely required.
6. Implement versioned chunk reassembly and reject mismatched server protocol
   versions with a visible error.
7. Keep addon installation separate from server installation and document its
   client path. Do not package a DBC or MPQ override.

Exit gate: the addon loads with script errors enabled, opens/closes repeatedly,
reassembles the largest recipe list, and survives relogging without Lua errors.

### Phase 8: Validation and release

Run the following end-to-end cases:

- One normal and one multi-output recipe per supported profession
- Minimum and maximum allowed order quantities
- Missing reagents, money, and inventory capacity
- Character-wide and account-wide cooldowns across relog and server restart
- Cooldowns involving alternate and deleted characters
- Recipe hand-ins where an item is moved, consumed, or replaced after preview
- Direct enchants on equipped and bagged items
- Incompatible targets and existing permanent/temporary enchants
- Disenchanting valid, invalid, bound, quest, and non-disenchantable items
- Full bags during crafting and disenchanting
- Duplicate, replayed, malformed, truncated, and oversized addon packets
- Commands sent remotely or after walking away from the NPC
- Addon protocol-version mismatch
- Large recipe lists and out-of-order/missing response chunks
- Module disable/re-enable and repeated database updates

Perform smoke testing on copies of live-schema databases. Record relevant row
counts and character money/item state before and after destructive-path tests.

## Build strategy for this host

This VPS is memory constrained. All validation builds must be single-threaded
and run at low CPU/I/O priority. Use a build-local compiler launcher that appends
`-O0 -g0` after the project's hard-coded optimization flags for compile
validation.

Configure the relevant module mode and build with:

```sh
nice -n 15 ionice -c 3 cmake --build build -- -j1
```

Validate static mode first. Validate dynamic mode as a separate gate only if the
module will advertise dynamic-module support. Do not run `-j2` or higher on this
host.

## Rollout and rollback

1. Back up the world and character databases.
2. Install the server module with its feature flag disabled.
3. Permit the module in `Database.AutoUpdate.AllowedModules` and run migrations
   on a staging copy first.
4. Enable one crafting NPC/service at a time and monitor errors and economic
   results.
5. Distribute the matching addon only after the protocol version is frozen.
6. Roll back by disabling the module and removing optional NPC spawns. Preserve
   unlock and cooldown tables for recovery; do not drop them automatically.
7. Client rollback consists only of disabling/removing the addon because the
   module does not ship replacement DBC or MPQ files.

## Completion criteria

The first release is complete when:

- The module builds and boots in its supported module modes.
- It requires no core source modification and no client DBC/MPQ replacement.
- Database migrations are additive, collision-aware, and idempotent.
- Supported recipes are derived correctly from local data.
- Every state-changing request requires a valid, nearby NPC interaction.
- Client-controlled identifiers cannot select unauthorized recipes, targets, or
  prices.
- Expected failures consume no materials, money, targets, cooldowns, or unlocks.
- All supported professions, including Turtle Jewelcrafting, pass end-to-end
  tests.
- The addon runs without Lua errors in the actual Turtle 1.12 client.
- Configuration, installation, addon deployment, NPC placement, and rollback
  are documented in the module README.

## Estimated effort

The recommended first release is approximately 7-12 focused development days,
including the server adaptation, 1.12 addon rewrite, and validation. Prospecting
with reviewed module-owned loot data is a separate feature. Full parity involving
Inscription or milling would require additional core/data design and should not
be included implicitly in the initial estimate.
