-- Additive, restart-safe world data for mod-crafting-orders.
-- Does not DROP, DELETE BETWEEN, or spawn world creatures.

CREATE TABLE IF NOT EXISTS `crafting_order_npc` (
  `entry` MEDIUMINT UNSIGNED NOT NULL,
  `professionId` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `service` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110001, 1288, 'Crafting Guild Associate', 'Crafting Orders: Blacksmithing', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110001);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110002, 4259, 'Crafting Guild Associate', 'Crafting Orders: Blacksmithing', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110002);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110003, 1288, 'Crafting Guild Associate', 'Crafting Orders: Blacksmithing', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110003);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110004, 1290, 'Crafting Guild Associate', 'Crafting Orders: Leatherworking', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110004);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110005, 4305, 'Crafting Guild Associate', 'Crafting Orders: Leatherworking', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110005);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110006, 1290, 'Crafting Guild Associate', 'Crafting Orders: Leatherworking', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110006);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110007, 5048, 'Crafting Guild Associate', 'Crafting Orders: Alchemy', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110007);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110008, 3533, 'Crafting Guild Associate', 'Crafting Orders: Alchemy', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110008);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110009, 5048, 'Crafting Guild Associate', 'Crafting Orders: Alchemy', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110009);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110010, 1290, 'Crafting Guild Associate', 'Crafting Orders: Tailoring', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110010);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110011, 3533, 'Crafting Guild Associate', 'Crafting Orders: Tailoring', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110011);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110012, 1290, 'Crafting Guild Associate', 'Crafting Orders: Tailoring', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110012);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110013, 4285, 'Crafting Guild Associate', 'Crafting Orders: Engineering', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110013);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110014, 4876, 'Crafting Guild Associate', 'Crafting Orders: Engineering', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110014);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110015, 4285, 'Crafting Guild Associate', 'Crafting Orders: Engineering', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110015);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110016, 2203, 'Crafting Guild Associate', 'Crafting Orders: Enchanting', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order_enchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110016);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110017, 3533, 'Crafting Guild Associate', 'Crafting Orders: Enchanting', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order_enchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110017);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110018, 2203, 'Crafting Guild Associate', 'Crafting Orders: Enchanting', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order_enchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110018);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110019, 1288, 'Crafting Guild Associate', 'Crafting Orders: Jewelcrafting', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110019);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110020, 4259, 'Crafting Guild Associate', 'Crafting Orders: Jewelcrafting', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110020);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110021, 1288, 'Crafting Guild Associate', 'Crafting Orders: Jewelcrafting', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110021);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110022, 2203, 'Crafting Guild Associate', 'Crafting Orders: Disenchanting', 60, 60, 3052, 3052, 11, 1, 1, 7, 'crafting_order_disenchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110022);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110023, 3533, 'Crafting Guild Associate', 'Crafting Orders: Disenchanting', 60, 60, 3052, 3052, 85, 1, 1, 7, 'crafting_order_disenchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110023);

INSERT INTO `creature_template`
(`entry`, `display_id1`, `name`, `subname`, `level_min`, `level_max`, `health_min`, `health_max`, `faction`, `npc_flags`, `unit_class`, `type`, `script_name`)
SELECT 5110024, 2203, 'Crafting Guild Associate', 'Crafting Orders: Disenchanting', 60, 60, 3052, 3052, 35, 1, 1, 7, 'crafting_order_disenchant'
FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM `creature_template` WHERE `entry` = 5110024);

INSERT IGNORE INTO `crafting_order_npc` (`entry`, `professionId`, `service`) VALUES
(5110001, 164, 1),
(5110002, 164, 1),
(5110003, 164, 1),
(5110004, 165, 1),
(5110005, 165, 1),
(5110006, 165, 1),
(5110007, 171, 1),
(5110008, 171, 1),
(5110009, 171, 1),
(5110010, 197, 1),
(5110011, 197, 1),
(5110012, 197, 1),
(5110013, 202, 1),
(5110014, 202, 1),
(5110015, 202, 1),
(5110016, 333, 2),
(5110017, 333, 2),
(5110018, 333, 2),
(5110019, 755, 1),
(5110020, 755, 1),
(5110021, 755, 1),
(5110022, 333, 3),
(5110023, 333, 3),
(5110024, 333, 3);
