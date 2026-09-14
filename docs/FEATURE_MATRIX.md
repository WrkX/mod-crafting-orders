# Feature Matrix

Source: [WoWGreymane/mod-crafting-Orders](https://github.com/WoWGreymane/mod-crafting-Orders)
Reviewed revision: `81e1dc45e6750fd9ebe623a68a18486f47f76548`

| Upstream feature | Status | Notes |
|---|---|---|
| Blacksmithing orders | Retained | Artisan/300 cap |
| Leatherworking orders | Retained | Artisan/300 cap |
| Alchemy orders | Retained | Artisan/300 cap |
| Tailoring orders | Retained | Artisan/300 cap |
| Engineering orders | Retained | Artisan/300 cap |
| Enchanting orders | Retained | Create-item formulas and direct item enchants |
| Jewelcrafting orders | Adapted | Turtle skill 755, capped at 300 |
| Disenchanting service | Retained | Uses local `LootTemplates_Disenchant` |
| Recipe / formula hand-ins | Adapted | Store and re-resolve item GUIDs before consume |
| Configurable fees | Adapted | Turtle tiers only; nonzero minimum fee for unsellable outputs |
| Character-wide cooldowns | Adapted | Explicit `scopeType`/`scopeId` rows |
| Account-wide cooldowns | Adapted | Do not depend on a surviving character row |
| Inscription orders | Deferred | Fork has no complete Inscription implementation |
| Milling | Deferred | No native milling loot store |
| Prospecting | Deferred | Spell effect unused; no native prospecting loot store |
| Master / Grand Master tiers | Deferred | Not present on this client/core |
| Glyph base prices | Deferred | Glyphs are a later-expansion item class |
| `SkillLine.dbc` / `patch-I.MPQ` | Deferred | Must not overwrite Turtle client data |
| Existing profession-trainer gossip | Adapted | Detected from trainer data at runtime; crafting is capped at that trainer's profession rank; no DB binding or dedicated NPC required |
| Automatic city/Outland/Northrend spawns | Deferred | No module creature spawns are created; eligible existing trainers are augmented |
| WotLK addon (interface 30300) | Adapted | Rewritten for Vanilla/Turtle 1.12 |
| Addon message protocol | Adapted | Versioned, session-bound, chunked, untrusted input |

Profession skill ranks stop at Artisan (1–300). Recipes that require skill above 300 are excluded.
