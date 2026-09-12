# ID Allocation

No new numeric IDs are allocated for normal operation. Existing profession
trainers are augmented in memory and their existing creature/template, gossip,
and trainer rows are not rewritten.

The range below is retained as a legacy reservation for installations upgraded
from the first release. Do not reuse those entries for unrelated content while
they remain in a database.

## Creature templates

Legacy range: **5110001–5110024** (`mediumint` creature `entry`).

New migrations do not create these templates or any `creature` spawn rows.
Older installations may retain them; they are not removed during upgrade or
rollback.

| Entry | Faction | Service | `script_name` |
|---:|---|---|---|
| 5110001 | Alliance | Blacksmithing | `crafting_order` |
| 5110002 | Horde | Blacksmithing | `crafting_order` |
| 5110003 | Neutral | Blacksmithing | `crafting_order` |
| 5110004 | Alliance | Leatherworking | `crafting_order` |
| 5110005 | Horde | Leatherworking | `crafting_order` |
| 5110006 | Neutral | Leatherworking | `crafting_order` |
| 5110007 | Alliance | Alchemy | `crafting_order` |
| 5110008 | Horde | Alchemy | `crafting_order` |
| 5110009 | Neutral | Alchemy | `crafting_order` |
| 5110010 | Alliance | Tailoring | `crafting_order` |
| 5110011 | Horde | Tailoring | `crafting_order` |
| 5110012 | Neutral | Tailoring | `crafting_order` |
| 5110013 | Alliance | Engineering | `crafting_order` |
| 5110014 | Horde | Engineering | `crafting_order` |
| 5110015 | Neutral | Engineering | `crafting_order` |
| 5110016 | Alliance | Enchanting | `crafting_order_enchant` |
| 5110017 | Horde | Enchanting | `crafting_order_enchant` |
| 5110018 | Neutral | Enchanting | `crafting_order_enchant` |
| 5110019 | Alliance | Jewelcrafting | `crafting_order` |
| 5110020 | Horde | Jewelcrafting | `crafting_order` |
| 5110021 | Neutral | Jewelcrafting | `crafting_order` |
| 5110022 | Alliance | Disenchanting | `crafting_order_disenchant` |
| 5110023 | Horde | Disenchanting | `crafting_order_disenchant` |
| 5110024 | Neutral | Disenchanting | `crafting_order_disenchant` |

Faction templates used: `11` (Alliance), `85` (Horde), `35` (friendly/neutral).

Display IDs are Vanilla-safe humanoid models. Change them locally if a model
does not match the intended race.

## Gossip

Gossip menus are built in script and attached to eligible profession trainers at
runtime. Greeting text reuses stock npc_text **1**, so the module does not
insert `broadcast_text`, `npc_text`, gossip-menu, or gossip-option rows.

## Character tables

Module-owned character tables (not numeric IDs):

- `crafting_order_recipes`
- `crafting_order_cooldowns`

## Legacy administrator mappings

The `crafting_order_npc` table and the `script_name` values in the table above
are compatibility data for older releases only. New installations must not add
rows for ordinary profession trainers or change their `script_name`. If a
legacy dedicated NPC is still used, preserve its existing binding and entry;
the runtime trainer integration does not require it.
