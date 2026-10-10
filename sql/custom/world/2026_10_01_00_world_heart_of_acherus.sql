-- Heart of Acherus: custom rune battleground in phased copies of Acherus: The Ebon Hold.
-- Every gameobject below is spawned per match by the script.

-- Queue NPC, spawn it where needed with .npc add 990000
DELETE FROM `creature_template` WHERE `entry` = 990000;
INSERT INTO `creature_template` (`entry`, `modelid1`, `name`, `subname`, `minlevel`, `maxlevel`, `exp`, `faction`, `npcflag`, `unit_class`, `unit_flags`, `type`, `ScriptName`) VALUES
(990000, 27153, 'Ebon Blade Battlemaster', 'Heart of Acherus', 80, 80, 2, 35, 1, 1, 768, 7, 'npc_heart_of_acherus_battlemaster');

-- Battlemaster gossip texts
DELETE FROM `npc_text` WHERE `ID` IN (990000, 990001);
INSERT INTO `npc_text` (`ID`, `text0_0`, `text0_1`, `BroadcastTextID0`, `lang0`, `Probability0`) VALUES
(990000, 'The Ebon Blade tests its champions in the halls of Acherus. Claim the runes of Frost, Blood and Unholy at the runeforges and hold them for your faction, $N.', '', 0, 0, 1),
(990001, 'Ten champions of each faction fight inside Acherus.$B$BClaim a rune at a runeforge and hold it. Every 5 seconds your faction scores 6 points while you stand in the sunken center, 4 on the runeforge platform and 2 outside the hall.$B$BThe rune empowers its carrier: more damage dealt, much more damage taken and less healing received, growing every 15 seconds. If the carrier falls, the rune returns to its runeforge.$B$BThe first faction to reach 1600 points wins. After 25 minutes, the highest score wins.', '', 0, 0, 1);

-- Clickable copies of the Acherus runeforges. Goober Data17 = allowMounted: the client does not dismount the player
-- to use them; the script dismounts only who takes the rune
DELETE FROM `gameobject_template` WHERE `entry` BETWEEN 990001 AND 990003;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data17`, `ScriptName`) VALUES
(990001, 10, 8175, 'Frost Runeforge',  '', '', '', 2.03, 1, 'go_heart_of_acherus_runeforge'),
(990002, 10, 8175, 'Blood Runeforge',  '', '', '', 2.03, 1, 'go_heart_of_acherus_runeforge'),
(990003, 10, 8175, 'Unholy Runeforge', '', '', '', 2.03, 1, 'go_heart_of_acherus_runeforge');

-- Berserk Buff, copy of 179905: trap type 1 (Data4), respawned by the script after 3 minutes; Data2 0 and Data5 3
-- give the 3 yard radius of the battleground buffs
DELETE FROM `gameobject_template` WHERE `entry` = 990004;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data2`, `Data3`, `Data4`, `Data5`) VALUES
(990004, 6, 5995, 'Berserk Buff', '', '', '', 1, 0, 23505, 1, 3);

-- Invisible collision wall (display of 180322, CollisionWallPvP01): octagon around each preparation dome (size 0.8,
-- ~8.9 yards wide each) and the stairs barrier (scaled by the script)
DELETE FROM `gameobject_template` WHERE `entry` = 990005;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990005, 5, 6391, 'Collision Wall', '', '', '', 0.8);

-- Pool of Blood on the Blood forge: display of the fishing hole 194479, as a nameless generic object (no tooltip)
DELETE FROM `gameobject_template` WHERE `entry` = 990006;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990006, 5, 8610, '', '', '', '', 0.75);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990006;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990006, 0, 16); -- GO_FLAG_NOT_SELECTABLE

-- Instruction book in each starting area (Lexicon of Power model of 193981). Information texts are in Portuguese,
-- the battleground UI and messages in English.
DELETE FROM `gameobject_template` WHERE `entry` = 990007;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`, `Data0`, `Data1`, `Data2`, `Data3`) VALUES
(990007, 9, 8520, 'O Coração de Acherus', '', '', '', 2.5, 990000, 0, 2, 1); -- Data2 2 = Stone page material

-- SimpleHTML pages (see docs/visuals.md); the blank lines under the first title leave room for the rune icons the
-- client UI draws there
DELETE FROM `page_text` WHERE `ID` IN (990000, 990001);
INSERT INTO `page_text` (`ID`, `Text`, `NextPageID`) VALUES
(990000, '<HTML>\n<BODY>\n<H1 align="center">|cffffd100O Coração de Acherus|r</H1>\n<BR/>\n<BR/>\n<P>As forjas rúnicas de Acherus guardam 3 runas de poder: |cffff3030Blood|r, |cff69ccf0Frost|r e |cff40ff40Unholy|r. Clique em uma forja para tomar a sua runa.</P>\n<BR/>\n<P>Enquanto carregar uma runa, seu time ganha pontos a cada 5 segundos:<BR/><BR/>+6 no fosso central<BR/>+4 na plataforma das forjas<BR/>+2 no segundo piso e fora do salão<BR/><BR/>Cada inimigo abatido vale +10 pontos.</P>\n<BR/>\n<P>Vence quem chegar primeiro a 1600 pontos ou, após 25 minutos, quem tiver mais.</P>\n<BR/>\n</BODY>\n</HTML>', 990001),
(990001, '<HTML>\n<BODY>\n<H1 align="center">|cffffd100O Preço do Poder|r</H1>\n<BR/>\n<P>A cada 15 segundos a runa fortalece quem a carrega: mais dano causado, porém mais dano recebido e menos cura recebida.</P>\n<BR/>\n<P>Cada jogador pode carregar apenas uma runa. Se você morrer, a runa volta para a sua forja.</P>\n<BR/>\n<P>O portal junto à porta leva ao andar superior e o salão guarda dois buffs de Berserk.</P>\n<BR/>\n<P>Boa sorte! Que a Lâmina de Ébano lembre do seu nome.</P>\n<BR/>\n</BODY>\n</HTML>', 0);

-- Blue aura column under each instruction book (display of 2904, AuraBlueTall), nameless scenery
DELETE FROM `gameobject_template` WHERE `entry` = 990008;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990008, 5, 298, '', '', '', '', 2);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990008;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990008, 0, 16); -- GO_FLAG_NOT_SELECTABLE

-- Green aura column on the Unholy forge (display of 148883, AuraGreenVeryTall), nameless scenery scaled by the script
DELETE FROM `gameobject_template` WHERE `entry` = 990009;
INSERT INTO `gameobject_template` (`entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`) VALUES
(990009, 5, 2473, '', '', '', '', 3.5);
DELETE FROM `gameobject_template_addon` WHERE `entry` = 990009;
INSERT INTO `gameobject_template_addon` (`entry`, `faction`, `flags`) VALUES
(990009, 0, 16); -- GO_FLAG_NOT_SELECTABLE
