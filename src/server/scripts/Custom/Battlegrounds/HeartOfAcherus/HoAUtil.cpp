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

#include "HoAUtil.h"
#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellHistory.h"
#include "TemporarySummon.h"
#include "WorldPacket.h"

namespace HeartOfAcherus
{
    namespace
    {
        // the forges and their visuals are seen from anywhere in the hall and outside it (200 yards, the map uses 100)
        constexpr VisibilityDistanceType FarVisibility = VisibilityDistanceType::Large;
    }

    ObjectGuid SpawnGameObject(Map* map, uint32 phaseMask, uint32 matchId, GameObjectSpawn const& spawn)
    {
        QuaternionData const rotation = spawn.Rotation
            ? *spawn.Rotation
            : QuaternionData::fromEulerAnglesZYX(spawn.Location.GetOrientation(), 0.0f, 0.0f);

        GameObject* object = new GameObject();
        if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), spawn.Entry, map, phaseMask, spawn.Location, rotation, 255, spawn.State))
        {
            TC_LOG_ERROR("scripts", "HeartOfAcherus: cannot create gameobject {} for match {}", spawn.Entry, matchId);
            delete object;
            return ObjectGuid::Empty;
        }

        if (spawn.Scale > 0.0f)
            object->SetObjectScale(spawn.Scale);

        if (spawn.NotSelectable)
            object->SetFlag(GO_FLAG_NOT_SELECTABLE);

        object->setActive(true);
        if (spawn.FarVisible)
            object->SetVisibilityDistanceOverride(FarVisibility);

        if (!map->AddToMap(object))
        {
            delete object;
            return ObjectGuid::Empty;
        }

        return object->GetGUID();
    }

    TempSummon* SummonCreature(Map* map, uint32 phaseMask, CreatureSpawn const& spawn)
    {
        TempSummon* creature = map->SummonCreature(spawn.Entry, spawn.Location);
        if (!creature)
            return nullptr;

        creature->SetPhaseMask(phaseMask, true);
        if (spawn.Active)
            creature->setActive(true);
        if (spawn.FarVisible)
            creature->SetVisibilityDistanceOverride(FarVisibility);
        if (spawn.Scale > 0.0f)
            creature->SetObjectScale(spawn.Scale);

        return creature;
    }

    void DeleteGameObject(Map* map, ObjectGuid& guid)
    {
        if (GameObject* object = map->GetGameObject(guid))
        {
            object->SetRespawnTime(0);
            object->Delete();
        }
        guid.Clear();
    }

    void DespawnCreature(Map* map, ObjectGuid& guid)
    {
        if (Creature* creature = map->GetCreature(guid))
            creature->DespawnOrUnsummon();
        guid.Clear();
    }

    Aura* ApplyPermanentAura(Unit* unit, uint32 spellId)
    {
        if (!spellId)
            return nullptr;

        Aura* aura = unit->AddAura(spellId, unit);
        if (!aura)
            return nullptr;

        aura->SetMaxDuration(-1);
        aura->SetDuration(-1);
        return aura;
    }

    void Dismount(Player* player)
    {
        player->RemoveAurasByType(SPELL_AURA_MOUNTED);
        player->Dismount();
    }

    // the client starts its global cooldown only for the casts it sends itself; as Player::EquipItem does for the weapon
    // switch, the packet starts it for the spell the server cast
    void StartClientGlobalCooldown(Player* player, uint32 spellId)
    {
        WorldPacket data;
        player->GetSpellHistory()->BuildCooldownPacket(data, SPELL_COOLDOWN_FLAG_INCLUDE_GCD, spellId, 0);
        player->SendDirectMessage(&data);
    }

    void Revive(Player* player)
    {
        player->ResurrectPlayer(1.0f);
        player->SpawnCorpseBones();
    }

    bool IsInHall(Player const* player)
    {
        return player->IsInWorld() && player->GetMapId() == Ids::MapId;
    }
}
