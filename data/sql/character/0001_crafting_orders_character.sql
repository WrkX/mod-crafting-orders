-- Additive character schema for mod-crafting-orders.
-- Cooldown scope is stored explicitly so account-wide cooldowns survive
-- character deletion.

CREATE TABLE IF NOT EXISTS `crafting_order_recipes` (
  `accountId` INT UNSIGNED NOT NULL,
  `professionId` SMALLINT UNSIGNED NOT NULL,
  `spellId` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`accountId`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `crafting_order_cooldowns` (
  `scopeType` TINYINT UNSIGNED NOT NULL,
  `scopeId` INT UNSIGNED NOT NULL,
  `spellId` INT UNSIGNED NOT NULL,
  `cooldownEndTime` BIGINT UNSIGNED NOT NULL,
  PRIMARY KEY (`scopeType`, `scopeId`, `spellId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
