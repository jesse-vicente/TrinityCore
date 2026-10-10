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

#ifndef TRINITY_HOA_MGR_H
#define TRINITY_HOA_MGR_H

#include "HoAClientUI.h"
#include "HoADefines.h"
#include "HoAQueue.h"
#include "ObjectGuid.h"
#include "Position.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class GameObject;
class Player;
class Unit;

namespace WorldPackets::WorldState
{
    class InitWorldStates;
}

namespace HeartOfAcherus
{
    class Match;
    struct RuneState;

    struct Settings
    {
        uint32 PlayersPerTeam = 10;
        uint32 MinPlayersPerTeam = 10;
        uint32 KillBonus = 10;
        OutdoorSpellsMethod OutdoorSpells = OutdoorSpellsMethod::None;
    };

    // Entry point of the module: queue, matchmaking, players entering and leaving matches, and the script hooks.
    // The logic runs in the world update (WorldScript::OnUpdate, after the maps); map thread hooks only read the
    // player -> match index or change their own match, and cross-thread requests are queued under _requestLock.
    class Manager
    {
    public:
        static Manager* instance();

        void LoadConfig();
        void Update(uint32 diff);

        Settings const& GetSettings() const { return _settings; }
        ClientUI& GetClientUI() { return _clientUI; }

        // queue
        bool Enqueue(Player* player, std::string& error, GroupJoinBattlegroundResult& reason, ObjectGuid group = {});
        bool EnqueueGroup(Player* leader, std::string& error, GroupJoinBattlegroundResult& reason); // whole party/raid, same match
        bool Dequeue(ObjectGuid guid);
        bool IsQueued(ObjectGuid guid) const { return _queue.Contains(guid); }
        bool IsInQueue(ObjectGuid guid) const { return IsQueued(guid) || IsInvited(guid); } // includes the "Enter Battle" confirm
        std::array<std::size_t, PVP_TEAMS_COUNT> GetQueueSizes() const { return _queue.GetSizes(); }
        void ForceStart() { _forceStart = true; }
        void EndAll() { _endAllRequested = true; }
        bool SkipPreparation(ObjectGuid guid);                      // like .bg start; empty guid = every match
        std::string GetStatus() const;

        // players
        bool IsInMatch(ObjectGuid guid) const { return _playerMatch.contains(guid); }
        Match* GetMatch(ObjectGuid guid) const;
        void RemovePlayer(Match& match, ObjectGuid guid, RemoveMode mode);

        // hooks
        void OnForgeUse(Player* player, GameObject* forge);
        void OnPvPKill(Player* killer, Player* killed);
        void OnUpdateZone(Player* player) const;
        bool IsSanctuaryDisabled(Player const* player) const;
        bool IsOutdoorsForced(Player const* player) const;
        bool IsRuneCarrier(ObjectGuid guid) const { return GetCarriedRuneState(guid) != nullptr; }
        bool OnRepop(Player* player);
        bool OnSpiritHealerQuery(Player* player, Creature* spiritHealer);
        bool OnSpiritHealerQueue(Player* player, Creature* spiritHealer);
        void OnLeaveRequest(Player* player);
        bool CanJoinRealBattlegroundQueue(Player* player);           // false while queued or in a match (with the reason)
        void OnBeforeLogout(Player* player);
        void OnLogout(Player* player);
        void OnLogin(Player* player);
        void OnPVPLogDataRequest(Player* player);
        void OnRequestBattlefieldStatus(Player* player);
        bool OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite);
        void OnAddonMessage(Player* player, std::string const& msg, bool& handled) { _clientUI.OnAddonMessage(player, msg, handled); }
        void OnWardenLuaExecuted(Player* player) { _clientUI.OnWardenLuaExecuted(player); }
        void FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet);
        void ModifyDamage(Unit* attacker, Unit* victim, uint32& damage) const;
        void ModifyHealing(Unit* healer, Unit* receiver, uint32& gain);
        void TrackDamage(Unit* attacker, Unit* victim, uint32 damage);

    private:
        // player sent back to the position saved on joining, after a crash or a logout that outlived the match
        struct PendingReturn
        {
            WorldLocation Destination;
            uint32 Timer = 0;
            uint8 Attempts = 0;
        };

        // a player invited to an already created match, waiting for the "Enter Battle" answer
        struct Invite
        {
            uint32 MatchId = 0;
            TeamId Team = TEAM_ALLIANCE;
            uint32 TimeLeft = 0;
        };

        Manager();

        void ProcessRequests();
        void ProcessPendingReturns(uint32 diff);
        void TickInvites(uint32 diff);
        void FillOpenMatches();
        void TryCreateMatch();
        void RemoveFinishedMatches();
        void AddPlayer(Match& match, Player* player);
        bool CanEnqueue(Player* player, std::string& error, GroupJoinBattlegroundResult& reason) const; // Enqueue rules, no side effects
        void InvitePlayer(Player* player, Match& match);
        void CancelInvite(ObjectGuid guid);
        Match* FindMatchById(uint32 id) const;
        bool IsInvited(ObjectGuid guid) const { return _invites.find(guid) != _invites.end(); }
        void OnMountRequest(Player* player, uint32 spellId);
        void OnFormRequest(Player* player, uint32 spellId, bool keepActive);
        void CastForClientUI(Player* player, uint32 spellId);
        void SendQueueStatus(Player* player);
        void ClearQueueStatus(ObjectGuid guid, Player* player);
        void SendInviteStatus(Player* player, Invite const& invite);
        RuneState const* GetCarriedRuneState(ObjectGuid guid) const;
        std::vector<ObjectGuid> GetParticipants() const;

        Settings _settings;
        MatchQueue _queue;
        ClientUI _clientUI;

        std::vector<std::unique_ptr<Match>> _matches;
        std::unordered_map<ObjectGuid, Match*> _playerMatch;        // only changed in the world update
        std::unordered_map<ObjectGuid, Invite> _invites;            // invited players, waiting for the port answer
        std::unordered_map<ObjectGuid, PendingReturn> _pendingReturns; // world thread only (login and world update)
        uint32 _usedPhases = 0;
        uint32 _nextMatchId = 1;

        std::mutex _requestLock;
        std::vector<ObjectGuid> _pendingLeaves;
        std::vector<ObjectGuid> _preparationSkips;                  // empty guid = every match
        std::atomic<bool> _forceStart = false;
        std::atomic<bool> _endAllRequested = false;
    };
}

#define sHeartOfAcherusMgr HeartOfAcherus::Manager::instance()

#endif // TRINITY_HOA_MGR_H
