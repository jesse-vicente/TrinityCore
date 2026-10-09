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

#include "HoAHall.h"
#include "HoAMatch.h"
#include "HoAUtil.h"
#include "Creature.h"
#include "CreatureData.h"
#include "GameObject.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "TemporarySummon.h"
#include <algorithm>
#include <cmath>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr float PreparationLeash = 13.0f;                   // beyond the walls: only who gets past them
        constexpr float PreparationDomeScale = 2.0f;                // Anti-Magic Zone is ~7 yards at scale 1
        constexpr uint8 PreparationWallCount = 8;                   // octagon around the dome
        constexpr float PreparationWallDistance = 10.0f;            // spawn to the middle of each wall, inside the dome
        constexpr float InstructionBookDistance = 6.0f;             // ahead of the spawn
        constexpr float InstructionBookHeight = 1.2f;               // over the model's own float
        constexpr float PortalRange = 3.0f;                         // Acherus teleporter aura 54724
        constexpr float AmbientCreatureRadius = 150.0f;             // map 0 spawns this close belong to the floating Acherus
        constexpr float StairsPortalScale = 8.0f;
        constexpr float StairsBarrierClearance = 3.0f;              // above the upper floor, beyond a jump from there

        // CollisionWallPvP01 collision box at scale 1 (GameObjectModels.dtree), along its local Y axis. Its only visible
        // part is a checkered bar at its base, which has to stay under the floor
        constexpr float WallWidth = 11.078f;
        constexpr float WallTop = 17.562f;

        // straight ahead of the spawn, facing it
        Position GetInstructionBookPosition(TeamId team)
        {
            Position const& spawn = Positions::Spawn[team];
            float const angle = spawn.GetOrientation();
            return Position(spawn.GetPositionX() + InstructionBookDistance * std::cos(angle),
                spawn.GetPositionY() + InstructionBookDistance * std::sin(angle), spawn.GetPositionZ(),
                Position::NormalizeOrientation(angle + float(M_PI)));
        }
    }

    // ----------------------------------------------------------------- spawns

    void MatchHall::SpawnPreparation(Map* map)
    {
        for (std::size_t rune = 0; rune < RuneCount; ++rune)
            SpawnForge(map, Rune(rune));

        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            SpawnPreparationArea(map, TeamId(team));

        SpawnStairsBarrier(map);

        CreatureSpawn const stairsPortal{ .Entry = Ids::NpcAuraTrigger, .Location = Positions::StairsPortal, .FarVisible = true, .Scale = StairsPortalScale };
        if (TempSummon* trigger = SummonCreature(map, _match.GetPhaseMask(), stairsPortal))
        {
            ApplyPermanentAura(trigger, Spells::StairsPortal);
            _stairsPortal = trigger->GetGUID();
        }

        SpawnAmbientCreatures(map);
    }

    void MatchHall::SpawnBattle(Map* map)
    {
        // only shows where to step: the teleport comes from standing on it (UsePortal)
        _portal = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = Ids::GoPortal, .Location = Positions::Portal,
            .Rotation = Positions::PortalRotation, .State = GO_STATE_ACTIVE, .FarVisible = true });

        for (std::size_t i = 0; i < _berserkBuffs.size(); ++i)
            SpawnBerserkBuff(map, i);
    }

    void MatchHall::SpawnForge(Map* map, Rune rune)
    {
        RuneTemplate const& runeTemplate = GetRuneTemplate(rune);
        ForgeState& forge = _forges[std::size_t(rune)];

        forge.Forge = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = runeTemplate.ForgeEntry,
            .Location = runeTemplate.ForgePosition, .Rotation = runeTemplate.ForgeRotation, .FarVisible = true });

        forge.Trigger = SummonForgeTrigger(map, runeTemplate, runeTemplate.ForgeAuraScale);
        for (std::size_t i = 0; i < runeTemplate.ForgeScaledAuras.size(); ++i)
            if (runeTemplate.ForgeScaledAuras[i].Spell)
                forge.ScaledAuraTriggers[i] = SummonForgeTrigger(map, runeTemplate, runeTemplate.ForgeScaledAuras[i].Scale);
    }

    // the auras are drawn relative to the trigger's facing, so it faces the pit
    ObjectGuid MatchHall::SummonForgeTrigger(Map* map, RuneTemplate const& runeTemplate, float scale)
    {
        Position position = runeTemplate.ForgePosition;
        position.SetOrientation(position.GetAbsoluteAngle(Positions::Center));

        TempSummon* trigger = SummonCreature(map, _match.GetPhaseMask(), { .Entry = Ids::NpcAuraTrigger, .Location = position, .FarVisible = true, .Scale = scale });
        return trigger ? trigger->GetGUID() : ObjectGuid::Empty;
    }

    // Anti-Magic Zone dome, instruction book and an octagon of walls around the spawn
    void MatchHall::SpawnPreparationArea(Map* map, TeamId team)
    {
        Position const& spawn = Positions::Spawn[team];

        // the dome is a channel kit (SpellVisual 11242), drawn only while a unit channels the spell
        if (TempSummon* dome = SummonCreature(map, _match.GetPhaseMask(), { .Entry = Ids::NpcPreparationDome, .Location = spawn,
            .FarVisible = true, .Scale = PreparationDomeScale }))
        {
            dome->SetFaction(FACTION_FRIENDLY);
            dome->SetReactState(REACT_PASSIVE);
            dome->SetChannelObjectGuid(dome->GetGUID());
            dome->SetChannelSpellId(Spells::PreparationDome);
            _preparationDomes[team] = dome->GetGUID();
        }

        Position const bookAura = GetInstructionBookPosition(team);
        Position book = bookAura;
        book.m_positionZ += InstructionBookHeight;
        _books[team] = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = Ids::GoInstructionBook, .Location = book });
        _bookAuras[team] = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = Ids::GoBookAura, .Location = bookAura });

        // the walls are tangent to a circle around the spawn; the client collision blocks the players
        for (uint8 i = 0; i < PreparationWallCount; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(PreparationWallCount);
            Position const position(spawn.GetPositionX() + PreparationWallDistance * std::cos(angle),
                spawn.GetPositionY() + PreparationWallDistance * std::sin(angle), spawn.GetPositionZ(), angle);

            ObjectGuid const wall = SpawnWall(map, position);
            if (!wall.IsEmpty())
                _preparationWalls.push_back(wall);
        }
    }

    // facing the position's orientation, so the wall extends across it
    ObjectGuid MatchHall::SpawnWall(Map* map, Position const& position, float scale /*= 0.0f*/)
    {
        return SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = Ids::GoCollisionWall, .Location = position, .Scale = scale });
    }

    // one row of overlapping walls along each segment, all on the lowest floor of the barrier so nothing passes under
    // them and their base stays buried, scaled to reach above the upper floor
    void MatchHall::SpawnStairsBarrier(Map* map)
    {
        float bottom = Positions::StairsBarrier.front().GetPositionZ();
        for (Position const& point : Positions::StairsBarrier)
            bottom = std::min(bottom, point.GetPositionZ());

        float const scale = (Positions::StairsBarrierTop + StairsBarrierClearance - bottom) / WallTop;
        float const width = WallWidth * scale;

        for (std::size_t i = 0; i + 1 < Positions::StairsBarrier.size(); ++i)
        {
            Position const& from = Positions::StairsBarrier[i];
            Position const& to = Positions::StairsBarrier[i + 1];
            float const facing = from.GetAbsoluteAngle(&to) - float(M_PI) / 2.0f;
            uint32 const columns = uint32(std::ceil(from.GetExactDist2d(&to) / (width * 0.9f)));

            for (uint32 column = 0; column < columns; ++column)
            {
                float const middle = (float(column) + 0.5f) / float(columns);
                Position const position(from.GetPositionX() + (to.GetPositionX() - from.GetPositionX()) * middle,
                    from.GetPositionY() + (to.GetPositionY() - from.GetPositionY()) * middle, bottom, facing);

                ObjectGuid const wall = SpawnWall(map, position, scale);
                if (!wall.IsEmpty())
                    _stairsBarrier.push_back(wall);
            }
        }
    }

    // Risen Drudges and Vigilant Gargoyles of this map and of the Acherus over the Eastern Plaguelands (map 0, same
    // coordinates), as scenery
    void MatchHall::SpawnAmbientCreatures(Map* map)
    {
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.id != Ids::NpcRisenDrudge && data.id != Ids::NpcVigilantGargoyle)
                continue;

            if (data.mapId != Ids::MapId && (data.mapId != 0 || data.spawnPoint.GetExactDist2d(Positions::Center) > AmbientCreatureRadius))
                continue;

            ObjectGuid const guid = SummonAmbientCreature(map, data);
            if (!guid.IsEmpty())
                _ambientCreatures.push_back(guid);
        }
    }

    // copied with stand state, emote and movement; nobody can affect them and they react to nothing
    ObjectGuid MatchHall::SummonAmbientCreature(Map* map, CreatureData const& data)
    {
        Position position = data.spawnPoint;
        for (Positions::AmbientCreatureMove const& move : Positions::AmbientCreatureMoves)
            if (move.SpawnId == data.spawnId)
                position = move.Destination;

        TempSummon* creature = SummonCreature(map, _match.GetPhaseMask(), { .Entry = data.id, .Location = position, .Active = false });
        if (!creature)
            return ObjectGuid::Empty;

        creature->SetFaction(FACTION_FRIENDLY);
        creature->SetReactState(REACT_PASSIVE);
        creature->SetImmuneToAll(true);
        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_UNINTERACTIBLE);

        CreatureAddon const* addon = sObjectMgr->GetCreatureAddon(data.spawnId);
        if (addon)
        {
            creature->SetStandState(UnitStandStateType(addon->standState));
            creature->SetSheath(SheathState(addon->sheathState));
            if (addon->emote)
                creature->SetEmoteState(Emote(addon->emote));
        }

        if (addon && addon->path_id)
            creature->GetMotionMaster()->MovePath(addon->path_id, true);
        else if (data.movementType == RANDOM_MOTION_TYPE && data.wander_distance > 0.0f)
        {
            creature->SetWanderDistance(data.wander_distance);
            creature->GetMotionMaster()->MoveRandom(data.wander_distance);
        }

        return creature->GetGUID();
    }

    void MatchHall::SpawnBerserkBuff(Map* map, std::size_t index)
    {
        BerserkBuff& buff = _berserkBuffs[index];
        buff.Armed = false;
        buff.Guid = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = Ids::GoBerserkBuff,
            .Location = Positions::BerserkBuffs[index], .FarVisible = true });

        if (buff.Guid.IsEmpty())
            buff.RespawnTimer = Timers::BuffRespawn;
    }

    // ----------------------------------------------------------------- despawns

    void MatchHall::DespawnPreparationArea(Map* map)
    {
        for (ObjectGuid& guid : _preparationDomes)
            DespawnCreature(map, guid);

        for (std::size_t team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            DeleteGameObject(map, _books[team]);
            DeleteGameObject(map, _bookAuras[team]);
        }

        for (ObjectGuid& guid : _preparationWalls)
            DeleteGameObject(map, guid);
        _preparationWalls.clear();
    }

    void MatchHall::DespawnAll(Map* map)
    {
        for (ForgeState& forge : _forges)
        {
            DeleteGameObject(map, forge.Forge);
            for (ObjectGuid& guid : forge.Objects)
                DeleteGameObject(map, guid);

            DespawnCreature(map, forge.Trigger);
            for (ObjectGuid& guid : forge.ScaledAuraTriggers)
                DespawnCreature(map, guid);
        }

        DeleteGameObject(map, _portal);

        for (BerserkBuff& buff : _berserkBuffs)
        {
            DeleteGameObject(map, buff.Guid);
            buff = BerserkBuff();
        }

        for (ObjectGuid& guid : _stairsBarrier)
            DeleteGameObject(map, guid);
        _stairsBarrier.clear();
        DespawnCreature(map, _stairsPortal);

        for (ObjectGuid& guid : _ambientCreatures)
            DespawnCreature(map, guid);
        _ambientCreatures.clear();

        DespawnPreparationArea(map);
    }

    // ----------------------------------------------------------------- battle

    // Battleground::HandleTriggerBuff does not run here. These traps cast their spell and go GO_JUST_DEACTIVATED, then
    // would arm themselves again; a new trap also starts GO_NOT_READY, so it only counts as used once seen ready
    void MatchHall::UpdateBerserkBuffs(uint32 diff)
    {
        Map* map = _match.GetMap();
        if (!map)
            return;

        for (std::size_t i = 0; i < _berserkBuffs.size(); ++i)
        {
            BerserkBuff& buff = _berserkBuffs[i];
            if (buff.Guid.IsEmpty())
            {
                if (buff.RespawnTimer > diff)
                {
                    buff.RespawnTimer -= diff;
                    continue;
                }

                buff.RespawnTimer = 0;
                SpawnBerserkBuff(map, i);
                continue;
            }

            if (GameObject* object = map->GetGameObject(buff.Guid))
            {
                LootState const state = object->getLootState();
                if (state == GO_READY)
                    buff.Armed = true;

                if (state == GO_READY || state == GO_ACTIVATED || !buff.Armed)
                    continue;

                object->Delete();
            }

            buff.Guid.Clear();
            buff.RespawnTimer = Timers::BuffRespawn;
        }
    }

    void MatchHall::SetForgeVisuals(Rune rune, bool on)
    {
        Map* map = _match.GetMap();
        if (!map)
            return;

        RuneTemplate const& runeTemplate = GetRuneTemplate(rune);
        ForgeState& forge = _forges[std::size_t(rune)];

        for (std::size_t i = 0; i < runeTemplate.ForgeObjects.size(); ++i)
        {
            ForgeObject const& object = runeTemplate.ForgeObjects[i];
            if (!object.Entry)
                continue;

            ObjectGuid& guid = forge.Objects[i];
            if (!on)
                DeleteGameObject(map, guid);
            else if (!map->GetGameObject(guid))
                guid = SpawnGameObject(map, _match.GetPhaseMask(), _match.GetId(), { .Entry = object.Entry, .Location = runeTemplate.ForgePosition,
                    .Scale = object.Scale, .NotSelectable = true, .FarVisible = true });
        }

        auto setAura = [on](Creature* trigger, uint32 spellId)
        {
            if (!trigger || !spellId)
                return;

            if (!on)
                trigger->RemoveAurasDueToSpell(spellId);
            else if (!trigger->HasAura(spellId))
                ApplyPermanentAura(trigger, spellId);
        };

        Creature* trigger = map->GetCreature(forge.Trigger);
        for (uint32 spellId : runeTemplate.ForgeAuras)
            setAura(trigger, spellId);

        for (std::size_t i = 0; i < runeTemplate.ForgeScaledAuras.size(); ++i)
            setAura(map->GetCreature(forge.ScaledAuraTriggers[i]), runeTemplate.ForgeScaledAuras[i].Spell);
    }

    void MatchHall::SetForgeVisuals(bool on)
    {
        for (std::size_t rune = 0; rune < RuneCount; ++rune)
            SetForgeVisuals(Rune(rune), on);
    }

    Optional<Rune> MatchHall::GetRuneOfForge(ObjectGuid forge) const
    {
        for (std::size_t rune = 0; rune < RuneCount; ++rune)
            if (_forges[rune].Forge == forge)
                return Rune(rune);

        return {};
    }

    // like the Acherus teleporter (aura 54724 of NPC 29581): within range, checked every second
    void MatchHall::UsePortal(Player* player) const
    {
        if (_portal.IsEmpty() || player->GetExactDist(&Positions::Portal) > PortalRange)
            return;

        player->NearTeleportTo(Positions::PortalDestination);
    }

    // backup of the walls, while the gates are closed
    void MatchHall::KeepInPreparationArea() const
    {
        for (auto const& [guid, matchPlayer] : _match.GetPlayers())
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !IsInHall(player) || player->IsBeingTeleported() || !player->IsAlive())
                continue;

            Position const& spawn = Positions::Spawn[matchPlayer.Team];
            if (player->GetExactDist2d(&spawn) > PreparationLeash)
                player->NearTeleportTo(spawn);
        }
    }
}
