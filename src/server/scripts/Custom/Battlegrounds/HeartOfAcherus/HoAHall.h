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

#ifndef TRINITY_HOA_HALL_H
#define TRINITY_HOA_HALL_H

#include "HoALayout.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <array>
#include <vector>

class Map;
class Player;
struct CreatureData;

namespace HeartOfAcherus
{
    class Match;

    // Everything a match spawns in its phase of the hall, where the original Acherus objects are not seen
    class MatchHall
    {
    public:
        explicit MatchHall(Match& match) : _match(match) { }

        void SpawnPreparation(Map* map);                            // forges, preparation areas, stairs, ambient
        void SpawnBattle(Map* map);                                 // portal and Berserk buffs
        void DespawnPreparationArea(Map* map);
        void DespawnAll(Map* map);
        void UpdateBerserkBuffs(uint32 diff);

        void SetForgeVisuals(Rune rune, bool on);
        void SetForgeVisuals(bool on);                              // every forge
        ObjectGuid GetForge(Rune rune) const { return _forges[std::size_t(rune)].Forge; }
        Optional<Rune> GetRuneOfForge(ObjectGuid forge) const;

        void UsePortal(Player* player) const;
        void KeepInPreparationArea() const;

    private:
        struct ForgeState
        {
            ObjectGuid Forge;
            ObjectGuid Trigger;
            std::array<ObjectGuid, 2> ScaledAuraTriggers;
            std::array<ObjectGuid, 3> Objects;
        };

        struct BerserkBuff
        {
            ObjectGuid Guid;
            bool Armed = false;                                     // seen ready since it was spawned
            uint32 RespawnTimer = 0;
        };

        void SpawnForge(Map* map, Rune rune);
        ObjectGuid SummonForgeTrigger(Map* map, RuneTemplate const& runeTemplate, float scale);
        void SpawnPreparationArea(Map* map, TeamId team);
        ObjectGuid SpawnWall(Map* map, Position const& position, float scale = 0.0f);
        void SpawnStairsBarrier(Map* map);
        void SpawnAmbientCreatures(Map* map);
        ObjectGuid SummonAmbientCreature(Map* map, CreatureData const& data);
        void SpawnBerserkBuff(Map* map, std::size_t index);

        Match& _match;
        std::array<ForgeState, RuneCount> _forges;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> _preparationDomes;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> _books;
        std::array<ObjectGuid, PVP_TEAMS_COUNT> _bookAuras;
        std::vector<ObjectGuid> _preparationWalls;
        ObjectGuid _portal;
        std::array<BerserkBuff, Positions::BerserkBuffCount> _berserkBuffs;
        std::vector<ObjectGuid> _stairsBarrier;
        ObjectGuid _stairsPortal;
        std::vector<ObjectGuid> _ambientCreatures;
    };
}

#endif // TRINITY_HOA_HALL_H
