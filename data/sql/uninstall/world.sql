-- Manual uninstall only. Do not place this file under data/sql/world/.
-- Preserve unlock/cooldown character tables unless an administrator
-- explicitly wants a full wipe.

DELETE FROM `crafting_order_npc` WHERE `entry` BETWEEN 5110001 AND 5110024;
DELETE FROM `creature_template` WHERE `entry` BETWEEN 5110001 AND 5110024
  AND `script_name` IN ('crafting_order', 'crafting_order_enchant', 'crafting_order_disenchant');
