# ID Allocation

Reserved numeric ranges for this module. Do not reuse them for unrelated content.

Startup collision detection refuses to overwrite an occupied creature template, gossip
menu, or npc_text row that does not already belong to this module.

## Creature templates

Range: **5110001–5110024** (`mediumint` creature `entry`).

No automatic `creature` spawn rows are shipped. Administrators place these
templates with `.npc add` or a local spawn SQL file kept outside
`data/sql/world/`.

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

Gossip menus are built in script. Greeting text reuses stock npc_text **1**
so the module does not insert `broadcast_text` / `npc_text` rows.

## Character tables

Module-owned character tables (not numeric IDs):

- `crafting_order_recipes`
- `crafting_order_cooldowns`

## Administrators mapping existing NPCs

To attach the service to an existing creature, insert a
`crafting_order_npc` row and set that template's `script_name` to the matching
script above. Do not reuse entries 5110001–5110024 for unrelated NPCs.
