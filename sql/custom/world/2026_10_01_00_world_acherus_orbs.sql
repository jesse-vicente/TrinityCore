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

-- Berserk Buff, copy of the battleground one (179905) spawned per match by the script.
-- Trap type 1 (Data4) despawns after casting, the script respawns it after 3 minutes; Data2 = 0 and Data5 = 3 give
-- the 3 yard radius the core uses for battleground buffs.
DELETE FROM `gameobject_template` WHERE `entry` = 990004;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data2`, `Data3`, `Data4`, `Data5`) VALUES
(990004, 6, 5995, 'Berserk Buff', '', '', '', 1, 0, 23505, 1, 3);

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

-- Instruction book in each starting area during the preparation, spawned per match by the script: a text object with
-- the floating Lexicon of Power model (193981). Instructions are in Portuguese, like the other information texts of
-- the server; the battleground UI and messages stay in English.
DELETE FROM `gameobject_template` WHERE `entry` = 990007;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data0`, `Data1`, `Data2`, `Data3`) VALUES
(990007, 9, 8520, 'O Coração de Acherus', '', '', '', 2.5, 990000, 0, 2, 1); -- Data2 2 = Stone page material

DELETE FROM `page_text` WHERE `ID` IN (990000, 990001);
INSERT INTO `page_text` (`ID`, `Text`, `NextPageID`) VALUES
(990000, 'O Coração de Acherus\n\nAs três Forjas Rúnicas de Acherus guardam Runas de poder. Clique em uma Forja para tomar a sua Runa.\n\nEnquanto você estiver vivo com uma Runa, seu time ganha pontos a cada 5 segundos:\n- 6 no Fosso Central\n- 4 na plataforma das Forjas\n- 2 fora do salão\n\nCada inimigo abatido vale mais 10 pontos.\n\nVence o primeiro time a chegar a 1600 pontos. Após 25 minutos, vence quem tiver mais.', 990001),
(990001, 'O preço do poder\n\nA cada 15 segundos a Runa fortalece quem a carrega: mais dano causado, porém mais dano recebido e menos cura recebida.\n\nCada jogador carrega apenas uma Runa. Ao tomá-la, você é desmontado e sai da furtividade.\n\nSe você morrer, a Runa volta para a sua Forja.\n\nO portal junto à porta leva ao andar superior, e o salão guarda dois buffs de Berserk.\n\nBoa sorte, e que a Lâmina de Ébano lembre do seu nome.', 0);

-- Blue aura column under each instruction book (display of 2904, AuraBlueTall), spawned per match by the script.
-- Scenery only: generic object without a name, not selectable.
DELETE FROM `gameobject_template` WHERE `entry` = 990008;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990008, 5, 298, '', '', '', '', 2);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990008;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990008, 0, 16); -- GO_FLAG_NOT_SELECTABLE

-- Green aura column on the Unholy forge (display of 148883, AuraGreenVeryTall), spawned with the forge visuals by the
-- script and scaled there. Scenery only: generic object without a name, not selectable.
DELETE FROM `gameobject_template` WHERE `entry` = 990009;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990009, 5, 2473, '', '', '', '', 3.5);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990009;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990009, 0, 16); -- GO_FLAG_NOT_SELECTABLE
