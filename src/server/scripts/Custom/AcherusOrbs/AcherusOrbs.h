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

#ifndef TRINITY_ACHERUS_ORBS_H
#define TRINITY_ACHERUS_ORBS_H

#include "Common.h"
#include "Define.h"
#include "GameObjectData.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "SharedDefines.h"
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Creature;
struct CreatureData;
class GameObject;
class Group;
class Map;
class Player;
class Unit;

namespace WorldPackets::WorldState
{
    class InitWorldStates;
}

// Custom Temple of Kotmogu-like match played in phased copies of the Acherus: The Ebon Hold hall
namespace AcherusOrbs
{
    namespace Ids
    {
        static constexpr uint32 MapId = 609;
        static constexpr uint32 HallAreaId = 4342;                  // Acherus: The Ebon Hold
        static constexpr uint32 NpcBattlemaster = 990000;
        static constexpr uint32 NpcBeamTrigger = 23837;              // ELM General Purpose Bunny
        static constexpr uint32 NpcRisenDrudge = 29212;              // ambient, copied from the Acherus spawns
        static constexpr uint32 NpcVigilantGargoyle = 29239;         // ambient, copied from the Acherus spawns
        static constexpr uint32 NpcPreparationDome = 28306;          // Anti-Magic Zone totem of the death knights: invisible, small air totem for GMs
        static constexpr uint32 NpcSpiritGuideAlliance = 13116;      // same spirit guides as the battlegrounds
        static constexpr uint32 NpcSpiritGuideHorde = 13117;
        static constexpr uint32 GoFrostForge = 990001;
        static constexpr uint32 GoBloodForge = 990002;
        static constexpr uint32 GoUnholyForge = 990003;
        static constexpr uint32 GoBerserkBuff = 990004;              // Berserk Buff (179905) that despawns when used
        static constexpr uint32 GoPortal = 191539;                   // Doodad_Nox_portal_purple_bossroom17, the Acherus portal by the door (outside the match phases)
        static constexpr uint32 GoPreparationWall = 990005;          // invisible PvP collision wall (display of 180322), rings the preparation domes
        static constexpr uint32 GoPoolOfBlood = 990006;              // generic copy of the Pool of Blood fishing hole (194479), on the Blood forge
        static constexpr uint32 GoInstructionBook = 990007;          // floating Lexicon of Power (193981) with the instructions, in each starting area
        static constexpr uint32 GoBookAura = 990008;                 // blue aura column (display of 2904) under each instruction book
        static constexpr float PoolOfBloodScale = 3.0f;              // small puddle, hidden under the forge at its template size (0.75)
        static constexpr uint32 GoScourgeCircle = 191206;            // SC_CastingCircle_01, on the Unholy forge
        static constexpr float ScourgeCircleScale = 1.4f;
        static constexpr uint32 GoUnholyLight = 990009;              // green aura column (display of 148883) on the Unholy forge
        static constexpr float UnholyLightScale = 10.0f;             // template size 3.5, raised to match the forge
    }

    namespace Spells
    {
        static constexpr uint32 UndyingResolve = 51915;              // zone aura of 4298, prevents dying
        static constexpr uint32 DominionOverAcherus = 51721;        // area aura of 4342 for death knights (quest 12657), +75% run speed
        static constexpr uint32 SpiritHealChannel = 22011;           // spirit guide channel visual
        static constexpr uint32 WaitingForResurrect = 2584;          // "Waiting to Resurrect", on the ghost while it waits at the guide
        static constexpr uint32 SpiritHeal = 22012;                  // Spirit Heal, effect 117, cast on the guide for the revive visual
        static constexpr uint32 ResurrectionVisual = 24171;          // Resurrection Impact Visual, cast on the player on the wave
        static constexpr uint32 ResurrectEffect = 6962;              // same spell Battleground::_ProcessResurrect casts on the revived player
        static constexpr uint32 SpiritHealMana = 44535;              // Spirit Heal, on the player after the resurrection
        static constexpr uint32 PreparationDome = 50461;             // Anti-Magic Zone: only its channel kit is used, the aura is never applied
        static constexpr uint32 StairsPortal = 42049;                // Boss Frost Portal State (dummy), on the trigger at the top of the stairs

        // forge auras, on the forge triggers while the orb is ready; visual only (dummy, except 58361, see README)
        static constexpr uint32 ForgeSpiritsFrost = 31954;           // Spirit Particles, super big (DND): Spells\Ghost_state.mdx
        static constexpr uint32 ForgeBeamFrost = 32840;              // Beam (Blue): MoonBeamBlue_Impact_Base.mdx
        static constexpr uint32 ForgeBeamBlood = 32839;              // Beam (Red): MoonBeamRed_Impact_Base.mdx
        static constexpr uint32 ForgeIceboundFrost = 58837;          // Icebound Fortitude: DeathKnight_IceboundFortitude.mdx
        static constexpr uint32 ForgeSpiritsBlood = 31951;           // Spirit Particles (red, super big) (DND): spells\redghost_state.mdx
        static constexpr uint32 ForgeHysteriaBlood = 58361;          // The Might of Mograine: DeathKnight_Hysteria.mdx without the Hysteria sound; its effects (damage, healing, max health) do nothing on the trigger
        static constexpr uint32 ForgeSpiritsUnholyBase = 61894;      // Spirit Particles (green - Base): Spells\GreenGhost_state.mdx
        static constexpr uint32 ForgeSpiritsUnholy = 43167;          // Spirit Particles (green): Spells\GreenGhost_state.mdx
        static constexpr uint32 ForgeSpiritsUnholyChest = 43161;     // Spirit Particles (green - Chest); also worn by the Unholy carrier
        static constexpr uint32 ForgePlagueUnholy = 63319;           // Saronite Animus Formation Visual: DeathKnight_PlagueStrikeState.mdx, already large at scale 1
        static constexpr uint32 ForgeGhostStateUnholy = 60426;       // Ghost State: sc_spirits_01.mdx

        // permanent carrier auras, all dummy (no stat effect). Each orb combines spirit particles (visual)
        // with a Portal State; the Portal States are visible in the aura bar (unlike the Banish States, which
        // are SPELL_ATTR0_HIDDEN_CLIENTSIDE), so the client UI script can relabel them with death knight icons
        // and avoid a death knight ending up with two presence icons. All three share the same icon
        // (Spell_Arcane_PortalOrgrimmar), so the client tells them apart by the localized spell name.
        static constexpr uint32 CarrierAuraFrost = 33340;            // Blue Portal State
        static constexpr uint32 CarrierAuraBlood = 33338;            // Red Portal State
        static constexpr uint32 CarrierAuraUnholy = 33339;           // Green Portal State

    }

    // impact kits (SpellVisual.dbc) of the death knight presences, played once when the orb is taken (they have no state kit)
    namespace VisualKits
    {
        static constexpr uint32 CarrierFrost = 10288;                // Frost Presence (48263), visual 11115
        static constexpr uint32 CarrierBlood = 10283;                // Blood Presence (48266), visual 11114
        static constexpr uint32 CarrierUnholy = 10297;               // Unholy Presence (48265), visual 11116
    }

    // battleground sounds of the 3.3.5 core (Battleground.h, BattlegroundWS.h)
    namespace Sounds
    {
        static constexpr uint32 OrbEvent = 8174;                    // BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP: orb taken, carrier killed, orb returned
        static constexpr uint32 AllianceWins = 8455;                // SOUND_ALLIANCE_WINS
        static constexpr uint32 HordeWins = 8454;                   // SOUND_HORDE_WINS
        static constexpr uint32 BattleStart = 3439;                 // SOUND_BG_START
    }

    namespace WorldStates
    {
        // Eye of the Storm frames, shown by claiming to be in its map/zone
        static constexpr uint32 FakeMapId = 566;
        static constexpr uint32 FakeZoneId = 3820;
        static constexpr uint16 FakeBattlemasterListId = BATTLEGROUND_EY; // battlefield status, gives the score frame its timers

        static constexpr uint32 AllianceScore = 2749;
        static constexpr uint32 HordeScore = 2750;
        static constexpr uint32 AllianceBases = 2752;               // used as "orbs held"
        static constexpr uint32 HordeBases = 2753;
        static constexpr uint32 AllianceTopStats = 2769;
        static constexpr uint32 HordeTopStats = 2770;
    }

    namespace Positions
    {
        inline Position const Center = { 2459.4f, -5593.4f, 414.12f };
        inline Position const Door = { 2410.68f, -5626.74f, 420.66f };

        // the original portal (gameobject 151235) and its teleporter (NPC 29581, aura 54724) lead to the Hall of Command
        // below; in the match it leads to the upper floor
        inline Position const Portal = { 2383.65f, -5645.20f, 420.772f };
        inline QuaternionData const PortalRotation = { 0.0f, 0.0f, 0.292371f, 0.956305f };
        inline Position const PortalDestination = { 2517.900879f, -5554.814453f, 444.124817f, 3.740502f };

        // Berserk buffs (spell 23505) in the hall, mirrored across the axis between the faction spawns (through the door):
        // each faction has one at the same distance from its spawn and its respawn
        static constexpr std::size_t BerserkBuffCount = 2;
        inline std::array<Position, BerserkBuffCount> const BerserkBuffs =
        {{
            { 2472.58f, -5530.56f, 420.649078f, 4.523f },
            { 2523.22f, -5605.63f, 420.648682f, 2.947f }
        }};

        // ambient creature copies placed away from their original spawn; nothing else of them changes
        struct AmbientCreatureMove
        {
            uint32 SpawnId;
            Position Destination;
        };

        inline std::array<AmbientCreatureMove, 1> const AmbientCreatureMoves =
        {{
            { 125787, { 2514.38f, -5603.94f, 420.65f, 2.73855f } }    // Risen Drudge wandering 5 yards over the second Berserk buff: 9 yards toward the center
        }};

        // the top of the stairs behind that portal leads out to the balcony, and the gaps along the sides of the stairs
        // lead to where the client switches to The Heart of Acherus. An invisible wall closes them (left bottom, left top,
        // right top, right bottom) up to above the upper floor, so nobody jumps over it from there either
        inline std::array<Position, 4> const StairsBarrier =
        {{
            { 2362.671387f, -5635.259766f, 420.713196f },
            { 2361.644287f, -5646.252441f, 426.696899f },
            { 2374.672363f, -5664.892090f, 426.658569f },
            { 2385.154785f, -5668.484375f, 422.844604f }
        }};
        inline float const StairsBarrierTop = 444.227661f;            // floor of the upper floor
        inline Position const StairsPortal = { 2369.197266f, -5655.271484f, 426.126343f, 0.623654f };  // facing the hall

        inline std::array<Position, PVP_TEAMS_COUNT> const Spawn =
        {{
            { 2447.56f, -5656.40f, 420.65f, 1.294f },                // Alliance (TEAM_ALLIANCE = 0)
            { 2397.17f, -5581.70f, 420.65f, 6.177f }                 // Horde
        }};

        // graveyards of each faction on the upper floor, each one a spirit guide; the Horde ones mirror the Alliance
        // ones across the axis between the spawns
        static constexpr std::size_t RespawnCount = 2;
        inline std::array<std::array<Position, RespawnCount>, PVP_TEAMS_COUNT> const Respawn =
        {{
            {{
                { 2438.10f, -5707.64f, 444.61f, 1.347f },
                { 2574.63f, -5615.65f, 444.613373f, 2.950f }
            }},
            {{
                { 2346.12f, -5571.28f, 444.62f, 6.123f },
                { 2482.54f, -5479.14f, 444.615540f, 4.520f }
            }}
        }};
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
        static constexpr float DoorDistance = 56.0f;                // door at 57.7 yards, measured along the door axis
        static constexpr float FloorMinZ = 410.0f;
        static constexpr float FloorMaxZ = 435.0f;
    }

    namespace Timers
    {
        static constexpr uint32 Preparation = 2 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 MatchDuration = 25 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 EndWait = 2 * MINUTE * IN_MILLISECONDS;
        static constexpr uint32 ResurrectWave = 30 * IN_MILLISECONDS;
        static constexpr uint32 OrbStack = 15 * IN_MILLISECONDS;
        static constexpr uint32 PlayerCheck = 1 * IN_MILLISECONDS;
        static constexpr uint32 OfflineGrace = 300 * IN_MILLISECONDS;      // MAX_OFFLINE_TIME of battlegrounds
        static constexpr uint32 ReturnRetry = 1 * IN_MILLISECONDS;
        static constexpr uint8 ReturnMaxAttempts = 10;
        static constexpr uint32 ClientPing = 25 * IN_MILLISECONDS;         // client UI script safety probe interval
        static constexpr uint32 OrbMarker = 250;                          // minimap orb marker refresh interval
        static constexpr uint32 BuffRespawn = 180 * IN_MILLISECONDS;       // BUFF_RESPAWN_TIME of battlegrounds
    }

    namespace OrbPower
    {
        // per stack, a new stack every 15 seconds; the stacks keep counting but the modifiers freeze at the caps
        static constexpr float DamageDonePct = 20.0f;               // +20% per stack
        static constexpr float DamageDoneMaxPct = 100.0f;           // capped at 5 stacks
        static constexpr float DamageTakenPct = 20.0f;              // +20% per stack
        static constexpr float DamageTakenMaxPct = 100.0f;          // capped at 5 stacks
        static constexpr float HealingTakenPct = -10.0f;            // -10% per stack
        static constexpr float HealingTakenMaxPct = -50.0f;         // capped at 5 stacks

        static constexpr float ScaleBase = 0.2f;
        static constexpr float ScalePerStack = 0.2f;                // +20% per stack
        static constexpr float ScaleMax = 1.0f;                     // up to twice the original size, capped at 5 stacks
    }

    enum OrbType : uint8
    {
        ORB_FROST = 0,
        ORB_BLOOD,
        ORB_UNHOLY,
        MAX_ORBS
    };

    struct ForgeScaledAura
    {
        uint32 Spell = 0;                                           // 0 = none
        float Scale = 1.0f;                                         // scale of its own trigger
    };

    struct ForgeObjectTemplate
    {
        uint32 Entry = 0;                                           // 0 = none
        float Scale = 0.0f;                                         // 0 = size of the gameobject template
    };

    struct OrbTemplate
    {
        char const* Name;
        char const* Color;                                          // chat color code of the orb name
        uint32 ForgeEntry;
        Position ForgePosition;
        QuaternionData ForgeRotation;
        float ForgeAuraScale;                                       // scale of the forge trigger, the forge auras are drawn at it
        std::array<uint32, 5> ForgeAuras;                          // permanent auras of the forge trigger while the orb is ready (0 = none)
        std::array<ForgeObjectTemplate, 3> ForgeObjects;            // gameobjects spawned on the forge while the orb is ready
        std::array<ForgeScaledAura, 2> ForgeScaledAuras;            // auras on their own trigger, to size them apart from the others
        uint32 CarrierVisualKit;
        std::array<uint32, 3> CarrierAuras;                         // permanent auras of the carrier (0 = none)
    };

    extern std::array<OrbTemplate, MAX_ORBS> const OrbTemplates;

    struct MatchPlayer
    {
        ObjectGuid Guid;
        TeamId Team = TEAM_ALLIANCE;
        WorldLocation Return;
        bool HandledDeath = false;
        bool WorldStatesSent = false;
        bool Offline = false;
        bool ForgesLocked = false;                                  // the forges were sent to this client as not usable
        uint32 OfflineTimer = 0;
        Optional<uint32> StatusSlot;                                // battlefield status slot used by the final score frame
        uint32 KillingBlows = 0;
        uint32 HonorableKills = 0;
        uint32 Deaths = 0;
        uint32 DamageDone = 0;
        uint32 HealingDone = 0;
        uint32 Points = 0;                                          // team points scored, shown in the Flag Captures column
    };

    struct BerserkBuffState
    {
        ObjectGuid Guid;
        bool Armed = false;                                         // seen ready since it was spawned
        uint32 RespawnTimer = 0;                                    // time left to respawn it
    };

    struct OrbState
    {
        ObjectGuid Forge;
        ObjectGuid Trigger;
        std::array<ObjectGuid, 2> ScaledAuraTriggers;
        std::array<ObjectGuid, 3> Objects;
        ObjectGuid Carrier;
        uint32 Stacks = 0;
        uint32 StackTimer = 0;
        float CarrierOriginalScale = 1.0f;
    };

    enum class MatchStatus : uint8
    {
        Preparation,
        InProgress,
        Ended
    };

    // player sent back to the position saved when joining, after a crash or a logout that outlived the match
    struct PendingReturn
    {
        WorldLocation Destination;
        uint32 Timer = 0;
        uint8 Attempts = 0;
    };

    enum class RemoveMode : uint8
    {
        TeleportOut,                                                // send back to the saved position
        Logout,                                                     // keep the saved position, used on next login
        Left                                                        // player left the map by himself
    };

    struct Match
    {
        uint32 Id = 0;
        uint32 PhaseMask = 0;
        MatchStatus Status = MatchStatus::Preparation;
        uint32 StatusTimer = 0;                                     // time left in the current status
        uint32 TickTimer = 0;
        uint32 ResurrectTimer = 0;
        uint32 PlayerCheckTimer = 0;
        uint32 OrbMarkerTimer = 0;
        uint32 BattleTime = 0;                                      // time played after the preparation, set when the match ends
        std::array<uint32, PVP_TEAMS_COUNT> Score = { };
        Optional<TeamId> Winner;                                    // TEAM_NEUTRAL = draw
        std::unordered_map<ObjectGuid, MatchPlayer> Players;
        std::array<OrbState, MAX_ORBS> Orbs;
        std::array<std::array<ObjectGuid, Positions::RespawnCount>, PVP_TEAMS_COUNT> SpiritGuides;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> PreparationSpiritGuides;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> PreparationDomes;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> PreparationBooks;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> PreparationBookAuras;
        std::vector<ObjectGuid> PreparationWalls;
        ObjectGuid Portal;
        std::array<BerserkBuffState, Positions::BerserkBuffCount> BerserkBuffs;
        std::vector<ObjectGuid> StairsBarrier;
        ObjectGuid StairsPortal;
        std::vector<ObjectGuid> AmbientCreatures;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> Raids;              // battlefield raid of each team, not stored in the database
        std::unordered_map<ObjectGuid, ObjectGuid> ResurrectQueue; // players waiting at a spirit healer for the next wave, mapped to that guide
    };

    class Manager
    {
    public:
        static Manager* instance();

        void LoadConfig();
        void Update(uint32 diff);

        // queue
        bool Enqueue(Player* player, std::string& error);
        bool Dequeue(ObjectGuid guid);
        bool IsQueued(ObjectGuid guid);
        std::array<std::size_t, PVP_TEAMS_COUNT> GetQueueSizes();
        void ForceStart() { _forceStart = true; }
        void EndAll() { _endAllRequested = true; }
        bool SkipPreparation(ObjectGuid guid);                      // like .bg start; empty guid = every match
        std::string GetStatus() const;

        // hooks
        bool IsInMatch(ObjectGuid guid) const { return _playerMatch.contains(guid); }
        void OnForgeUse(Player* player, GameObject* forge);
        void OnPvPKill(Player* killer, Player* killed);
        void OnUpdateZone(Player* player) const;
        bool IsSanctuaryDisabled(Player const* player) const;
        bool OnRepop(Player* player);
        bool OnSpiritHealerQuery(Player* player, Creature* spiritHealer);
        void OnSpiritHealerQueue(Player* player, Creature* spiritHealer, bool& handled);
        void OnLeaveRequest(Player* player);
        void OnJoinRealBattlegroundQueue(Player* player);
        void OnBeforeLogout(Player* player);
        void OnLogout(Player* player);
        void OnLogin(Player* player);
        void OnPVPLogDataRequest(Player* player);
        void OnRequestBattlefieldStatus(Player* player);
        void OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite, bool& handled);
        void OnAddonMessage(Player* player, std::string const& msg, bool& handled);
        void OnWardenLuaExecuted(Player* player);
        void FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet);
        void ModifyDamage(Unit* attacker, Unit* victim, uint32& damage) const;
        void ModifyHealing(Unit* healer, Unit* receiver, uint32& gain);
        void TrackDamage(Unit* attacker, Unit* victim, uint32 damage);

    private:
        Manager() = default;

        void ProcessRequests();
        void ProcessPendingReturns(uint32 diff);
        void FillOpenMatches();
        void TryCreateMatch();
        void StartPreparation(Match& match);
        void StartMatch(Match& match);
        void EndMatch(Match& match, TeamId winner);
        void UpdateMatch(Match& match, uint32 diff);
        void CheckPlayers(Match& match);
        void KeepInPreparationArea(Match& match);

        // raids
        static Group* GetRaid(Match const& match, TeamId team);
        void UpdateRaid(Match& match, MatchPlayer const& matchPlayer, Player* player);
        void CheckRaids(Match& match);
        void LeaveRaid(Match& match, ObjectGuid guid, TeamId team);
        void DisbandRaids(Match& match);
        void ResurrectDead(Match& match);
        void ScoreTick(Match& match);
        void UpdateCarriers(Match& match, uint32 diff);
        void SendOrbMarkers(Match& match);

        void AddPlayer(Match& match, Player* player);
        void RemovePlayer(Match& match, ObjectGuid guid, RemoveMode mode);
        void ApplyMatchState(Match const& match, Player* player) const;
        static void RestorePhase(Player* player);

        void SpawnObjects(Match& match, Map* map);
        void DespawnObjects(Match& match, Map* map);
        static ObjectGuid SummonSpiritGuide(Match const& match, Map* map, Position const& graveyard, TeamId team);
        static ObjectGuid SummonPreparationDome(Match const& match, Map* map, Position const& center);
        static void SpawnPreparationWalls(Match& match, Map* map, Position const& center);
        static Position GetInstructionBookPosition(TeamId team);
        static ObjectGuid SpawnInstructionBook(Match const& match, Map* map, TeamId team);
        static ObjectGuid SpawnBookAura(Match const& match, Map* map, TeamId team);
        static ObjectGuid SpawnWall(Match const& match, Map* map, Position const& position, float scale = 0.0f);
        static void SpawnStairsBarrier(Match& match, Map* map);
        static ObjectGuid SummonStairsPortal(Match const& match, Map* map);
        static void SpawnAmbientCreatures(Match& match, Map* map);
        static ObjectGuid SummonAmbientCreature(Match const& match, Map* map, CreatureData const& data);
        static void DespawnPreparationArea(Match& match, Map* map);
        static void DespawnCreature(Map* map, ObjectGuid& guid);
        static Position const& GetGraveyard(Match const& match, TeamId team, Player const* player);
        static bool IsTeamSpiritGuide(Match const& match, TeamId team, ObjectGuid guid);
        static ObjectGuid SpawnPortal(Match const& match, Map* map);
        static void SpawnBerserkBuff(Match const& match, Map* map, std::size_t index, BerserkBuffState& buff);
        static void UpdateBerserkBuffs(Match& match, uint32 diff);
        static void UsePortal(Match const& match, Player* player);
        void SetForgeVisuals(Match& match, OrbType orb, bool on);
        static ObjectGuid SpawnForgeObject(Match const& match, Map* map, OrbTemplate const& orbTemplate, ForgeObjectTemplate const& objectTemplate);
        static ObjectGuid SummonForgeTrigger(Match const& match, Map* map, OrbTemplate const& orbTemplate, float scale);
        static bool IsAboveForges(Player const* player);
        static void UpdateForgeUsable(Match const& match, MatchPlayer& matchPlayer, Player* player);
        void SendUseError(Player* player, std::string const& text) const;
        void PickUpOrb(Match& match, OrbType orb, Player* player);
        void DropOrb(Match& match, OrbType orb, bool announce);
        Optional<OrbType> GetCarriedOrb(Match const& match, ObjectGuid guid) const;
        OrbState const* GetCarriedOrbState(ObjectGuid guid) const;

        uint32 GetPointsForPosition(Player const* player) const;
        uint32 GetHeldOrbCount(Match const& match, TeamId team) const;
        void UpdateWorldStates(Match& match);
        void SendScoreboard(Match& match, Player* target = nullptr);
        void SendEndState(Match& match, MatchPlayer& matchPlayer, Player* player);
        void SendBattlefieldStatus(Match const& match, MatchPlayer& matchPlayer, Player* player) const;
        static void ClearBattlefieldStatus(MatchPlayer& matchPlayer, Player* player);
        void SendQueueStatus(Player* player);
        void ClearQueueStatus(ObjectGuid guid, Player* player);
        static BattlegroundQueueTypeId GetFakeQueueTypeId(Player const* player);
        void LoadClientScript();
        void SendClientScript(Player* player);
        void RequestClientScript(Player* player);
        static void SendAddonMessage(Player* player, std::string const& text);
        void SetClientRelabel(Player* player, bool active) const;
        void PingClientScript();
        void SendBootstrap(Player* player);
        void ProbeClientScript(Player* player, bool assertRelabel = false);
        void Announce(Match& match, ChatMsg type, std::string const& text);
        void PlaySound(Match& match, uint32 soundId);
        Match* GetMatch(ObjectGuid guid) const;

        std::mutex _queueLock;
        std::array<std::deque<ObjectGuid>, PVP_TEAMS_COUNT> _queue;
        std::unordered_map<ObjectGuid, uint32> _queueStatusSlots;   // protected by _queueLock
        std::unordered_map<ObjectGuid, uint32> _clientScriptCooldowns; // protected by _queueLock
        std::unordered_map<ObjectGuid, uint32> _clientBootstrapCooldowns; // protected by _queueLock
        std::unordered_map<ObjectGuid, uint32> _clientBootstrapTimeouts; // protected by _queueLock
        // requests dropped while the matching cooldown was active, resent as soon as it expires
        std::unordered_set<ObjectGuid> _pendingBootstraps;          // protected by _queueLock
        std::unordered_set<ObjectGuid> _pendingPayloads;            // protected by _queueLock
        // players whose listener bootstrap part 1 was sent and still need part 2 (the OnEvent handler)
        std::unordered_set<ObjectGuid> _bootstrapListenerPending;   // protected by _queueLock
        std::vector<ObjectGuid> _pendingLeaves;                     // protected by _queueLock, handled in the world update
        bool _endAllRequested = false;
        std::vector<ObjectGuid> _preparationSkips;                  // protected by _queueLock, empty guid = every match

        std::vector<std::unique_ptr<Match>> _matches;
        std::unordered_map<ObjectGuid, Match*> _playerMatch;        // only changed from the world update
        std::unordered_map<ObjectGuid, PendingReturn> _pendingReturns; // world thread only (login and world update)
        uint32 _usedPhases = 0;
        uint32 _nextMatchId = 1;
        bool _forceStart = false;

        // config
        uint32 _playersPerTeam = 10;
        uint32 _minPlayersPerTeam = 10;
        uint32 _killBonus = 10;

        // client-side UI relabel (Warden bootstrap + addon messages), see README
        bool _clientUiEnabled = false;
        std::string _clientLuaFile;
        std::string _clientScript;
        uint32 _clientPingTimer = 0;
    };
}

#define sAcherusOrbs AcherusOrbs::Manager::instance()

#endif // TRINITY_ACHERUS_ORBS_H
