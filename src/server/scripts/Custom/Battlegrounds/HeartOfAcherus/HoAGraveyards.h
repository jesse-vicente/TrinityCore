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

#ifndef TRINITY_HOA_GRAVEYARDS_H
#define TRINITY_HOA_GRAVEYARDS_H

#include "HoALayout.h"
#include "ObjectGuid.h"
#include <array>
#include <unordered_map>

class Creature;
class Map;
class Player;

namespace HeartOfAcherus
{
    class Match;

    // Spirit guides and 30 s resurrection waves, like the battlegrounds: one guide in each starting area during the
    // preparation (as in Warsong Gulch), two per faction on the upper floor
    class MatchGraveyards
    {
    public:
        explicit MatchGraveyards(Match& match) : _match(match) { }

        void Spawn(Map* map);
        void DespawnPreparation(Map* map);
        void DespawnAll(Map* map);
        void Update(uint32 diff);
        void ResetWave() { _waveTimer = 0; }

        bool OnRepop(Player* player);
        bool OnSpiritHealerQuery(Player* player, Creature* spiritHealer);
        bool OnSpiritHealerQueue(Player* player, Creature* spiritHealer);
        void LeaveQueue(ObjectGuid guid, Player* player);

    private:
        Position const& GetGraveyard(TeamId team, Player const* player) const;
        bool IsTeamSpiritGuide(TeamId team, ObjectGuid guid) const;
        ObjectGuid SummonSpiritGuide(Map* map, Position const& graveyard, TeamId team, float offset);
        void ResurrectQueued();

        Match& _match;
        std::array<std::array<ObjectGuid, Positions::RespawnCount>, PVP_TEAMS_COUNT> _spiritGuides;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> _preparationGuides;
        std::unordered_map<ObjectGuid, ObjectGuid> _resurrectQueue; // ghost -> spirit guide it queued at
        uint32 _waveTimer = 0;
    };
}

#endif // TRINITY_HOA_GRAVEYARDS_H
