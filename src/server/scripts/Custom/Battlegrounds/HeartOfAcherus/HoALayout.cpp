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

#include "HoALayout.h"
#include <cmath>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr float PoolOfBloodScale = 3.0f;                    // hidden under the forge at its template size
        constexpr float ScourgeCircleScale = 1.4f;
        constexpr float UnholyLightScale = 10.0f;                   // template size 3.5, raised to the forge
    }

    std::array<RuneTemplate, RuneCount> const RuneTemplates =
    {{
        {
            .Name = "Frost",
            .Color = "ff69ccf0",
            .ForgeEntry = Ids::GoFrostForge,
            .ForgePosition = { 2493.37f, -5642.43f, 420.863f, 2.16421f },
            .ForgeRotation = QuaternionData(0.0f, 0.0f, 0.882948f, 0.469471f),
            .ForgeAuraScale = 3.0f,
            .ForgeAuras = { Spells::ForgeSpiritsFrost },
            .ForgeObjects = { },
            .ForgeScaledAuras = {{ { Spells::ForgeBeamFrost, 2.0f }, { Spells::ForgeIceboundFrost, 8.0f } }},
            .CarrierVisualKit = VisualKits::CarrierFrost,
            .CarrierAuras = { Spells::ForgeSpiritsFrost, Spells::CarrierAuraFrost }
        },
        {
            .Name = "Blood",
            .Color = "ffff3030",
            .ForgeEntry = Ids::GoBloodForge,
            .ForgePosition = { 2427.28f, -5544.45f, 420.863f, -0.983229f },
            .ForgeRotation = QuaternionData(0.0f, 0.0f, -0.47205f, 0.881572f),
            .ForgeAuraScale = 3.0f,
            .ForgeAuras = { Spells::ForgeSpiritsBlood, Spells::ForgeHysteriaBlood },
            .ForgeObjects = {{ { Ids::GoPoolOfBlood, PoolOfBloodScale } }},
            .ForgeScaledAuras = {{ { Spells::ForgeBeamBlood, 1.8f } }},       // the red beam model is wider
            .CarrierVisualKit = VisualKits::CarrierBlood,
            .CarrierAuras = { Spells::CarrierAuraBlood, Spells::ForgeSpiritsBlood }
        },
        {
            .Name = "Unholy",
            .Color = "ff40ff40",
            .ForgeEntry = Ids::GoUnholyForge,
            .ForgePosition = { 2509.31f, -5560.39f, 420.863f, -2.55402f },
            .ForgeRotation = QuaternionData(0.0f, 0.0f, -0.957154f, 0.289578f),
            .ForgeAuraScale = 5.0f,
            .ForgeAuras = { Spells::ForgeSpiritsUnholyBase, Spells::ForgeSpiritsUnholy },
            .ForgeObjects = {{ { Ids::GoScourgeCircle, ScourgeCircleScale }, { Ids::GoUnholyLight, UnholyLightScale } }},
            .ForgeScaledAuras = {{ { Spells::ForgePlagueUnholy, 1.0f }, { Spells::ForgeGhostStateUnholy, 1.0f } }},
            .CarrierVisualKit = VisualKits::CarrierUnholy,
            .CarrierAuras = { Spells::ForgeSpiritsUnholyChest, Spells::ForgeSpiritsUnholy, Spells::CarrierAuraUnholy }
        }
    }};

    uint32 GetPointsForPosition(Position const& position)
    {
        float const dx = position.GetPositionX() - Positions::Center.GetPositionX();
        float const dy = position.GetPositionY() - Positions::Center.GetPositionY();
        float const z = position.GetPositionZ();
        float const distance = std::sqrt(dx * dx + dy * dy);

        if (z < Scoring::FloorMinZ || z > Scoring::FloorMaxZ)
            return Scoring::PointsOutside;

        if (distance <= Scoring::CenterRadius && z < Scoring::CenterMaxZ)
            return Scoring::PointsCenter;

        // the hall is round, except for the door that leads outside
        float const doorX = Positions::Door.GetPositionX() - Positions::Center.GetPositionX();
        float const doorY = Positions::Door.GetPositionY() - Positions::Center.GetPositionY();
        float const alongDoor = (dx * doorX + dy * doorY) / std::sqrt(doorX * doorX + doorY * doorY);

        if (distance <= Scoring::PlatformRadius && alongDoor <= Scoring::DoorDistance)
            return Scoring::PointsPlatform;

        return Scoring::PointsOutside;
    }
}
