/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TRINITY_HOA_DEFINES_H
#define TRINITY_HOA_DEFINES_H

#include "Common.h"
#include "SharedDefines.h"

// Temple of Kotmogu-like battleground played in phased copies of the Acherus: The Ebon Hold hall (see README.md)
namespace HeartOfAcherus
{
    namespace Ids
    {
        static constexpr uint32 MapId = 609;
        static constexpr uint32 HallAreaId = 4342;                  // Acherus: The Ebon Hold
        static constexpr uint32 NpcBattlemaster = 990000;
        static constexpr uint32 NpcAuraTrigger = 23837;             // ELM General Purpose Bunny, invisible aura carrier
        static constexpr uint32 NpcRisenDrudge = 29212;             // ambient, copied from the Acherus spawns
        static constexpr uint32 NpcVigilantGargoyle = 29239;        // ambient, copied from the Acherus spawns
        static constexpr uint32 NpcPreparationDome = 28306;         // Anti-Magic Zone totem: invisible to players
        static constexpr uint32 NpcSpiritGuideAlliance = 13116;     // battleground spirit guides
        static constexpr uint32 NpcSpiritGuideHorde = 13117;
        static constexpr uint32 GoFrostForge = 990001;
        static constexpr uint32 GoBloodForge = 990002;
        static constexpr uint32 GoUnholyForge = 990003;
        static constexpr uint32 GoBerserkBuff = 990004;             // copy of 179905 that despawns when used
        static constexpr uint32 GoPortal = 191539;                  // Doodad_Nox_portal_purple_bossroom17
        static constexpr uint32 GoCollisionWall = 990005;           // invisible CollisionWallPvP01 (display of 180322)
        static constexpr uint32 GoPoolOfBlood = 990006;             // scenery copy of 194479, on the Blood forge
        static constexpr uint32 GoInstructionBook = 990007;         // Lexicon of Power model (193981) with the rules
        static constexpr uint32 GoBookAura = 990008;                // blue aura column under each book
        static constexpr uint32 GoScourgeCircle = 191206;           // SC_CastingCircle_01, on the Unholy forge
        static constexpr uint32 GoUnholyLight = 990009;             // green aura column on the Unholy forge
    }

    namespace Spells
    {
        static constexpr uint32 UndyingResolve = 51915;             // zone aura of 4298, prevents dying
        static constexpr uint32 DominionOverAcherus = 51721;        // spell_area of 4342 (quest 12657), +75% run speed
        static constexpr uint32 SpiritHealChannel = 22011;          // spirit guide channel visual
        static constexpr uint32 WaitingForResurrect = 2584;
        static constexpr uint32 SpiritHeal = 22012;                 // cast on the guide for the revive visual
        static constexpr uint32 ResurrectionVisual = 24171;
        static constexpr uint32 ResurrectEffect = 6962;             // as in Battleground::_ProcessResurrect
        static constexpr uint32 SpiritHealMana = 44535;
        static constexpr uint32 Preparation = 44521;                // SPELL_PREPARATION of battlegrounds, -100% power cost
        static constexpr uint32 SafeFall = 24350;                   // SPELL_AURA_SAFE_FALL, hidden and passive, no visual
        static constexpr uint32 PreparationDome = 50461;            // Anti-Magic Zone: only its channel kit is used
        static constexpr uint32 StairsPortal = 42049;               // Boss Frost Portal State (dummy)
        static constexpr uint32 AcherusDeathcharger = 48778;        // mount aura and +100% mounted speed
        static constexpr uint32 TravelForm = 783;                   // outdoors only (SPELL_ATTR0_OUTDOORS_ONLY)
        static constexpr uint32 GhostWolf = 2645;                   // outdoors only (SPELL_ATTR0_OUTDOORS_ONLY)

        // forge auras, visual only (see docs/visuals.md)
        static constexpr uint32 ForgeSpiritsFrost = 31954;          // Spirit Particles, super big (DND)
        static constexpr uint32 ForgeBeamFrost = 32840;             // Beam (Blue)
        static constexpr uint32 ForgeBeamBlood = 32839;             // Beam (Red)
        static constexpr uint32 ForgeIceboundFrost = 58837;         // Icebound Fortitude
        static constexpr uint32 ForgeSpiritsBlood = 31951;          // Spirit Particles (red, super big) (DND)
        static constexpr uint32 ForgeHysteriaBlood = 58361;         // The Might of Mograine: Hysteria model, no sound
        static constexpr uint32 ForgeSpiritsUnholyBase = 61894;     // Spirit Particles (green - Base)
        static constexpr uint32 ForgeSpiritsUnholy = 43167;         // Spirit Particles (green)
        static constexpr uint32 ForgeSpiritsUnholyChest = 43161;    // Spirit Particles (green - Chest)
        static constexpr uint32 ForgePlagueUnholy = 63319;          // Saronite Animus Formation Visual
        static constexpr uint32 ForgeGhostStateUnholy = 60426;      // Ghost State

        // carrier auras: dummy Portal States, visible in the aura bar so the client UI relabels them
        static constexpr uint32 CarrierAuraFrost = 33340;           // Blue Portal State
        static constexpr uint32 CarrierAuraBlood = 33338;           // Red Portal State
        static constexpr uint32 CarrierAuraUnholy = 33339;          // Green Portal State
    }

    // death knight presence impact kits (SpellVisual.dbc), played once when a rune is taken
    namespace VisualKits
    {
        static constexpr uint32 CarrierFrost = 10288;               // Frost Presence (48263)
        static constexpr uint32 CarrierBlood = 10283;               // Blood Presence (48266)
        static constexpr uint32 CarrierUnholy = 10297;              // Unholy Presence (48265)
    }

    // 3.3.5 battleground sounds (Battleground.h, BattlegroundWS.h)
    namespace Sounds
    {
        static constexpr uint32 RuneEvent = 8174;                   // BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP
        static constexpr uint32 AllianceWins = 8455;                // SOUND_ALLIANCE_WINS
        static constexpr uint32 HordeWins = 8454;                   // SOUND_HORDE_WINS
        static constexpr uint32 BattleStart = 3439;                 // SOUND_BG_START
    }

    // Eye of the Storm frames, shown by claiming to be in its map/zone
    namespace WorldStates
    {
        static constexpr uint32 FakeMapId = 566;
        static constexpr uint32 FakeZoneId = 3820;
        static constexpr uint16 FakeBattlemasterListId = BATTLEGROUND_EY;

        static constexpr uint32 AllianceScore = 2749;
        static constexpr uint32 HordeScore = 2750;
        static constexpr uint32 AllianceRunes = 2752;               // EotS "Bases"
        static constexpr uint32 HordeRunes = 2753;
        static constexpr uint32 AllianceTopStats = 2769;
        static constexpr uint32 HordeTopStats = 2770;
    }

    namespace Scoring
    {
        static constexpr uint32 MaxScore = 1600;
        static constexpr uint32 TickInterval = 5 * IN_MILLISECONDS;
        static constexpr uint32 PointsCenter = 6;
        static constexpr uint32 PointsPlatform = 4;
        static constexpr uint32 PointsOutside = 2;

        static constexpr float CenterRadius = 25.0f;                // stairs at 24.0 and 25.3 yards
        static constexpr float CenterMaxZ = 418.0f;                 // pit floor 414.1, platform 420.6
        static constexpr float PlatformRadius = 62.0f;              // forges at ~60 yards
        static constexpr float DoorDistance = 56.0f;                // door at 57.7 yards, along the door axis
        static constexpr float FloorMinZ = 410.0f;
        static constexpr float FloorMaxZ = 435.0f;
    }

    namespace Timers
    {
        static constexpr uint32 Preparation = 2 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 PreparationPowerReset = 5 * IN_MILLISECONDS;   // m_ResetStatTimer of Battleground::_ProcessJoin
        static constexpr uint32 MatchDuration = 25 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 EndWait = 2 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 ResurrectWave = 30 * IN_MILLISECONDS;
        static constexpr uint32 RuneStack = 15 * IN_MILLISECONDS;
        static constexpr uint32 PlayerCheck = 1 * IN_MILLISECONDS;
        static constexpr uint32 OfflineGrace = 300 * IN_MILLISECONDS;  // MAX_OFFLINE_TIME of battlegrounds
        static constexpr uint32 ReturnRetry = 1 * IN_MILLISECONDS;
        static constexpr uint8 ReturnMaxAttempts = 10;
        static constexpr uint32 ClientPing = 25 * IN_MILLISECONDS;     // client UI safety probe
        static constexpr uint32 RuneMarkers = 250;                     // minimap rune marker refresh
        static constexpr uint32 BuffRespawn = 180 * IN_MILLISECONDS;   // BUFF_RESPAWN_TIME of battlegrounds
    }

    // carrier modifiers per stack, a stack every 15 s; the stacks stop at MaxStacks, where every modifier is capped
    namespace RunePower
    {
        static constexpr uint32 MaxStacks = 5;                      // the caps below are all reached at 5 stacks

        static constexpr float DamageDonePct = 20.0f;
        static constexpr float DamageDoneMaxPct = 100.0f;
        static constexpr float DamageTakenPct = 20.0f;
        static constexpr float DamageTakenMaxPct = 100.0f;
        static constexpr float HealingTakenPct = -10.0f;
        static constexpr float HealingTakenMaxPct = -50.0f;

        static constexpr float ScaleBase = 0.2f;
        static constexpr float ScalePerStack = 0.2f;
        static constexpr float ScaleMax = 1.0f;                     // twice the original size
    }

    enum class Rune : uint8
    {
        Frost,
        Blood,
        Unholy
    };

    static constexpr std::size_t RuneCount = 3;

    enum class MatchStatus : uint8
    {
        Preparation,
        InProgress,
        Ended
    };

    enum class RemoveMode : uint8
    {
        TeleportOut,                                                // back to the saved position
        Logout,                                                     // keep the saved position for the next login
        Left                                                        // left the map by himself
    };

    // HeartOfAcherus.OutdoorSpellsMethod; the id is also sent to the client UI, which runs the client side of the method
    enum class OutdoorSpellsMethod : uint8
    {
        None        = 0,                                            // the hall stays indoors
        MountButton = 1,                                            // client mount button, forms through the client hook
        ClientHook  = 2,                                            // mounts and forms through the client hook
        Max
    };
}

#endif // TRINITY_HOA_DEFINES_H
