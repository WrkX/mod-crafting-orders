-- Additive, restart-safe world data for mod-crafting-orders.
--
-- Existing profession trainers are discovered by the module at runtime. A
-- new installation therefore needs no creature_template, creature, gossip,
-- or script_name rows for this module. Keep this table definition for
-- upgrades from releases that used dedicated crafting-order NPCs; the loader
-- treats rows in it as legacy bindings. The table is optional for runtime
-- trainer discovery; no rows are needed for a new installation.

CREATE TABLE IF NOT EXISTS `crafting_order_npc` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `professionId` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `service` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Deliberately no INSERTs. Earlier versions inserted reserved creature
-- templates and legacy bindings here. They are not needed on fresh
-- installations and must remain untouched on upgrades.
