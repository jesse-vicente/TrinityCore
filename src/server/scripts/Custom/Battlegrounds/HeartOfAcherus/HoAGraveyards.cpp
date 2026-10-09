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

#include "HoAGraveyards.h"
#include "HoAMatch.h"
#include "HoAUtil.h"
#include "BattlegroundPackets.h"
#include "Creature.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "TemporarySummon.h"
#include <cmath>
#include <vector>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr float SpiritGuideOffset = 3.0f;                   // graveyard guides stand in front of the respawn point
        constexpr float SpiritHealerRange = 17.0f;                  // client AREA_SPIRIT_HEALER_IN_RANGE, measured in game
    }

    void MatchGraveyards::Spawn(Map* map)
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            for (std::size_t i = 0; i < Positions::RespawnCount; ++i)
                _spiritGuides[team][i] = SummonSpiritGuide(map, Positions::Respawn[team][i], TeamId(team), SpiritGuideOffset);
            _preparationGuides[team] = SummonSpiritGuide(map, Positions::Spawn[team], TeamId(team), 0.0f);
        }
    }

    // the starting area guides leave with the preparation
    void MatchGraveyards::DespawnPreparation(Map* map)
    {
        for (ObjectGuid& guid : _preparationGuides)
            DespawnCreature(map, guid);
    }

    void MatchGraveyards::DespawnAll(Map* map)
    {
        for (std::array<ObjectGuid, Positions::RespawnCount>& guides : _spiritGuides)
            for (ObjectGuid& guid : guides)
                DespawnCreature(map, guid);

        DespawnPreparation(map);
    }

    // like battlegrounds, the waves also run while the gates are closed
    void MatchGraveyards::Update(uint32 diff)
    {
        _waveTimer += diff;
        if (_waveTimer < Timers::ResurrectWave)
            return;

        _waveTimer = 0;
        ResurrectQueued();
    }

    // during the preparation the graveyard is the starting area, so nobody starts outside of it
    bool MatchGraveyards::OnRepop(Player* player)
    {
        MatchPlayer const* matchPlayer = _match.GetPlayer(player->GetGUID());
        if (!matchPlayer || player->GetMapId() != Ids::MapId)
            return false;

        player->NearTeleportTo(GetGraveyard(matchPlayer->Team, player));
        return true;
    }

    // the native timer, so the client shows its AREA_SPIRIT_HEAL popup
    bool MatchGraveyards::OnSpiritHealerQuery(Player* player, Creature* spiritHealer)
    {
        MatchPlayer const* matchPlayer = _match.GetPlayer(player->GetGUID());
        if (!matchPlayer || !IsTeamSpiritGuide(matchPlayer->Team, spiritHealer->GetGUID()))
            return false;

        WorldPackets::Battleground::AreaSpiritHealerTime time;
        time.HealerGuid = spiritHealer->GetGUID();
        time.TimeLeft = int32(_waveTimer < Timers::ResurrectWave ? Timers::ResurrectWave - _waveTimer : 0);
        player->SendDirectMessage(time.Write());
        return true;
    }

    // walking into a guide's range queues the ghost for the next wave; refreshed on every queue, like
    // Battleground::AddPlayerToResurrectQueue
    bool MatchGraveyards::OnSpiritHealerQueue(Player* player, Creature* spiritHealer)
    {
        MatchPlayer const* matchPlayer = _match.GetPlayer(player->GetGUID());
        if (!matchPlayer || !IsTeamSpiritGuide(matchPlayer->Team, spiritHealer->GetGUID()))
            return false;

        _resurrectQueue.insert_or_assign(player->GetGUID(), spiritHealer->GetGUID());
        player->CastSpell(player, Spells::WaitingForResurrect, true);
        return true;
    }

    void MatchGraveyards::LeaveQueue(ObjectGuid guid, Player* player)
    {
        if (_resurrectQueue.erase(guid) && player)
            player->RemoveAurasDueToSpell(Spells::WaitingForResurrect);
    }

    // like BattlegroundAB::GetClosestGraveyard: the closest (2D) of the faction
    Position const& MatchGraveyards::GetGraveyard(TeamId team, Player const* player) const
    {
        if (_match.GetStatus() == MatchStatus::Preparation)
            return Positions::Spawn[team];

        Position const* closest = &Positions::Respawn[team].front();
        for (Position const& graveyard : Positions::Respawn[team])
            if (player->GetExactDist2dSq(graveyard) < player->GetExactDist2dSq(*closest))
                closest = &graveyard;

        return *closest;
    }

    bool MatchGraveyards::IsTeamSpiritGuide(TeamId team, ObjectGuid guid) const
    {
        if (_preparationGuides[team] == guid)
            return true;

        for (ObjectGuid const& guide : _spiritGuides[team])
            if (guide == guid)
                return true;

        return false;
    }

    // same setup as Battleground::AddSpiritGuide
    ObjectGuid MatchGraveyards::SummonSpiritGuide(Map* map, Position const& graveyard, TeamId team, float offset)
    {
        Position const position(graveyard.GetPositionX() + offset * std::cos(graveyard.GetOrientation()),
            graveyard.GetPositionY() + offset * std::sin(graveyard.GetOrientation()), graveyard.GetPositionZ(), graveyard.GetOrientation());

        uint32 const entry = team == TEAM_ALLIANCE ? Ids::NpcSpiritGuideAlliance : Ids::NpcSpiritGuideHorde;
        TempSummon* guide = SummonCreature(map, _match.GetPhaseMask(), { .Entry = entry, .Location = position });
        if (!guide)
            return ObjectGuid::Empty;

        guide->SetChannelObjectGuid(guide->GetGUID());
        guide->SetChannelSpellId(Spells::SpiritHealChannel);
        guide->SetModCastingSpeed(1.0f);
        return guide->GetGUID();
    }

    // like Battleground::_ProcessResurrect: only released ghosts still in range of a guide of their faction, the client
    // hides its popup once the ghost leaves the guide
    void MatchGraveyards::ResurrectQueued()
    {
        for (auto const& [guid, guideGuid] : _resurrectQueue)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported())
                continue;

            if (player->IsAlive() || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            MatchPlayer* matchPlayer = _match.GetPlayer(guid);
            if (!matchPlayer)
                continue;

            std::vector<ObjectGuid> candidates = { guideGuid, _preparationGuides[matchPlayer->Team] };
            candidates.insert(candidates.end(), _spiritGuides[matchPlayer->Team].begin(), _spiritGuides[matchPlayer->Team].end());

            Creature* guide = nullptr;
            for (ObjectGuid const& candidate : candidates)
            {
                Creature* creature = player->GetMap()->GetCreature(candidate);
                if (creature && player->GetDistance(creature) <= SpiritHealerRange)
                {
                    guide = creature;
                    break;
                }
            }

            if (!guide)
                continue;

            matchPlayer->HandledDeath = false;

            guide->CastSpell(guide, Spells::SpiritHeal, true);
            player->CastSpell(player, Spells::ResurrectionVisual, true);

            player->ResurrectPlayer(1.0f);
            player->CastSpell(player, Spells::ResurrectEffect, true);
            player->CastSpell(player, Spells::SpiritHealMana, true);
            player->SpawnCorpseBones(false);
        }

        _resurrectQueue.clear();
    }
}
