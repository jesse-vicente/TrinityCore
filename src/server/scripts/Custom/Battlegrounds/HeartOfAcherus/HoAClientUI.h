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

#ifndef TRINITY_HOA_CLIENT_UI_H
#define TRINITY_HOA_CLIENT_UI_H

#include "HoADefines.h"
#include "ObjectGuid.h"
#include <array>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Player;

namespace HeartOfAcherus
{
    // the client UI Lua is split in two parts: login (every player) and match (queued or in a match only)
    enum PayloadPart : uint8
    {
        PAYLOAD_LOGIN = 0,
        PAYLOAD_MATCH,
        PAYLOAD_PART_COUNT
    };

    // Client-side UI without a client patch: a Lua listener installed through Warden::SendLua runs the payload parts
    // and commands sent as addon messages, and answers through whispers to self (see docs/client-ui.md).
    // Called from the world and map threads, the state is protected by _lock.
    class ClientUI
    {
    public:
        using CommandHandler = std::function<void(Player*, std::string const& args)>;
        using ParticipantCheck = std::function<bool(ObjectGuid)>;

        void Load(bool enabled, std::array<std::string, PAYLOAD_PART_COUNT> const& files);
        bool IsEnabled() const { return _enabled && !_scripts[PAYLOAD_LOGIN].empty(); }
        bool HasPart(PayloadPart part) const { return _enabled && !_scripts[part].empty(); }
        void PrependToPart(PayloadPart part, std::string const& text);

        // client message bodies handled outside of the UI: "<name>" or "<name> <args>", e.g. "mount 23221"
        void RegisterCommand(std::string const& body, CommandHandler handler);
        // queued or in a match: gets the match part and the relabel
        void SetParticipantCheck(ParticipantCheck check) { _isParticipant = std::move(check); }

        // world update
        void Update(uint32 diff);
        bool IsPingDue(uint32 diff);
        void Ping(std::vector<ObjectGuid> const& participants);

        // hooks
        void OnLogin(Player* player);
        void OnLogout(ObjectGuid guid);
        void OnWardenLuaExecuted(Player* player);
        void OnAddonMessage(Player* player, std::string const& msg, bool& handled);
        void OnStatusRequest(Player* player);
        void OnParticipantJoined(Player* player);

        static void Send(Player* player, std::string const& lua);
        void SetRelabel(Player* player, bool active) const;
        void ShowError(Player* player, std::string const& text) const;

    private:
        void RequestPart(Player* player, PayloadPart part);
        void SendPart(Player* player, PayloadPart part);
        void SendBootstrap(Player* player);
        void Probe(Player* player, bool assertRelabel = false);
        void Resync(Player* player);
        void ClearState(ObjectGuid guid);
        bool ConsumePendingResync(ObjectGuid guid);
        bool IsParticipant(ObjectGuid guid) const { return _isParticipant && _isParticipant(guid); }

        bool _enabled = false;
        std::array<std::string, PAYLOAD_PART_COUNT> _scripts;
        std::unordered_map<std::string, CommandHandler> _commands;
        ParticipantCheck _isParticipant;
        uint32 _pingTimer = 0;

        std::mutex _lock;
        std::array<std::unordered_map<ObjectGuid, uint32>, PAYLOAD_PART_COUNT> _partCooldowns;
        std::array<std::unordered_map<ObjectGuid, uint8>, PAYLOAD_PART_COUNT> _partAttempts;
        std::array<std::unordered_set<ObjectGuid>, PAYLOAD_PART_COUNT> _pendingParts;      // asked during a cooldown
        std::array<std::unordered_set<ObjectGuid>, PAYLOAD_PART_COUNT> _appliedParts;
        std::unordered_set<ObjectGuid> _listenerInstalled;
        std::unordered_map<ObjectGuid, uint32> _bootstrapCooldowns;
        std::unordered_map<ObjectGuid, uint32> _probeTimeouts;      // no answer in time: bootstrap again
        std::unordered_set<ObjectGuid> _pendingBootstraps;          // asked during a cooldown or with Warden busy
        std::unordered_set<ObjectGuid> _bootstrapSecondPending;     // first bootstrap eval sent, OnEvent still missing
        std::unordered_set<ObjectGuid> _pendingResync;              // reported PLAYER_LOGOUT: /reload or logout
    };
}

#endif // TRINITY_HOA_CLIENT_UI_H
