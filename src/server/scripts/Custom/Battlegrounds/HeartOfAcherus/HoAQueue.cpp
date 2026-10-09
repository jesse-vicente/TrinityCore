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

#include "HoAQueue.h"
#include "HoABattlegroundUI.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include <algorithm>

namespace HeartOfAcherus
{
    namespace
    {
        bool IsEligibleForMatch(Player const* player)
        {
            return player->IsInWorld() && player->IsAlive() && !player->IsInCombat() && !player->IsInFlight()
                && !player->IsBeingTeleported() && !player->InBattleground() && !player->InArena()
                && !player->InBattlegroundQueue() && !player->GetMap()->Instanceable();
        }
    }

    bool MatchQueue::Add(ObjectGuid guid, TeamId team)
    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::deque<ObjectGuid> const& queue : _queues)
            if (std::find(queue.begin(), queue.end(), guid) != queue.end())
                return false;

        _queues[team].push_back(guid);
        return true;
    }

    bool MatchQueue::Remove(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::deque<ObjectGuid>& queue : _queues)
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

    bool MatchQueue::Contains(ObjectGuid guid) const
    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::deque<ObjectGuid> const& queue : _queues)
            if (std::find(queue.begin(), queue.end(), guid) != queue.end())
                return true;
        return false;
    }

    std::array<std::size_t, PVP_TEAMS_COUNT> MatchQueue::GetSizes() const
    {
        std::lock_guard<std::mutex> lock(_lock);
        return { _queues[TEAM_ALLIANCE].size(), _queues[TEAM_HORDE].size() };
    }

    std::vector<ObjectGuid> MatchQueue::GetAll() const
    {
        std::lock_guard<std::mutex> lock(_lock);
        std::vector<ObjectGuid> guids;
        for (std::deque<ObjectGuid> const& queue : _queues)
            guids.insert(guids.end(), queue.begin(), queue.end());
        return guids;
    }

    MatchQueue::TeamPlayers MatchQueue::Collect(std::array<uint32, PVP_TEAMS_COUNT> const& freeSlots, bool dropOffline)
    {
        TeamPlayers players;
        std::lock_guard<std::mutex> lock(_lock);
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            for (auto itr = _queues[team].begin(); itr != _queues[team].end();)
            {
                Player* player = ObjectAccessor::FindConnectedPlayer(*itr);
                if (!player && dropOffline)
                {
                    itr = _queues[team].erase(itr);
                    continue;
                }

                if (player && players[team].size() < freeSlots[team] && IsEligibleForMatch(player))
                    players[team].push_back(player);
                ++itr;
            }
        }
        return players;
    }

    Optional<uint32> MatchQueue::AssignStatusSlot(Player const* player)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto itr = _statusSlots.find(player->GetGUID());
        if (itr != _statusSlots.end())
            return itr->second;

        Optional<uint32> slot = BattlegroundUI::FindFreeStatusSlot(player);
        if (slot)
            _statusSlots[player->GetGUID()] = *slot;
        return slot;
    }

    Optional<uint32> MatchQueue::ReleaseStatusSlot(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        auto itr = _statusSlots.find(guid);
        if (itr == _statusSlots.end())
            return {};

        uint32 const slot = itr->second;
        _statusSlots.erase(itr);
        return slot;
    }
}
