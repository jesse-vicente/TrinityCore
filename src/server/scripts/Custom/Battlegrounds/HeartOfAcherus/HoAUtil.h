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

#ifndef TRINITY_HOA_UTIL_H
#define TRINITY_HOA_UTIL_H

#include "HoADefines.h"
#include "GameObjectData.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"

class Aura;
class Map;
class Player;
class TempSummon;
class Unit;

namespace HeartOfAcherus
{
    // per match object, only in the match phase and never saved
    struct GameObjectSpawn
    {
        uint32 Entry = 0;
        Position Location;
        Optional<QuaternionData> Rotation;                          // default: from the location orientation
        GOState State = GO_STATE_READY;
        float Scale = 0.0f;                                         // 0 = template size
        bool NotSelectable = false;                                 // scenery: no mouseover highlight
        bool FarVisible = false;                                    // seen from anywhere in the hall
    };

    struct CreatureSpawn
    {
        uint32 Entry = 0;
        Position Location;
        bool Active = true;
        bool FarVisible = false;
        float Scale = 0.0f;                                         // 0 = template scale
    };

    ObjectGuid SpawnGameObject(Map* map, uint32 phaseMask, uint32 matchId, GameObjectSpawn const& spawn);
    TempSummon* SummonCreature(Map* map, uint32 phaseMask, CreatureSpawn const& spawn);
    void DeleteGameObject(Map* map, ObjectGuid& guid);
    void DespawnCreature(Map* map, ObjectGuid& guid);

    Aura* ApplyPermanentAura(Unit* unit, uint32 spellId);           // 0 = none
    void Dismount(Player* player);
    void Revive(Player* player);
    bool IsInHall(Player const* player);                            // in the world, on the Acherus map
}

#endif // TRINITY_HOA_UTIL_H
