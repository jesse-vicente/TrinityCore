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

#include "HoAMgr.h"
#include "HoABattlegroundUI.h"
#include "HoALayout.h"
#include "HoAMatch.h"
#include "HoAUtil.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SmartEnum.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr uint8 FirstPhaseBit = 9;                          // phases 1..256 are used by the Acherus quests
        constexpr uint8 LastPhaseBit = 31;
        constexpr uint8 RequiredLevel = 80;
        constexpr float ReturnArrivalDistance = 10.0f;

        // same as AuraEffect::HandlePhase
        void RestorePhase(Player* player)
        {
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

        // what a player keeps of the match once out of it
        void RestorePlayer(Player* player)
        {
            RestorePhase(player);
            player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);
            player->RemoveAurasDueToSpell(Spells::AcherusDeathcharger);
        }

        void SaveReturnPosition(Player const* player)
        {
            CharacterDatabase.PExecute("REPLACE INTO custom_heart_of_acherus_return (guid, map, position_x, position_y, position_z, orientation) VALUES ({}, {}, {}, {}, {}, {})",
                player->GetGUID().GetCounter(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        }

        void DeleteReturnPosition(ObjectGuid guid)
        {
            CharacterDatabase.PExecute("DELETE FROM custom_heart_of_acherus_return WHERE guid = {}", guid.GetCounter());
        }

        char const* GetStatusName(MatchStatus status)
        {
            switch (status)
            {
                case MatchStatus::Preparation: return "preparation";
                case MatchStatus::InProgress: return "in progress";
                default: return "ended";
            }
        }
    }

    Manager::Manager()
    {
        _clientUI.SetParticipantCheck([this](ObjectGuid guid) { return IsQueued(guid) || IsInMatch(guid) || IsInvited(guid); });
        // "mount" (mount button) summons the Acherus Deathcharger, "mount <spell id>" (client hook) the player's own mount
        _clientUI.RegisterCommand("mount", [this](Player* player, std::string const& args)
        {
            if (_settings.OutdoorSpells == OutdoorSpellsMethod::None)
                return;

            if (args.empty())
                OnMountRequest(player, Spells::AcherusDeathcharger);
            else if (Optional<uint32> spellId = Trinity::StringTo<uint32>(args))
                OnMountRequest(player, *spellId);
        });

        _clientUI.RegisterCommand("dismount", [this](Player* player, std::string const& /*args*/)
        {
            if (_settings.OutdoorSpells != OutdoorSpellsMethod::None && player->IsMounted())
                Dismount(player);
        });

        // "form! <id>" comes from "/cast !<name>", which casts an active form again instead of leaving it
        for (bool keepActive : { false, true })
        {
            _clientUI.RegisterCommand(keepActive ? "form!" : "form", [this, keepActive](Player* player, std::string const& args)
            {
                if (_settings.OutdoorSpells == OutdoorSpellsMethod::None)
                    return;

                Optional<uint32> spellId = Trinity::StringTo<uint32>(args);
                if (spellId == Spells::TravelForm || spellId == Spells::GhostWolf)
                    OnFormRequest(player, *spellId, keepActive);
            });
        }
    }

    Manager* Manager::instance()
    {
        static Manager instance;
        return &instance;
    }

    void Manager::LoadConfig()
    {
        _settings.PlayersPerTeam = std::max(1, sConfigMgr->GetIntDefault("HeartOfAcherus.PlayersPerTeam", 10));
        _settings.MinPlayersPerTeam = std::clamp<uint32>(sConfigMgr->GetIntDefault("HeartOfAcherus.MinPlayersPerTeam", 10), 1, _settings.PlayersPerTeam);
        _settings.KillBonus = std::max(0, sConfigMgr->GetIntDefault("HeartOfAcherus.KillBonus", 10));

        _clientUI.Load(sConfigMgr->GetBoolDefault("HeartOfAcherus.ClientUI", false), {
            sConfigMgr->GetStringDefault("HeartOfAcherus.ClientLoginLuaFile", ""),
            sConfigMgr->GetStringDefault("HeartOfAcherus.ClientMatchLuaFile", "") });

        uint32 const outdoorSpellsMethod = sConfigMgr->GetIntDefault("HeartOfAcherus.OutdoorSpellsMethod", 0);
        _settings.OutdoorSpells = outdoorSpellsMethod < uint32(OutdoorSpellsMethod::Max) ? OutdoorSpellsMethod(outdoorSpellsMethod) : OutdoorSpellsMethod::None;
        if (outdoorSpellsMethod >= uint32(OutdoorSpellsMethod::Max))
            TC_LOG_ERROR("scripts", "HeartOfAcherus: unknown HeartOfAcherus.OutdoorSpellsMethod {}, outdoor spells disabled in the hall", outdoorSpellsMethod);

        // every method so far has a client side, in the match part
        if (_settings.OutdoorSpells != OutdoorSpellsMethod::None && !_clientUI.HasPart(PAYLOAD_MATCH))
        {
            TC_LOG_ERROR("scripts", "HeartOfAcherus: HeartOfAcherus.OutdoorSpellsMethod {} needs the client UI match part (HeartOfAcherus.ClientUI, HeartOfAcherus.ClientMatchLuaFile), outdoor spells disabled in the hall", outdoorSpellsMethod);
            _settings.OutdoorSpells = OutdoorSpellsMethod::None;
        }

        // read by the match part, sent again with it every time
        _clientUI.PrependToPart(PAYLOAD_MATCH, Trinity::StringFormat("AcherusBG_OutdoorSpellsMethod={}\n", uint32(_settings.OutdoorSpells)));
    }

    // ----------------------------------------------------------------- world update

    void Manager::Update(uint32 diff)
    {
        ProcessRequests();
        ProcessPendingReturns(diff);
        _clientUI.Update(diff);

        TickInvites(diff);

        FillOpenMatches();
        TryCreateMatch();

        if (_clientUI.IsPingDue(diff))
            _clientUI.Ping(GetParticipants());

        for (std::unique_ptr<Match>& match : _matches)
            match->Update(diff);

        RemoveFinishedMatches();
    }

    void Manager::ProcessRequests()
    {
        std::vector<ObjectGuid> leaves;
        std::vector<ObjectGuid> skips;
        {
            std::lock_guard<std::mutex> lock(_requestLock);
            std::swap(leaves, _pendingLeaves);
            std::swap(skips, _preparationSkips);
        }

        for (ObjectGuid const& guid : leaves)
            if (Match* match = GetMatch(guid))
                RemovePlayer(*match, guid, RemoveMode::TeleportOut);

        for (ObjectGuid const& guid : skips)
            for (std::unique_ptr<Match>& match : _matches)
                if (guid.IsEmpty() || match->GetPlayer(guid))
                    match->SkipPreparation();

        if (_endAllRequested.exchange(false))
            for (std::unique_ptr<Match>& match : _matches)
                if (match->GetStatus() != MatchStatus::Ended)
                    match->End(TEAM_NEUTRAL);
    }

    // like battlegrounds, the free places of the running matches are taken before a new match is created; offline
    // players keep their place
    void Manager::FillOpenMatches()
    {
        for (std::unique_ptr<Match>& match : _matches)
        {
            if (match->GetStatus() == MatchStatus::Ended)
                continue;

            std::array<uint32, PVP_TEAMS_COUNT> freeSlots;
            freeSlots.fill(_settings.PlayersPerTeam);
            for (auto const& [guid, matchPlayer] : match->GetPlayers())
                if (freeSlots[matchPlayer.Team])
                    --freeSlots[matchPlayer.Team];

            // a player invited but not yet entered already holds his place
            for (auto const& [guid, invite] : _invites)
                if (invite.MatchId == match->GetId() && freeSlots[invite.Team])
                    --freeSlots[invite.Team];

            for (std::vector<Player*> const& team : _queue.Collect(freeSlots, false))
                for (Player* player : team)
                    InvitePlayer(player, *match);
        }
    }

    void Manager::TryCreateMatch()
    {
        MatchQueue::TeamPlayers const ready = _queue.Collect({ _settings.PlayersPerTeam, _settings.PlayersPerTeam }, true);

        bool const enough = ready[TEAM_ALLIANCE].size() >= _settings.MinPlayersPerTeam && ready[TEAM_HORDE].size() >= _settings.MinPlayersPerTeam;
        bool const forced = _forceStart && (!ready[TEAM_ALLIANCE].empty() || !ready[TEAM_HORDE].empty());
        if (!enough && !forced)
            return;

        uint32 phaseMask = 0;
        for (uint8 bit = FirstPhaseBit; bit <= LastPhaseBit && !phaseMask; ++bit)
            if (!(_usedPhases & (1u << bit)))
                phaseMask = 1u << bit;

        if (!phaseMask)
        {
            TC_LOG_ERROR("scripts", "HeartOfAcherus: no free phase for a new match ({} running)", _matches.size());
            return;
        }

        _forceStart = false;
        _usedPhases |= phaseMask;

        Match& match = *_matches.emplace_back(std::make_unique<Match>(*this, _nextMatchId++, phaseMask));
        for (std::vector<Player*> const& team : ready)
            for (Player* player : team)
                InvitePlayer(player, match);

        TC_LOG_INFO("scripts", "HeartOfAcherus: match {} created in phase {} ({} x {})", match.GetId(), phaseMask, ready[TEAM_ALLIANCE].size(), ready[TEAM_HORDE].size());
        match.StartPreparation();
    }

    void Manager::RemoveFinishedMatches()
    {
        for (auto itr = _matches.begin(); itr != _matches.end();)
        {
            Match& match = **itr;
            if (!match.IsFinished())
            {
                ++itr;
                continue;
            }

            std::vector<ObjectGuid> players;
            for (auto const& [guid, matchPlayer] : match.GetPlayers())
                players.push_back(guid);

            for (ObjectGuid const& guid : players)
                RemovePlayer(match, guid, RemoveMode::TeleportOut);

            if (Map* map = match.GetMap())
            {
                match.GetHall().DespawnAll(map);
                match.GetGraveyards().DespawnAll(map);
            }

            match.GetRaids().Disband();
            _usedPhases &= ~match.GetPhaseMask();
            TC_LOG_INFO("scripts", "HeartOfAcherus: match {} finished", match.GetId());
            itr = _matches.erase(itr);
        }
    }

    // a far teleport sent while the client is still loading the world can be lost: done from here and confirmed
    void Manager::ProcessPendingReturns(uint32 diff)
    {
        for (auto itr = _pendingReturns.begin(); itr != _pendingReturns.end();)
        {
            ObjectGuid const guid = itr->first;
            PendingReturn& pending = itr->second;

            // logged out again (the saved position is kept), or a new match saved its own position
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

            if (player->GetMapId() == pending.Destination.GetMapId() && player->GetExactDist(&pending.Destination) < ReturnArrivalDistance)
            {
                DeleteReturnPosition(guid);
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
                TC_LOG_ERROR("scripts", "HeartOfAcherus: could not send {} back to map {} ({}, {}, {}), kept for the next login", player->GetName(),
                    pending.Destination.GetMapId(), pending.Destination.GetPositionX(), pending.Destination.GetPositionY(), pending.Destination.GetPositionZ());
                itr = _pendingReturns.erase(itr);
                continue;
            }

            ++pending.Attempts;
            if (!player->IsAlive())
                Revive(player);

            RestorePlayer(player);
            if (!player->TeleportTo(pending.Destination))
                TC_LOG_ERROR("scripts", "HeartOfAcherus: teleport of {} back to map {} refused (attempt {})", player->GetName(), pending.Destination.GetMapId(), pending.Attempts);
            ++itr;
        }
    }

    // ----------------------------------------------------------------- queue

    // invitees that let the "Enter Battle" popup expire are dropped, and a match left without players and invites is ended
    void Manager::TickInvites(uint32 diff)
    {
        for (auto itr = _invites.begin(); itr != _invites.end();)
        {
            if (itr->second.TimeLeft > diff)
            {
                itr->second.TimeLeft -= diff;
                ++itr;
                continue;
            }

            ObjectGuid const guid = itr->first;
            ClearQueueStatus(guid, ObjectAccessor::FindConnectedPlayer(guid));
            itr = _invites.erase(itr);
        }

        for (std::unique_ptr<Match>& match : _matches)
        {
            if (match->GetStatus() != MatchStatus::Preparation || !match->GetPlayers().empty())
                continue;

            bool invited = false;
            for (auto const& [guid, invite] : _invites)
                if (invite.MatchId == match->GetId())
                {
                    invited = true;
                    break;
                }

            if (!invited)
                match->End(TEAM_NEUTRAL);
        }
    }

    // the rules for entering the queue, without side effects; also used to validate a whole group before queueing any.
    // reason is the client-visible error (a GroupJoinBattlegroundResult shown in the client's error frame)
    bool Manager::CanEnqueue(Player* player, std::string& error, GroupJoinBattlegroundResult& reason) const
    {
        if (IsInMatch(player->GetGUID()))
        {
            error = "You are already in the battle for the Heart of Acherus.";
            reason = ERR_BATTLEGROUND_NOT_IN_BATTLEGROUND;
            return false;
        }

        if (IsQueued(player->GetGUID()))
        {
            error = "You are already queued for the battle for the Heart of Acherus.";
            reason = ERR_BATTLEGROUND_TOO_MANY_QUEUES;
            return false;
        }

        if (player->InBattleground() || player->InArena())
        {
            error = "You cannot queue while in a battleground or arena.";
            reason = ERR_BATTLEGROUND_NOT_IN_BATTLEGROUND;
            return false;
        }

        // a real queue owns one of the two queue slots the client shares with this mode
        if (player->InBattlegroundQueue())
        {
            error = "You cannot queue while in a battleground or arena queue.";
            reason = ERR_BATTLEGROUND_TOO_MANY_QUEUES;
            return false;
        }

        if (player->GetLevel() < RequiredLevel)
        {
            error = Trinity::StringFormat("You must be level {} to join the battle for the Heart of Acherus.", RequiredLevel);
            reason = ERR_BATTLEGROUND_JOIN_RANGE_INDEX;
            return false;
        }

        TeamId const team = player->GetTeamId();
        if (team != TEAM_ALLIANCE && team != TEAM_HORDE)
        {
            error = "Your team cannot join the Heart of Acherus.";
            reason = ERR_BATTLEGROUND_JOIN_FAILED;
            return false;
        }

        return true;
    }

    bool Manager::Enqueue(Player* player, std::string& error, GroupJoinBattlegroundResult& reason, ObjectGuid group)
    {
        if (!CanEnqueue(player, error, reason))
            return false;

        if (!_queue.Add(player->GetGUID(), player->GetTeamId(), group))
        {
            error = "You are already queued for the battle for the Heart of Acherus.";
            reason = ERR_BATTLEGROUND_TOO_MANY_QUEUES;
            return false;
        }

        SendQueueStatus(player);
        _clientUI.OnParticipantJoined(player);
        return true;
    }

    // queues the whole party/raid under the leader, so the group is taken into the same match; already queued members
    // are skipped, and a group bigger than a team is refused
    bool Manager::EnqueueGroup(Player* leader, std::string& error, GroupJoinBattlegroundResult& reason)
    {
        reason = ERR_BATTLEGROUND_JOIN_FAILED;

        Group* group = leader->GetGroup();
        if (!group)
        {
            error = "You are not in a group.";
            return false;
        }

        if (!group->IsLeader(leader->GetGUID()))
        {
            error = "Only the group leader can queue for the Heart of Acherus.";
            return false;
        }

        uint32 members = 0;
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (itr->GetSource())
                ++members;

        if (members > _settings.PlayersPerTeam)
        {
            error = Trinity::StringFormat("A group of {} cannot queue for the Heart of Acherus ({} per team).", members, _settings.PlayersPerTeam);
            return false;
        }

        // every online member must be able to queue, or the whole group stays out
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            GroupJoinBattlegroundResult memberReason = ERR_BATTLEGROUND_JOIN_FAILED;
            std::string memberError;
            if (!CanEnqueue(member, memberError, memberReason))
            {
                error = Trinity::StringFormat("{} cannot queue: {}", member->GetName(), memberError);
                return false;
            }
        }

        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->GetSource())
            {
                GroupJoinBattlegroundResult memberReason = ERR_BATTLEGROUND_JOIN_FAILED;
                std::string memberError;
                Enqueue(member, memberError, memberReason, leader->GetGUID());
            }

        return true;
    }

    bool Manager::Dequeue(ObjectGuid guid)
    {
        if (!_queue.Remove(guid))
            return false;

        ClearQueueStatus(guid, ObjectAccessor::FindConnectedPlayer(guid));
        return true;
    }

    bool Manager::SkipPreparation(ObjectGuid guid)
    {
        if (!guid.IsEmpty() && !IsInMatch(guid))
            return false;

        std::lock_guard<std::mutex> lock(_requestLock);
        _preparationSkips.push_back(guid);
        return true;
    }

    std::string Manager::GetStatus() const
    {
        std::string text = Trinity::StringFormat("Heart of Acherus: {} match(es), {} per team (min {}), kill bonus {}",
            _matches.size(), _settings.PlayersPerTeam, _settings.MinPlayersPerTeam, _settings.KillBonus);

        for (std::unique_ptr<Match> const& match : _matches)
        {
            std::array<uint32, PVP_TEAMS_COUNT> const& score = match->GetScore();
            text += Trinity::StringFormat("\n#{} phase {} {} - {}s left - Alliance {} x {} Horde - {} players", match->GetId(), match->GetPhaseMask(),
                GetStatusName(match->GetStatus()), match->GetStatusTimer() / IN_MILLISECONDS, score[TEAM_ALLIANCE], score[TEAM_HORDE], match->GetPlayers().size());
        }

        return text;
    }

    // the fake queued status gives the minimap button and the "Leave Queue" of the PvP frame
    void Manager::SendQueueStatus(Player* player)
    {
        Optional<uint32> slot = _queue.AssignStatusSlot(player);
        if (!slot)
            return;

        BattlegroundUI::SendStatusQueued(player, *slot);
        _clientUI.SetRelabel(player, true);
    }

    void Manager::ClearQueueStatus(ObjectGuid guid, Player* player)
    {
        Optional<uint32> slot = _queue.ReleaseStatusSlot(guid);
        if (!slot || !player)
            return;

        BattlegroundUI::SendStatusNone(player, *slot);
        _clientUI.SetRelabel(player, false);
    }

    // re-sends the "Enter Battle" popup, e.g. after a UI reload; the remaining time is kept
    void Manager::SendInviteStatus(Player* player, Invite const& invite)
    {
        Optional<uint32> slot = _queue.AssignStatusSlot(player);
        if (!slot)
            return;

        BattlegroundUI::SendStatusConfirm(player, *slot, invite.MatchId, WorldStates::FakeMapId, invite.TimeLeft);
        _clientUI.SetRelabel(player, true);
    }

    // takes a queued player out of the queue but keeps the status slot, and asks him to enter the created match
    void Manager::InvitePlayer(Player* player, Match& match)
    {
        _queue.Remove(player->GetGUID());

        Optional<uint32> slot = _queue.AssignStatusSlot(player);
        if (!slot)
            return;

        _invites[player->GetGUID()] = { match.GetId(), player->GetTeamId(), Timers::InviteWait };
        BattlegroundUI::SendStatusConfirm(player, *slot, match.GetId(), WorldStates::FakeMapId, Timers::InviteWait);
        _clientUI.SetRelabel(player, true);

        TC_LOG_INFO("scripts", "HeartOfAcherus: invited {} to match {}", player->GetName(), match.GetId());
    }

    void Manager::CancelInvite(ObjectGuid guid)
    {
        if (_invites.erase(guid))
            ClearQueueStatus(guid, ObjectAccessor::FindConnectedPlayer(guid));
    }

    Match* Manager::FindMatchById(uint32 id) const
    {
        for (std::unique_ptr<Match> const& match : _matches)
            if (match->GetId() == id)
                return match.get();

        return nullptr;
    }

    std::vector<ObjectGuid> Manager::GetParticipants() const
    {
        std::vector<ObjectGuid> guids;
        for (std::unique_ptr<Match> const& match : _matches)
            for (auto const& [guid, matchPlayer] : match->GetPlayers())
                guids.push_back(guid);

        std::vector<ObjectGuid> const queued = _queue.GetAll();
        guids.insert(guids.end(), queued.begin(), queued.end());

        for (auto const& [guid, invite] : _invites)
            guids.push_back(guid);

        return guids;
    }

    // ----------------------------------------------------------------- players

    Match* Manager::GetMatch(ObjectGuid guid) const
    {
        auto itr = _playerMatch.find(guid);
        return itr != _playerMatch.end() ? itr->second : nullptr;
    }

    void Manager::AddPlayer(Match& match, Player* player)
    {
        MatchPlayer& matchPlayer = match.GetPlayers()[player->GetGUID()];
        matchPlayer.Guid = player->GetGUID();
        matchPlayer.Team = player->GetTeamId();
        matchPlayer.Return.WorldRelocate(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        _playerMatch[player->GetGUID()] = &match;
        SaveReturnPosition(player);

        if (player->IsMounted())
            Dismount(player);

        player->CombatStop();
        player->SetPhaseMask(match.GetPhaseMask(), false);
        player->GetSession()->SendSetPhaseShift(match.GetPhaseMask());

        Position const& spawn = Positions::Spawn[matchPlayer.Team];
        player->TeleportTo(Ids::MapId, spawn.GetPositionX(), spawn.GetPositionY(), spawn.GetPositionZ(), spawn.GetOrientation());

        ChatHandler handler(player->GetSession());
        handler.SendSysMessage(match.GetStatus() == MatchStatus::InProgress
            ? "You are joining the battle for the Heart of Acherus in progress." : "Your battle for the Heart of Acherus is starting.");
        if (player->IsGameMaster())
            handler.SendSysMessage("You are in GM mode and see every phase. Use .gm off to play the match.");

        _clientUI.OnParticipantJoined(player);
    }

    void Manager::RemovePlayer(Match& match, ObjectGuid guid, RemoveMode mode)
    {
        MatchPlayer* matchPlayer = match.GetPlayer(guid);
        if (!matchPlayer)
            return;

        match.GetRunes().DropCarried(guid);

        // the original group comes back
        match.GetRaids().Leave(guid, matchPlayer->Team);

        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (player)
            match.ClearBattlefieldStatus(*matchPlayer, player);

        WorldLocation const destination = matchPlayer->Return;
        match.GetGraveyards().LeaveQueue(guid, player);
        match.GetPlayers().erase(guid);
        _playerMatch.erase(guid);

        // offline players keep the saved position for their next login
        if (mode != RemoveMode::Logout && player)
            DeleteReturnPosition(guid);

        if (!player)
            return;

        RestorePlayer(player);

        if (mode != RemoveMode::TeleportOut)
            return;

        if (!player->IsAlive())
            Revive(player);

        if (!player->TeleportTo(destination))
        {
            player->SetClientControl(player, true);
            TC_LOG_ERROR("scripts", "HeartOfAcherus: could not send {} back to map {} ({}, {}, {})", player->GetName(),
                destination.GetMapId(), destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
        }
    }

    // ----------------------------------------------------------------- hooks

    void Manager::OnForgeUse(Player* player, GameObject* forge)
    {
        if (Match* match = GetMatch(player->GetGUID()))
            match->GetRunes().OnForgeUse(player, forge);
    }

    void Manager::OnPvPKill(Player* killer, Player* killed)
    {
        Match* match = GetMatch(killed->GetGUID());
        if (match && match == GetMatch(killer->GetGUID()) && match->GetStatus() == MatchStatus::InProgress)
            match->OnKill(killer, killed);
    }

    void Manager::OnUpdateZone(Player* player) const
    {
        if (Match const* match = GetMatch(player->GetGUID()))
            if (player->GetMapId() == Ids::MapId)
                match->ApplyPlayerState(player);
    }

    bool Manager::IsSanctuaryDisabled(Player const* player) const
    {
        Match const* match = GetMatch(player->GetGUID());
        return match && match->AreHallRulesActive(player);
    }

    // with an outdoor spells method the hall counts as outdoors during a match, like the battlegrounds; the real
    // Acherus keeps its rule
    bool Manager::IsOutdoorsForced(Player const* player) const
    {
        if (_settings.OutdoorSpells == OutdoorSpellsMethod::None)
            return false;

        Match const* match = GetMatch(player->GetGUID());
        return match && match->AreHallRulesActive(player);
    }

    // the client refuses mount spells indoors by itself, so the client UI asks the server, which casts the mount with
    // its cast time: the player's own one (a known spell with a mount aura), or the Acherus Deathcharger for the mount
    // button and for the flying-only mounts, which have no ground speed. The client ignores the results of a cast it did
    // not start, so the usual refusals are checked here and shown with the client's own errors. Dismounting is its own request,
    // so a repeated click never mounts again right after
    void Manager::OnMountRequest(Player* player, uint32 spellId)
    {
        if (player->IsMounted())
            return;

        if (spellId != Spells::AcherusDeathcharger)
        {
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo || !player->HasSpell(spellId) || !spellInfo->HasAura(SPELL_AURA_MOUNTED))
                return;

            if (spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED))
                spellId = Spells::AcherusDeathcharger;
        }

        Match const* match = GetMatch(player->GetGUID());
        if (!match || !IsOutdoorsForced(player))
            _clientUI.ShowClientError(player, "SPELL_FAILED_NO_MOUNTS_ALLOWED");
        else if (!player->IsAlive())
            _clientUI.ShowClientError(player, "SPELL_FAILED_CASTER_DEAD");
        else if (match->GetRunes().GetCarried(player->GetGUID()))
            _clientUI.ShowClientError(player, "SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW");
        else if (player->IsInCombat())
            _clientUI.ShowClientError(player, "SPELL_FAILED_AFFECTING_COMBAT");
        else if (player->isMoving())
            _clientUI.ShowClientError(player, "SPELL_FAILED_MOVING");
        else if (player->IsInDisallowedMountForm())
            _clientUI.ShowClientError(player, "ERR_MOUNT_SHAPESHIFTED");
        else if (player->IsNonMeleeSpellCast(false))
            _clientUI.ShowClientError(player, "SPELL_FAILED_SPELL_IN_PROGRESS");
        else
            CastForClientUI(player, spellId);
    }

    // Travel Form and Ghost Wolf are outdoors only, refused by the client in the hall like the mounts: the client hook
    // asks for them after that refusal. A rune carrier may use them. The cast goes through the usual checks; its
    // result is shown here, as for the mount
    void Manager::OnFormRequest(Player* player, uint32 spellId, bool keepActive)
    {
        if (!player->HasSpell(spellId))
            return;

        if (!keepActive && player->HasAura(spellId))
        {
            player->RemoveAurasDueToSpell(spellId);
            return;
        }

        if (!IsOutdoorsForced(player))
        {
            _clientUI.ShowClientError(player, "SPELL_FAILED_ONLY_OUTDOORS");
            return;
        }

        // both are SPELL_ATTR0_NOT_SHAPESHIFT: the client leaves the current form (cat, bear, or the same one with "!")
        // before casting such a spell, the server cast has to do it too or SpellInfo::CheckShapeshift refuses it
        if (player->GetShapeshiftForm() != FORM_NONE)
            player->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);

        CastForClientUI(player, spellId);
    }

    // a cast the server starts for the client UI: the client ignores its result and does not start its own global
    // cooldown, so both are done here
    void Manager::CastForClientUI(Player* player, uint32 spellId)
    {
        switch (SpellCastResult const result = player->CastSpell(player, spellId, false))
        {
            case SPELL_CAST_OK:
                StartClientGlobalCooldown(player, spellId);
                break;
            case SPELL_FAILED_DONT_REPORT:
                break;
            case SPELL_FAILED_NO_POWER:
                // the client words it by power type; these spells cost mana
                _clientUI.ShowClientError(player, "ERR_OUT_OF_MANA");
                break;
            default:
                // the GlobalStrings name of a cast result is its constant name
                _clientUI.ShowClientError(player, EnumUtils::ToConstant(result));
                break;
        }
    }

    bool Manager::OnRepop(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        return match && match->GetGraveyards().OnRepop(player);
    }

    bool Manager::OnSpiritHealerQuery(Player* player, Creature* spiritHealer)
    {
        Match* match = GetMatch(player->GetGUID());
        return match && match->GetGraveyards().OnSpiritHealerQuery(player, spiritHealer);
    }

    bool Manager::OnSpiritHealerQueue(Player* player, Creature* spiritHealer)
    {
        Match* match = GetMatch(player->GetGUID());
        return match && match->GetGraveyards().OnSpiritHealerQueue(player, spiritHealer);
    }

    // "Leave Battleground" of the final score, handled in the world update
    void Manager::OnLeaveRequest(Player* player)
    {
        if (!IsInMatch(player->GetGUID()))
            return;

        std::lock_guard<std::mutex> lock(_requestLock);
        _pendingLeaves.push_back(player->GetGUID());
    }

    // the PvP queues exclude the Acherus one: while queued or in a match the player cannot join a real battleground
    // (the OnJoinBattlegroundQueue hook sends the client error, a GroupJoinBattlegroundResult)
    bool Manager::CanJoinRealBattlegroundQueue(Player* player)
    {
        return !IsQueued(player->GetGUID()) && !IsInMatch(player->GetGUID());
    }

    // before the player is saved, so the rune auras are never stored
    void Manager::OnBeforeLogout(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        if (match->GetStatus() == MatchStatus::Ended)
        {
            RemovePlayer(*match, player->GetGUID(), RemoveMode::Logout);
            return;
        }

        match->GetRunes().DropCarried(player->GetGUID());

        if (MatchPlayer* matchPlayer = match->GetPlayer(player->GetGUID()))
        {
            matchPlayer->Offline = true;
            matchPlayer->OfflineTimer = 0;
        }
    }

    void Manager::OnLogout(Player* player)
    {
        CancelInvite(player->GetGUID());
        Dequeue(player->GetGUID());
        _clientUI.OnLogout(player->GetGUID());
    }

    void Manager::OnLogin(Player* player)
    {
        _clientUI.OnLogin(player);
        MatchRunes::RemoveCarrierAuras(player);

        // back within the offline grace: still in the match
        if (Match* match = GetMatch(player->GetGUID()))
        {
            if (MatchPlayer* matchPlayer = match->GetPlayer(player->GetGUID()))
            {
                matchPlayer->Offline = false;
                matchPlayer->OfflineTimer = 0;
                matchPlayer->WorldStatesSent = false;
                if (player->GetMapId() == Ids::MapId)
                    match->ApplyPlayerState(player);

                // logged out before the end, back while the final score is shown
                if (match->GetStatus() == MatchStatus::Ended)
                    match->SendEndState(*matchPlayer, player);
            }
            return;
        }

        QueryResult result = CharacterDatabase.PQuery("SELECT map, position_x, position_y, position_z, orientation FROM custom_heart_of_acherus_return WHERE guid = {}", player->GetGUID().GetCounter());
        if (!result)
            return;

        Field* fields = result->Fetch();
        PendingReturn& pending = _pendingReturns[player->GetGUID()];
        pending.Destination.WorldRelocate(fields[0].GetUInt16(), fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
        pending.Timer = 0;
        pending.Attempts = 0;
    }

    // the client asks for the scoreboard with MSG_PVP_LOG_DATA, which nothing answers outside a real battleground
    void Manager::OnPVPLogDataRequest(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (match && player->GetMapId() == Ids::MapId && match->GetPlayer(player->GetGUID()))
            match->SendScoreboard(player);
    }

    // CMSG_BATTLEFIELD_STATUS, also when the UI loads: keeps the minimap button of the mode
    void Manager::OnRequestBattlefieldStatus(Player* player)
    {
        _clientUI.OnStatusRequest(player);

        if (Match* match = GetMatch(player->GetGUID()))
        {
            if (player->GetMapId() != Ids::MapId)
                return;

            if (MatchPlayer* matchPlayer = match->GetPlayer(player->GetGUID()))
                match->SendBattlefieldStatus(*matchPlayer, player);
            return;
        }

        if (auto itr = _invites.find(player->GetGUID()); itr != _invites.end())
        {
            SendInviteStatus(player, itr->second);
            return;
        }

        if (IsQueued(player->GetGUID()))
            SendQueueStatus(player);
    }

    // CMSG_BATTLEFIELD_PORT: accepting enters the invited match; declining (or "Leave Queue") drops the player
    bool Manager::OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite)
    {
        BattlegroundQueueTypeId const queueTypeId = BattlegroundQueueTypeId::FromPacked(queueID);
        if (!BattlegroundUI::IsFakeQueue(queueTypeId))
            return false;

        // a real queue with the same id owns the slot
        if (player->GetBattlegroundQueueIndex(queueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            return false;

        ObjectGuid const guid = player->GetGUID();

        if (auto itr = _invites.find(guid); itr != _invites.end())
        {
            uint32 const matchId = itr->second.MatchId;
            _invites.erase(itr);

            if (acceptedInvite)
            {
                if (Match* match = FindMatchById(matchId))
                {
                    _queue.ForgetStatusSlot(guid);
                    AddPlayer(*match, player);
                }
                else
                    ClearQueueStatus(guid, player);
            }
            else
                ClearQueueStatus(guid, player);
            return true;
        }

        if (acceptedInvite || !IsQueued(guid))
            return false;

        Dequeue(guid);
        return true;
    }

    void Manager::FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet)
    {
        Match* match = GetMatch(player->GetGUID());
        if (match && player->GetMapId() == Ids::MapId)
            match->FillInitWorldStates(player, packet);
    }

    RuneState const* Manager::GetCarriedRuneState(ObjectGuid guid) const
    {
        Match const* match = GetMatch(guid);
        if (!match || match->GetStatus() != MatchStatus::InProgress)
            return nullptr;

        return match->GetRunes().GetCarriedState(guid);
    }

    void Manager::ModifyDamage(Unit* attacker, Unit* victim, uint32& damage) const
    {
        if (!damage || victim->GetMapId() != Ids::MapId)
            return;

        float multiplier = 1.0f;

        if (Player* player = attacker ? attacker->ToPlayer() : nullptr)
            if (RuneState const* state = GetCarriedRuneState(player->GetGUID()))
                multiplier *= MatchRunes::GetDamageDoneMultiplier(state->Stacks);

        if (Player* player = victim->ToPlayer())
            if (RuneState const* state = GetCarriedRuneState(player->GetGUID()))
                multiplier *= MatchRunes::GetDamageTakenMultiplier(state->Stacks);

        if (multiplier != 1.0f)
            damage = uint32(damage * multiplier);
    }

    // healing taken by carriers, and the healing done for the scoreboard
    void Manager::ModifyHealing(Unit* healer, Unit* receiver, uint32& gain)
    {
        if (!gain || receiver->GetMapId() != Ids::MapId)
            return;

        if (Player* player = receiver->ToPlayer())
            if (RuneState const* state = GetCarriedRuneState(player->GetGUID()))
                gain = uint32(gain * MatchRunes::GetHealingTakenMultiplier(state->Stacks));

        Player* player = healer ? healer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player)
            return;

        if (Match* match = GetMatch(player->GetGUID()))
            if (match->GetStatus() == MatchStatus::InProgress)
                match->AddHealingDone(player->GetGUID(), gain);
    }

    // damage done to players, for the scoreboard
    void Manager::TrackDamage(Unit* attacker, Unit* victim, uint32 damage)
    {
        if (!damage || !attacker || victim->GetMapId() != Ids::MapId || !victim->ToPlayer())
            return;

        Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player || player == victim)
            return;

        Match* match = GetMatch(player->GetGUID());
        if (match && match->GetStatus() == MatchStatus::InProgress && match == GetMatch(victim->GetGUID()))
            match->AddDamageDone(player->GetGUID(), damage);
    }
}
