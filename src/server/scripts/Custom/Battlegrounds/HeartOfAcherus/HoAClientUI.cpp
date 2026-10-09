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

#include "HoAClientUI.h"
#include "ChatPackets.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "Util.h"
#include "Warden.h"
#include "WorldSession.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr char Prefix[] = "AcherusBG";                      // addon message prefix of the listener and payloads
        constexpr uint32 PartCooldown = 1500;                       // retry of a failed part push
        constexpr uint32 BootstrapCooldown = 1500;
        constexpr uint32 ProbeTimeout = 1 * IN_MILLISECONDS;
        constexpr uint8 MaxPartAttempts = 3;                        // then give up and log
        constexpr std::size_t ChunkSize = 190;

        constexpr std::array<char const*, PAYLOAD_PART_COUNT> PartNames = { "login", "match" };
        constexpr std::array<char const*, PAYLOAD_PART_COUNT> PartAccumulators = { "AcherusBG_P1", "AcherusBG_P2" };
        constexpr std::array<char const*, PAYLOAD_PART_COUNT> PartFailures = { "return -2", "return -3" };

        // the listener, in two Warden evals under the 166 char Lua limit: frame first, then its OnEvent handler.
        // It only runs messages sent by the player itself; pcall swallows malformed ones
        constexpr char Bootstrap1[] = "AcherusBG_Listener=AcherusBG_Listener or CreateFrame\"Frame\"AcherusBG_Listener:RegisterEvent\"CHAT_MSG_ADDON\"";
        constexpr char Bootstrap2[] = "AcherusBG_Listener:SetScript(\"OnEvent\",function(_,_,p,m,_,s)if p==\"AcherusBG\"and s==UnitName\"player\"then pcall(loadstring(m))end end)";

        constexpr char RelabelOn[] = "if AcherusBG_UI then AcherusBG_UI.active=true AcherusBG_UI.Relabel() end";
        constexpr char RelabelOff[] = "if AcherusBG_UI then AcherusBG_UI.active=false AcherusBG_UI.Relabel() end";

        // asks back the applied parts as a bitmask (1 = login, 2 = match); assertRelabel turns the relabel on in the
        // same message
        std::string ProbeBody(bool assertRelabel)
        {
            std::string body = assertRelabel ? Trinity::StringFormat("{} ", RelabelOn) : "";
            return body + Trinity::StringFormat(
                "local f=0 if AcherusBG_UI then f=f+1 end if AcherusBG_Part2 then f=f+2 end "
                "SendAddonMessage('{}','return '..f,'WHISPER',UnitName'player')", Prefix);
        }

        // counts the timers down, erases the expired ones and returns them
        std::vector<ObjectGuid> CountDown(std::unordered_map<ObjectGuid, uint32>& timers, uint32 diff)
        {
            std::vector<ObjectGuid> expired;
            for (auto itr = timers.begin(); itr != timers.end();)
            {
                if (itr->second <= diff)
                {
                    expired.push_back(itr->first);
                    itr = timers.erase(itr);
                }
                else
                {
                    itr->second -= diff;
                    ++itr;
                }
            }
            return expired;
        }
    }

    // ordinary .lua files, read at startup and on .reload config
    void ClientUI::Load(bool enabled, std::array<std::string, PAYLOAD_PART_COUNT> const& files)
    {
        _enabled = enabled;
        for (std::string& script : _scripts)
            script.clear();

        if (!_enabled)
            return;

        for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
        {
            if (files[part].empty())
            {
                TC_LOG_INFO("scripts", "HeartOfAcherus: HeartOfAcherus.ClientUI is enabled but the client UI {} part file is empty, that part is disabled", PartNames[part]);
                continue;
            }

            std::ifstream file(files[part], std::ios::binary);
            if (!file)
            {
                TC_LOG_ERROR("scripts", "HeartOfAcherus: cannot open client UI {} part '{}'", PartNames[part], files[part]);
                continue;
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            _scripts[part] = buffer.str();

            // the client may cut chat messages at CR/LF
            _scripts[part].erase(std::remove(_scripts[part].begin(), _scripts[part].end(), '\r'), _scripts[part].end());

            TC_LOG_INFO("scripts", "HeartOfAcherus: loaded client UI {} part ({} bytes) from '{}'", PartNames[part], _scripts[part].size(), files[part]);
        }
    }

    void ClientUI::PrependToPart(PayloadPart part, std::string const& text)
    {
        if (!_scripts[part].empty())
            _scripts[part].insert(0, text);
    }

    void ClientUI::RegisterCommand(std::string const& body, CommandHandler handler)
    {
        _commands[body] = std::move(handler);
    }

    // ----------------------------------------------------------------- world update

    // resends what was asked during a cooldown and bootstraps the clients that did not answer a probe
    void ClientUI::Update(uint32 diff)
    {
        std::vector<ObjectGuid> unanswered;
        {
            std::lock_guard<std::mutex> lock(_lock);
            for (auto& cooldowns : _partCooldowns)
                CountDown(cooldowns, diff);
            CountDown(_bootstrapCooldowns, diff);
            unanswered = CountDown(_probeTimeouts, diff);
        }

        for (ObjectGuid const& guid : unanswered)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBootstrap(player);

        std::vector<ObjectGuid> retryBootstrap;
        std::array<std::vector<ObjectGuid>, PAYLOAD_PART_COUNT> retryParts;
        {
            std::lock_guard<std::mutex> lock(_lock);
            for (ObjectGuid const& guid : _pendingBootstraps)
                if (!_bootstrapCooldowns.contains(guid))
                    retryBootstrap.push_back(guid);

            for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
                for (ObjectGuid const& guid : _pendingParts[part])
                    if (!_partCooldowns[part].contains(guid))
                        retryParts[part].push_back(guid);
        }

        for (ObjectGuid const& guid : retryBootstrap)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBootstrap(player);
            else
            {
                std::lock_guard<std::mutex> lock(_lock);
                _pendingBootstraps.erase(guid);
            }
        }

        for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
        {
            for (ObjectGuid const& guid : retryParts[part])
            {
                if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                    RequestPart(player, PayloadPart(part));
                else
                {
                    std::lock_guard<std::mutex> lock(_lock);
                    _pendingParts[part].erase(guid);
                }
            }
        }
    }

    bool ClientUI::IsPingDue(uint32 diff)
    {
        _pingTimer += diff;
        if (_pingTimer < Timers::ClientPing)
            return false;

        _pingTimer = 0;
        return true;
    }

    // safety probe of the players in a match or in the queue
    void ClientUI::Ping(std::vector<ObjectGuid> const& participants)
    {
        if (!IsEnabled())
            return;

        for (ObjectGuid const& guid : participants)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                if (player->IsInWorld())
                    Probe(player, true);
    }

    // ----------------------------------------------------------------- hooks

    // fresh session: install the listener, its Warden answer pushes the login part
    void ClientUI::OnLogin(Player* player)
    {
        ClearState(player->GetGUID());
        SendBootstrap(player);
    }

    void ClientUI::OnLogout(ObjectGuid guid)
    {
        ClearState(guid);
    }

    // the client ran a Warden Lua chunk: the second bootstrap eval, or the login part once the listener is up
    void ClientUI::OnWardenLuaExecuted(Player* player)
    {
        if (!IsEnabled())
            return;

        bool secondPending = false;
        {
            std::lock_guard<std::mutex> lock(_lock);
            _probeTimeouts.erase(player->GetGUID());
            secondPending = _bootstrapSecondPending.erase(player->GetGUID()) > 0;
        }

        if (secondPending)
        {
            bool sent = false;
            if (Warden* warden = player->GetSession()->GetWarden())
                sent = warden->SendLua(Bootstrap2);

            if (!sent)
            {
                std::lock_guard<std::mutex> lock(_lock);
                _pendingBootstraps.insert(player->GetGUID());
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_lock);
            _listenerInstalled.insert(player->GetGUID());
            _pendingParts[PAYLOAD_LOGIN].erase(player->GetGUID());
            _pendingParts[PAYLOAD_MATCH].erase(player->GetGUID());
        }

        // the match part follows from the login part ack
        RequestPart(player, PAYLOAD_LOGIN);
    }

    // bodies: a registered command, "return N" (probe answer, N = applied parts bitmask), "return -1" (PLAYER_LOGOUT),
    // "return -2"/"return -3" (login/match part failed to run)
    void ClientUI::OnAddonMessage(Player* player, std::string const& msg, bool& handled)
    {
        if (!IsEnabled())
            return;

        std::string::size_type const tab = msg.find('\t');
        std::string const prefix = tab == std::string::npos ? msg : msg.substr(0, tab);
        if (prefix != Prefix)
            return;

        handled = true;
        if (tab == std::string::npos)
            return;

        std::string const body = msg.substr(tab + 1);
        std::string::size_type const space = body.find(' ');
        if (auto command = _commands.find(body.substr(0, space)); command != _commands.end())
        {
            command->second(player, space == std::string::npos ? std::string() : body.substr(space + 1));
            return;
        }

        ObjectGuid const guid = player->GetGUID();

        // PLAYER_LOGOUT fires on logout and on /reload: the next request of the player tells them apart
        // (OnStatusRequest, OnParticipantJoined), a real logout clears it in OnLogout
        if (body == "return -1")
        {
            std::lock_guard<std::mutex> lock(_lock);
            _probeTimeouts.erase(guid);
            _listenerInstalled.erase(guid);
            for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
            {
                _appliedParts[part].erase(guid);
                _partAttempts[part].erase(guid);
            }
            _pendingResync.insert(guid);
            return;
        }

        for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
        {
            if (body == PartFailures[part])
            {
                TC_LOG_ERROR("scripts", "HeartOfAcherus: {} failed to apply the client UI {} part", player->GetName(), PartNames[part]);
                RequestPart(player, PayloadPart(part));
                return;
            }
        }

        if (!StringStartsWith(body, "return "))
            return;

        int32 const value = std::atoi(body.c_str() + 7);
        bool const hasLogin = (value & 1) != 0;
        bool const hasMatch = (value & 2) != 0;

        {
            std::lock_guard<std::mutex> lock(_lock);
            _probeTimeouts.erase(guid);
            _pendingResync.erase(guid);
            _listenerInstalled.insert(guid);                        // an answer proves the listener
            if (hasLogin)
            {
                _appliedParts[PAYLOAD_LOGIN].insert(guid);
                _partAttempts[PAYLOAD_LOGIN].erase(guid);
            }
            if (hasMatch)
            {
                _appliedParts[PAYLOAD_MATCH].insert(guid);
                _partAttempts[PAYLOAD_MATCH].erase(guid);
            }
        }

        if (hasLogin && hasMatch)
        {
            TC_LOG_INFO("scripts", "HeartOfAcherus: {} applied both client UI parts", player->GetName());
            return;
        }

        bool const participant = IsParticipant(guid);
        if (!hasLogin)
        {
            TC_LOG_INFO("scripts", "HeartOfAcherus: {} requested the client UI login part", player->GetName());
            RequestPart(player, PAYLOAD_LOGIN);
        }
        else if (participant)
        {
            TC_LOG_INFO("scripts", "HeartOfAcherus: {} requested the client UI match part", player->GetName());
            RequestPart(player, PAYLOAD_MATCH);
        }

        if (participant)
            SetRelabel(player, true);
    }

    // CMSG_BATTLEFIELD_STATUS comes when the UI loads: after a PLAYER_LOGOUT report it was a /reload
    void ClientUI::OnStatusRequest(Player* player)
    {
        if (ConsumePendingResync(player->GetGUID()))
            Resync(player);
        else
            Probe(player);
    }

    // queued or entering a match: the match part, or a resync if a /reload was not detected yet
    void ClientUI::OnParticipantJoined(Player* player)
    {
        if (ConsumePendingResync(player->GetGUID()))
            Resync(player);
        else
        {
            RequestPart(player, PAYLOAD_MATCH);
            Probe(player);
        }
    }

    // ----------------------------------------------------------------- messages

    // whisper to self in the addon language: the only sender the listener accepts
    void ClientUI::Send(Player* player, std::string const& lua)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, lua, 0, "", LOCALE_enUS, Prefix);
        player->SendDirectMessage(packet.Write());
    }

    // the client relabels the fake Eye of the Storm only while active
    void ClientUI::SetRelabel(Player* player, bool active) const
    {
        if (!IsEnabled() || !player)
            return;

        Send(player, active ? RelabelOn : RelabelOff);
    }

    // red text like the client's own errors, or a notification without the UI
    void ClientUI::ShowError(Player* player, std::string const& text) const
    {
        if (IsEnabled())
            Send(player, Trinity::StringFormat("UIErrorsFrame:AddMessage('{}',1,0.1,0.1,1)", text));
        else
            player->GetSession()->SendNotification("%s", text.c_str());
    }

    // ----------------------------------------------------------------- delivery

    // pushes a part, or queues it during the cooldown; without a listener the bootstrap is redone instead
    void ClientUI::RequestPart(Player* player, PayloadPart part)
    {
        if (!HasPart(part) || !player)
            return;

        ObjectGuid const guid = player->GetGUID();
        bool needsBootstrap = false;
        {
            std::lock_guard<std::mutex> lock(_lock);
            if (_appliedParts[part].contains(guid))
                return;

            if (!_listenerInstalled.contains(guid))
                needsBootstrap = true;
            else
            {
                auto& cooldowns = _partCooldowns[part];
                auto itr = cooldowns.find(guid);
                if (itr != cooldowns.end() && itr->second)
                {
                    _pendingParts[part].insert(guid);
                    return;
                }

                cooldowns[guid] = PartCooldown;
                _pendingParts[part].erase(guid);
            }
        }

        if (needsBootstrap)
        {
            SendBootstrap(player);
            return;
        }

        SendPart(player, part);
    }

    // chunks of Lua appended to an accumulator per part, then run; the payload acks itself, a failure reports back
    void ClientUI::SendPart(Player* player, PayloadPart part)
    {
        std::string const& script = _scripts[part];
        if (script.empty())
            return;

        if (script.find("]==]") != std::string::npos)
        {
            TC_LOG_ERROR("scripts", "HeartOfAcherus: client UI {} part contains ']==]' and cannot be sent", PartNames[part]);
            return;
        }

        ObjectGuid const guid = player->GetGUID();
        {
            std::lock_guard<std::mutex> lock(_lock);
            uint8& attempts = _partAttempts[part][guid];
            if (attempts >= MaxPartAttempts)
            {
                if (attempts == MaxPartAttempts)
                {
                    attempts = MaxPartAttempts + 1;                 // log only once
                    TC_LOG_ERROR("scripts", "HeartOfAcherus: client UI {} part failed for {} after {} attempts, giving up", PartNames[part], player->GetName(), uint32(MaxPartAttempts));
                }
                return;
            }
            ++attempts;
        }

        char const* accumulator = PartAccumulators[part];
        TC_LOG_INFO("scripts", "HeartOfAcherus: sending client UI {} part to {} ({} bytes)", PartNames[part], player->GetName(), script.size());

        for (std::size_t offset = 0; offset < script.size(); offset += ChunkSize)
        {
            // a long string skips the newline right after its opening bracket: lead with one so it is never the payload's
            std::string body = offset == 0
                ? Trinity::StringFormat("{}=[==[\n", accumulator)
                : Trinity::StringFormat("{}={}..[==[\n", accumulator, accumulator);
            body += script.substr(offset, ChunkSize);
            body += "]==]";
            Send(player, body);
        }

        Send(player, Trinity::StringFormat(
            "local f,e=loadstring({}) if not(f and pcall(f))then SendAddonMessage('{}','{}','WHISPER',UnitName'player')end",
            accumulator, Prefix, PartFailures[part]));

        // the payload starts inactive
        SetRelabel(player, IsParticipant(guid));

        // the answer (or the probe timeout) drives the retry or a new bootstrap
        Probe(player);
    }

    // installs the listener through Warden, on demand (outside the check scheduler)
    void ClientUI::SendBootstrap(Player* player)
    {
        if (!IsEnabled())
            return;

        ObjectGuid const guid = player->GetGUID();
        {
            std::lock_guard<std::mutex> lock(_lock);
            _probeTimeouts.erase(guid);

            auto itr = _bootstrapCooldowns.find(guid);
            if (itr != _bootstrapCooldowns.end() && itr->second)
            {
                _pendingBootstraps.insert(guid);
                return;
            }

            _bootstrapCooldowns[guid] = BootstrapCooldown;
            _pendingBootstraps.erase(guid);
        }

        bool sent = false;
        if (Warden* warden = player->GetSession()->GetWarden())
            sent = warden->SendLua(Bootstrap1);

        std::lock_guard<std::mutex> lock(_lock);
        if (sent)
        {
            // a new listener means a new client Lua environment
            _listenerInstalled.erase(guid);
            for (auto& applied : _appliedParts)
                applied.erase(guid);
            _bootstrapSecondPending.insert(guid);
        }
        else
            _pendingBootstraps.insert(guid);
    }

    // with the listener up the client answers the applied parts, otherwise the timeout bootstraps it; one probe in flight
    void ClientUI::Probe(Player* player, bool assertRelabel /*= false*/)
    {
        if (!IsEnabled())
            return;

        {
            std::lock_guard<std::mutex> lock(_lock);
            if (_probeTimeouts.contains(player->GetGUID()))
                return;

            _probeTimeouts[player->GetGUID()] = ProbeTimeout;
        }

        Send(player, ProbeBody(assertRelabel));
    }

    // a /reload wiped the listener: bootstrap again, even during its cooldown
    void ClientUI::Resync(Player* player)
    {
        if (!IsEnabled())
            return;

        {
            std::lock_guard<std::mutex> lock(_lock);
            _bootstrapCooldowns.erase(player->GetGUID());
        }

        TC_LOG_INFO("scripts", "HeartOfAcherus: {} reloaded the client UI, reinstalling the listener and payload", player->GetName());
        SendBootstrap(player);
    }

    void ClientUI::ClearState(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _bootstrapCooldowns.erase(guid);
        _probeTimeouts.erase(guid);
        _pendingBootstraps.erase(guid);
        _bootstrapSecondPending.erase(guid);
        _listenerInstalled.erase(guid);
        _pendingResync.erase(guid);
        for (uint8 part = 0; part < PAYLOAD_PART_COUNT; ++part)
        {
            _partCooldowns[part].erase(guid);
            _partAttempts[part].erase(guid);
            _pendingParts[part].erase(guid);
            _appliedParts[part].erase(guid);
        }
    }

    bool ClientUI::ConsumePendingResync(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_lock);
        return _pendingResync.erase(guid) > 0;
    }
}
