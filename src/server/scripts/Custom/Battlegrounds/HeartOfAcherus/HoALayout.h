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

#ifndef TRINITY_HOA_LAYOUT_H
#define TRINITY_HOA_LAYOUT_H

#include "HoADefines.h"
#include "GameObjectData.h"
#include "Position.h"
#include <array>

// Hall layout (map 609). Every per-faction position mirrors the other across the spawn axis: the perpendicular
// bisector of the two faction spawns, through the door.
namespace HeartOfAcherus
{
    namespace Positions
    {
        inline Position const Center = { 2459.4f, -5593.4f, 414.12f };    // circumcenter of the forges
        inline Position const Door = { 2410.68f, -5626.74f, 420.66f };

        // like the Acherus portal by the door (gameobject 151235, teleporter aura 54724), but to the upper floor
        inline Position const Portal = { 2383.65f, -5645.20f, 420.772f };
        inline QuaternionData const PortalRotation = { 0.0f, 0.0f, 0.292371f, 0.956305f };
        inline Position const PortalDestination = { 2517.900879f, -5554.814453f, 444.124817f, 3.740502f };

        // one Berserk buff (spell 23505) per faction, same distance from its spawn and respawn
        static constexpr std::size_t BerserkBuffCount = 2;
        inline std::array<Position, BerserkBuffCount> const BerserkBuffs =
        {{
            { 2472.58f, -5530.56f, 420.649078f, 4.523f },
            { 2523.22f, -5605.63f, 420.648682f, 2.947f }
        }};

        // ambient creature copies placed away from their original spawn
        struct AmbientCreatureMove
        {
            uint32 SpawnId;
            Position Destination;
        };

        inline std::array<AmbientCreatureMove, 1> const AmbientCreatureMoves =
        {{
            { 125787, { 2514.38f, -5603.94f, 420.65f, 2.73855f } }      // Risen Drudge off the second Berserk buff
        }};

        // invisible wall along the stairs behind the portal (left bottom, left top, right top, right bottom): closes
        // the balcony and the gaps that lead out of the hall, up to above the upper floor
        inline std::array<Position, 4> const StairsBarrier =
        {{
            { 2362.671387f, -5635.259766f, 420.713196f },
            { 2361.644287f, -5646.252441f, 426.696899f },
            { 2374.672363f, -5664.892090f, 426.658569f },
            { 2385.154785f, -5668.484375f, 422.844604f }
        }};
        inline float const StairsBarrierTop = 444.227661f;              // upper floor
        inline Position const StairsPortal = { 2369.197266f, -5655.271484f, 426.126343f, 0.623654f };

        inline std::array<Position, PVP_TEAMS_COUNT> const Spawn =
        {{
            { 2447.56f, -5656.40f, 420.65f, 1.294f },                    // TEAM_ALLIANCE
            { 2397.17f, -5581.70f, 420.65f, 6.177f }                     // TEAM_HORDE
        }};

        // graveyards on the upper floor, a spirit guide each
        static constexpr std::size_t RespawnCount = 2;
        inline std::array<std::array<Position, RespawnCount>, PVP_TEAMS_COUNT> const Respawn =
        {{
            {{
                { 2438.10f, -5707.64f, 444.61f, 1.347f },
                { 2574.63f, -5615.65f, 444.613373f, 2.950f }
            }},
            {{
                { 2346.12f, -5571.28f, 444.62f, 6.123f },
                { 2482.54f, -5479.14f, 444.615540f, 4.520f }
            }}
        }};
    }

    struct ForgeScaledAura
    {
        uint32 Spell = 0;                                           // 0 = none
        float Scale = 1.0f;                                         // scale of its own trigger
    };

    struct ForgeObject
    {
        uint32 Entry = 0;                                           // 0 = none
        float Scale = 0.0f;                                         // 0 = template size
    };

    struct RuneTemplate
    {
        char const* Name;
        char const* Color;                                          // chat color code of the name
        uint32 ForgeEntry;
        Position ForgePosition;
        QuaternionData ForgeRotation;
        float ForgeAuraScale;                                       // scale of the forge trigger
        std::array<uint32, 5> ForgeAuras;                           // on the forge trigger while the rune is ready
        std::array<ForgeObject, 3> ForgeObjects;                    // on the forge while the rune is ready
        std::array<ForgeScaledAura, 2> ForgeScaledAuras;            // each on its own trigger, to size it apart
        uint32 CarrierVisualKit;
        std::array<uint32, 3> CarrierAuras;                         // permanent while carried
    };

    extern std::array<RuneTemplate, RuneCount> const RuneTemplates;

    inline RuneTemplate const& GetRuneTemplate(Rune rune)
    {
        return RuneTemplates[std::size_t(rune)];
    }

    // points a carrier scores per tick at this position
    uint32 GetPointsForPosition(Position const& position);
}

#endif // TRINITY_HOA_LAYOUT_H
