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

#include "HoARunes.h"
#include "HoAClientUI.h"
#include "HoALayout.h"
#include "HoAMatch.h"
#include "HoAUtil.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include <algorithm>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr float ForgeMaxHeight = 8.0f;                      // the upper floor is ~23 yards over the forges, in use range
        constexpr uint32 ForgeLockedFlags = GO_FLAG_INTERACT_COND;  // without GO_DYNFLAG_LO_ACTIVATE: plain cursor, not usable

        float GetCarrierScale(RuneState const& state)
        {
            float const bonus = std::min(RunePower::ScaleBase + RunePower::ScalePerStack * float(state.Stacks - 1), RunePower::ScaleMax);
            return state.CarrierOriginalScale * (1.0f + bonus);
        }

        // the Portal State is not stackable in the DBC, so the client shows its charges as the stack count
        bool IsCarrierStackAura(uint32 spellId)
        {
            return spellId == Spells::CarrierAuraFrost || spellId == Spells::CarrierAuraBlood || spellId == Spells::CarrierAuraUnholy;
        }

        void SetCarrierStackCharges(Player* player, RuneTemplate const& runeTemplate, uint32 stacks)
        {
            for (uint32 spellId : runeTemplate.CarrierAuras)
                if (IsCarrierStackAura(spellId))
                    if (Aura* aura = player->GetAura(spellId))
                        aura->SetCharges(uint8(std::min<uint32>(stacks, 255)));
        }

        // the forge use range reaches the upper floor through the ceiling
        bool IsAboveForges(Player const* player)
        {
            return player->GetPositionZ() - RuneTemplates.front().ForgePosition.GetPositionZ() > ForgeMaxHeight;
        }
    }

    void MatchRunes::OnForgeUse(Player* player, GameObject* forge)
    {
        Optional<Rune> rune = _match.GetHall().GetRuneOfForge(forge->GetGUID());
        if (!rune)
            return;

        ClientUI const& clientUI = _match.GetClientUI();
        if (_match.GetStatus() != MatchStatus::InProgress)
            clientUI.ShowError(player, "The runes are not active yet.");
        else if (!player->IsAlive())
            return;
        else if (IsAboveForges(player))
            clientUI.ShowClientError(player, "ERR_USE_TOO_FAR");
        else if (!_runes[std::size_t(*rune)].Carrier.IsEmpty())
            clientUI.ShowError(player, "This rune is already taken.");
        else if (GetCarried(player->GetGUID()))
            clientUI.ShowError(player, "You can only carry one rune.");
        else
            PickUp(*rune, player);
    }

    void MatchRunes::PickUp(Rune rune, Player* player)
    {
        RuneTemplate const& runeTemplate = GetRuneTemplate(rune);
        RuneState& state = _runes[std::size_t(rune)];

        state.Carrier = player->GetGUID();
        state.Stacks = 1;
        state.StackTimer = 0;
        state.CarrierOriginalScale = player->GetObjectScale();

        player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
        player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
        if (player->IsMounted())
            Dismount(player);

        player->SetObjectScale(GetCarrierScale(state));

        // the presences have stat effects, only their impact kit is played; it has sound, so only once
        player->SendPlaySpellVisualKit(runeTemplate.CarrierVisualKit, 0);
        for (uint32 spellId : runeTemplate.CarrierAuras)
            if (Aura* aura = ApplyPermanentAura(player, spellId))
                if (IsCarrierStackAura(spellId))
                    aura->SetCharges(uint8(std::min<uint32>(state.Stacks, 255)));

        _match.GetHall().SetForgeVisuals(rune, false);
        _match.UpdateWorldStates();

        _match.Announce(CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("{} has taken the |c{}{}|r rune!", player->GetName(), runeTemplate.Color, runeTemplate.Name));
        _match.PlaySound(Sounds::RuneEvent);
    }

    void MatchRunes::Drop(Rune rune, bool announce)
    {
        RuneTemplate const& runeTemplate = GetRuneTemplate(rune);
        RuneState& state = _runes[std::size_t(rune)];
        if (state.Carrier.IsEmpty())
            return;

        if (Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier))
        {
            for (uint32 spellId : runeTemplate.CarrierAuras)
                if (spellId)
                    player->RemoveAurasDueToSpell(spellId);
            player->SetObjectScale(state.CarrierOriginalScale);
        }

        state.Carrier.Clear();
        state.Stacks = 0;
        state.StackTimer = 0;

        if (_match.GetStatus() == MatchStatus::InProgress)
            _match.GetHall().SetForgeVisuals(rune, true);

        _match.UpdateWorldStates();

        if (announce)
        {
            _match.Announce(CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("The |c{}{}|r rune has returned to its runeforge!", runeTemplate.Color, runeTemplate.Name));
            _match.PlaySound(Sounds::RuneEvent);
        }
    }

    void MatchRunes::DropCarried(ObjectGuid guid)
    {
        if (Optional<Rune> rune = GetCarried(guid))
            Drop(*rune, true);
    }

    void MatchRunes::Update(uint32 diff)
    {
        for (std::size_t i = 0; i < RuneCount; ++i)
        {
            RuneState& state = _runes[i];
            if (state.Carrier.IsEmpty())
                continue;

            RuneTemplate const& runeTemplate = RuneTemplates[i];
            Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier);

            // the carrier aura is visible and cancelable: put it back so the client never desyncs from the carried state.
            // A recreated aura starts without the stack count, so that is restored every tick
            if (player)
            {
                for (uint32 spellId : runeTemplate.CarrierAuras)
                    if (spellId && !player->HasAura(spellId))
                        ApplyPermanentAura(player, spellId);

                SetCarrierStackCharges(player, runeTemplate, state.Stacks);
            }

            state.StackTimer += diff;
            if (state.StackTimer < Timers::RuneStack)
                continue;

            state.StackTimer -= Timers::RuneStack;
            ++state.Stacks;

            if (player)
            {
                player->SetObjectScale(GetCarrierScale(state));
                SetCarrierStackCharges(player, runeTemplate, state.Stacks);
            }
        }
    }

    // one Lua call per player with world coordinates (the client converts them); a carrier of the observer's team is a
    // raid member, so its name lets the client follow that unit smoothly
    void MatchRunes::SendMarkers()
    {
        if (!_match.GetClientUI().HasPart(PAYLOAD_MATCH))
            return;

        for (auto const& [guid, observer] : _match.GetPlayers())
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !IsInHall(player))
                continue;

            std::string text = Trinity::StringFormat("AcherusBG_Runes.Update({:.1f},{:.1f}", player->GetPositionX(), player->GetPositionY());

            for (std::size_t i = 0; i < RuneCount; ++i)
            {
                RuneState const& state = _runes[i];

                std::string carrierName;
                float x = RuneTemplates[i].ForgePosition.GetPositionX();
                float y = RuneTemplates[i].ForgePosition.GetPositionY();

                if (Player* carrier = ObjectAccessor::FindConnectedPlayer(state.Carrier))
                {
                    MatchPlayer const* carrierData = _match.GetPlayer(state.Carrier);
                    if (carrierData && carrierData->Team == observer.Team)
                        carrierName = carrier->GetName();

                    x = carrier->GetPositionX();
                    y = carrier->GetPositionY();
                }

                text += Trinity::StringFormat(",{:.1f},{:.1f},'{}'", x, y, carrierName);
            }

            text += ")";
            ClientUI::Send(player, text);
        }
    }

    // the client lights the use cursor on its own range check, so from above only this client gets the forges as not
    // usable, through a GAMEOBJECT_FLAGS values update
    void MatchRunes::UpdateForgeLock(MatchPlayer& matchPlayer, Player* player)
    {
        bool const locked = IsAboveForges(player);
        if (locked == matchPlayer.ForgesLocked)
            return;

        Map* map = player->GetMap();
        UpdateData data;
        for (std::size_t rune = 0; rune < RuneCount; ++rune)
        {
            GameObject* forge = map->GetGameObject(_match.GetHall().GetForge(Rune(rune)));
            if (!forge)
                continue;

            uint32 flags = forge->GetUInt32Value(GAMEOBJECT_FLAGS);
            if (locked)
                flags |= ForgeLockedFlags;

            ByteBuffer& buffer = data.GetBuffer();
            buffer << uint8(UPDATETYPE_VALUES);
            buffer << forge->GetPackGUID();
            UpdateMaskPacketBuilder mask(forge->GetValuesCount());
            mask.SetBit(GAMEOBJECT_FLAGS);
            mask.AppendToPacket(&buffer);
            buffer << flags;
            data.AddUpdateBlock();
        }

        if (!data.HasData())
            return;

        WorldPacket packet;
        data.BuildPacket(&packet);
        player->SendDirectMessage(&packet);
        matchPlayer.ForgesLocked = locked;
    }

    Optional<Rune> MatchRunes::GetCarried(ObjectGuid guid) const
    {
        for (std::size_t rune = 0; rune < RuneCount; ++rune)
            if (_runes[rune].Carrier == guid)
                return Rune(rune);

        return {};
    }

    RuneState const* MatchRunes::GetCarriedState(ObjectGuid guid) const
    {
        for (RuneState const& state : _runes)
            if (state.Carrier == guid)
                return &state;

        return nullptr;
    }

    std::array<uint32, PVP_TEAMS_COUNT> MatchRunes::GetHeldCounts() const
    {
        std::array<uint32, PVP_TEAMS_COUNT> counts = { };
        for (RuneState const& state : _runes)
        {
            if (state.Carrier.IsEmpty())
                continue;

            if (MatchPlayer const* carrier = _match.GetPlayer(state.Carrier))
                ++counts[carrier->Team];
        }
        return counts;
    }

    float MatchRunes::GetDamageDoneMultiplier(uint32 stacks)
    {
        return 1.0f + std::min(RunePower::DamageDonePct * stacks / 100.0f, RunePower::DamageDoneMaxPct / 100.0f);
    }

    float MatchRunes::GetDamageTakenMultiplier(uint32 stacks)
    {
        return 1.0f + std::min(RunePower::DamageTakenPct * stacks / 100.0f, RunePower::DamageTakenMaxPct / 100.0f);
    }

    float MatchRunes::GetHealingTakenMultiplier(uint32 stacks)
    {
        return 1.0f + std::max(RunePower::HealingTakenMaxPct / 100.0f, RunePower::HealingTakenPct * stacks / 100.0f);
    }

    // a rune is never kept across a login, a crash may have saved its auras
    void MatchRunes::RemoveCarrierAuras(Player* player)
    {
        for (RuneTemplate const& runeTemplate : RuneTemplates)
            for (uint32 spellId : runeTemplate.CarrierAuras)
                if (spellId)
                    player->RemoveAurasDueToSpell(spellId);
    }
}
