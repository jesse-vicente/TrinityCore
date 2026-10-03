-- Battle for Acherus: position to return players to after a crash or logout during a match
CREATE TABLE IF NOT EXISTS `custom_acherus_orbs_return` (
  `guid` INT UNSIGNED NOT NULL,
  `map` SMALLINT UNSIGNED NOT NULL,
  `position_x` FLOAT NOT NULL,
  `position_y` FLOAT NOT NULL,
  `position_z` FLOAT NOT NULL,
  `orientation` FLOAT NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Battle for Acherus return positions';
