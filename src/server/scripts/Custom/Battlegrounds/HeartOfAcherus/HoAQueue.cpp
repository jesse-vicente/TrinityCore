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
#include <unordered_set>

namespace HeartOfAcherus
{
    namespace
    {
        bool IsEligibleForMatch(Player const* player)
        {
            return player->IsInWorld() && player->IsAlive() && !player->IsInFlight()
                && !player->IsBeingTeleported() && !player->InBattleground() && !player->InArena()
                && !player->InBattlegroundQueue() && !player->GetMap()->Instanceable();
        }
    }

    bool MatchQueue::Add(ObjectGuid guid, TeamId team, ObjectGuid group)
    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::deque<Entry> const& queue : _queues)
            for (Entry const& entry : queue)
                if (entry.Guid == guid)
                    return false;

        _queues[team].push_back({ guid, group.IsEmpty() ? guid : group });
        return true;
    }

    bool MatchQueue::Remove(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::deque<Entry>& queue : _queues)
        {
            auto itr = std::find_if(queue.begin(), queue.end(), [guid](Entry const& entry) { return entry.Guid == guid; });
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
        for (std::deque<Entry> const& queue : _queues)
            for (Entry const& entry : queue)
                if (entry.Guid == guid)
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
        for (std::deque<Entry> const& queue : _queues)
            for (Entry const& entry : queue)
                guids.push_back(entry.Guid);
        return guids;
    }

    // a group is taken whole or not at all, so its members never end up in different matches; a member that is offline
    // (when kept) or cannot enter right now makes the whole group wait
    MatchQueue::TeamPlayers MatchQueue::Collect(std::array<uint32, PVP_TEAMS_COUNT> const& freeSlots, bool dropOffline)
    {
        TeamPlayers players;
        std::lock_guard<std::mutex> lock(_lock);

        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            std::deque<Entry>& queue = _queues[team];

            if (dropOffline)
                for (auto itr = queue.begin(); itr != queue.end();)
                    if (!ObjectAccessor::FindConnectedPlayer(itr->Guid))
                        itr = queue.erase(itr);
                    else
                        ++itr;

            std::unordered_set<ObjectGuid> seenGroups;
            for (Entry const& first : queue)
            {
                if (!seenGroups.insert(first.Group).second)
                    continue;

                std::vector<Player*> group;
                bool blocked = false;
                for (Entry const& entry : queue)
                {
                    if (entry.Group != first.Group)
                        continue;

                    Player* player = ObjectAccessor::FindConnectedPlayer(entry.Guid);
                    if (!player || !IsEligibleForMatch(player))
                    {
                        blocked = true;
                        continue;
                    }
                    group.push_back(player);
                }

                if (!blocked && !group.empty() && players[team].size() + group.size() <= freeSlots[team])
                    players[team].insert(players[team].end(), group.begin(), group.end());
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

    void MatchQueue::ForgetStatusSlot(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _statusSlots.erase(guid);
    }
}
