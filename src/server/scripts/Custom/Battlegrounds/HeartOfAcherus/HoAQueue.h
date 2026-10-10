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

#ifndef TRINITY_HOA_QUEUE_H
#define TRINITY_HOA_QUEUE_H

#include "HoADefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <array>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

class Player;

namespace HeartOfAcherus
{
    // one queue per faction, oldest first; used from the world and map threads
    class MatchQueue
    {
    public:
        using TeamPlayers = std::array<std::vector<Player*>, PVP_TEAMS_COUNT>;

        bool Add(ObjectGuid guid, TeamId team, ObjectGuid group = {}); // false when already queued; group keeps a party together
        bool Remove(ObjectGuid guid);
        bool Contains(ObjectGuid guid) const;
        std::array<std::size_t, PVP_TEAMS_COUNT> GetSizes() const;
        std::vector<ObjectGuid> GetAll() const;

        // connected players able to enter a match, up to freeSlots per team; a group is taken whole or not at all, and
        // dropOffline removes the disconnected ones
        TeamPlayers Collect(std::array<uint32, PVP_TEAMS_COUNT> const& freeSlots, bool dropOffline);

        // battlefield status slot of the fake queued status, kept while queued
        Optional<uint32> AssignStatusSlot(Player const* player);
        Optional<uint32> ReleaseStatusSlot(ObjectGuid guid);
        void ForgetStatusSlot(ObjectGuid guid);                     // drops the slot without sending a status

    private:
        struct Entry
        {
            ObjectGuid Guid;
            ObjectGuid Group;                                       // guid of the group leader, or the player when solo
        };

        mutable std::mutex _lock;
        std::array<std::deque<Entry>, PVP_TEAMS_COUNT> _queues;
        std::unordered_map<ObjectGuid, uint32> _statusSlots;
    };
}

#endif // TRINITY_HOA_QUEUE_H
