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

#ifndef TRINITY_HOA_RUNES_H
#define TRINITY_HOA_RUNES_H

#include "HoADefines.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <array>

class GameObject;
class Player;

namespace HeartOfAcherus
{
    class Match;
    struct MatchPlayer;

    struct RuneState
    {
        ObjectGuid Carrier;
        uint32 Stacks = 0;
        uint32 StackTimer = 0;
        float CarrierOriginalScale = 1.0f;
    };

    // The three runes of a match: taken at their runeforge, carried until the carrier dies or leaves
    class MatchRunes
    {
    public:
        explicit MatchRunes(Match& match) : _match(match) { }

        void OnForgeUse(Player* player, GameObject* forge);
        void Drop(Rune rune, bool announce);
        void DropCarried(ObjectGuid guid);                          // announced, if the player carries one
        void Update(uint32 diff);                                   // carrier stacks
        void SendMarkers();                                         // minimap markers of the client UI
        void UpdateForgeLock(MatchPlayer& matchPlayer, Player* player);

        Optional<Rune> GetCarried(ObjectGuid guid) const;
        RuneState const* GetCarriedState(ObjectGuid guid) const;
        std::array<RuneState, RuneCount> const& GetStates() const { return _runes; }
        std::array<uint32, PVP_TEAMS_COUNT> GetHeldCounts() const;

        static float GetDamageDoneMultiplier(uint32 stacks);
        static float GetDamageTakenMultiplier(uint32 stacks);
        static float GetHealingTakenMultiplier(uint32 stacks);
        static void RemoveCarrierAuras(Player* player);

    private:
        void PickUp(Rune rune, Player* player);

        Match& _match;
        std::array<RuneState, RuneCount> _runes;
    };
}

#endif // TRINITY_HOA_RUNES_H
