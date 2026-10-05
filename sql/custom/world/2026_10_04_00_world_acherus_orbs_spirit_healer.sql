-- Battle for Acherus: spirit healer resurrection spells
-- SpellInfo::CheckLocation (SpellInfo.cpp) restricts 2584, 22012 and 44535 to battlegrounds and Wintergrasp.
-- Acherus: The Ebon Hold is map 609, area 4342, so they are allowed there through spell_area.
DELETE FROM `spell_area` WHERE `spell` IN (2584, 22012, 44535) AND `area` = 4342;
INSERT INTO `spell_area` (`spell`, `area`) VALUES
(2584,  4342), -- Waiting to Resurrect, on the ghost while it waits at the guide
(22012, 4342), -- Spirit Heal, cast on the guide for the revive visual
(44535, 4342); -- Spirit Heal, on the player after the resurrection
