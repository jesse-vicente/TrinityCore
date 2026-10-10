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

#include "HoABattlegroundUI.h"
#include "BattlegroundPackets.h"
#include "DBCStores.h"
#include "Player.h"
#include "WorldStatePackets.h"

namespace HeartOfAcherus::BattlegroundUI
{
    namespace
    {
        void FillStatusHeader(WorldPackets::Battleground::BattlefieldStatusHeader& header, Player const* player, uint32 slot)
        {
            PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());

            header.QueueSlot = slot;
            header.QueueID = GetFakeQueueTypeId(player).GetPacked();
            header.RangeMin = uint8(bracket ? bracket->MinLevel : player->GetLevel());
            header.RangeMax = uint8(bracket ? bracket->MaxLevel : player->GetLevel());
        }
    }

    void FillInitWorldStates(WorldPackets::WorldState::InitWorldStates& packet, TeamValues const& score, TeamValues const& runesHeld)
    {
        packet.MapID = WorldStates::FakeMapId;
        packet.AreaID = WorldStates::FakeZoneId;
        packet.SubareaID = WorldStates::FakeZoneId;

        packet.Worldstates.emplace_back(WorldStates::AllianceTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::HordeTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::AllianceScore, score[TEAM_ALLIANCE]);
        packet.Worldstates.emplace_back(WorldStates::HordeScore, score[TEAM_HORDE]);
        packet.Worldstates.emplace_back(WorldStates::AllianceRunes, runesHeld[TEAM_ALLIANCE]);
        packet.Worldstates.emplace_back(WorldStates::HordeRunes, runesHeld[TEAM_HORDE]);
        packet.Worldstates.emplace_back(2565, 142);                 // sent by Eye of the Storm, purpose unknown
        packet.Worldstates.emplace_back(3085, 379);
    }

    void SendWorldStates(Player* player, TeamValues const& score, TeamValues const& runesHeld)
    {
        player->SendUpdateWorldState(WorldStates::AllianceScore, score[TEAM_ALLIANCE]);
        player->SendUpdateWorldState(WorldStates::HordeScore, score[TEAM_HORDE]);
        player->SendUpdateWorldState(WorldStates::AllianceRunes, runesHeld[TEAM_ALLIANCE]);
        player->SendUpdateWorldState(WorldStates::HordeRunes, runesHeld[TEAM_HORDE]);
    }

    // with a winner the client opens the final "Alliance/Horde Wins" screen
    void SendScoreboard(std::vector<Player*> const& targets, Optional<TeamId> winner, std::vector<ScoreRow> const& rows)
    {
        WorldPackets::Battleground::PVPMatchStatistics statistics;
        if (winner)
            statistics.Winner = *winner == TEAM_ALLIANCE ? PVP_TEAM_ALLIANCE : *winner == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_NEUTRAL;

        for (ScoreRow const& row : rows)
        {
            WorldPackets::Battleground::PVPLogData_Player& data = statistics.Players.emplace_back();
            data.PlayerGUID = row.Guid;
            data.Kills = row.KillingBlows;
            data.HonorOrFaction = WorldPackets::Battleground::PVPLogData_Honor{ row.HonorableKills, row.Deaths, 0 };
            data.DamageDone = row.DamageDone;
            data.HealingDone = row.HealingDone;
            data.Stats = { row.Points };                            // the client expects the EotS column with the status
        }

        WorldPacket const* packet = statistics.Write();
        for (Player* player : targets)
            player->SendDirectMessage(packet);
    }

    // Eye of the Storm's battleground list id, so the client shows its frames
    BattlegroundQueueTypeId GetFakeQueueTypeId(Player const* player)
    {
        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());
        return BattlegroundQueueTypeId{
            .BattlemasterListId = WorldStates::FakeBattlemasterListId,
            .BracketId = uint8(bracket ? bracket->GetBracketId() : 0),
            .TeamSize = 0
        };
    }

    bool IsFakeQueue(BattlegroundQueueTypeId queueTypeId)
    {
        return queueTypeId.BattlemasterListId == WorldStates::FakeBattlemasterListId;
    }

    Optional<uint32> FindFreeStatusSlot(Player const* player)
    {
        for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
            if (player->GetBattlegroundQueueTypeId(slot) == BATTLEGROUND_QUEUE_NONE)
                return slot;

        return {};
    }

    // "Time Elapsed" and "Battleground closing in" of the score frame come from here, as in Battleground::EndBattleground
    void SendStatusActive(Player* player, uint32 slot, uint32 instanceId, uint32 shutdownTimer, uint32 startTimer, TeamId team)
    {
        WorldPackets::Battleground::BattlefieldStatusActive status;
        FillStatusHeader(status.Hdr, player, slot);
        status.Hdr.InstanceID = instanceId;
        status.Mapid = WorldStates::FakeMapId;
        status.ShutdownTimer = shutdownTimer;
        status.StartTimer = startTimer;
        status.ArenaFaction = team == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_ALLIANCE;
        player->SendDirectMessage(status.Write());
    }

    // gives queued players the minimap battleground button
    void SendStatusQueued(Player* player, uint32 slot)
    {
        WorldPackets::Battleground::BattlefieldStatusQueued status;
        FillStatusHeader(status.Hdr, player, slot);
        status.AverageWaitTime = 0;
        status.WaitTime = 0;
        player->SendDirectMessage(status.Write());
    }

    // the red error the client shows for a refused queue, like BattlegroundMgr::BuildBattlegroundStatusFailed
    void SendStatusFailed(Player* player, GroupJoinBattlegroundResult result)
    {
        WorldPackets::Battleground::BattlefieldStatusFailed status;
        status.Reason = result;
        player->SendDirectMessage(status.Write());
    }

    void SendStatusNone(Player* player, uint32 slot)
    {
        WorldPackets::Battleground::BattlefieldStatusNone status;
        status.QueueSlot = slot;
        player->SendDirectMessage(status.Write());
    }
}
