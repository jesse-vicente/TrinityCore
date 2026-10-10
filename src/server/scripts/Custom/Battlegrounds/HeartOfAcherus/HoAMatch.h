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

#ifndef TRINITY_HOA_MATCH_H
#define TRINITY_HOA_MATCH_H

#include "HoADefines.h"
#include "HoAGraveyards.h"
#include "HoAHall.h"
#include "HoARaids.h"
#include "HoARunes.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include <array>
#include <string>
#include <unordered_map>

class Map;
class Player;

namespace WorldPackets::WorldState
{
    class InitWorldStates;
}

namespace HeartOfAcherus
{
    class ClientUI;
    class Manager;

    struct MatchPlayer
    {
        ObjectGuid Guid;
        TeamId Team = TEAM_ALLIANCE;
        WorldLocation Return;
        bool HandledDeath = false;
        bool WorldStatesSent = false;
        bool Offline = false;
        bool ForgesLocked = false;                                  // forges sent to this client as not usable
        uint32 OfflineTimer = 0;
        Optional<uint32> StatusSlot;                                // battlefield status slot of the score frame
        uint32 KillingBlows = 0;
        uint32 HonorableKills = 0;
        uint32 Deaths = 0;
        uint32 DamageDone = 0;
        uint32 HealingDone = 0;
        uint32 Points = 0;                                          // team points scored by this player
    };

    // One match in its own phase of the hall: preparation, battle, final score
    class Match
    {
    public:
        using PlayerMap = std::unordered_map<ObjectGuid, MatchPlayer>;

        Match(Manager& manager, uint32 id, uint32 phaseMask);
        Match(Match const&) = delete;
        Match& operator=(Match const&) = delete;

        uint32 GetId() const { return _id; }
        uint32 GetPhaseMask() const { return _phaseMask; }
        MatchStatus GetStatus() const { return _status; }
        uint32 GetStatusTimer() const { return _statusTimer; }
        std::array<uint32, PVP_TEAMS_COUNT> const& GetScore() const { return _score; }
        bool IsFinished() const { return _status == MatchStatus::Ended && !_statusTimer; }
        Map* GetMap() const;
        ClientUI& GetClientUI() const;

        PlayerMap& GetPlayers() { return _players; }
        PlayerMap const& GetPlayers() const { return _players; }
        MatchPlayer* GetPlayer(ObjectGuid guid);
        MatchPlayer const* GetPlayer(ObjectGuid guid) const;

        MatchHall& GetHall() { return _hall; }
        MatchRunes& GetRunes() { return _runes; }
        MatchRunes const& GetRunes() const { return _runes; }
        MatchGraveyards& GetGraveyards() { return _graveyards; }
        MatchRaids& GetRaids() { return _raids; }

        void StartPreparation();
        void SkipPreparation();                                     // the battle begins on the next update
        void Update(uint32 diff);
        void End(TeamId winner);                                    // TEAM_NEUTRAL = draw

        bool AreHallRulesActive(Player const* player) const;        // no sanctuary, outdoors with an outdoor spells method
        void ApplyPlayerState(Player* player) const;
        void OnKill(Player* killer, Player* killed);
        void AddDamageDone(ObjectGuid guid, uint32 damage);
        void AddHealingDone(ObjectGuid guid, uint32 healing);

        void FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet);
        void UpdateWorldStates();
        void SendScoreboard(Player* target = nullptr);
        void SendEndState(MatchPlayer& matchPlayer, Player* player);
        void SendBattlefieldStatus(MatchPlayer& matchPlayer, Player* player);
        void ClearBattlefieldStatus(MatchPlayer& matchPlayer, Player* player);
        void Announce(ChatMsg type, std::string const& text);
        void PlaySound(uint32 soundId);

    private:
        void StartBattle();
        void CheckPlayers();
        void ScoreTick();
        void AwardPoints(MatchPlayer& matchPlayer, Player* player, uint32 points);

        Manager& _manager;
        uint32 _id;
        uint32 _phaseMask;
        MatchStatus _status = MatchStatus::Preparation;
        uint32 _statusTimer = 0;                                    // time left in the current status
        uint32 _tickTimer = 0;
        uint32 _playerCheckTimer = 0;
        uint32 _markerTimer = 0;
        uint32 _battleTime = 0;                                     // battle time played, set when the match ends
        std::array<uint32, PVP_TEAMS_COUNT> _score = { };
        Optional<TeamId> _winner;
        PlayerMap _players;

        MatchHall _hall;
        MatchRunes _runes;
        MatchGraveyards _graveyards;
        MatchRaids _raids;
    };
}

#endif // TRINITY_HOA_MATCH_H
