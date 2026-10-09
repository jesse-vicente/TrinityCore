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

#ifndef TRINITY_HOA_RAIDS_H
#define TRINITY_HOA_RAIDS_H

#include "HoADefines.h"
#include "ObjectGuid.h"
#include <array>

class Group;
class Player;

namespace HeartOfAcherus
{
    class Match;
    struct MatchPlayer;

    // A battlefield raid per team: not saved, keeps the original group of its members and gives it back when they
    // leave, like a battleground raid. Only GUIDs are kept, a Group may disband itself inside RemoveMember
    class MatchRaids
    {
    public:
        explicit MatchRaids(Match& match) : _match(match) { }

        void Update(MatchPlayer const& matchPlayer, Player* player);
        void RemoveOutsiders();
        void Leave(ObjectGuid guid, TeamId team);
        void Disband();

    private:
        Group* GetRaid(TeamId team) const;

        Match& _match;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> _raids;
    };
}

#endif // TRINITY_HOA_RAIDS_H
