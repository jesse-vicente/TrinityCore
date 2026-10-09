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

#include "HoARaids.h"
#include "HoAMatch.h"
#include "Battlefield.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Player.h"
#include <vector>

namespace HeartOfAcherus
{
    namespace
    {
        // Group only checks its battlefield pointer for null; this one is never registered in the BattlefieldMgr nor
        // updated, so no core change is needed for a battlefield raid
        class RaidAnchor final : public Battlefield
        {
        public:
            void SendInitWorldStatesToAll() override { }
            void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) override { }
        };

        Battlefield* GetRaidAnchor()
        {
            static RaidAnchor anchor;
            return &anchor;
        }
    }

    Group* MatchRaids::GetRaid(TeamId team) const
    {
        return _raids[team].IsEmpty() ? nullptr : sGroupMgr->GetGroupByGUID(_raids[team]);
    }

    // like Battleground::AddOrSetPlayerToCorrectBgGroup; also brings back who left the raid or logged in again
    void MatchRaids::Update(MatchPlayer const& matchPlayer, Player* player)
    {
        Group* raid = GetRaid(matchPlayer.Team);
        if (!raid)
        {
            raid = new Group();
            raid->SetBattlefieldGroup(GetRaidAnchor());
            if (!raid->Create(player))
            {
                delete raid;
                return;
            }

            sGroupMgr->AddGroup(raid);
            _raids[matchPlayer.Team] = raid->GetGUID();
            return;
        }

        if (!raid->IsMember(player->GetGUID()))
        {
            raid->AddMember(player);
            return;
        }

        if (player->GetGroup() != raid)
        {
            player->SetBattlegroundOrBattlefieldRaid(raid, raid->GetMemberGroup(player->GetGUID()));
            raid->SendUpdate();
        }
    }

    // e.g. someone invited by the raid leader
    void MatchRaids::RemoveOutsiders()
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            Group* raid = GetRaid(TeamId(team));
            if (!raid)
                continue;

            std::vector<ObjectGuid> outsiders;
            for (Group::MemberSlot const& slot : raid->GetMemberSlots())
            {
                MatchPlayer const* matchPlayer = _match.GetPlayer(slot.guid);
                if (!matchPlayer || matchPlayer->Team != team)
                    outsiders.push_back(slot.guid);
            }

            for (ObjectGuid const& guid : outsiders)
                Leave(guid, TeamId(team));
        }
    }

    void MatchRaids::Leave(ObjectGuid guid, TeamId team)
    {
        if (Group* raid = GetRaid(team))
            if (raid->IsMember(guid))
                raid->RemoveMember(guid);
    }

    void MatchRaids::Disband()
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            if (Group* raid = GetRaid(TeamId(team)))
                raid->Disband();
            _raids[team].Clear();
        }
    }
}
