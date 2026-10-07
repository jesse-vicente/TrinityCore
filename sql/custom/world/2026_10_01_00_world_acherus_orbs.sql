-- Battle for Acherus (custom orb battleground in phased copies of Acherus: The Ebon Hold)

-- Queue NPC, spawn it where needed with .npc add 990000
DELETE FROM `creature_template` WHERE `entry` = 990000;
INSERT INTO `creature_template` (`entry`, `modelid1`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `ScriptName`) VALUES
(990000, 27153, 'Ebon Blade Battlemaster', 'Battle for Acherus', 80, 80, 2, 35, 1, 1, 768, 7, 'npc_acherus_orbs_battlemaster');

-- Battlemaster gossip texts
DELETE FROM `npc_text` WHERE `ID` IN (990000, 990001);
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `BroadcastTextID0`, `lang0`, `Probability0`) VALUES
(990000, 'The Ebon Blade tests its champions in the halls of Acherus. Claim the orbs of Frost, Blood and Unholy at the runeforges and hold them for your faction, $N.', '', 0, 0, 1),
(990001, 'Ten champions of each faction fight inside Acherus.$B$BClaim an orb at a runeforge and hold it. Every 5 seconds your faction scores 6 points while you stand in the sunken center, 4 on the runeforge platform and 2 outside the hall.$B$BThe orb empowers its carrier: more damage dealt, much more damage taken and less healing received, growing every 15 seconds. If the carrier falls, the orb returns to its runeforge.$B$BThe first faction to reach 1600 points wins. After 25 minutes, the highest score wins.', '', 0, 0, 1);

-- Clickable copies of the Acherus runeforges, spawned per match by the script
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 990001 AND 990003;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `ScriptName`) VALUES
(990001, 10, 8175, 'Frost Runeforge',  '', '', '', 2.03, 'go_acherus_orbs_runeforge'),
(990002, 10, 8175, 'Blood Runeforge',  '', '', '', 2.03, 'go_acherus_orbs_runeforge'),
(990003, 10, 8175, 'Unholy Runeforge', '', '', '', 2.03, 'go_acherus_orbs_runeforge');

-- 990004 was the Berserk Buff, removed
DELETE FROM `gameobject_template` WHERE `entry` = 990004;

-- Invisible collision wall (display of 180322, CollisionWallPvP01), spawned as an octagon around each preparation dome
-- by the script. At size 0.8 each wall is ~8.9 yards wide, enough to close the octagon at 10 yards from the spawn.
DELETE FROM `gameobject_template` WHERE `entry` = 990005;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990005, 5, 6391, 'Preparation Wall', '', '', '', 0.8);

-- Pool of Blood scenery on the Blood forge: display of 194479 (tradeskill_fishschool_red), which is a fishing hole and
-- shows a tooltip and a mouseover highlight. As a generic object without a name it is scenery only.
DELETE FROM `gameobject_template` WHERE `entry` = 990006;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990006, 5, 8610, '', '', '', '', 0.75);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990006;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990006, 0, 16); -- GO_FLAG_NOT_SELECTABLE
