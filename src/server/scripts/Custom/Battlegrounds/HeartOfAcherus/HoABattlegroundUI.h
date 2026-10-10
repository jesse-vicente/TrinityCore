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

#ifndef TRINITY_HOA_BATTLEGROUND_UI_H
#define TRINITY_HOA_BATTLEGROUND_UI_H

#include "HoADefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <array>
#include <vector>

class Player;

namespace WorldPackets::WorldState
{
    class InitWorldStates;
}

// Native battleground frames reused by posing as Eye of the Storm: score frame, scoreboard and battlefield status
// (see docs/client-ui.md). Packets only, no state.
namespace HeartOfAcherus::BattlegroundUI
{
    using TeamValues = std::array<uint32, PVP_TEAMS_COUNT>;

    struct ScoreRow
    {
        ObjectGuid Guid;
        uint32 KillingBlows = 0;
        uint32 HonorableKills = 0;
        uint32 Deaths = 0;
        uint32 DamageDone = 0;
        uint32 HealingDone = 0;
        uint32 Points = 0;                                          // the Eye of the Storm column (Flag Captures)
    };

    void FillInitWorldStates(WorldPackets::WorldState::InitWorldStates& packet, TeamValues const& score, TeamValues const& runesHeld);
    void SendWorldStates(Player* player, TeamValues const& score, TeamValues const& runesHeld);
    void SendScoreboard(std::vector<Player*> const& targets, Optional<TeamId> winner, std::vector<ScoreRow> const& rows);

    BattlegroundQueueTypeId GetFakeQueueTypeId(Player const* player);
    bool IsFakeQueue(BattlegroundQueueTypeId queueTypeId);
    Optional<uint32> FindFreeStatusSlot(Player const* player);      // a queue slot not used by a real battleground
    void SendStatusActive(Player* player, uint32 slot, uint32 instanceId, uint32 shutdownTimer, uint32 startTimer, TeamId team);
    void SendStatusQueued(Player* player, uint32 slot);
    void SendStatusFailed(Player* player, GroupJoinBattlegroundResult result);
    void SendStatusNone(Player* player, uint32 slot);
}

#endif // TRINITY_HOA_BATTLEGROUND_UI_H
