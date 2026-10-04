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

#include "AcherusOrbs.h"
#include "Battlefield.h"
#include "BattlegroundPackets.h"
#include "Chat.h"
#include "ChatPackets.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"
#include <algorithm>
#include <cmath>

namespace AcherusOrbs
{
    std::array<OrbTemplate, MAX_ORBS> const OrbTemplates =
    {{
        { "Frost",  "ff69ccf0", Ids::GoFrostForge,  { 2493.37f, -5642.43f, 420.863f,  2.16421f  }, QuaternionData(0.0f, 0.0f,  0.882948f, 0.469471f), Spells::ForgeBeamFrost,  0, VisualKits::CarrierFrost,  Spells::CarrierAuraFrost  },
        { "Blood",  "ffff3030", Ids::GoBloodForge,  { 2427.28f, -5544.45f, 420.863f, -0.983229f }, QuaternionData(0.0f, 0.0f, -0.47205f,  0.881572f), Spells::ForgeBeamBlood,  0, VisualKits::CarrierBlood,  Spells::CarrierAuraBlood  },
        { "Unholy", "ff40ff40", Ids::GoUnholyForge, { 2509.31f, -5560.39f, 420.863f, -2.55402f  }, QuaternionData(0.0f, 0.0f, -0.957154f, 0.289578f), Spells::ForgeBeamUnholy, Spells::ForgeAuraUnholy, VisualKits::CarrierUnholy, Spells::CarrierAuraUnholy }
    }};

    namespace
    {
        constexpr uint8 FirstPhaseBit = 9;                          // phases 1..256 are used by Acherus quests
        constexpr uint8 LastPhaseBit = 31;
        constexpr float PreparationLeash = 13.0f;                  // beyond the walls: only catches who gets past them
        constexpr float PreparationDomeScale = 2.0f;               // Anti-Magic Zone is ~7 yards at scale 1
        constexpr uint8 PreparationWallCount = 8;                  // octagon around the dome
        constexpr float PreparationWallDistance = 10.0f;           // from the spawn to the middle of each wall, just inside the dome
        constexpr float HonorableKillRange = 40.0f;
        constexpr uint8 RequiredLevel = 80;
        constexpr float SpiritGuideOffset = 3.0f;                   // spirit guide stands in front of the respawn point
        // forges and their beams are seen from anywhere in the hall and outside it; only these objects, not the map setting
        constexpr VisibilityDistanceType ForgeVisibility = VisibilityDistanceType::Large;

        char const* TeamName(TeamId team)
        {
            return team == TEAM_ALLIANCE ? "Alliance" : "Horde";
        }

        ChatMsg TeamChatMsg(TeamId team)
        {
            return team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE;
        }

        float GetCarrierScale(OrbState const& state)
        {
            float const bonus = std::min(OrbPower::ScaleBase + OrbPower::ScalePerStack * float(state.Stacks - 1), OrbPower::ScaleMax);
            return state.CarrierOriginalScale * (1.0f + bonus);
        }

        void ApplyPermanentAura(Player* player, uint32 spellId)
        {
            if (!spellId)
                return;

            if (Aura* aura = player->AddAura(spellId, player))
            {
                aura->SetMaxDuration(-1);
                aura->SetDuration(-1);
            }
        }

        bool IsEligibleForMatch(Player const* player)
        {
            return player->IsInWorld() && player->IsAlive() && !player->IsInCombat() && !player->IsInFlight()
                && !player->IsBeingTeleported() && !player->InBattleground() && !player->InArena()
                && !player->GetMap()->Instanceable();
        }

        // Group only checks its battlefield pointer for null, never uses it. A battlefield raid is not stored in the
        // database, keeps the original group of its members and gives it back when they leave, like a battleground raid.
        // This one is never registered in the BattlefieldMgr nor updated, so no core change is needed.
        class RaidAnchor final : public Battlefield
        {
        public:
            void SendInitWorldStatesToAll() override { }
            void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) override { }
        };

        Battlefield* GetRaidAnchor()
        {
            static RaidAnchor anchor;
            return &anchor;
        }
    }

    Manager* Manager::instance()
    {
        static Manager instance;
        return &instance;
    }

    void Manager::LoadConfig()
    {
        _playersPerTeam = std::max(1, sConfigMgr->GetIntDefault("AcherusOrbs.PlayersPerTeam", 10));
        _minPlayersPerTeam = std::clamp<uint32>(sConfigMgr->GetIntDefault("AcherusOrbs.MinPlayersPerTeam", 10), 1, _playersPerTeam);
        _killBonus = std::max(0, sConfigMgr->GetIntDefault("AcherusOrbs.KillBonus", 10));
    }

    // ----------------------------------------------------------------- queue

    bool Manager::Enqueue(Player* player, std::string& error)
    {
        if (IsInMatch(player->GetGUID()))
        {
            error = "You are already in a battle for Acherus.";
            return false;
        }

        if (player->InBattleground() || player->InArena())
        {
            error = "You cannot queue while in a battleground or arena.";
            return false;
        }

        if (player->GetLevel() < RequiredLevel)
        {
            error = Trinity::StringFormat("You must be level {} to join the battle for Acherus.", RequiredLevel);
            return false;
        }

        TeamId const team = player->GetTeamId();
        if (team != TEAM_ALLIANCE && team != TEAM_HORDE)
            return false;

        std::lock_guard<std::mutex> lock(_queueLock);
        for (std::deque<ObjectGuid> const& queue : _queue)
        {
            if (std::find(queue.begin(), queue.end(), player->GetGUID()) != queue.end())
            {
                error = "You are already queued for the battle for Acherus.";
                return false;
            }
        }

        _queue[team].push_back(player->GetGUID());
        return true;
    }

    bool Manager::Dequeue(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        for (std::deque<ObjectGuid>& queue : _queue)
        {
            auto itr = std::find(queue.begin(), queue.end(), guid);
            if (itr != queue.end())
            {
                queue.erase(itr);
                return true;
            }
        }
        return false;
    }

    bool Manager::IsQueued(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        for (std::deque<ObjectGuid> const& queue : _queue)
            if (std::find(queue.begin(), queue.end(), guid) != queue.end())
                return true;
        return false;
    }

    std::array<std::size_t, PVP_TEAMS_COUNT> Manager::GetQueueSizes()
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        return { _queue[TEAM_ALLIANCE].size(), _queue[TEAM_HORDE].size() };
    }

    std::string Manager::GetStatus() const
    {
        std::string text = Trinity::StringFormat("Acherus orbs: {} match(es), {} per team (min {}), kill bonus {}",
            _matches.size(), _playersPerTeam, _minPlayersPerTeam, _killBonus);

        for (std::unique_ptr<Match> const& match : _matches)
        {
            char const* status = match->Status == MatchStatus::Preparation ? "preparation" : match->Status == MatchStatus::InProgress ? "in progress" : "ended";
            text += Trinity::StringFormat("\n#{} phase {} {} - {}s left - Alliance {} x {} Horde - {} players", match->Id, match->PhaseMask,
                status, match->StatusTimer / IN_MILLISECONDS, match->Score[TEAM_ALLIANCE], match->Score[TEAM_HORDE], match->Players.size());
        }

        return text;
    }

    // ----------------------------------------------------------------- update (world thread, maps are not being updated)

    void Manager::Update(uint32 diff)
    {
        ProcessRequests();
        ProcessPendingReturns(diff);
        FillOpenMatches();
        TryCreateMatch();

        for (std::unique_ptr<Match>& match : _matches)
            UpdateMatch(*match, diff);

        for (auto itr = _matches.begin(); itr != _matches.end();)
        {
            Match& match = **itr;
            if (match.Status != MatchStatus::Ended || match.StatusTimer)
            {
                ++itr;
                continue;
            }

            std::vector<ObjectGuid> players;
            for (auto const& [guid, matchPlayer] : match.Players)
                players.push_back(guid);

            for (ObjectGuid const& guid : players)
                RemovePlayer(match, guid, RemoveMode::TeleportOut);

            if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
                DespawnObjects(match, map);

            DisbandRaids(match);
            _usedPhases &= ~match.PhaseMask;
            TC_LOG_INFO("scripts", "AcherusOrbs: match {} finished", match.Id);
            itr = _matches.erase(itr);
        }
    }

    void Manager::ProcessRequests()
    {
        std::vector<ObjectGuid> leaves;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            std::swap(leaves, _pendingLeaves);
        }

        for (ObjectGuid const& guid : leaves)
            if (Match* match = GetMatch(guid))
                RemovePlayer(*match, guid, RemoveMode::TeleportOut);

        std::vector<ObjectGuid> skips;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            std::swap(skips, _preparationSkips);
        }

        // the match starts in its next update, as Battleground::SetStartDelayTime(0) does for .bg start
        for (ObjectGuid const& guid : skips)
        {
            for (std::unique_ptr<Match>& match : _matches)
                if (match->Status == MatchStatus::Preparation && (guid.IsEmpty() || match->Players.contains(guid)))
                    match->StatusTimer = 0;
        }

        if (_endAllRequested)
        {
            _endAllRequested = false;
            for (std::unique_ptr<Match>& match : _matches)
                if (match->Status != MatchStatus::Ended)
                    EndMatch(*match, TEAM_NEUTRAL);
        }
    }

    // like battlegrounds, queued players take the free places of the running matches before a new one is created
    void Manager::FillOpenMatches()
    {
        for (std::unique_ptr<Match>& match : _matches)
        {
            if (match->Status == MatchStatus::Ended)
                continue;

            // offline players keep their place, as in battlegrounds
            std::array<uint32, PVP_TEAMS_COUNT> teamSize = { };
            for (auto const& [guid, matchPlayer] : match->Players)
                ++teamSize[matchPlayer.Team];

            std::array<std::vector<Player*>, PVP_TEAMS_COUNT> joining;
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
                {
                    for (ObjectGuid const& guid : _queue[team])
                    {
                        if (teamSize[team] + joining[team].size() >= _playersPerTeam)
                            break;

                        if (Player* player = ObjectAccessor::FindConnectedPlayer(guid); player && IsEligibleForMatch(player))
                            joining[team].push_back(player);
                    }
                }
            }

            for (std::vector<Player*> const& team : joining)
            {
                for (Player* player : team)
                {
                    Dequeue(player->GetGUID());
                    AddPlayer(*match, player);
                    TC_LOG_INFO("scripts", "AcherusOrbs: {} joined match {} in progress", player->GetName(), match->Id);
                }
            }
        }
    }

    bool Manager::SkipPreparation(ObjectGuid guid)
    {
        if (!guid.IsEmpty() && !IsInMatch(guid))
            return false;

        std::lock_guard<std::mutex> lock(_queueLock);
        _preparationSkips.push_back(guid);
        return true;
    }

    void Manager::TryCreateMatch()
    {
        std::array<std::vector<Player*>, PVP_TEAMS_COUNT> ready;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            {
                for (auto itr = _queue[team].begin(); itr != _queue[team].end();)
                {
                    Player* player = ObjectAccessor::FindConnectedPlayer(*itr);
                    if (!player)
                    {
                        itr = _queue[team].erase(itr);
                        continue;
                    }

                    if (ready[team].size() < _playersPerTeam && IsEligibleForMatch(player))
                        ready[team].push_back(player);
                    ++itr;
                }
            }
        }

        bool const enough = ready[TEAM_ALLIANCE].size() >= _minPlayersPerTeam && ready[TEAM_HORDE].size() >= _minPlayersPerTeam;
        bool const forced = _forceStart && (!ready[TEAM_ALLIANCE].empty() || !ready[TEAM_HORDE].empty());
        if (!enough && !forced)
            return;

        uint32 phaseMask = 0;
        for (uint8 bit = FirstPhaseBit; bit <= LastPhaseBit && !phaseMask; ++bit)
            if (!(_usedPhases & (1u << bit)))
                phaseMask = 1u << bit;

        if (!phaseMask)
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: no free phase for a new match ({} running)", _matches.size());
            return;
        }

        _forceStart = false;
        _usedPhases |= phaseMask;

        std::unique_ptr<Match>& match = _matches.emplace_back(std::make_unique<Match>());
        match->Id = _nextMatchId++;
        match->PhaseMask = phaseMask;

        for (std::vector<Player*> const& team : ready)
        {
            for (Player* player : team)
            {
                Dequeue(player->GetGUID());
                AddPlayer(*match, player);
            }
        }

        TC_LOG_INFO("scripts", "AcherusOrbs: match {} created in phase {} ({} x {})", match->Id, phaseMask, ready[TEAM_ALLIANCE].size(), ready[TEAM_HORDE].size());
        StartPreparation(*match);
    }

    void Manager::UpdateMatch(Match& match, uint32 diff)
    {
        uint32 const oldTimer = match.StatusTimer;
        match.StatusTimer = match.StatusTimer > diff ? match.StatusTimer - diff : 0;

        // like battlegrounds, resurrection waves also run while the gates are closed
        if (match.Status != MatchStatus::Ended)
        {
            match.ResurrectTimer += diff;
            if (match.ResurrectTimer >= Timers::ResurrectWave)
            {
                match.ResurrectTimer = 0;
                ResurrectDead(match);
            }
        }

        match.PlayerCheckTimer += diff;
        if (match.PlayerCheckTimer >= Timers::PlayerCheck)
        {
            match.PlayerCheckTimer = 0;
            CheckPlayers(match);
        }

        switch (match.Status)
        {
            case MatchStatus::Preparation:
                if (oldTimer > MINUTE * IN_MILLISECONDS && match.StatusTimer <= MINUTE * IN_MILLISECONDS)
                    Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 1 minute.");
                else if (oldTimer > 30 * IN_MILLISECONDS && match.StatusTimer <= 30 * IN_MILLISECONDS)
                    Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 30 seconds. Prepare yourselves!");

                if (!match.StatusTimer)
                    StartMatch(match);
                break;
            case MatchStatus::InProgress:
            {
                UpdateCarriers(match, diff);
                UpdateBerserkBuff(match, diff);

                match.TickTimer += diff;
                if (match.TickTimer >= Scoring::TickInterval)
                {
                    match.TickTimer -= Scoring::TickInterval;
                    ScoreTick(match);
                }

                // points come from the ticks and from kills (map thread)
                uint32 const alliance = match.Score[TEAM_ALLIANCE];
                uint32 const horde = match.Score[TEAM_HORDE];
                if (alliance >= Scoring::MaxScore || horde >= Scoring::MaxScore)
                    EndMatch(match, alliance == horde ? TEAM_NEUTRAL : alliance > horde ? TEAM_ALLIANCE : TEAM_HORDE);
                else if (!match.StatusTimer)
                    EndMatch(match, alliance > horde ? TEAM_ALLIANCE : horde > alliance ? TEAM_HORDE : TEAM_NEUTRAL);
                break;
            }
            case MatchStatus::Ended:
                break;
        }
    }

    void Manager::CheckPlayers(Match& match)
    {
        std::vector<ObjectGuid> left;
        std::vector<ObjectGuid> expired;

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player)
            {
                // like battlegrounds, a disconnected player keeps his place for a while
                if (matchPlayer.Offline)
                {
                    matchPlayer.OfflineTimer += Timers::PlayerCheck;
                    if (matchPlayer.OfflineTimer >= Timers::OfflineGrace)
                        expired.push_back(guid);
                }
                continue;
            }

            if (!player->IsInWorld() || player->IsBeingTeleported())
                continue;

            // a ghost may be sent to a graveyard; it is brought back by the resurrect wave
            if (player->GetMapId() != Ids::MapId)
            {
                if (player->IsAlive())
                    left.push_back(guid);
                continue;
            }

            ApplyMatchState(match, player);
            UpdateRaid(match, matchPlayer, player);

            if (!matchPlayer.WorldStatesSent)
            {
                matchPlayer.WorldStatesSent = true;
                player->SendInitWorldStates(player->GetZoneId(), player->GetAreaId());
            }

            if (!player->IsAlive())
            {
                if (!matchPlayer.HandledDeath)
                {
                    matchPlayer.HandledDeath = true;
                    matchPlayer.LastCountdown = 0;
                    if (match.Status == MatchStatus::InProgress)
                        ++matchPlayer.Deaths;
                    if (Optional<OrbType> orb = GetCarriedOrb(match, guid))
                        DropOrb(match, *orb, true);
                }

                // the native spirit healer timer frame does not work outside of battlegrounds, released players get a countdown instead
                if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    SendResurrectCountdown(match, matchPlayer, player);
                continue;
            }

            matchPlayer.HandledDeath = false;
            matchPlayer.LastCountdown = 0;

            if (player->IsMounted() && GetCarriedOrb(match, guid))
            {
                player->RemoveAurasByType(SPELL_AURA_MOUNTED);
                player->Dismount();
            }
        }

        for (ObjectGuid const& guid : left)
            RemovePlayer(match, guid, RemoveMode::Left);

        for (ObjectGuid const& guid : expired)
            RemovePlayer(match, guid, RemoveMode::Logout);

        if (match.Status == MatchStatus::Preparation)
            KeepInPreparationArea(match);

        CheckRaids(match);
    }

    // ----------------------------------------------------------------- raids
    // A Group may disband itself inside RemoveMember, so only its GUID is kept and it is looked up on every use

    Group* Manager::GetRaid(Match const& match, TeamId team)
    {
        return match.Raids[team].IsEmpty() ? nullptr : sGroupMgr->GetGroupByGUID(match.Raids[team]);
    }

    // like Battleground::AddOrSetPlayerToCorrectBgGroup, also brings back players that left the raid or logged in again
    void Manager::UpdateRaid(Match& match, MatchPlayer const& matchPlayer, Player* player)
    {
        Group* raid = GetRaid(match, matchPlayer.Team);
        if (!raid)
        {
            raid = new Group();
            raid->SetBattlefieldGroup(GetRaidAnchor());
            if (!raid->Create(player))
            {
                delete raid;
                return;
            }

            sGroupMgr->AddGroup(raid);
            match.Raids[matchPlayer.Team] = raid->GetGUID();
            return;
        }

        if (!raid->IsMember(player->GetGUID()))
        {
            raid->AddMember(player);
            return;
        }

        if (player->GetGroup() != raid)
        {
            player->SetBattlegroundOrBattlefieldRaid(raid, raid->GetMemberGroup(player->GetGUID()));
            raid->SendUpdate();
        }
    }

    // nobody else stays in the raid, for example someone invited by its leader
    void Manager::CheckRaids(Match& match)
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            Group* raid = GetRaid(match, TeamId(team));
            if (!raid)
                continue;

            std::vector<ObjectGuid> outsiders;
            for (Group::MemberSlot const& slot : raid->GetMemberSlots())
            {
                auto itr = match.Players.find(slot.guid);
                if (itr == match.Players.end() || itr->second.Team != team)
                    outsiders.push_back(slot.guid);
            }

            for (ObjectGuid const& guid : outsiders)
                LeaveRaid(match, guid, TeamId(team));
        }
    }

    void Manager::LeaveRaid(Match& match, ObjectGuid guid, TeamId team)
    {
        if (Group* raid = GetRaid(match, team))
            if (raid->IsMember(guid))
                raid->RemoveMember(guid);
    }

    void Manager::DisbandRaids(Match& match)
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            if (Group* raid = GetRaid(match, TeamId(team)))
                raid->Disband();
            match.Raids[team].Clear();
        }
    }

    void Manager::ResurrectDead(Match& match)
    {
        for (auto& [guid, matchPlayer] : match.Players)
        {
            if (!matchPlayer.HandledDeath)
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported())
                continue;

            if (player->IsAlive())
            {
                matchPlayer.HandledDeath = false;
                continue;
            }

            // only released players are revived by the spirit guide
            if (!player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            matchPlayer.HandledDeath = false;
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();

            Position const& graveyard = GetGraveyard(match, matchPlayer.Team);
            player->TeleportTo(Ids::MapId, graveyard.GetPositionX(), graveyard.GetPositionY(), graveyard.GetPositionZ(), graveyard.GetOrientation());
        }
    }

    void Manager::UpdateCarriers(Match& match, uint32 diff)
    {
        for (OrbState& state : match.Orbs)
        {
            if (state.Carrier.IsEmpty())
                continue;

            state.StackTimer += diff;
            if (state.StackTimer < Timers::OrbStack)
                continue;

            state.StackTimer -= Timers::OrbStack;
            ++state.Stacks;

            if (Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier))
                player->SetObjectScale(GetCarrierScale(state));
        }
    }

    void Manager::ScoreTick(Match& match)
    {
        for (OrbState const& state : match.Orbs)
        {
            if (state.Carrier.IsEmpty())
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier);
            auto itr = match.Players.find(state.Carrier);
            if (!player || !player->IsAlive() || player->GetMapId() != Ids::MapId || itr == match.Players.end())
                continue;

            uint32& score = match.Score[itr->second.Team];
            uint32 const before = score;
            score = std::min(score + GetPointsForPosition(player), Scoring::MaxScore);
            itr->second.Points += score - before;
            if (score > before)
                player->GetSession()->SendAreaTriggerMessage("+%u points", score - before);
        }

        UpdateWorldStates(match);
    }

    uint32 Manager::GetPointsForPosition(Player const* player) const
    {
        float const dx = player->GetPositionX() - Positions::Center.GetPositionX();
        float const dy = player->GetPositionY() - Positions::Center.GetPositionY();
        float const z = player->GetPositionZ();
        float const distance = std::sqrt(dx * dx + dy * dy);

        if (z < Scoring::FloorMinZ || z > Scoring::FloorMaxZ)
            return Scoring::PointsOutside;

        if (distance <= Scoring::CenterRadius && z < Scoring::CenterMaxZ)
            return Scoring::PointsCenter;

        // the hall is round, except for the door that leads outside
        float const doorX = Positions::Door.GetPositionX() - Positions::Center.GetPositionX();
        float const doorY = Positions::Door.GetPositionY() - Positions::Center.GetPositionY();
        float const alongDoor = (dx * doorX + dy * doorY) / std::sqrt(doorX * doorX + doorY * doorY);

        if (distance <= Scoring::PlatformRadius && alongDoor <= Scoring::DoorDistance)
            return Scoring::PointsPlatform;

        return Scoring::PointsOutside;
    }

    // ----------------------------------------------------------------- match flow

    void Manager::StartPreparation(Match& match)
    {
        match.Status = MatchStatus::Preparation;
        match.StatusTimer = Timers::Preparation;

        if (Map* map = sMapMgr->CreateBaseMap(Ids::MapId))
            SpawnObjects(match, map);

        // the forges already glow during the preparation, the orbs can only be taken once the battle begins
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            SetForgeBeam(match, OrbType(orb), true);

        Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 2 minutes.");
    }

    void Manager::StartMatch(Match& match)
    {
        match.Status = MatchStatus::InProgress;
        match.StatusTimer = Timers::MatchDuration;
        match.TickTimer = 0;
        match.ResurrectTimer = 0;

        // the starting area spirit guides leave with the preparation, their ghosts are revived where they are
        if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
        {
            for (ObjectGuid& guid : match.PreparationSpiritGuides)
                DespawnCreature(map, guid);
            DespawnPreparationArea(match, map);
        }

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsAlive() || player->GetMapId() != Ids::MapId || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            matchPlayer.HandledDeath = false;
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
            player->NearTeleportTo(Positions::Spawn[matchPlayer.Team]);
        }

        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            SetForgeBeam(match, OrbType(orb), true);

        if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
            SpawnBerserkBuff(match, map);

        UpdateWorldStates(match);
        Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus has begun! Claim the orbs at the runeforges!");
        PlaySound(match, Sounds::BattleStart);
    }

    void Manager::EndMatch(Match& match, TeamId winner)
    {
        match.BattleTime = match.Status == MatchStatus::InProgress ? Timers::MatchDuration - match.StatusTimer : 0;
        match.Status = MatchStatus::Ended;
        match.StatusTimer = Timers::EndWait;
        match.Winner = winner;

        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
        {
            DropOrb(match, OrbType(orb), false);
            SetForgeBeam(match, OrbType(orb), false);
        }

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->IsInWorld() && !player->IsAlive() && player->GetMapId() == Ids::MapId)
            {
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }
        }

        UpdateWorldStates(match);

        for (auto& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendEndState(match, matchPlayer, player);

        if (winner == TEAM_NEUTRAL)
            Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus ended in a draw.");
        else
        {
            Announce(match, TeamChatMsg(winner), Trinity::StringFormat("The {} wins the battle for Acherus!", TeamName(winner)));
            PlaySound(match, winner == TEAM_ALLIANCE ? Sounds::AllianceWins : Sounds::HordeWins);
        }
    }

    // ----------------------------------------------------------------- players

    void Manager::AddPlayer(Match& match, Player* player)
    {
        MatchPlayer& matchPlayer = match.Players[player->GetGUID()];
        matchPlayer.Guid = player->GetGUID();
        matchPlayer.Team = player->GetTeamId();
        matchPlayer.Return.WorldRelocate(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        _playerMatch[player->GetGUID()] = &match;

        CharacterDatabase.PExecute("REPLACE INTO custom_acherus_orbs_return (guid, map, position_x, position_y, position_z, orientation) VALUES ({}, {}, {}, {}, {}, {})",
            player->GetGUID().GetCounter(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());

        if (player->IsMounted())
        {
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
            player->Dismount();
        }

        player->CombatStop();
        player->SetPhaseMask(match.PhaseMask, false);
        player->GetSession()->SendSetPhaseShift(match.PhaseMask);

        Position const& spawn = Positions::Spawn[matchPlayer.Team];
        player->TeleportTo(Ids::MapId, spawn.GetPositionX(), spawn.GetPositionY(), spawn.GetPositionZ(), spawn.GetOrientation());

        ChatHandler(player->GetSession()).SendSysMessage(match.Status == MatchStatus::InProgress
            ? "You are joining a battle for Acherus in progress." : "Your battle for Acherus is starting.");
        if (player->IsGameMaster())
            ChatHandler(player->GetSession()).SendSysMessage("You are in GM mode and see every phase. Use .gm off to play the match.");
    }

    void Manager::RemovePlayer(Match& match, ObjectGuid guid, RemoveMode mode)
    {
        auto itr = match.Players.find(guid);
        if (itr == match.Players.end())
            return;

        if (Optional<OrbType> orb = GetCarriedOrb(match, guid))
            DropOrb(match, *orb, true);

        // the player gets his original group back
        LeaveRaid(match, guid, itr->second.Team);

        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (player)
            ClearBattlefieldStatus(itr->second, player);

        WorldLocation const destination = itr->second.Return;
        match.Players.erase(itr);
        _playerMatch.erase(guid);

        // offline players keep the saved position, they are sent back on their next login
        if (mode != RemoveMode::Logout && player)
            CharacterDatabase.PExecute("DELETE FROM custom_acherus_orbs_return WHERE guid = {}", guid.GetCounter());

        if (!player)
            return;

        RestorePhase(player);
        player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);

        if (mode != RemoveMode::TeleportOut)
            return;

        if (!player->IsAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }

        if (!player->TeleportTo(destination))
        {
            player->SetClientControl(player, true);
            TC_LOG_ERROR("scripts", "AcherusOrbs: could not send {} back to map {} ({}, {}, {})", player->GetName(),
                destination.GetMapId(), destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
        }
    }

    void Manager::ApplyMatchState(Match const& match, Player* player) const
    {
        if (!player->IsGameMaster() && player->GetPhaseMask() != match.PhaseMask)
        {
            player->SetPhaseMask(match.PhaseMask, true);
            player->GetSession()->SendSetPhaseShift(match.PhaseMask);
        }

        player->RemoveAurasDueToSpell(Spells::UndyingResolve);

        // every participant runs like the death knights of Acherus, mounts are not allowed indoors. Same area as the
        // spell_area entry; the core removes it from others when they change area, and it is given back here
        if (player->IsAlive() && player->GetAreaId() == Ids::HallAreaId && !player->HasAura(Spells::DominionOverAcherus))
            player->AddAura(Spells::DominionOverAcherus, player);

        // the whole map is a sanctuary (AreaTableEntry::IsSanctuary), also covers players that were already in Acherus when they joined
        if (IsSanctuaryDisabled(player) && player->IsInSanctuary())
        {
            player->RemovePvpFlag(UNIT_BYTE2_FLAG_SANCTUARY);
            player->pvpInfo.IsInNoPvPArea = false;
        }

        if (!player->IsPvP())
            player->UpdatePvP(true, true);
    }

    bool Manager::IsSanctuaryDisabled(Player const* player) const
    {
        Match const* match = GetMatch(player->GetGUID());
        return match && match->Status != MatchStatus::Ended && player->GetMapId() == Ids::MapId;
    }

    bool Manager::OnRepop(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match || player->GetMapId() != Ids::MapId)
            return false;

        auto itr = match->Players.find(player->GetGUID());
        if (itr == match->Players.end())
            return false;

        // during the preparation the graveyard is inside the starting area, so nobody starts outside of it
        player->NearTeleportTo(GetGraveyard(*match, itr->second.Team));
        itr->second.LastCountdown = 0;
        return true;
    }

    Position const& Manager::GetGraveyard(Match const& match, TeamId team)
    {
        return match.Status == MatchStatus::Preparation ? Positions::Spawn[team] : Positions::Respawn[team];
    }

    void Manager::SendResurrectCountdown(Match const& match, MatchPlayer& matchPlayer, Player* player, bool force) const
    {
        static constexpr std::array<uint32, 8> Steps = { 30, 20, 10, 5, 4, 3, 2, 1 };

        uint32 const left = Timers::ResurrectWave - std::min(match.ResurrectTimer, Timers::ResurrectWave);
        uint32 const seconds = std::max<uint32>((left + IN_MILLISECONDS - 1) / IN_MILLISECONDS, 1);

        // first message right after the release or a new wave, then only when a step is reached
        bool show = force || !matchPlayer.LastCountdown || seconds > matchPlayer.LastCountdown;
        for (uint32 step : Steps)
            if (seconds <= step && step < matchPlayer.LastCountdown)
                show = true;

        if (!show || seconds == matchPlayer.LastCountdown)
            return;

        matchPlayer.LastCountdown = seconds;
        player->GetSession()->SendAreaTriggerMessage("Resurrection in %u second%s", seconds, seconds == 1 ? "" : "s");
    }

    void Manager::RestorePhase(Player* player)
    {
        // same as AuraEffect::HandlePhase
        uint32 phaseMask = 0;
        for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_PHASE))
            phaseMask |= effect->GetMiscValue();

        if (!phaseMask)
            phaseMask = PHASEMASK_NORMAL;

        if (player->IsGameMaster())
            phaseMask = PHASEMASK_ANYWHERE;

        player->SetPhaseMask(phaseMask, true);
        player->GetSession()->SendSetPhaseShift(phaseMask);
    }

    // ----------------------------------------------------------------- orbs

    void Manager::SpawnObjects(Match& match, Map* map)
    {
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
        {
            OrbTemplate const& orbTemplate = OrbTemplates[orb];
            OrbState& state = match.Orbs[orb];

            GameObject* forge = new GameObject();
            if (!forge->Create(map->GenerateLowGuid<HighGuid::GameObject>(), orbTemplate.ForgeEntry, map, match.PhaseMask, orbTemplate.ForgePosition, orbTemplate.ForgeRotation, 255, GO_STATE_READY))
            {
                TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create forge gameobject {} for match {}", orbTemplate.ForgeEntry, match.Id);
                delete forge;
            }
            else
            {
                forge->setActive(true);
                forge->SetVisibilityDistanceOverride(ForgeVisibility);
                if (map->AddToMap(forge))
                    state.Forge = forge->GetGUID();
                else
                    delete forge;
            }

            if (TempSummon* trigger = map->SummonCreature(Ids::NpcBeamTrigger, orbTemplate.ForgePosition))
            {
                trigger->SetPhaseMask(match.PhaseMask, true);
                trigger->setActive(true);
                trigger->SetVisibilityDistanceOverride(ForgeVisibility);
                state.Trigger = trigger->GetGUID();
            }
        }

        // starting area spirit guides only exist during the preparation, like the ones inside the Warsong Gulch bases
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            match.SpiritGuides[team] = SummonSpiritGuide(match, map, Positions::Respawn[team], TeamId(team));
            match.PreparationSpiritGuides[team] = SummonSpiritGuide(match, map, Positions::Spawn[team], TeamId(team));
            match.PreparationDomes[team] = SummonPreparationDome(match, map, Positions::Spawn[team]);
            SpawnPreparationWalls(match, map, Positions::Spawn[team]);
        }
    }

    ObjectGuid Manager::SummonSpiritGuide(Match const& match, Map* map, Position const& graveyard, TeamId team)
    {
        Position const position(graveyard.GetPositionX() + SpiritGuideOffset * std::cos(graveyard.GetOrientation()),
            graveyard.GetPositionY() + SpiritGuideOffset * std::sin(graveyard.GetOrientation()), graveyard.GetPositionZ(), graveyard.GetOrientation());

        uint32 const entry = team == TEAM_ALLIANCE ? Ids::NpcSpiritGuideAlliance : Ids::NpcSpiritGuideHorde;
        TempSummon* guide = map->SummonCreature(entry, position);
        if (!guide)
            return ObjectGuid::Empty;

        // same setup as Battleground::AddSpiritGuide
        guide->SetPhaseMask(match.PhaseMask, true);
        guide->setActive(true);
        guide->SetChannelObjectGuid(guide->GetGUID());
        guide->SetChannelSpellId(Spells::SpiritHealChannel);
        guide->SetModCastingSpeed(1.0f);
        return guide->GetGUID();
    }

    // the Anti-Magic Zone dome is a channel kit (SpellVisual 11242): it is only drawn while a unit channels the spell, so the
    // trigger "channels" it like the spirit guides do with their visual, and the aura itself is never applied
    ObjectGuid Manager::SummonPreparationDome(Match const& match, Map* map, Position const& center)
    {
        TempSummon* dome = map->SummonCreature(Ids::NpcPreparationDome, center);
        if (!dome)
            return ObjectGuid::Empty;

        // not summoned as a totem, so it casts nothing; kept friendly and passive anyway
        dome->SetFaction(FACTION_FRIENDLY);
        dome->SetReactState(REACT_PASSIVE);

        dome->SetPhaseMask(match.PhaseMask, true);
        dome->setActive(true);
        dome->SetVisibilityDistanceOverride(ForgeVisibility);
        dome->SetObjectScale(PreparationDomeScale);
        dome->SetChannelObjectGuid(dome->GetGUID());
        dome->SetChannelSpellId(Spells::PreparationDome);
        return dome->GetGUID();
    }

    // octagon of invisible collision walls (CollisionWallPvP01, ~11 yards wide at scale 1, the model extends along its
    // local Y axis) tangent to a circle around the spawn; the client blocks movement through them
    void Manager::SpawnPreparationWalls(Match& match, Map* map, Position const& center)
    {
        for (uint8 i = 0; i < PreparationWallCount; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(PreparationWallCount);
            Position const position(center.GetPositionX() + PreparationWallDistance * std::cos(angle),
                center.GetPositionY() + PreparationWallDistance * std::sin(angle), center.GetPositionZ(), angle);

            GameObject* wall = new GameObject();
            if (!wall->Create(map->GenerateLowGuid<HighGuid::GameObject>(), Ids::GoPreparationWall, map, match.PhaseMask, position,
                QuaternionData::fromEulerAnglesZYX(angle, 0.0f, 0.0f), 255, GO_STATE_READY))
            {
                TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create preparation wall gameobject {} for match {}", Ids::GoPreparationWall, match.Id);
                delete wall;
                continue;
            }

            wall->setActive(true);
            if (map->AddToMap(wall))
                match.PreparationWalls.push_back(wall->GetGUID());
            else
                delete wall;
        }
    }

    void Manager::DespawnPreparationArea(Match& match, Map* map)
    {
        for (ObjectGuid& guid : match.PreparationDomes)
            DespawnCreature(map, guid);

        for (ObjectGuid const& guid : match.PreparationWalls)
        {
            if (GameObject* wall = map->GetGameObject(guid))
            {
                wall->SetRespawnTime(0);
                wall->Delete();
            }
        }
        match.PreparationWalls.clear();
    }

    // backup of the walls, checked with the other player checks (every second) while the gates are closed
    void Manager::KeepInPreparationArea(Match& match)
    {
        for (auto const& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->GetMapId() != Ids::MapId || !player->IsAlive())
                continue;

            Position const& spawn = Positions::Spawn[matchPlayer.Team];
            if (player->GetExactDist2d(&spawn) > PreparationLeash)
                player->NearTeleportTo(spawn);
        }
    }

    void Manager::DespawnCreature(Map* map, ObjectGuid& guid)
    {
        if (Creature* creature = map->GetCreature(guid))
            creature->DespawnOrUnsummon();
        guid.Clear();
    }

    bool Manager::OnSpiritHealerQuery(Player* player, Creature* spiritHealer)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return false;

        auto itr = match->Players.find(player->GetGUID());
        if (itr == match->Players.end())
            return false;

        TeamId const team = itr->second.Team;
        if (match->SpiritGuides[team] != spiritHealer->GetGUID() && match->PreparationSpiritGuides[team] != spiritHealer->GetGUID())
            return false;

        // no SMSG_AREA_SPIRIT_HEALER_TIME: outside of battlegrounds the client keeps re-querying it, flooding until it disconnects
        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            SendResurrectCountdown(*match, itr->second, player, true);
        return true;
    }

    void Manager::DespawnObjects(Match& match, Map* map)
    {
        for (OrbState& state : match.Orbs)
        {
            if (GameObject* forge = map->GetGameObject(state.Forge))
            {
                forge->SetRespawnTime(0);
                forge->Delete();
            }

            DespawnCreature(map, state.Trigger);
            state.Forge.Clear();
        }

        if (GameObject* buff = map->GetGameObject(match.BerserkBuff))
            buff->Delete();
        match.BerserkBuff.Clear();

        for (ObjectGuid& guid : match.SpiritGuides)
            DespawnCreature(map, guid);

        for (ObjectGuid& guid : match.PreparationSpiritGuides)
            DespawnCreature(map, guid);

        DespawnPreparationArea(match, map);
    }

    void Manager::SpawnBerserkBuff(Match& match, Map* map)
    {
        match.BerserkBuffArmed = false;

        // facing the pit
        Position position = Positions::BerserkBuff;
        position.SetOrientation(position.GetAbsoluteAngle(Positions::Center));

        GameObject* buff = new GameObject();
        if (!buff->Create(map->GenerateLowGuid<HighGuid::GameObject>(), Ids::GoBerserkBuff, map, match.PhaseMask, position,
            QuaternionData::fromEulerAnglesZYX(position.GetOrientation(), 0.0f, 0.0f), 255, GO_STATE_READY))
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create berserk buff gameobject {} for match {}", Ids::GoBerserkBuff, match.Id);
            delete buff;
            match.BerserkBuffTimer = Timers::BuffRespawn;
            return;
        }

        buff->setActive(true);
        if (!map->AddToMap(buff))
        {
            delete buff;
            match.BerserkBuffTimer = Timers::BuffRespawn;
            return;
        }

        match.BerserkBuff = buff->GetGUID();
    }

    // battleground buffs are despawned and respawned by Battleground::HandleTriggerBuff. This one is a trap of type 1:
    // it casts its spell, then goes GO_JUST_DEACTIVATED and GO_NOT_READY, and would arm itself again after its cooldown.
    // A new trap also starts GO_NOT_READY, so it only counts as used after it was seen ready.
    void Manager::UpdateBerserkBuff(Match& match, uint32 diff)
    {
        Map* map = sMapMgr->FindMap(Ids::MapId, 0);
        if (!map)
            return;

        if (match.BerserkBuff.IsEmpty())
        {
            if (match.BerserkBuffTimer > diff)
            {
                match.BerserkBuffTimer -= diff;
                return;
            }

            match.BerserkBuffTimer = 0;
            SpawnBerserkBuff(match, map);
            return;
        }

        GameObject* buff = map->GetGameObject(match.BerserkBuff);
        if (buff)
        {
            LootState const state = buff->getLootState();
            if (state == GO_READY)
                match.BerserkBuffArmed = true;

            if (state == GO_READY || state == GO_ACTIVATED || !match.BerserkBuffArmed)
                return;

            buff->Delete();
        }

        match.BerserkBuff.Clear();
        match.BerserkBuffTimer = Timers::BuffRespawn;
    }

    void Manager::SetForgeBeam(Match& match, OrbType orb, bool on)
    {
        Map* map = sMapMgr->FindMap(Ids::MapId, 0);
        if (!map)
            return;

        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];

        Creature* trigger = map->GetCreature(state.Trigger);
        if (!trigger)
            return;

        for (uint32 spellId : { orbTemplate.ForgeBeam, orbTemplate.ForgeAura })
        {
            if (!spellId)
                continue;

            if (!on)
                trigger->RemoveAurasDueToSpell(spellId);
            else if (!trigger->HasAura(spellId))
                if (Aura* aura = trigger->AddAura(spellId, trigger))
                {
                    aura->SetMaxDuration(-1);
                    aura->SetDuration(-1);
                }
        }
    }

    void Manager::OnForgeUse(Player* player, GameObject* forge)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        Optional<OrbType> orb;
        for (uint8 i = 0; i < MAX_ORBS; ++i)
            if (match->Orbs[i].Forge == forge->GetGUID())
                orb = OrbType(i);

        if (!orb)
            return;

        ChatHandler handler(player->GetSession());
        if (match->Status != MatchStatus::InProgress)
            handler.SendSysMessage("The orbs are not active yet.");
        else if (!player->IsAlive())
            return;
        else if (!match->Orbs[*orb].Carrier.IsEmpty())
            handler.SendSysMessage("This orb is already taken.");
        else if (GetCarriedOrb(*match, player->GetGUID()))
            handler.SendSysMessage("You can only carry one orb.");
        else
            PickUpOrb(*match, *orb, player);
    }

    void Manager::PickUpOrb(Match& match, OrbType orb, Player* player)
    {
        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];

        state.Carrier = player->GetGUID();
        state.Stacks = 1;
        state.StackTimer = 0;
        state.CarrierOriginalScale = player->GetObjectScale();

        player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
        player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
        if (player->IsMounted())
        {
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
            player->Dismount();
        }

        player->SetObjectScale(GetCarrierScale(state));
        player->SendPlaySpellVisualKit(orbTemplate.CarrierVisualKit, 0);
        ApplyPermanentAura(player, orbTemplate.CarrierAura);

        SetForgeBeam(match, orb, false);
        UpdateWorldStates(match);

        Announce(match, CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("{} has taken the |c{}{}|r orb!", player->GetName(), orbTemplate.Color, orbTemplate.Name));
        PlaySound(match, Sounds::OrbEvent);
    }

    void Manager::DropOrb(Match& match, OrbType orb, bool announce)
    {
        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];
        if (state.Carrier.IsEmpty())
            return;

        if (Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier))
        {
            if (orbTemplate.CarrierAura)
                player->RemoveAurasDueToSpell(orbTemplate.CarrierAura);
            player->SetObjectScale(state.CarrierOriginalScale);
        }

        state.Carrier.Clear();
        state.Stacks = 0;
        state.StackTimer = 0;

        if (match.Status == MatchStatus::InProgress)
            SetForgeBeam(match, orb, true);

        UpdateWorldStates(match);

        if (announce)
        {
            Announce(match, CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("The |c{}{}|r orb has returned to its runeforge!", orbTemplate.Color, orbTemplate.Name));
            PlaySound(match, Sounds::OrbEvent);
        }
    }

    Optional<OrbType> Manager::GetCarriedOrb(Match const& match, ObjectGuid guid) const
    {
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            if (match.Orbs[orb].Carrier == guid)
                return OrbType(orb);
        return {};
    }

    OrbState const* Manager::GetCarriedOrbState(ObjectGuid guid) const
    {
        Match const* match = GetMatch(guid);
        if (!match || match->Status != MatchStatus::InProgress)
            return nullptr;

        for (OrbState const& state : match->Orbs)
            if (state.Carrier == guid)
                return &state;
        return nullptr;
    }

    uint32 Manager::GetHeldOrbCount(Match const& match, TeamId team) const
    {
        uint32 count = 0;
        for (OrbState const& state : match.Orbs)
        {
            if (state.Carrier.IsEmpty())
                continue;

            auto itr = match.Players.find(state.Carrier);
            if (itr != match.Players.end() && itr->second.Team == team)
                ++count;
        }
        return count;
    }

    // ----------------------------------------------------------------- hooks

    void Manager::OnPvPKill(Player* killer, Player* killed)
    {
        Match* match = GetMatch(killed->GetGUID());
        if (!match || match != GetMatch(killer->GetGUID()) || match->Status != MatchStatus::InProgress)
            return;

        MatchPlayer& killerData = match->Players[killer->GetGUID()];
        MatchPlayer const& killedData = match->Players[killed->GetGUID()];
        if (killerData.Team == killedData.Team)
            return;

        ++killerData.KillingBlows;

        for (auto& [guid, matchPlayer] : match->Players)
        {
            if (matchPlayer.Team != killerData.Team)
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->IsAlive() && player->GetMapId() == Ids::MapId && player->IsWithinDistInMap(killed, HonorableKillRange))
                ++matchPlayer.HonorableKills;
        }

        // any enemy player, carrying an orb or not; the victory is checked in the world update
        if (_killBonus)
        {
            uint32& score = match->Score[killerData.Team];
            uint32 const before = score;
            score = std::min(score + _killBonus, Scoring::MaxScore);
            killerData.Points += score - before;
            if (score > before)
                killer->GetSession()->SendAreaTriggerMessage("+%u points", score - before);
            UpdateWorldStates(*match);
        }
    }

    void Manager::OnUpdateZone(Player* player) const
    {
        if (Match const* match = GetMatch(player->GetGUID()))
            if (player->GetMapId() == Ids::MapId)
                ApplyMatchState(*match, player);
    }

    void Manager::OnLeaveRequest(Player* player)
    {
        if (!IsInMatch(player->GetGUID()))
            return;

        std::lock_guard<std::mutex> lock(_queueLock);
        _pendingLeaves.push_back(player->GetGUID());
    }

    // called before the player is saved, so the orb auras are never stored
    void Manager::OnBeforeLogout(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        if (match->Status == MatchStatus::Ended)
        {
            RemovePlayer(*match, player->GetGUID(), RemoveMode::Logout);
            return;
        }

        if (Optional<OrbType> orb = GetCarriedOrb(*match, player->GetGUID()))
            DropOrb(*match, *orb, true);

        MatchPlayer& matchPlayer = match->Players[player->GetGUID()];
        matchPlayer.Offline = true;
        matchPlayer.OfflineTimer = 0;
    }

    void Manager::OnLogout(Player* player)
    {
        Dequeue(player->GetGUID());
    }

    void Manager::OnLogin(Player* player)
    {
        // the orb is never kept across a login, its aura may still have been saved by a crash
        for (OrbTemplate const& orbTemplate : OrbTemplates)
        {
            if (orbTemplate.CarrierAura)
                player->RemoveAurasDueToSpell(orbTemplate.CarrierAura);
        }

        // back within MAX_OFFLINE_TIME, the player is still in the match where he logged out
        if (Match* match = GetMatch(player->GetGUID()))
        {
            MatchPlayer& matchPlayer = match->Players[player->GetGUID()];
            matchPlayer.Offline = false;
            matchPlayer.OfflineTimer = 0;
            matchPlayer.WorldStatesSent = false;
            if (player->GetMapId() == Ids::MapId)
                ApplyMatchState(*match, player);

            // logged out before the end, back while the final score is shown
            if (match->Status == MatchStatus::Ended)
                SendEndState(*match, matchPlayer, player);
            return;
        }

        QueryResult result = CharacterDatabase.PQuery("SELECT map, position_x, position_y, position_z, orientation FROM custom_acherus_orbs_return WHERE guid = {}", player->GetGUID().GetCounter());
        if (!result)
            return;

        // a far teleport sent while the client is still loading the world can be lost, it is done from the world update and confirmed there
        Field* fields = result->Fetch();
        PendingReturn& pending = _pendingReturns[player->GetGUID()];
        pending.Destination.WorldRelocate(fields[0].GetUInt16(), fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
        pending.Timer = 0;
        pending.Attempts = 0;
    }

    void Manager::ProcessPendingReturns(uint32 diff)
    {
        for (auto itr = _pendingReturns.begin(); itr != _pendingReturns.end();)
        {
            ObjectGuid const guid = itr->first;
            PendingReturn& pending = itr->second;

            // logged out again: the saved position is kept for the next login
            // a new match saved its own position, which replaced this one
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || IsInMatch(guid))
            {
                itr = _pendingReturns.erase(itr);
                continue;
            }

            if (!player->IsInWorld() || player->IsBeingTeleported())
            {
                ++itr;
                continue;
            }

            if (player->GetMapId() == pending.Destination.GetMapId() && player->GetExactDist(&pending.Destination) < 10.0f)
            {
                CharacterDatabase.PExecute("DELETE FROM custom_acherus_orbs_return WHERE guid = {}", guid.GetCounter());
                itr = _pendingReturns.erase(itr);
                continue;
            }

            pending.Timer += diff;
            if (pending.Timer < Timers::ReturnRetry)
            {
                ++itr;
                continue;
            }

            pending.Timer = 0;
            if (pending.Attempts >= Timers::ReturnMaxAttempts)
            {
                TC_LOG_ERROR("scripts", "AcherusOrbs: could not send {} back to map {} ({}, {}, {}), kept for the next login", player->GetName(),
                    pending.Destination.GetMapId(), pending.Destination.GetPositionX(), pending.Destination.GetPositionY(), pending.Destination.GetPositionZ());
                itr = _pendingReturns.erase(itr);
                continue;
            }

            ++pending.Attempts;
            if (!player->IsAlive())
            {
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }

            RestorePhase(player);
            player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);
            if (!player->TeleportTo(pending.Destination))
                TC_LOG_ERROR("scripts", "AcherusOrbs: teleport of {} back to map {} refused (attempt {})", player->GetName(), pending.Destination.GetMapId(), pending.Attempts);
            ++itr;
        }
    }

    void Manager::FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match || player->GetMapId() != Ids::MapId)
            return;

        packet.MapID = WorldStates::FakeMapId;
        packet.AreaID = WorldStates::FakeZoneId;
        packet.SubareaID = WorldStates::FakeZoneId;

        packet.Worldstates.emplace_back(WorldStates::AllianceTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::HordeTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::AllianceScore, match->Score[TEAM_ALLIANCE]);
        packet.Worldstates.emplace_back(WorldStates::HordeScore, match->Score[TEAM_HORDE]);
        packet.Worldstates.emplace_back(WorldStates::AllianceBases, GetHeldOrbCount(*match, TEAM_ALLIANCE));
        packet.Worldstates.emplace_back(WorldStates::HordeBases, GetHeldOrbCount(*match, TEAM_HORDE));
        packet.Worldstates.emplace_back(2565, 142);                 // sent by Eye of the Storm, purpose unknown
        packet.Worldstates.emplace_back(3085, 379);

        match->Players[player->GetGUID()].WorldStatesSent = true;
    }

    void Manager::ModifyDamage(Unit* attacker, Unit* victim, uint32& damage) const
    {
        if (!damage || victim->GetMapId() != Ids::MapId)
            return;

        float multiplier = 1.0f;

        if (Player* player = attacker ? attacker->ToPlayer() : nullptr)
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                multiplier *= 1.0f + OrbPower::DamageDonePct * state->Stacks / 100.0f;

        if (Player* player = victim->ToPlayer())
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                multiplier *= 1.0f + OrbPower::DamageTakenPct * state->Stacks / 100.0f;

        if (multiplier != 1.0f)
            damage = uint32(damage * multiplier);
    }

    void Manager::ModifyHealing(Unit* healer, Unit* receiver, uint32& gain)
    {
        if (!gain || receiver->GetMapId() != Ids::MapId)
            return;

        if (Player* player = receiver->ToPlayer())
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                gain = uint32(gain * std::max(0.0f, 1.0f + OrbPower::HealingTakenPct * state->Stacks / 100.0f));

        Player* player = healer ? healer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player)
            return;

        if (Match* match = GetMatch(player->GetGUID()))
            if (match->Status == MatchStatus::InProgress)
                match->Players[player->GetGUID()].HealingDone += gain;
    }

    void Manager::TrackDamage(Unit* attacker, Unit* victim, uint32 damage)
    {
        if (!damage || !attacker || victim->GetMapId() != Ids::MapId || !victim->ToPlayer())
            return;

        Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player || player == victim)
            return;

        Match* match = GetMatch(player->GetGUID());
        if (match && match->Status == MatchStatus::InProgress && match == GetMatch(victim->GetGUID()))
            match->Players[player->GetGUID()].DamageDone += damage;
    }

    // ----------------------------------------------------------------- client updates

    void Manager::UpdateWorldStates(Match& match)
    {
        uint32 const allianceOrbs = GetHeldOrbCount(match, TEAM_ALLIANCE);
        uint32 const hordeOrbs = GetHeldOrbCount(match, TEAM_HORDE);

        for (auto const& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->GetMapId() != Ids::MapId)
                continue;

            player->SendUpdateWorldState(WorldStates::AllianceScore, match.Score[TEAM_ALLIANCE]);
            player->SendUpdateWorldState(WorldStates::HordeScore, match.Score[TEAM_HORDE]);
            player->SendUpdateWorldState(WorldStates::AllianceBases, allianceOrbs);
            player->SendUpdateWorldState(WorldStates::HordeBases, hordeOrbs);
        }
    }

    // like Battleground::EndBattleground: final score, battlefield status for its timers, and the player can no longer move
    void Manager::SendEndState(Match& match, MatchPlayer& matchPlayer, Player* player)
    {
        if (!player->IsInWorld() || player->GetMapId() != Ids::MapId)
            return;

        SendScoreboard(match, player);
        SendBattlefieldStatus(match, matchPlayer, player);

        // the client gives the control back by itself on the teleport out
        player->SetClientControl(player, false);
    }

    void Manager::SendScoreboard(Match& match, Player* target /*= nullptr*/)
    {
        WorldPackets::Battleground::PVPMatchStatistics statistics;
        if (match.Winner)
            statistics.Winner = *match.Winner == TEAM_ALLIANCE ? PVP_TEAM_ALLIANCE : *match.Winner == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_NEUTRAL;

        for (auto const& [guid, matchPlayer] : match.Players)
        {
            WorldPackets::Battleground::PVPLogData_Player& data = statistics.Players.emplace_back();
            data.PlayerGUID = guid;
            data.Kills = matchPlayer.KillingBlows;
            data.HonorOrFaction = WorldPackets::Battleground::PVPLogData_Honor{ matchPlayer.HonorableKills, matchPlayer.Deaths, 0 };
            data.DamageDone = matchPlayer.DamageDone;
            data.HealingDone = matchPlayer.HealingDone;
            data.Stats = { matchPlayer.Points };                    // Eye of the Storm column (Flag Captures), shown because of the battlefield status
        }

        WorldPacket const* packet = statistics.Write();
        if (target)
        {
            target->SendDirectMessage(packet);
            return;
        }

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(packet);
    }

    // the score frame takes "Time Elapsed" and "Battleground closing in" from an active battlefield status, as sent by
    // Battleground::EndBattleground. It goes in a queue slot the player is not using for a real battleground.
    void Manager::SendBattlefieldStatus(Match const& match, MatchPlayer& matchPlayer, Player* player) const
    {
        if (!matchPlayer.StatusSlot)
        {
            for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
            {
                if (player->GetBattlegroundQueueTypeId(slot) == BATTLEGROUND_QUEUE_NONE)
                {
                    matchPlayer.StatusSlot = slot;
                    break;
                }
            }

            if (!matchPlayer.StatusSlot)
                return;
        }

        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());

        WorldPackets::Battleground::BattlefieldStatusActive status;
        status.Hdr.QueueSlot = *matchPlayer.StatusSlot;
        status.Hdr.QueueID = BattlegroundQueueTypeId{
            .BattlemasterListId = WorldStates::FakeBattlemasterListId,
            .BracketId = uint8(bracket ? bracket->GetBracketId() : 0),
            .TeamSize = 0
        }.GetPacked();
        status.Hdr.RangeMin = uint8(bracket ? bracket->MinLevel : player->GetLevel());
        status.Hdr.RangeMax = uint8(bracket ? bracket->MaxLevel : player->GetLevel());
        status.Hdr.InstanceID = match.Id;
        status.Mapid = WorldStates::FakeMapId;
        status.ShutdownTimer = match.StatusTimer;
        status.StartTimer = match.BattleTime;
        status.ArenaFaction = matchPlayer.Team == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_ALLIANCE;
        player->SendDirectMessage(status.Write());
    }

    void Manager::ClearBattlefieldStatus(MatchPlayer& matchPlayer, Player* player)
    {
        if (!matchPlayer.StatusSlot)
            return;

        WorldPackets::Battleground::BattlefieldStatusNone status;
        status.QueueSlot = *matchPlayer.StatusSlot;
        player->SendDirectMessage(status.Write());
        matchPlayer.StatusSlot.reset();
    }

    void Manager::Announce(Match& match, ChatMsg type, std::string const& text)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(type, LANG_UNIVERSAL, nullptr, nullptr, text);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }

    // like Battleground::PlaySoundToAll
    void Manager::PlaySound(Match& match, uint32 soundId)
    {
        WorldPackets::Misc::PlaySound packet(soundId);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }

    Match* Manager::GetMatch(ObjectGuid guid) const
    {
        auto itr = _playerMatch.find(guid);
        return itr != _playerMatch.end() ? itr->second : nullptr;
    }
}
