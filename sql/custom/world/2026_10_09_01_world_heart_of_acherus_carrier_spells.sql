-- Heart of Acherus: spells refused to a rune carrier, and on one (SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW).
-- Picked from Spell.dbc by aura type: SPELL_AURA_SCHOOL_IMMUNITY (39), SPELL_AURA_MOD_STEALTH (16) and
-- SPELL_AURA_MOD_INVISIBILITY (18), player class and racial spells only; Nitro Boosts by id.
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_heart_of_acherus_rune_carrier_restricted';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
-- immunities
(642,   'spell_heart_of_acherus_rune_carrier_restricted'), -- Divine Shield
(45438, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Ice Block
(1022,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Hand of Protection (Rank 1)
(5599,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Hand of Protection (Rank 2)
(10278, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Hand of Protection (Rank 3)
(19752, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Divine Intervention
-- stealth
(1784,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Stealth
(1856,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Vanish (Rank 1)
(1857,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Vanish (Rank 2)
(26889, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Vanish (Rank 3)
(5215,  'spell_heart_of_acherus_rune_carrier_restricted'), -- Prowl
(58984, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Shadowmeld
-- invisibility
(66,    'spell_heart_of_acherus_rune_carrier_restricted'), -- Invisibility (the fading)
(32612, 'spell_heart_of_acherus_rune_carrier_restricted'), -- Invisibility (after the fading, if a rune was taken meanwhile)
-- Nitro Boosts (use spell of enchantment 3606, casts 54861 or the backfire)
(55004, 'spell_heart_of_acherus_rune_carrier_restricted');
