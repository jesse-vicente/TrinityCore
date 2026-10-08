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

#include "AcherusOrbs.h"
#include "Battlefield.h"
#include "BattlegroundPackets.h"
#include "Chat.h"
#include "ChatPackets.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "UpdateData.h"
#include "UpdateMask.h"
#include "Warden.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace AcherusOrbs
{
    std::array<OrbTemplate, MAX_ORBS> const OrbTemplates =
    {{
        { "Frost",  "ff69ccf0", Ids::GoFrostForge,  { 2493.37f, -5642.43f, 420.863f,  2.16421f  }, QuaternionData(0.0f, 0.0f,  0.882948f, 0.469471f), 3.0f, { Spells::ForgeSpiritsFrost, 0, 0, 0, 0 }, {}, {{ { Spells::ForgeBeamFrost, 2.0f }, { Spells::ForgeIceboundFrost, 8.0f } }}, VisualKits::CarrierFrost,  { Spells::ForgeSpiritsFrost, Spells::CarrierAuraFrost } },
        { "Blood",  "ffff3030", Ids::GoBloodForge,  { 2427.28f, -5544.45f, 420.863f, -0.983229f }, QuaternionData(0.0f, 0.0f, -0.47205f,  0.881572f), 3.0f, { Spells::ForgeSpiritsBlood, Spells::ForgeHysteriaBlood, 0, 0, 0 }, {{ { Ids::GoPoolOfBlood, Ids::PoolOfBloodScale } }}, {{ { Spells::ForgeBeamBlood, 1.8f } }}, VisualKits::CarrierBlood,  { Spells::CarrierAuraBlood, Spells::ForgeSpiritsBlood } },
        { "Unholy", "ff40ff40", Ids::GoUnholyForge, { 2509.31f, -5560.39f, 420.863f, -2.55402f  }, QuaternionData(0.0f, 0.0f, -0.957154f, 0.289578f), 5.0f, { Spells::ForgeSpiritsUnholyBase, Spells::ForgeSpiritsUnholy, 0, 0, 0 }, {{ { Ids::GoScourgeCircle, Ids::ScourgeCircleScale } }}, {{ { Spells::ForgePlagueUnholy, 1.0f }, { Spells::ForgeGhostStateUnholy, 1.0f } }}, VisualKits::CarrierUnholy, { Spells::ForgeSpiritsUnholyChest, Spells::ForgeSpiritsUnholy, Spells::CarrierAuraUnholy } }
    }};

    namespace
    {
        constexpr uint8 FirstPhaseBit = 9;                          // phases 1..256 are used by Acherus quests
        constexpr uint8 LastPhaseBit = 31;
        constexpr float PreparationLeash = 13.0f;                  // beyond the walls: only catches who gets past them
        constexpr float PreparationDomeScale = 2.0f;               // Anti-Magic Zone is ~7 yards at scale 1
        constexpr uint8 PreparationWallCount = 8;                  // octagon around the dome
        constexpr float PreparationWallDistance = 10.0f;           // from the spawn to the middle of each wall, just inside the dome
        constexpr float HonorableKillRange = 40.0f;
        constexpr uint8 RequiredLevel = 80;
        constexpr float SpiritGuideOffset = 3.0f;                   // spirit guide stands in front of the respawn point
        constexpr float SpiritHealerRange = 17.0f;                  // client AREA_SPIRIT_HEALER_IN_RANGE radius, measured in game
        constexpr float PortalRange = 3.0f;                         // radius of the Acherus teleporter aura (54724, SpellRadius 15)
        constexpr float AmbientCreatureRadius = 150.0f;            // map 0 spawns this close to the hall belong to the floating Acherus
        constexpr float ForgeMaxHeight = 8.0f;                     // the upper floor is ~23 yards over the forges, inside their use range
        constexpr uint32 ForgeLockedFlags = GO_FLAG_INTERACT_COND;  // usable only with GO_DYNFLAG_LO_ACTIVATE: the client shows the plain cursor
        constexpr float StairsPortalScale = 8.0f;
        // collision box of CollisionWallPvP01 (GameObjectModels.dtree) at scale 1: 11.08 yards along its local Y axis and
        // 17.56 high. Its only visible part is a thin checkered bar at its base (GameObjectDisplayInfo box: -0.36 to -0.07),
        // which has to stay under the floor
        constexpr float WallWidth = 11.078f;
        constexpr float WallTop = 17.562f;
        constexpr float StairsBarrierClearance = 3.0f;              // above the upper floor, beyond a jump from there
        // forges and their visuals are seen from anywhere in the hall and outside it; only these objects, not the map setting
        constexpr VisibilityDistanceType ForgeVisibility = VisibilityDistanceType::Large;

        // client-side UI relabel: the Warden bootstrap listener and the pushed payload share this prefix
        constexpr char ClientScriptPrefix[] = "AcherusBG";
        constexpr uint32 ClientScriptCooldown = 5 * IN_MILLISECONDS;
        constexpr uint32 ClientBootstrapCooldown = 1500;           // 1.5 s
        constexpr uint32 ClientBootstrapTimeout = 1 * IN_MILLISECONDS;

        // installed through Warden::SendLua in two evals (each under the 166 char Lua limit): the frame is
        // created and registered first, then the OnEvent handler is set. pcall swallows malformed messages.
        constexpr char ClientBootstrap1[] = "AcherusBG_Listener=AcherusBG_Listener or CreateFrame\"Frame\"AcherusBG_Listener:RegisterEvent\"CHAT_MSG_ADDON\"";
        constexpr char ClientBootstrap2[] = "AcherusBG_Listener:SetScript(\"OnEvent\",function(_,_,p,m,_,s)if p==\"AcherusBG\"and s==UnitName\"player\"then pcall(loadstring(m))end end)";

        // the client listener only runs Lua when the sender is the player itself; the probe reports whether the
        // script is already applied (return 2) or still needed (return 1). The reply bodies are valid Lua
        // no-ops because the guild broadcast echoes them back to the sender.
        std::string ClientPingBody()
        {
            return Trinity::StringFormat("if AcherusBG_UI then SendAddonMessage('{}','return 2','GUILD')else SendAddonMessage('{}','return 1','GUILD')end", ClientScriptPrefix, ClientScriptPrefix);
        }

        // the same probe, with the active flag refresh folded in: a periodic ping costs one message
        std::string ClientPingAndRelabelBody()
        {
            return Trinity::StringFormat("if AcherusBG_UI then AcherusBG_UI.active=true AcherusBG_UI.Relabel() SendAddonMessage('{}','return 2','GUILD')else SendAddonMessage('{}','return 1','GUILD')end", ClientScriptPrefix, ClientScriptPrefix);
        }

        char const* TeamName(TeamId team)
        {
            return team == TEAM_ALLIANCE ? "Alliance" : "Horde";
        }

        ChatMsg TeamChatMsg(TeamId team)
        {
            return team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE;
        }

        float GetCarrierScale(OrbState const& state)
        {
            float const bonus = std::min(OrbPower::ScaleBase + OrbPower::ScalePerStack * float(state.Stacks - 1), OrbPower::ScaleMax);
            return state.CarrierOriginalScale * (1.0f + bonus);
        }

        Aura* ApplyPermanentAura(Player* player, uint32 spellId)
        {
            if (!spellId)
                return nullptr;

            Aura* aura = player->AddAura(spellId, player);
            if (!aura)
                return nullptr;

            aura->SetMaxDuration(-1);
            aura->SetDuration(-1);
            return aura;
        }

        // the portal aura is the visible carrier aura (the client relabels it). Its DBC CumulativeAura is 0,
        // so the client applications field carries the charges instead: mirroring the stack count there makes
        // the client show the stacks on the buff icon and exposes them through UnitBuff for the tooltip.
        bool IsCarrierStackAura(uint32 spellId)
        {
            return spellId == Spells::CarrierAuraFrost || spellId == Spells::CarrierAuraBlood
                || spellId == Spells::CarrierAuraUnholy;
        }

        void SetCarrierStackCharges(Player* player, OrbTemplate const& orbTemplate, uint32 stacks)
        {
            for (uint32 spellId : orbTemplate.CarrierAuras)
                if (IsCarrierStackAura(spellId))
                    if (Aura* aura = player->GetAura(spellId))
                        aura->SetCharges(uint8(std::min<uint32>(stacks, 255)));
        }

        bool IsEligibleForMatch(Player const* player)
        {
            return player->IsInWorld() && player->IsAlive() && !player->IsInCombat() && !player->IsInFlight()
                && !player->IsBeingTeleported() && !player->InBattleground() && !player->InArena()
                && !player->InBattlegroundQueue() && !player->GetMap()->Instanceable();
        }

        // Group only checks its battlefield pointer for null, never uses it. A battlefield raid is not stored in the
        // database, keeps the original group of its members and gives it back when they leave, like a battleground raid.
        // This one is never registered in the BattlefieldMgr nor updated, so no core change is needed.
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

    Manager* Manager::instance()
    {
        static Manager instance;
        return &instance;
    }

    void Manager::LoadConfig()
    {
        _playersPerTeam = std::max(1, sConfigMgr->GetIntDefault("AcherusOrbs.PlayersPerTeam", 10));
        _minPlayersPerTeam = std::clamp<uint32>(sConfigMgr->GetIntDefault("AcherusOrbs.MinPlayersPerTeam", 10), 1, _playersPerTeam);
        _killBonus = std::max(0, sConfigMgr->GetIntDefault("AcherusOrbs.KillBonus", 10));

        _clientUiEnabled = sConfigMgr->GetBoolDefault("AcherusOrbs.ClientUi", false);
        _clientLuaFile = sConfigMgr->GetStringDefault("AcherusOrbs.ClientLuaFile", "");
        LoadClientScript();
    }

    // the client UI script is an ordinary .lua file read at startup and on .reload config
    void Manager::LoadClientScript()
    {
        _clientScript.clear();

        if (!_clientUiEnabled)
            return;

        if (_clientLuaFile.empty())
        {
            TC_LOG_INFO("scripts", "AcherusOrbs: AcherusOrbs.ClientUi is enabled but AcherusOrbs.ClientLuaFile is empty, client UI script disabled");
            return;
        }

        std::ifstream file(_clientLuaFile, std::ios::binary);
        if (!file)
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot open client UI script '{}'", _clientLuaFile);
            return;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        _clientScript = buffer.str();

        // the client may cut chat messages at CR/LF, so the payload is sent with LF only
        _clientScript.erase(std::remove(_clientScript.begin(), _clientScript.end(), '\r'), _clientScript.end());

        TC_LOG_INFO("scripts", "AcherusOrbs: loaded client UI script ({} bytes) from '{}'", _clientScript.size(), _clientLuaFile);
    }

    void Manager::SendAddonMessage(Player* player, std::string const& text)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, text, 0, "", LOCALE_enUS, ClientScriptPrefix);
        player->SendDirectMessage(packet.Write());
    }

    // the client only relabels while the server says the Eye of the Storm it sees is the Acherus fake;
    // the body is valid Lua executed by the listener, and a no-op while the payload is not applied
    void Manager::SetClientRelabel(Player* player, bool active) const
    {
        if (!_clientUiEnabled || _clientScript.empty() || !player)
            return;

        SendAddonMessage(player, active ? "if AcherusBG_UI then AcherusBG_UI.active=true AcherusBG_UI.Relabel() end" : "if AcherusBG_UI then AcherusBG_UI.active=false AcherusBG_UI.Relabel() end");
    }

    // pushes the payload, or remembers the request while the send cooldown is active (retried on expiry)
    void Manager::RequestClientScript(Player* player)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            auto itr = _clientScriptCooldowns.find(player->GetGUID());
            if (itr != _clientScriptCooldowns.end() && itr->second)
            {
                _pendingPayloads.insert(player->GetGUID());
                return;
            }

            _clientScriptCooldowns[player->GetGUID()] = ClientScriptCooldown;
            _pendingPayloads.erase(player->GetGUID());
        }

        SendClientScript(player);
    }

    // each message is valid Lua executed by the client listener: B accumulates the script, the last one runs it
    void Manager::SendClientScript(Player* player)
    {
        if (_clientScript.empty())
            return;

        if (_clientScript.find("]==]") != std::string::npos)
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: client UI script contains ']==]' and cannot be sent");
            return;
        }

        constexpr std::size_t ChunkSize = 190;

        TC_LOG_INFO("scripts", "AcherusOrbs: sending client UI script to {} ({} bytes)", player->GetName(), _clientScript.size());

        bool first = true;
        for (std::size_t offset = 0; offset < _clientScript.size(); offset += ChunkSize)
        {
            // a long string ignores the newline right after the opening bracket, so a chunk that starts
            // with one would lose it; the artificial leading newline is the one skipped, keeping the
            // payload byte-for-byte intact no matter where the chunk boundary falls
            std::string body = first ? "AcherusBG_Payload=[==[\n" : "AcherusBG_Payload=AcherusBG_Payload..[==[\n";
            first = false;
            body += _clientScript.substr(offset, ChunkSize);
            body += "]==]";
            SendAddonMessage(player, body);
        }

        SendAddonMessage(player, "loadstring(AcherusBG_Payload)()");

        // the payload defaults to inactive, so re-assert the current state right after applying it
        SetClientRelabel(player, GetMatch(player->GetGUID()) != nullptr || IsQueued(player->GetGUID()));
    }

    // pings the UI script to players in a match or in the queue; the body only replies while the script is not applied
    void Manager::PingClientScript()
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        for (std::unique_ptr<Match> const& match : _matches)
            for (auto const& [guid, matchPlayer] : match->Players)
                if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                    if (player->IsInWorld())
                        ProbeClientScript(player, true);

        std::vector<ObjectGuid> queued;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (std::deque<ObjectGuid> const& queue : _queue)
                queued.insert(queued.end(), queue.begin(), queue.end());
        }

        for (ObjectGuid const& guid : queued)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                if (player->IsInWorld())
                    ProbeClientScript(player, true);
    }

    // installs the addon message listener through the Warden module, on demand (not the check scheduler)
    void Manager::SendBootstrap(Player* player)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _clientBootstrapTimeouts.erase(player->GetGUID());

            auto itr = _clientBootstrapCooldowns.find(player->GetGUID());
            if (itr != _clientBootstrapCooldowns.end() && itr->second)
            {
                // still cooling down: remember the request so the next update can retry
                _pendingBootstraps.insert(player->GetGUID());
                return;
            }

            _clientBootstrapCooldowns[player->GetGUID()] = ClientBootstrapCooldown;
            _pendingBootstraps.erase(player->GetGUID());
        }

        bool sent = false;
        if (Warden* warden = player->GetSession()->GetWarden())
            sent = warden->SendLua(ClientBootstrap1);

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            if (sent)
                _bootstrapListenerPending.insert(player->GetGUID());    // part 2 (OnEvent) on the next exec
            else
                _pendingBootstraps.insert(player->GetGUID());           // Warden busy: retry when the cooldown expires
        }
    }

    // cheap probe: if the listener is present it answers return 1/return 2; otherwise a timeout sends the
    // bootstrap. A probe already in flight is left alone (rate limit); assertRelabel folds the active flag
    // refresh into the same message, so a ping costs a single addon message
    void Manager::ProbeClientScript(Player* player, bool assertRelabel)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            if (_clientBootstrapTimeouts.count(player->GetGUID()))
                return;

            _clientBootstrapTimeouts[player->GetGUID()] = ClientBootstrapTimeout;
        }

        SendAddonMessage(player, assertRelabel ? ClientPingAndRelabelBody() : ClientPingBody());
    }

    // the client processed a Warden Lua chunk: part 2 of the bootstrap, or the payload once the listener is up
    void Manager::OnWardenLuaExecuted(Player* player)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        bool listenerPending = false;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _clientBootstrapTimeouts.erase(player->GetGUID());
            listenerPending = _bootstrapListenerPending.erase(player->GetGUID()) > 0;
        }

        if (listenerPending)
        {
            // part 1 created and registered the frame: set the OnEvent handler; the payload follows next
            bool sent = false;
            if (Warden* warden = player->GetSession()->GetWarden())
                sent = warden->SendLua(ClientBootstrap2);

            if (!sent)
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                _pendingBootstraps.insert(player->GetGUID());            // redo the bootstrap when the cooldown expires
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _pendingPayloads.erase(player->GetGUID());                   // the payload is being pushed now
        }

        SendClientScript(player);
    }

    // the client listener answers the probe; return 1 means the script is still needed
    void Manager::OnAddonMessage(Player* player, std::string const& msg, bool& handled)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        std::string::size_type const tab = msg.find('\t');
        std::string const prefix = tab == std::string::npos ? msg : msg.substr(0, tab);
        if (prefix != ClientScriptPrefix)
            return;

        // the message belongs to the mode, never let it reach the default addon handling
        handled = true;

        if (tab == std::string::npos)
            return;

        std::string const body = msg.substr(tab + 1);
        if (body == "return 2")
        {
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                _clientBootstrapTimeouts.erase(player->GetGUID());
            }
            TC_LOG_INFO("scripts", "AcherusOrbs: {} applied the client UI script", player->GetName());
            return;
        }

        if (body != "return 1")
            return;

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _clientBootstrapTimeouts.erase(player->GetGUID());
        }

        TC_LOG_INFO("scripts", "AcherusOrbs: {} requested the client UI script", player->GetName());
        RequestClientScript(player);
    }

    // ----------------------------------------------------------------- queue

    bool Manager::Enqueue(Player* player, std::string& error)
    {
        if (IsInMatch(player->GetGUID()))
        {
            error = "You are already in a battle for Acherus.";
            return false;
        }

        if (player->InBattleground() || player->InArena())
        {
            error = "You cannot queue while in a battleground or arena.";
            return false;
        }

        // a real battleground or arena queue also owns one of the two queue slots, and a player in one
        // cannot join the Acherus queue (the custom mode shares the client with the native pool)
        if (player->InBattlegroundQueue())
        {
            error = "You cannot queue while in a battleground or arena queue.";
            return false;
        }

        if (player->GetLevel() < RequiredLevel)
        {
            error = Trinity::StringFormat("You must be level {} to join the battle for Acherus.", RequiredLevel);
            return false;
        }

        TeamId const team = player->GetTeamId();
        if (team != TEAM_ALLIANCE && team != TEAM_HORDE)
            return false;

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (std::deque<ObjectGuid> const& queue : _queue)
            {
                if (std::find(queue.begin(), queue.end(), player->GetGUID()) != queue.end())
                {
                    error = "You are already queued for the battle for Acherus.";
                    return false;
                }
            }

            _queue[team].push_back(player->GetGUID());
        }

        SendQueueStatus(player);
        ProbeClientScript(player);
        return true;
    }

    bool Manager::Dequeue(ObjectGuid guid)
    {
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (std::deque<ObjectGuid>& queue : _queue)
            {
                auto itr = std::find(queue.begin(), queue.end(), guid);
                if (itr != queue.end())
                {
                    queue.erase(itr);
                    found = true;
                    break;
                }
            }
        }

        if (found)
            ClearQueueStatus(guid, ObjectAccessor::FindConnectedPlayer(guid));

        return found;
    }

    bool Manager::IsQueued(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        for (std::deque<ObjectGuid> const& queue : _queue)
            if (std::find(queue.begin(), queue.end(), guid) != queue.end())
                return true;
        return false;
    }

    std::array<std::size_t, PVP_TEAMS_COUNT> Manager::GetQueueSizes()
    {
        std::lock_guard<std::mutex> lock(_queueLock);
        return { _queue[TEAM_ALLIANCE].size(), _queue[TEAM_HORDE].size() };
    }

    std::string Manager::GetStatus() const
    {
        std::string text = Trinity::StringFormat("Acherus orbs: {} match(es), {} per team (min {}), kill bonus {}",
            _matches.size(), _playersPerTeam, _minPlayersPerTeam, _killBonus);

        for (std::unique_ptr<Match> const& match : _matches)
        {
            char const* status = match->Status == MatchStatus::Preparation ? "preparation" : match->Status == MatchStatus::InProgress ? "in progress" : "ended";
            text += Trinity::StringFormat("\n#{} phase {} {} - {}s left - Alliance {} x {} Horde - {} players", match->Id, match->PhaseMask,
                status, match->StatusTimer / IN_MILLISECONDS, match->Score[TEAM_ALLIANCE], match->Score[TEAM_HORDE], match->Players.size());
        }

        return text;
    }

    // ----------------------------------------------------------------- update (world thread, maps are not being updated)

    void Manager::Update(uint32 diff)
    {
        ProcessRequests();
        ProcessPendingReturns(diff);

        std::vector<ObjectGuid> expiredBootstrap;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (auto itr = _clientScriptCooldowns.begin(); itr != _clientScriptCooldowns.end();)
            {
                if (itr->second <= diff)
                    itr = _clientScriptCooldowns.erase(itr);
                else
                {
                    itr->second -= diff;
                    ++itr;
                }
            }

            for (auto itr = _clientBootstrapCooldowns.begin(); itr != _clientBootstrapCooldowns.end();)
            {
                if (itr->second <= diff)
                    itr = _clientBootstrapCooldowns.erase(itr);
                else
                {
                    itr->second -= diff;
                    ++itr;
                }
            }

            for (auto itr = _clientBootstrapTimeouts.begin(); itr != _clientBootstrapTimeouts.end();)
            {
                if (itr->second <= diff)
                {
                    expiredBootstrap.push_back(itr->first);
                    itr = _clientBootstrapTimeouts.erase(itr);
                }
                else
                {
                    itr->second -= diff;
                    ++itr;
                }
            }
        }

        // a probe got no answer: the listener is missing, install it through Warden
        for (ObjectGuid const& guid : expiredBootstrap)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBootstrap(player);

        // requests dropped while a cooldown was active: send them as soon as it expires
        std::vector<ObjectGuid> retryBootstrap;
        std::vector<ObjectGuid> retryPayload;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (ObjectGuid const& guid : _pendingBootstraps)
                if (_clientBootstrapCooldowns.count(guid) == 0)
                    retryBootstrap.push_back(guid);

            for (ObjectGuid const& guid : _pendingPayloads)
                if (_clientScriptCooldowns.count(guid) == 0)
                    retryPayload.push_back(guid);
        }

        for (ObjectGuid const& guid : retryBootstrap)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBootstrap(player);
            else
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                _pendingBootstraps.erase(guid);
            }
        }

        for (ObjectGuid const& guid : retryPayload)
        {
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                RequestClientScript(player);
            else
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                _pendingPayloads.erase(guid);
            }
        }

        FillOpenMatches();
        TryCreateMatch();

        _clientPingTimer += diff;
        if (_clientPingTimer >= Timers::ClientPing)
        {
            _clientPingTimer = 0;
            PingClientScript();
        }

        for (std::unique_ptr<Match>& match : _matches)
            UpdateMatch(*match, diff);

        for (auto itr = _matches.begin(); itr != _matches.end();)
        {
            Match& match = **itr;
            if (match.Status != MatchStatus::Ended || match.StatusTimer)
            {
                ++itr;
                continue;
            }

            std::vector<ObjectGuid> players;
            for (auto const& [guid, matchPlayer] : match.Players)
                players.push_back(guid);

            for (ObjectGuid const& guid : players)
                RemovePlayer(match, guid, RemoveMode::TeleportOut);

            if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
                DespawnObjects(match, map);

            DisbandRaids(match);
            _usedPhases &= ~match.PhaseMask;
            TC_LOG_INFO("scripts", "AcherusOrbs: match {} finished", match.Id);
            itr = _matches.erase(itr);
        }
    }

    void Manager::ProcessRequests()
    {
        std::vector<ObjectGuid> leaves;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            std::swap(leaves, _pendingLeaves);
        }

        for (ObjectGuid const& guid : leaves)
            if (Match* match = GetMatch(guid))
                RemovePlayer(*match, guid, RemoveMode::TeleportOut);

        std::vector<ObjectGuid> skips;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            std::swap(skips, _preparationSkips);
        }

        // the match starts in its next update, as Battleground::SetStartDelayTime(0) does for .bg start
        for (ObjectGuid const& guid : skips)
        {
            for (std::unique_ptr<Match>& match : _matches)
                if (match->Status == MatchStatus::Preparation && (guid.IsEmpty() || match->Players.contains(guid)))
                    match->StatusTimer = 0;
        }

        if (_endAllRequested)
        {
            _endAllRequested = false;
            for (std::unique_ptr<Match>& match : _matches)
                if (match->Status != MatchStatus::Ended)
                    EndMatch(*match, TEAM_NEUTRAL);
        }
    }

    // like battlegrounds, queued players take the free places of the running matches before a new one is created
    void Manager::FillOpenMatches()
    {
        for (std::unique_ptr<Match>& match : _matches)
        {
            if (match->Status == MatchStatus::Ended)
                continue;

            // offline players keep their place, as in battlegrounds
            std::array<uint32, PVP_TEAMS_COUNT> teamSize = { };
            for (auto const& [guid, matchPlayer] : match->Players)
                ++teamSize[matchPlayer.Team];

            std::array<std::vector<Player*>, PVP_TEAMS_COUNT> joining;
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
                {
                    for (ObjectGuid const& guid : _queue[team])
                    {
                        if (teamSize[team] + joining[team].size() >= _playersPerTeam)
                            break;

                        if (Player* player = ObjectAccessor::FindConnectedPlayer(guid); player && IsEligibleForMatch(player))
                            joining[team].push_back(player);
                    }
                }
            }

            for (std::vector<Player*> const& team : joining)
            {
                for (Player* player : team)
                {
                    Dequeue(player->GetGUID());
                    AddPlayer(*match, player);
                    TC_LOG_INFO("scripts", "AcherusOrbs: {} joined match {} in progress", player->GetName(), match->Id);
                }
            }
        }
    }

    bool Manager::SkipPreparation(ObjectGuid guid)
    {
        if (!guid.IsEmpty() && !IsInMatch(guid))
            return false;

        std::lock_guard<std::mutex> lock(_queueLock);
        _preparationSkips.push_back(guid);
        return true;
    }

    void Manager::TryCreateMatch()
    {
        std::array<std::vector<Player*>, PVP_TEAMS_COUNT> ready;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
            {
                for (auto itr = _queue[team].begin(); itr != _queue[team].end();)
                {
                    Player* player = ObjectAccessor::FindConnectedPlayer(*itr);
                    if (!player)
                    {
                        itr = _queue[team].erase(itr);
                        continue;
                    }

                    if (ready[team].size() < _playersPerTeam && IsEligibleForMatch(player))
                        ready[team].push_back(player);
                    ++itr;
                }
            }
        }

        bool const enough = ready[TEAM_ALLIANCE].size() >= _minPlayersPerTeam && ready[TEAM_HORDE].size() >= _minPlayersPerTeam;
        bool const forced = _forceStart && (!ready[TEAM_ALLIANCE].empty() || !ready[TEAM_HORDE].empty());
        if (!enough && !forced)
            return;

        uint32 phaseMask = 0;
        for (uint8 bit = FirstPhaseBit; bit <= LastPhaseBit && !phaseMask; ++bit)
            if (!(_usedPhases & (1u << bit)))
                phaseMask = 1u << bit;

        if (!phaseMask)
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: no free phase for a new match ({} running)", _matches.size());
            return;
        }

        _forceStart = false;
        _usedPhases |= phaseMask;

        std::unique_ptr<Match>& match = _matches.emplace_back(std::make_unique<Match>());
        match->Id = _nextMatchId++;
        match->PhaseMask = phaseMask;

        for (std::vector<Player*> const& team : ready)
        {
            for (Player* player : team)
            {
                Dequeue(player->GetGUID());
                AddPlayer(*match, player);
            }
        }

        TC_LOG_INFO("scripts", "AcherusOrbs: match {} created in phase {} ({} x {})", match->Id, phaseMask, ready[TEAM_ALLIANCE].size(), ready[TEAM_HORDE].size());
        StartPreparation(*match);
    }

    void Manager::UpdateMatch(Match& match, uint32 diff)
    {
        uint32 const oldTimer = match.StatusTimer;
        match.StatusTimer = match.StatusTimer > diff ? match.StatusTimer - diff : 0;

        // like battlegrounds, resurrection waves also run while the gates are closed
        if (match.Status != MatchStatus::Ended)
        {
            match.ResurrectTimer += diff;
            if (match.ResurrectTimer >= Timers::ResurrectWave)
            {
                match.ResurrectTimer = 0;
                ResurrectDead(match);
            }
        }

        match.PlayerCheckTimer += diff;
        if (match.PlayerCheckTimer >= Timers::PlayerCheck)
        {
            match.PlayerCheckTimer = 0;
            CheckPlayers(match);
        }

        switch (match.Status)
        {
            case MatchStatus::Preparation:
                if (oldTimer > MINUTE * IN_MILLISECONDS && match.StatusTimer <= MINUTE * IN_MILLISECONDS)
                    Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 1 minute.");
                else if (oldTimer > 30 * IN_MILLISECONDS && match.StatusTimer <= 30 * IN_MILLISECONDS)
                    Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 30 seconds. Prepare yourselves!");

                if (!match.StatusTimer)
                    StartMatch(match);
                break;
            case MatchStatus::InProgress:
            {
                UpdateCarriers(match, diff);
                UpdateBerserkBuffs(match, diff);

                match.TickTimer += diff;
                if (match.TickTimer >= Scoring::TickInterval)
                {
                    match.TickTimer -= Scoring::TickInterval;
                    ScoreTick(match);
                }

                // points come from the ticks and from kills (map thread)
                uint32 const alliance = match.Score[TEAM_ALLIANCE];
                uint32 const horde = match.Score[TEAM_HORDE];
                if (alliance >= Scoring::MaxScore || horde >= Scoring::MaxScore)
                    EndMatch(match, alliance == horde ? TEAM_NEUTRAL : alliance > horde ? TEAM_ALLIANCE : TEAM_HORDE);
                else if (!match.StatusTimer)
                    EndMatch(match, alliance > horde ? TEAM_ALLIANCE : horde > alliance ? TEAM_HORDE : TEAM_NEUTRAL);
                break;
            }
            case MatchStatus::Ended:
                break;
        }

        match.OrbMarkerTimer += diff;
        if (match.OrbMarkerTimer >= Timers::OrbMarker)
        {
            match.OrbMarkerTimer = 0;
            SendOrbMarkers(match);
        }
    }

    void Manager::CheckPlayers(Match& match)
    {
        std::vector<ObjectGuid> left;
        std::vector<ObjectGuid> expired;

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player)
            {
                // the forges come back with their real flags when the client sees them again
                matchPlayer.ForgesLocked = false;

                // like battlegrounds, a disconnected player keeps his place for a while
                if (matchPlayer.Offline)
                {
                    matchPlayer.OfflineTimer += Timers::PlayerCheck;
                    if (matchPlayer.OfflineTimer >= Timers::OfflineGrace)
                        expired.push_back(guid);
                }
                continue;
            }

            if (!player->IsInWorld() || player->IsBeingTeleported())
                continue;

            // a ghost may be sent to a graveyard; it is brought back by the resurrect wave
            if (player->GetMapId() != Ids::MapId)
            {
                matchPlayer.ForgesLocked = false;
                if (player->IsAlive())
                    left.push_back(guid);
                continue;
            }

            ApplyMatchState(match, player);
            UpdateRaid(match, matchPlayer, player);
            UpdateForgeUsable(match, matchPlayer, player);

            if (!matchPlayer.WorldStatesSent)
            {
                matchPlayer.WorldStatesSent = true;
                player->SendInitWorldStates(player->GetZoneId(), player->GetAreaId());

                // the client shows the battleground button and asks for the scoreboard while an active
                // battlefield status is present, so it is (re)sent once the player finished entering the world
                SendBattlefieldStatus(match, matchPlayer, player);
            }

            if (!player->IsAlive())
            {
                if (!matchPlayer.HandledDeath)
                {
                    matchPlayer.HandledDeath = true;
                    if (match.Status == MatchStatus::InProgress)
                        ++matchPlayer.Deaths;
                    if (Optional<OrbType> orb = GetCarriedOrb(match, guid))
                        DropOrb(match, *orb, true);
                }

                // the spirit guides are on the upper floor: a ghost that dropped into the hall goes back up through the portal
                if (match.Status == MatchStatus::InProgress && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    UsePortal(match, player);

                continue;
            }

            matchPlayer.HandledDeath = false;

            if (player->IsMounted() && GetCarriedOrb(match, guid))
            {
                player->RemoveAurasByType(SPELL_AURA_MOUNTED);
                player->Dismount();
            }

            if (match.Status == MatchStatus::InProgress)
                UsePortal(match, player);
        }

        for (ObjectGuid const& guid : left)
            RemovePlayer(match, guid, RemoveMode::Left);

        for (ObjectGuid const& guid : expired)
            RemovePlayer(match, guid, RemoveMode::Logout);

        if (match.Status == MatchStatus::Preparation)
            KeepInPreparationArea(match);

        CheckRaids(match);
    }

    // ----------------------------------------------------------------- raids
    // A Group may disband itself inside RemoveMember, so only its GUID is kept and it is looked up on every use

    Group* Manager::GetRaid(Match const& match, TeamId team)
    {
        return match.Raids[team].IsEmpty() ? nullptr : sGroupMgr->GetGroupByGUID(match.Raids[team]);
    }

    // like Battleground::AddOrSetPlayerToCorrectBgGroup, also brings back players that left the raid or logged in again
    void Manager::UpdateRaid(Match& match, MatchPlayer const& matchPlayer, Player* player)
    {
        Group* raid = GetRaid(match, matchPlayer.Team);
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
            match.Raids[matchPlayer.Team] = raid->GetGUID();
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

    // nobody else stays in the raid, for example someone invited by its leader
    void Manager::CheckRaids(Match& match)
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            Group* raid = GetRaid(match, TeamId(team));
            if (!raid)
                continue;

            std::vector<ObjectGuid> outsiders;
            for (Group::MemberSlot const& slot : raid->GetMemberSlots())
            {
                auto itr = match.Players.find(slot.guid);
                if (itr == match.Players.end() || itr->second.Team != team)
                    outsiders.push_back(slot.guid);
            }

            for (ObjectGuid const& guid : outsiders)
                LeaveRaid(match, guid, TeamId(team));
        }
    }

    void Manager::LeaveRaid(Match& match, ObjectGuid guid, TeamId team)
    {
        if (Group* raid = GetRaid(match, team))
            if (raid->IsMember(guid))
                raid->RemoveMember(guid);
    }

    void Manager::DisbandRaids(Match& match)
    {
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            if (Group* raid = GetRaid(match, TeamId(team)))
                raid->Disband();
            match.Raids[team].Clear();
        }
    }

    // like Battleground::_ProcessResurrect: only the players still in range of the guide they queued at are revived on the wave
    void Manager::ResurrectDead(Match& match)
    {
        for (auto const& [guid, guideGuid] : match.ResurrectQueue)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported())
                continue;

            // the guide revives only released ghosts; anyone else just leaves the queue
            if (player->IsAlive() || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            auto itr = match.Players.find(guid);
            if (itr == match.Players.end())
                continue;

            // the client hides its popup once the ghost leaves the guide, so it must not be revived either
            Creature* guide = player->GetMap()->GetCreature(guideGuid);
            if (!guide)
                guide = player->GetMap()->GetCreature(match.SpiritGuides[itr->second.Team]);
            if (!guide)
                guide = player->GetMap()->GetCreature(match.PreparationSpiritGuides[itr->second.Team]);
            if (!guide || player->GetDistance(guide) > SpiritHealerRange)
                continue;

            MatchPlayer& matchPlayer = itr->second;
            matchPlayer.HandledDeath = false;

            // same visuals as Battleground::_ProcessResurrect
            guide->CastSpell(guide, Spells::SpiritHeal, true);
            player->CastSpell(player, Spells::ResurrectionVisual, true);

            player->ResurrectPlayer(1.0f);
            player->CastSpell(player, Spells::ResurrectEffect, true);
            player->CastSpell(player, Spells::SpiritHealMana, true);
            player->SpawnCorpseBones(false);
        }

        match.ResurrectQueue.clear();
    }

    void Manager::UpdateCarriers(Match& match, uint32 diff)
    {
        for (uint8 i = 0; i < MAX_ORBS; ++i)
        {
            OrbState& state = match.Orbs[i];
            if (state.Carrier.IsEmpty())
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier);

            // the carrier aura is visible (the client relabels it) and cancelable: put it back if the player
            // right-clicked it off, so the carried state never desyncs from the client visual
            if (player)
            {
                for (uint32 spellId : OrbTemplates[i].CarrierAuras)
                    if (spellId && !player->HasAura(spellId))
                        ApplyPermanentAura(player, spellId);

                // an aura that was just recreated starts without the stack count, so restore it every tick
                SetCarrierStackCharges(player, OrbTemplates[i], state.Stacks);
            }

            state.StackTimer += diff;
            if (state.StackTimer < Timers::OrbStack)
                continue;

            state.StackTimer -= Timers::OrbStack;
            ++state.Stacks;

            if (player)
            {
                player->SetObjectScale(GetCarrierScale(state));
                SetCarrierStackCharges(player, OrbTemplates[i], state.Stacks);
            }
        }
    }

    // minimap markers are server-driven: one Lua snippet per player every OrbMarker ms, executed by the
    // client listener like the rest of the payload. Positions are world coordinates, the client converts them.
    void Manager::SendOrbMarkers(Match& match)
    {
        if (!_clientUiEnabled || _clientScript.empty())
            return;

        for (auto const& entry : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(entry.first);
            if (!player || !player->IsInWorld() || player->GetMapId() != Ids::MapId)
                continue;

            std::string text = Trinity::StringFormat("AcherusBG_Orbs.Update({:.1f},{:.1f}",
                player->GetPositionX(), player->GetPositionY());

            for (uint8 i = 0; i < MAX_ORBS; ++i)
            {
                OrbState const& state = match.Orbs[i];

                std::string carrierName;
                float x = OrbTemplates[i].ForgePosition.GetPositionX();
                float y = OrbTemplates[i].ForgePosition.GetPositionY();

                if (Player* carrier = ObjectAccessor::FindConnectedPlayer(state.Carrier))
                {
                    auto itr = match.Players.find(state.Carrier);

                    // a carrier of the observer's team is a battlefield raid member: send the name so
                    // the client can follow that unit and move the marker smoothly
                    if (itr != match.Players.end() && itr->second.Team == entry.second.Team)
                        carrierName = carrier->GetName();

                    x = carrier->GetPositionX();
                    y = carrier->GetPositionY();
                }

                text += Trinity::StringFormat(",{:.1f},{:.1f},'{}'", x, y, carrierName);
            }

            text += ")";
            SendAddonMessage(player, text);
        }
    }

    void Manager::ScoreTick(Match& match)
    {
        for (OrbState const& state : match.Orbs)
        {
            if (state.Carrier.IsEmpty())
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier);
            auto itr = match.Players.find(state.Carrier);
            if (!player || !player->IsAlive() || player->GetMapId() != Ids::MapId || itr == match.Players.end())
                continue;

            uint32& score = match.Score[itr->second.Team];
            uint32 const before = score;
            score = std::min(score + GetPointsForPosition(player), Scoring::MaxScore);
            itr->second.Points += score - before;
            if (score > before)
                player->GetSession()->SendAreaTriggerMessage("+%u points", score - before);
        }

        UpdateWorldStates(match);
    }

    uint32 Manager::GetPointsForPosition(Player const* player) const
    {
        float const dx = player->GetPositionX() - Positions::Center.GetPositionX();
        float const dy = player->GetPositionY() - Positions::Center.GetPositionY();
        float const z = player->GetPositionZ();
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

    // ----------------------------------------------------------------- match flow

    void Manager::StartPreparation(Match& match)
    {
        match.Status = MatchStatus::Preparation;
        match.StatusTimer = Timers::Preparation;

        if (Map* map = sMapMgr->CreateBaseMap(Ids::MapId))
            SpawnObjects(match, map);

        // the forges already glow during the preparation, the orbs can only be taken once the battle begins
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            SetForgeVisuals(match, OrbType(orb), true);

        Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus begins in 2 minutes.");
    }

    void Manager::StartMatch(Match& match)
    {
        match.Status = MatchStatus::InProgress;
        match.StatusTimer = Timers::MatchDuration;
        match.TickTimer = 0;
        match.ResurrectTimer = 0;

        // the starting area spirit guides leave with the preparation, their ghosts are revived where they are
        if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
        {
            for (ObjectGuid& guid : match.PreparationSpiritGuides)
                DespawnCreature(map, guid);
            DespawnPreparationArea(match, map);
        }

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsAlive() || player->GetMapId() != Ids::MapId || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            matchPlayer.HandledDeath = false;
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
            player->NearTeleportTo(Positions::Spawn[matchPlayer.Team]);
        }

        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            SetForgeVisuals(match, OrbType(orb), true);

        if (Map* map = sMapMgr->FindMap(Ids::MapId, 0))
        {
            match.Portal = SpawnPortal(match, map);
            for (std::size_t i = 0; i < match.BerserkBuffs.size(); ++i)
                SpawnBerserkBuff(match, map, i, match.BerserkBuffs[i]);
        }

        UpdateWorldStates(match);

        // refresh the battlefield status so the score frame timers switch from the preparation to the match
        for (auto& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBattlefieldStatus(match, matchPlayer, player);

        Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus has begun! Claim the orbs at the runeforges!");
        PlaySound(match, Sounds::BattleStart);
    }

    void Manager::EndMatch(Match& match, TeamId winner)
    {
        match.BattleTime = match.Status == MatchStatus::InProgress ? Timers::MatchDuration - match.StatusTimer : 0;
        match.Status = MatchStatus::Ended;
        match.StatusTimer = Timers::EndWait;
        match.Winner = winner;

        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
        {
            DropOrb(match, OrbType(orb), false);
            SetForgeVisuals(match, OrbType(orb), false);
        }

        for (auto& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->IsInWorld() && !player->IsAlive() && player->GetMapId() == Ids::MapId)
            {
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }
        }

        UpdateWorldStates(match);

        for (auto& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendEndState(match, matchPlayer, player);

        if (winner == TEAM_NEUTRAL)
            Announce(match, CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for Acherus ended in a draw.");
        else
        {
            Announce(match, TeamChatMsg(winner), Trinity::StringFormat("The {} wins the battle for Acherus!", TeamName(winner)));
            PlaySound(match, winner == TEAM_ALLIANCE ? Sounds::AllianceWins : Sounds::HordeWins);
        }
    }

    // ----------------------------------------------------------------- players

    void Manager::AddPlayer(Match& match, Player* player)
    {
        MatchPlayer& matchPlayer = match.Players[player->GetGUID()];
        matchPlayer.Guid = player->GetGUID();
        matchPlayer.Team = player->GetTeamId();
        matchPlayer.Return.WorldRelocate(player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());
        _playerMatch[player->GetGUID()] = &match;

        CharacterDatabase.PExecute("REPLACE INTO custom_acherus_orbs_return (guid, map, position_x, position_y, position_z, orientation) VALUES ({}, {}, {}, {}, {}, {})",
            player->GetGUID().GetCounter(), player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation());

        if (player->IsMounted())
        {
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
            player->Dismount();
        }

        player->CombatStop();
        player->SetPhaseMask(match.PhaseMask, false);
        player->GetSession()->SendSetPhaseShift(match.PhaseMask);

        Position const& spawn = Positions::Spawn[matchPlayer.Team];
        player->TeleportTo(Ids::MapId, spawn.GetPositionX(), spawn.GetPositionY(), spawn.GetPositionZ(), spawn.GetOrientation());

        ChatHandler(player->GetSession()).SendSysMessage(match.Status == MatchStatus::InProgress
            ? "You are joining a battle for Acherus in progress." : "Your battle for Acherus is starting.");
        if (player->IsGameMaster())
            ChatHandler(player->GetSession()).SendSysMessage("You are in GM mode and see every phase. Use .gm off to play the match.");

        ProbeClientScript(player);
    }

    void Manager::RemovePlayer(Match& match, ObjectGuid guid, RemoveMode mode)
    {
        auto itr = match.Players.find(guid);
        if (itr == match.Players.end())
            return;

        if (Optional<OrbType> orb = GetCarriedOrb(match, guid))
            DropOrb(match, *orb, true);

        // the player gets his original group back
        LeaveRaid(match, guid, itr->second.Team);

        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (player)
            ClearBattlefieldStatus(itr->second, player);

        WorldLocation const destination = itr->second.Return;
        if (match.ResurrectQueue.erase(guid) && player)
            player->RemoveAurasDueToSpell(Spells::WaitingForResurrect);
        match.Players.erase(itr);
        _playerMatch.erase(guid);

        // offline players keep the saved position, they are sent back on their next login
        if (mode != RemoveMode::Logout && player)
            CharacterDatabase.PExecute("DELETE FROM custom_acherus_orbs_return WHERE guid = {}", guid.GetCounter());

        if (!player)
            return;

        RestorePhase(player);
        player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);

        if (mode != RemoveMode::TeleportOut)
            return;

        if (!player->IsAlive())
        {
            player->ResurrectPlayer(1.0f);
            player->SpawnCorpseBones();
        }

        if (!player->TeleportTo(destination))
        {
            player->SetClientControl(player, true);
            TC_LOG_ERROR("scripts", "AcherusOrbs: could not send {} back to map {} ({}, {}, {})", player->GetName(),
                destination.GetMapId(), destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ());
        }
    }

    void Manager::ApplyMatchState(Match const& match, Player* player) const
    {
        if (!player->IsGameMaster() && player->GetPhaseMask() != match.PhaseMask)
        {
            player->SetPhaseMask(match.PhaseMask, true);
            player->GetSession()->SendSetPhaseShift(match.PhaseMask);
        }

        player->RemoveAurasDueToSpell(Spells::UndyingResolve);

        // every participant runs like the death knights of Acherus, mounts are not allowed indoors. Same area as the
        // spell_area entry; the core removes it from others when they change area, and it is given back here
        if (player->IsAlive() && player->GetAreaId() == Ids::HallAreaId && !player->HasAura(Spells::DominionOverAcherus))
            player->AddAura(Spells::DominionOverAcherus, player);

        // the whole map is a sanctuary (AreaTableEntry::IsSanctuary), also covers players that were already in Acherus when they joined
        if (IsSanctuaryDisabled(player) && player->IsInSanctuary())
        {
            player->RemovePvpFlag(UNIT_BYTE2_FLAG_SANCTUARY);
            player->pvpInfo.IsInNoPvPArea = false;
        }

        if (!player->IsPvP())
            player->UpdatePvP(true, true);
    }

    bool Manager::IsSanctuaryDisabled(Player const* player) const
    {
        Match const* match = GetMatch(player->GetGUID());
        return match && match->Status != MatchStatus::Ended && player->GetMapId() == Ids::MapId;
    }

    bool Manager::OnRepop(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match || player->GetMapId() != Ids::MapId)
            return false;

        auto itr = match->Players.find(player->GetGUID());
        if (itr == match->Players.end())
            return false;

        // during the preparation the graveyard is inside the starting area, so nobody starts outside of it
        player->NearTeleportTo(GetGraveyard(*match, itr->second.Team));
        return true;
    }

    Position const& Manager::GetGraveyard(Match const& match, TeamId team)
    {
        return match.Status == MatchStatus::Preparation ? Positions::Spawn[team] : Positions::Respawn[team];
    }

    void Manager::RestorePhase(Player* player)
    {
        // same as AuraEffect::HandlePhase
        uint32 phaseMask = 0;
        for (AuraEffect const* effect : player->GetAuraEffectsByType(SPELL_AURA_PHASE))
            phaseMask |= effect->GetMiscValue();

        if (!phaseMask)
            phaseMask = PHASEMASK_NORMAL;

        if (player->IsGameMaster())
            phaseMask = PHASEMASK_ANYWHERE;

        player->SetPhaseMask(phaseMask, true);
        player->GetSession()->SendSetPhaseShift(phaseMask);
    }

    // ----------------------------------------------------------------- orbs

    void Manager::SpawnObjects(Match& match, Map* map)
    {
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
        {
            OrbTemplate const& orbTemplate = OrbTemplates[orb];
            OrbState& state = match.Orbs[orb];

            GameObject* forge = new GameObject();
            if (!forge->Create(map->GenerateLowGuid<HighGuid::GameObject>(), orbTemplate.ForgeEntry, map, match.PhaseMask, orbTemplate.ForgePosition, orbTemplate.ForgeRotation, 255, GO_STATE_READY))
            {
                TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create forge gameobject {} for match {}", orbTemplate.ForgeEntry, match.Id);
                delete forge;
            }
            else
            {
                forge->setActive(true);
                forge->SetVisibilityDistanceOverride(ForgeVisibility);
                if (map->AddToMap(forge))
                    state.Forge = forge->GetGUID();
                else
                    delete forge;
            }

            state.Trigger = SummonForgeTrigger(match, map, orbTemplate, orbTemplate.ForgeAuraScale);
            for (std::size_t i = 0; i < orbTemplate.ForgeScaledAuras.size(); ++i)
                if (orbTemplate.ForgeScaledAuras[i].Spell)
                    state.ScaledAuraTriggers[i] = SummonForgeTrigger(match, map, orbTemplate, orbTemplate.ForgeScaledAuras[i].Scale);
        }

        // starting area spirit guides only exist during the preparation, like the ones inside the Warsong Gulch bases
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            match.SpiritGuides[team] = SummonSpiritGuide(match, map, Positions::Respawn[team], TeamId(team));
            match.PreparationSpiritGuides[team] = SummonSpiritGuide(match, map, Positions::Spawn[team], TeamId(team));
            match.PreparationDomes[team] = SummonPreparationDome(match, map, Positions::Spawn[team]);
            SpawnPreparationWalls(match, map, Positions::Spawn[team]);
        }

        SpawnStairsBarrier(match, map);
        match.StairsPortal = SummonStairsPortal(match, map);
        SpawnAmbientCreatures(match, map);
    }

    // the Risen Drudges and Vigilant Gargoyles of Acherus, as scenery: the spawns of the Acherus floating over the
    // Eastern Plaguelands (map 0, same coordinates as the hall) and of this map, copied with their stand state, emote
    // and movement
    void Manager::SpawnAmbientCreatures(Match& match, Map* map)
    {
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.id != Ids::NpcRisenDrudge && data.id != Ids::NpcVigilantGargoyle)
                continue;

            if (data.mapId != Ids::MapId && (data.mapId != 0 || data.spawnPoint.GetExactDist2d(Positions::Center) > AmbientCreatureRadius))
                continue;

            ObjectGuid const guid = SummonAmbientCreature(match, map, data);
            if (!guid.IsEmpty())
                match.AmbientCreatures.push_back(guid);
        }
    }

    // nobody can select, attack, heal or otherwise affect them, and they react to nothing
    ObjectGuid Manager::SummonAmbientCreature(Match const& match, Map* map, CreatureData const& data)
    {
        Position position = data.spawnPoint;
        for (Positions::AmbientCreatureMove const& move : Positions::AmbientCreatureMoves)
            if (move.SpawnId == data.spawnId)
                position = move.Destination;

        TempSummon* creature = map->SummonCreature(data.id, position);
        if (!creature)
            return ObjectGuid::Empty;

        creature->SetPhaseMask(match.PhaseMask, true);
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

    ObjectGuid Manager::SummonSpiritGuide(Match const& match, Map* map, Position const& graveyard, TeamId team)
    {
        Position const position(graveyard.GetPositionX() + SpiritGuideOffset * std::cos(graveyard.GetOrientation()),
            graveyard.GetPositionY() + SpiritGuideOffset * std::sin(graveyard.GetOrientation()), graveyard.GetPositionZ(), graveyard.GetOrientation());

        uint32 const entry = team == TEAM_ALLIANCE ? Ids::NpcSpiritGuideAlliance : Ids::NpcSpiritGuideHorde;
        TempSummon* guide = map->SummonCreature(entry, position);
        if (!guide)
            return ObjectGuid::Empty;

        // same setup as Battleground::AddSpiritGuide
        guide->SetPhaseMask(match.PhaseMask, true);
        guide->setActive(true);
        guide->SetChannelObjectGuid(guide->GetGUID());
        guide->SetChannelSpellId(Spells::SpiritHealChannel);
        guide->SetModCastingSpeed(1.0f);
        return guide->GetGUID();
    }

    // the Anti-Magic Zone dome is a channel kit (SpellVisual 11242): it is only drawn while a unit channels the spell, so the
    // trigger "channels" it like the spirit guides do with their visual, and the aura itself is never applied
    ObjectGuid Manager::SummonPreparationDome(Match const& match, Map* map, Position const& center)
    {
        TempSummon* dome = map->SummonCreature(Ids::NpcPreparationDome, center);
        if (!dome)
            return ObjectGuid::Empty;

        // not summoned as a totem, so it casts nothing; kept friendly and passive anyway
        dome->SetFaction(FACTION_FRIENDLY);
        dome->SetReactState(REACT_PASSIVE);

        dome->SetPhaseMask(match.PhaseMask, true);
        dome->setActive(true);
        dome->SetVisibilityDistanceOverride(ForgeVisibility);
        dome->SetObjectScale(PreparationDomeScale);
        dome->SetChannelObjectGuid(dome->GetGUID());
        dome->SetChannelSpellId(Spells::PreparationDome);
        return dome->GetGUID();
    }

    // octagon of invisible collision walls (CollisionWallPvP01, ~11 yards wide at scale 1, the model extends along its
    // local Y axis) tangent to a circle around the spawn; the client blocks movement through them
    void Manager::SpawnPreparationWalls(Match& match, Map* map, Position const& center)
    {
        for (uint8 i = 0; i < PreparationWallCount; ++i)
        {
            float const angle = float(i) * 2.0f * float(M_PI) / float(PreparationWallCount);
            Position const position(center.GetPositionX() + PreparationWallDistance * std::cos(angle),
                center.GetPositionY() + PreparationWallDistance * std::sin(angle), center.GetPositionZ(), angle);

            ObjectGuid const wall = SpawnWall(match, map, position);
            if (!wall.IsEmpty())
                match.PreparationWalls.push_back(wall);
        }
    }

    // one invisible collision wall facing the position's orientation, so it extends across it
    ObjectGuid Manager::SpawnWall(Match const& match, Map* map, Position const& position, float scale /*= 0.0f*/)
    {
        GameObject* wall = new GameObject();
        if (!wall->Create(map->GenerateLowGuid<HighGuid::GameObject>(), Ids::GoPreparationWall, map, match.PhaseMask, position,
            QuaternionData::fromEulerAnglesZYX(position.GetOrientation(), 0.0f, 0.0f), 255, GO_STATE_READY))
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create wall gameobject {} for match {}", Ids::GoPreparationWall, match.Id);
            delete wall;
            return ObjectGuid::Empty;
        }

        if (scale > 0.0f)
            wall->SetObjectScale(scale);

        wall->setActive(true);
        if (!map->AddToMap(wall))
        {
            delete wall;
            return ObjectGuid::Empty;
        }

        return wall->GetGUID();
    }

    // overlapping walls along each segment of the barrier, in a single row: they all stand on the lowest floor of the
    // barrier (the bottom of the gaps beside the stairs), so nothing passes under them and their visible base stays under
    // the floor everywhere, and are scaled to reach above the upper floor
    void Manager::SpawnStairsBarrier(Match& match, Map* map)
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

                ObjectGuid const wall = SpawnWall(match, map, position, scale);
                if (!wall.IsEmpty())
                    match.StairsBarrier.push_back(wall);
            }
        }
    }

    ObjectGuid Manager::SummonStairsPortal(Match const& match, Map* map)
    {
        TempSummon* trigger = map->SummonCreature(Ids::NpcBeamTrigger, Positions::StairsPortal);
        if (!trigger)
            return ObjectGuid::Empty;

        trigger->SetPhaseMask(match.PhaseMask, true);
        trigger->setActive(true);
        trigger->SetVisibilityDistanceOverride(ForgeVisibility);
        trigger->SetObjectScale(StairsPortalScale);
        if (Aura* aura = trigger->AddAura(Spells::StairsPortal, trigger))
        {
            aura->SetMaxDuration(-1);
            aura->SetDuration(-1);
        }

        return trigger->GetGUID();
    }

    void Manager::DespawnPreparationArea(Match& match, Map* map)
    {
        for (ObjectGuid& guid : match.PreparationDomes)
            DespawnCreature(map, guid);

        for (ObjectGuid const& guid : match.PreparationWalls)
        {
            if (GameObject* wall = map->GetGameObject(guid))
            {
                wall->SetRespawnTime(0);
                wall->Delete();
            }
        }
        match.PreparationWalls.clear();
    }

    // backup of the walls, checked with the other player checks (every second) while the gates are closed
    void Manager::KeepInPreparationArea(Match& match)
    {
        for (auto const& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->IsBeingTeleported() || player->GetMapId() != Ids::MapId || !player->IsAlive())
                continue;

            Position const& spawn = Positions::Spawn[matchPlayer.Team];
            if (player->GetExactDist2d(&spawn) > PreparationLeash)
                player->NearTeleportTo(spawn);
        }
    }

    void Manager::DespawnCreature(Map* map, ObjectGuid& guid)
    {
        if (Creature* creature = map->GetCreature(guid))
            creature->DespawnOrUnsummon();
        guid.Clear();
    }

    bool Manager::OnSpiritHealerQuery(Player* player, Creature* spiritHealer)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return false;

        auto itr = match->Players.find(player->GetGUID());
        if (itr == match->Players.end())
            return false;

        TeamId const team = itr->second.Team;
        if (match->SpiritGuides[team] != spiritHealer->GetGUID() && match->PreparationSpiritGuides[team] != spiritHealer->GetGUID())
            return false;

        // native timer, so the client shows its AREA_SPIRIT_HEAL popup (GetAreaSpiritHealerTime) while a ghost
        WorldPackets::Battleground::AreaSpiritHealerTime time;
        time.HealerGuid = spiritHealer->GetGUID();
        time.TimeLeft = int32(match->ResurrectTimer < Timers::ResurrectWave ? Timers::ResurrectWave - match->ResurrectTimer : 0);
        player->SendDirectMessage(time.Write());
        return true;
    }

    // walking into a spirit guide's range queues the player for the next wave, like a battleground
    void Manager::OnSpiritHealerQueue(Player* player, Creature* spiritHealer, bool& handled)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        auto itr = match->Players.find(player->GetGUID());
        if (itr == match->Players.end())
            return;

        TeamId const team = itr->second.Team;
        if (match->SpiritGuides[team] != spiritHealer->GetGUID() && match->PreparationSpiritGuides[team] != spiritHealer->GetGUID())
            return;

        handled = true;

        // same visual as Battleground::AddPlayerToResurrectQueue: refreshed on every queue, so leaving and
        // re-entering the guide's range brings the aura back without waiting for the next wave
        match->ResurrectQueue.insert_or_assign(player->GetGUID(), spiritHealer->GetGUID());
        player->CastSpell(player, Spells::WaitingForResurrect, true);
    }

    void Manager::DespawnObjects(Match& match, Map* map)
    {
        for (OrbState& state : match.Orbs)
        {
            if (GameObject* forge = map->GetGameObject(state.Forge))
            {
                forge->SetRespawnTime(0);
                forge->Delete();
            }

            for (ObjectGuid& guid : state.Objects)
            {
                if (GameObject* object = map->GetGameObject(guid))
                    object->Delete();
                guid.Clear();
            }

            DespawnCreature(map, state.Trigger);
            for (ObjectGuid& guid : state.ScaledAuraTriggers)
                DespawnCreature(map, guid);
            state.Forge.Clear();
        }

        if (GameObject* portal = map->GetGameObject(match.Portal))
            portal->Delete();
        match.Portal.Clear();

        for (BerserkBuffState& buff : match.BerserkBuffs)
        {
            if (GameObject* object = map->GetGameObject(buff.Guid))
                object->Delete();
            buff = BerserkBuffState();
        }

        for (ObjectGuid const& guid : match.StairsBarrier)
        {
            if (GameObject* wall = map->GetGameObject(guid))
            {
                wall->SetRespawnTime(0);
                wall->Delete();
            }
        }
        match.StairsBarrier.clear();
        DespawnCreature(map, match.StairsPortal);

        for (ObjectGuid& guid : match.SpiritGuides)
            DespawnCreature(map, guid);

        for (ObjectGuid& guid : match.AmbientCreatures)
            DespawnCreature(map, guid);
        match.AmbientCreatures.clear();

        for (ObjectGuid& guid : match.PreparationSpiritGuides)
            DespawnCreature(map, guid);

        DespawnPreparationArea(match, map);
    }

    // the portal only shows where to step: like the original one, the teleport comes from standing on it (UsePortal)
    ObjectGuid Manager::SpawnPortal(Match const& match, Map* map)
    {
        GameObject* portal = new GameObject();
        if (!portal->Create(map->GenerateLowGuid<HighGuid::GameObject>(), Ids::GoPortal, map, match.PhaseMask, Positions::Portal,
            Positions::PortalRotation, 255, GO_STATE_ACTIVE))
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create portal gameobject {} for match {}", Ids::GoPortal, match.Id);
            delete portal;
            return ObjectGuid::Empty;
        }

        portal->setActive(true);
        portal->SetVisibilityDistanceOverride(ForgeVisibility);
        if (!map->AddToMap(portal))
        {
            delete portal;
            return ObjectGuid::Empty;
        }

        return portal->GetGUID();
    }

    void Manager::SpawnBerserkBuff(Match const& match, Map* map, std::size_t index, BerserkBuffState& buff)
    {
        buff.Guid.Clear();
        buff.Armed = false;

        Position const& position = Positions::BerserkBuffs[index];
        GameObject* object = new GameObject();
        if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), Ids::GoBerserkBuff, map, match.PhaseMask, position,
            QuaternionData::fromEulerAnglesZYX(position.GetOrientation(), 0.0f, 0.0f), 255, GO_STATE_READY))
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create berserk buff gameobject {} for match {}", Ids::GoBerserkBuff, match.Id);
            delete object;
            buff.RespawnTimer = Timers::BuffRespawn;
            return;
        }

        object->setActive(true);
        object->SetVisibilityDistanceOverride(ForgeVisibility);
        if (!map->AddToMap(object))
        {
            delete object;
            buff.RespawnTimer = Timers::BuffRespawn;
            return;
        }

        buff.Guid = object->GetGUID();
    }

    // battleground buffs are despawned and respawned by Battleground::HandleTriggerBuff. These are traps of type 1:
    // each casts its spell, then goes GO_JUST_DEACTIVATED and GO_NOT_READY, and would arm itself again after its cooldown.
    // A new trap also starts GO_NOT_READY, so it only counts as used after it was seen ready.
    void Manager::UpdateBerserkBuffs(Match& match, uint32 diff)
    {
        Map* map = sMapMgr->FindMap(Ids::MapId, 0);
        if (!map)
            return;

        for (std::size_t i = 0; i < match.BerserkBuffs.size(); ++i)
        {
            BerserkBuffState& buff = match.BerserkBuffs[i];
            if (buff.Guid.IsEmpty())
            {
                if (buff.RespawnTimer > diff)
                {
                    buff.RespawnTimer -= diff;
                    continue;
                }

                buff.RespawnTimer = 0;
                SpawnBerserkBuff(match, map, i, buff);
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

    // same rule as the Acherus teleporter (aura 54724 of NPC 29581): within 3 yards, checked every second
    void Manager::UsePortal(Match const& match, Player* player)
    {
        if (match.Portal.IsEmpty() || player->GetExactDist(&Positions::Portal) > PortalRange)
            return;

        player->NearTeleportTo(Positions::PortalDestination);
    }

    void Manager::SetForgeVisuals(Match& match, OrbType orb, bool on)
    {
        Map* map = sMapMgr->FindMap(Ids::MapId, 0);
        if (!map)
            return;

        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];

        for (std::size_t i = 0; i < orbTemplate.ForgeObjects.size(); ++i)
        {
            ForgeObjectTemplate const& objectTemplate = orbTemplate.ForgeObjects[i];
            if (!objectTemplate.Entry)
                continue;

            ObjectGuid& guid = state.Objects[i];
            if (!on)
            {
                if (GameObject* object = map->GetGameObject(guid))
                    object->Delete();
                guid.Clear();
            }
            else if (!map->GetGameObject(guid))
                guid = SpawnForgeObject(match, map, orbTemplate, objectTemplate);
        }

        auto setAura = [on](Creature* trigger, uint32 spellId)
        {
            if (!trigger || !spellId)
                return;

            if (!on)
                trigger->RemoveAurasDueToSpell(spellId);
            else if (!trigger->HasAura(spellId))
                if (Aura* aura = trigger->AddAura(spellId, trigger))
                {
                    aura->SetMaxDuration(-1);
                    aura->SetDuration(-1);
                }
        };

        Creature* trigger = map->GetCreature(state.Trigger);
        for (uint32 spellId : orbTemplate.ForgeAuras)
            setAura(trigger, spellId);

        for (std::size_t i = 0; i < orbTemplate.ForgeScaledAuras.size(); ++i)
            setAura(map->GetCreature(state.ScaledAuraTriggers[i]), orbTemplate.ForgeScaledAuras[i].Spell);
    }

    // invisible bunny on the forge carrying its auras, facing the pit since the auras are drawn relative to its facing
    ObjectGuid Manager::SummonForgeTrigger(Match const& match, Map* map, OrbTemplate const& orbTemplate, float scale)
    {
        Position position = orbTemplate.ForgePosition;
        position.SetOrientation(position.GetAbsoluteAngle(Positions::Center));

        TempSummon* trigger = map->SummonCreature(Ids::NpcBeamTrigger, position);
        if (!trigger)
            return ObjectGuid::Empty;

        trigger->SetPhaseMask(match.PhaseMask, true);
        trigger->setActive(true);
        trigger->SetVisibilityDistanceOverride(ForgeVisibility);
        trigger->SetObjectScale(scale);
        return trigger->GetGUID();
    }

    ObjectGuid Manager::SpawnForgeObject(Match const& match, Map* map, OrbTemplate const& orbTemplate, ForgeObjectTemplate const& objectTemplate)
    {
        GameObject* object = new GameObject();
        if (!object->Create(map->GenerateLowGuid<HighGuid::GameObject>(), objectTemplate.Entry, map, match.PhaseMask, orbTemplate.ForgePosition,
            QuaternionData::fromEulerAnglesZYX(orbTemplate.ForgePosition.GetOrientation(), 0.0f, 0.0f), 255, GO_STATE_READY))
        {
            TC_LOG_ERROR("scripts", "AcherusOrbs: cannot create forge gameobject {} for match {}", objectTemplate.Entry, match.Id);
            delete object;
            return ObjectGuid::Empty;
        }

        if (objectTemplate.Scale > 0.0f)
            object->SetObjectScale(objectTemplate.Scale);

        // scenery only: no mouseover highlight
        object->SetFlag(GO_FLAG_NOT_SELECTABLE);

        object->setActive(true);
        object->SetVisibilityDistanceOverride(ForgeVisibility);
        if (!map->AddToMap(object))
        {
            delete object;
            return ObjectGuid::Empty;
        }

        return object->GetGUID();
    }

    void Manager::OnForgeUse(Player* player, GameObject* forge)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        Optional<OrbType> orb;
        for (uint8 i = 0; i < MAX_ORBS; ++i)
            if (match->Orbs[i].Forge == forge->GetGUID())
                orb = OrbType(i);

        if (!orb)
            return;

        if (match->Status != MatchStatus::InProgress)
            SendUseError(player, "The orbs are not active yet.");
        else if (!player->IsAlive())
            return;
        else if (IsAboveForges(player))
            SendUseError(player, "You are too far away.");
        else if (!match->Orbs[*orb].Carrier.IsEmpty())
            SendUseError(player, "This orb is already taken.");
        else if (GetCarriedOrb(*match, player->GetGUID()))
            SendUseError(player, "You can only carry one orb.");
        else
            PickUpOrb(*match, *orb, player);
    }

    // the forge use range reaches the upper floor through the ceiling of the hall
    bool Manager::IsAboveForges(Player const* player)
    {
        return player->GetPositionZ() - OrbTemplates[0].ForgePosition.GetPositionZ() > ForgeMaxHeight;
    }

    // the client lights the use cursor on its own range check: from above, this player gets the forges as not
    // usable. Only this client is told, through a values update of GAMEOBJECT_FLAGS built here
    void Manager::UpdateForgeUsable(Match const& match, MatchPlayer& matchPlayer, Player* player)
    {
        bool const locked = IsAboveForges(player);
        if (locked == matchPlayer.ForgesLocked)
            return;

        Map* map = player->GetMap();
        UpdateData data;
        for (OrbState const& state : match.Orbs)
        {
            GameObject* forge = map->GetGameObject(state.Forge);
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

    // red text on the screen like the client's own errors when the UI listener is installed, else a notification
    void Manager::SendUseError(Player* player, std::string const& text) const
    {
        if (_clientUiEnabled && !_clientScript.empty())
            SendAddonMessage(player, Trinity::StringFormat("UIErrorsFrame:AddMessage('{}',1,0.1,0.1,1)", text));
        else
            player->GetSession()->SendNotification("%s", text.c_str());
    }

    void Manager::PickUpOrb(Match& match, OrbType orb, Player* player)
    {
        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];

        state.Carrier = player->GetGUID();
        state.Stacks = 1;
        state.StackTimer = 0;
        state.CarrierOriginalScale = player->GetObjectScale();

        player->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
        player->RemoveAurasByType(SPELL_AURA_MOD_INVISIBILITY);
        if (player->IsMounted())
        {
            player->RemoveAurasByType(SPELL_AURA_MOUNTED);
            player->Dismount();
        }

        player->SetObjectScale(GetCarrierScale(state));
        player->SendPlaySpellVisualKit(orbTemplate.CarrierVisualKit, 0);
        for (uint32 spellId : orbTemplate.CarrierAuras)
            if (Aura* aura = ApplyPermanentAura(player, spellId))
                if (IsCarrierStackAura(spellId))
                    aura->SetCharges(uint8(std::min<uint32>(state.Stacks, 255)));

        SetForgeVisuals(match, orb, false);
        UpdateWorldStates(match);

        Announce(match, CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("{} has taken the |c{}{}|r orb!", player->GetName(), orbTemplate.Color, orbTemplate.Name));
        PlaySound(match, Sounds::OrbEvent);
    }

    void Manager::DropOrb(Match& match, OrbType orb, bool announce)
    {
        OrbTemplate const& orbTemplate = OrbTemplates[orb];
        OrbState& state = match.Orbs[orb];
        if (state.Carrier.IsEmpty())
            return;

        if (Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier))
        {
            for (uint32 spellId : orbTemplate.CarrierAuras)
                if (spellId)
                    player->RemoveAurasDueToSpell(spellId);
            player->SetObjectScale(state.CarrierOriginalScale);
        }

        state.Carrier.Clear();
        state.Stacks = 0;
        state.StackTimer = 0;

        if (match.Status == MatchStatus::InProgress)
            SetForgeVisuals(match, orb, true);

        UpdateWorldStates(match);

        if (announce)
        {
            Announce(match, CHAT_MSG_RAID_BOSS_EMOTE, Trinity::StringFormat("The |c{}{}|r orb has returned to its runeforge!", orbTemplate.Color, orbTemplate.Name));
            PlaySound(match, Sounds::OrbEvent);
        }
    }

    Optional<OrbType> Manager::GetCarriedOrb(Match const& match, ObjectGuid guid) const
    {
        for (uint8 orb = 0; orb < MAX_ORBS; ++orb)
            if (match.Orbs[orb].Carrier == guid)
                return OrbType(orb);
        return {};
    }

    OrbState const* Manager::GetCarriedOrbState(ObjectGuid guid) const
    {
        Match const* match = GetMatch(guid);
        if (!match || match->Status != MatchStatus::InProgress)
            return nullptr;

        for (OrbState const& state : match->Orbs)
            if (state.Carrier == guid)
                return &state;
        return nullptr;
    }

    uint32 Manager::GetHeldOrbCount(Match const& match, TeamId team) const
    {
        uint32 count = 0;
        for (OrbState const& state : match.Orbs)
        {
            if (state.Carrier.IsEmpty())
                continue;

            auto itr = match.Players.find(state.Carrier);
            if (itr != match.Players.end() && itr->second.Team == team)
                ++count;
        }
        return count;
    }

    // ----------------------------------------------------------------- hooks

    void Manager::OnPvPKill(Player* killer, Player* killed)
    {
        Match* match = GetMatch(killed->GetGUID());
        if (!match || match != GetMatch(killer->GetGUID()) || match->Status != MatchStatus::InProgress)
            return;

        MatchPlayer& killerData = match->Players[killer->GetGUID()];
        MatchPlayer const& killedData = match->Players[killed->GetGUID()];
        if (killerData.Team == killedData.Team)
            return;

        ++killerData.KillingBlows;

        for (auto& [guid, matchPlayer] : match->Players)
        {
            if (matchPlayer.Team != killerData.Team)
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->IsAlive() && player->GetMapId() == Ids::MapId && player->IsWithinDistInMap(killed, HonorableKillRange))
                ++matchPlayer.HonorableKills;
        }

        // any enemy player, carrying an orb or not; the victory is checked in the world update
        if (_killBonus)
        {
            uint32& score = match->Score[killerData.Team];
            uint32 const before = score;
            score = std::min(score + _killBonus, Scoring::MaxScore);
            killerData.Points += score - before;
            if (score > before)
                killer->GetSession()->SendAreaTriggerMessage("+%u points", score - before);
            UpdateWorldStates(*match);
        }
    }

    void Manager::OnUpdateZone(Player* player) const
    {
        if (Match const* match = GetMatch(player->GetGUID()))
            if (player->GetMapId() == Ids::MapId)
                ApplyMatchState(*match, player);
    }

    void Manager::OnLeaveRequest(Player* player)
    {
        if (!IsInMatch(player->GetGUID()))
            return;

        std::lock_guard<std::mutex> lock(_queueLock);
        _pendingLeaves.push_back(player->GetGUID());
    }

    // the player joined a real battleground or arena queue: the two queues are mutually exclusive, so the
    // Acherus queue is dropped (Dequeue also clears the fake status/slot and turns the relabel off)
    void Manager::OnJoinRealBattlegroundQueue(Player* player)
    {
        if (Dequeue(player->GetGUID()))
            ChatHandler(player->GetSession()).SendSysMessage("You left the queue for the battle for Acherus to join a battleground.");
    }

    // called before the player is saved, so the orb auras are never stored
    void Manager::OnBeforeLogout(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match)
            return;

        if (match->Status == MatchStatus::Ended)
        {
            RemovePlayer(*match, player->GetGUID(), RemoveMode::Logout);
            return;
        }

        if (Optional<OrbType> orb = GetCarriedOrb(*match, player->GetGUID()))
            DropOrb(*match, *orb, true);

        MatchPlayer& matchPlayer = match->Players[player->GetGUID()];
        matchPlayer.Offline = true;
        matchPlayer.OfflineTimer = 0;
    }

    void Manager::OnLogout(Player* player)
    {
        Dequeue(player->GetGUID());

        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _clientBootstrapTimeouts.erase(player->GetGUID());
            _pendingBootstraps.erase(player->GetGUID());
            _pendingPayloads.erase(player->GetGUID());
            _bootstrapListenerPending.erase(player->GetGUID());
        }
    }

    void Manager::OnLogin(Player* player)
    {
        // fresh session: install the listener through Warden; its response pushes the payload
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            _clientBootstrapCooldowns.erase(player->GetGUID());
        }
        SendBootstrap(player);

        // the orb is never kept across a login, its aura may still have been saved by a crash
        for (OrbTemplate const& orbTemplate : OrbTemplates)
            for (uint32 spellId : orbTemplate.CarrierAuras)
                if (spellId)
                    player->RemoveAurasDueToSpell(spellId);

        // back within MAX_OFFLINE_TIME, the player is still in the match where he logged out
        if (Match* match = GetMatch(player->GetGUID()))
        {
            MatchPlayer& matchPlayer = match->Players[player->GetGUID()];
            matchPlayer.Offline = false;
            matchPlayer.OfflineTimer = 0;
            matchPlayer.WorldStatesSent = false;
            if (player->GetMapId() == Ids::MapId)
                ApplyMatchState(*match, player);

            // logged out before the end, back while the final score is shown
            if (match->Status == MatchStatus::Ended)
                SendEndState(*match, matchPlayer, player);
            return;
        }

        QueryResult result = CharacterDatabase.PQuery("SELECT map, position_x, position_y, position_z, orientation FROM custom_acherus_orbs_return WHERE guid = {}", player->GetGUID().GetCounter());
        if (!result)
            return;

        // a far teleport sent while the client is still loading the world can be lost, it is done from the world update and confirmed there
        Field* fields = result->Fetch();
        PendingReturn& pending = _pendingReturns[player->GetGUID()];
        pending.Destination.WorldRelocate(fields[0].GetUInt16(), fields[1].GetFloat(), fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat());
        pending.Timer = 0;
        pending.Attempts = 0;
    }

    void Manager::ProcessPendingReturns(uint32 diff)
    {
        for (auto itr = _pendingReturns.begin(); itr != _pendingReturns.end();)
        {
            ObjectGuid const guid = itr->first;
            PendingReturn& pending = itr->second;

            // logged out again: the saved position is kept for the next login
            // a new match saved its own position, which replaced this one
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || IsInMatch(guid))
            {
                itr = _pendingReturns.erase(itr);
                continue;
            }

            if (!player->IsInWorld() || player->IsBeingTeleported())
            {
                ++itr;
                continue;
            }

            if (player->GetMapId() == pending.Destination.GetMapId() && player->GetExactDist(&pending.Destination) < 10.0f)
            {
                CharacterDatabase.PExecute("DELETE FROM custom_acherus_orbs_return WHERE guid = {}", guid.GetCounter());
                itr = _pendingReturns.erase(itr);
                continue;
            }

            pending.Timer += diff;
            if (pending.Timer < Timers::ReturnRetry)
            {
                ++itr;
                continue;
            }

            pending.Timer = 0;
            if (pending.Attempts >= Timers::ReturnMaxAttempts)
            {
                TC_LOG_ERROR("scripts", "AcherusOrbs: could not send {} back to map {} ({}, {}, {}), kept for the next login", player->GetName(),
                    pending.Destination.GetMapId(), pending.Destination.GetPositionX(), pending.Destination.GetPositionY(), pending.Destination.GetPositionZ());
                itr = _pendingReturns.erase(itr);
                continue;
            }

            ++pending.Attempts;
            if (!player->IsAlive())
            {
                player->ResurrectPlayer(1.0f);
                player->SpawnCorpseBones();
            }

            RestorePhase(player);
            player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);
            if (!player->TeleportTo(pending.Destination))
                TC_LOG_ERROR("scripts", "AcherusOrbs: teleport of {} back to map {} refused (attempt {})", player->GetName(), pending.Destination.GetMapId(), pending.Attempts);
            ++itr;
        }
    }

    void Manager::FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match || player->GetMapId() != Ids::MapId)
            return;

        packet.MapID = WorldStates::FakeMapId;
        packet.AreaID = WorldStates::FakeZoneId;
        packet.SubareaID = WorldStates::FakeZoneId;

        packet.Worldstates.emplace_back(WorldStates::AllianceTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::HordeTopStats, 1);
        packet.Worldstates.emplace_back(WorldStates::AllianceScore, match->Score[TEAM_ALLIANCE]);
        packet.Worldstates.emplace_back(WorldStates::HordeScore, match->Score[TEAM_HORDE]);
        packet.Worldstates.emplace_back(WorldStates::AllianceBases, GetHeldOrbCount(*match, TEAM_ALLIANCE));
        packet.Worldstates.emplace_back(WorldStates::HordeBases, GetHeldOrbCount(*match, TEAM_HORDE));
        packet.Worldstates.emplace_back(2565, 142);                 // sent by Eye of the Storm, purpose unknown
        packet.Worldstates.emplace_back(3085, 379);

        match->Players[player->GetGUID()].WorldStatesSent = true;
    }

    // the client asks for the scoreboard with MSG_PVP_LOG_DATA; outside of a real battleground nothing answers it
    void Manager::OnPVPLogDataRequest(Player* player)
    {
        Match* match = GetMatch(player->GetGUID());
        if (!match || player->GetMapId() != Ids::MapId)
            return;

        if (!match->Players.contains(player->GetGUID()))
            return;

        SendScoreboard(*match, player);
    }

    // the client asks for the state of its battleground queues (CMSG_BATTLEFIELD_STATUS); outside of a real
    // battleground nothing answers it, so the minimap button of the mode is maintained here
    void Manager::OnRequestBattlefieldStatus(Player* player)
    {
        // the client asks for the status when the UI loads or reloads, so probe the listener there too
        ProbeClientScript(player);

        if (Match* match = GetMatch(player->GetGUID()))
        {
            if (player->GetMapId() != Ids::MapId)
                return;

            auto itr = match->Players.find(player->GetGUID());
            if (itr == match->Players.end())
                return;

            SendBattlefieldStatus(*match, itr->second, player);
            return;
        }

        if (IsQueued(player->GetGUID()))
            SendQueueStatus(player);
    }

    // CMSG_BATTLEFIELD_PORT, the "Leave Queue" button of the PvP frame
    void Manager::OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite, bool& handled)
    {
        // entering a match is done by the module itself, there is never an invitation to accept
        if (acceptedInvite)
            return;

        if (!IsQueued(player->GetGUID()))
            return;

        BattlegroundQueueTypeId const queueTypeId = BattlegroundQueueTypeId::FromPacked(queueID);
        if (queueTypeId.BattlemasterListId != WorldStates::FakeBattlemasterListId)
            return;

        // a real queue with the same id owns the slot, let the core handle the leave
        if (player->GetBattlegroundQueueIndex(queueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            return;

        if (Dequeue(player->GetGUID()))
            ChatHandler(player->GetSession()).SendSysMessage("You left the queue for the battle for Acherus.");

        handled = true;
    }

    void Manager::ModifyDamage(Unit* attacker, Unit* victim, uint32& damage) const
    {
        if (!damage || victim->GetMapId() != Ids::MapId)
            return;

        float multiplier = 1.0f;

        if (Player* player = attacker ? attacker->ToPlayer() : nullptr)
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                multiplier *= 1.0f + std::min(OrbPower::DamageDonePct * state->Stacks / 100.0f, OrbPower::DamageDoneMaxPct / 100.0f);

        if (Player* player = victim->ToPlayer())
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                multiplier *= 1.0f + std::min(OrbPower::DamageTakenPct * state->Stacks / 100.0f, OrbPower::DamageTakenMaxPct / 100.0f);

        if (multiplier != 1.0f)
            damage = uint32(damage * multiplier);
    }

    void Manager::ModifyHealing(Unit* healer, Unit* receiver, uint32& gain)
    {
        if (!gain || receiver->GetMapId() != Ids::MapId)
            return;

        if (Player* player = receiver->ToPlayer())
            if (OrbState const* state = GetCarriedOrbState(player->GetGUID()))
                gain = uint32(gain * (1.0f + std::max(OrbPower::HealingTakenMaxPct / 100.0f, OrbPower::HealingTakenPct * state->Stacks / 100.0f)));

        Player* player = healer ? healer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player)
            return;

        if (Match* match = GetMatch(player->GetGUID()))
            if (match->Status == MatchStatus::InProgress)
                match->Players[player->GetGUID()].HealingDone += gain;
    }

    void Manager::TrackDamage(Unit* attacker, Unit* victim, uint32 damage)
    {
        if (!damage || !attacker || victim->GetMapId() != Ids::MapId || !victim->ToPlayer())
            return;

        Player* player = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!player || player == victim)
            return;

        Match* match = GetMatch(player->GetGUID());
        if (match && match->Status == MatchStatus::InProgress && match == GetMatch(victim->GetGUID()))
            match->Players[player->GetGUID()].DamageDone += damage;
    }

    // ----------------------------------------------------------------- client updates

    void Manager::UpdateWorldStates(Match& match)
    {
        uint32 const allianceOrbs = GetHeldOrbCount(match, TEAM_ALLIANCE);
        uint32 const hordeOrbs = GetHeldOrbCount(match, TEAM_HORDE);

        for (auto const& [guid, matchPlayer] : match.Players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !player->IsInWorld() || player->GetMapId() != Ids::MapId)
                continue;

            player->SendUpdateWorldState(WorldStates::AllianceScore, match.Score[TEAM_ALLIANCE]);
            player->SendUpdateWorldState(WorldStates::HordeScore, match.Score[TEAM_HORDE]);
            player->SendUpdateWorldState(WorldStates::AllianceBases, allianceOrbs);
            player->SendUpdateWorldState(WorldStates::HordeBases, hordeOrbs);
        }
    }

    // like Battleground::EndBattleground: final score, battlefield status for its timers, and the player can no longer move
    void Manager::SendEndState(Match& match, MatchPlayer& matchPlayer, Player* player)
    {
        if (!player->IsInWorld() || player->GetMapId() != Ids::MapId)
            return;

        SendScoreboard(match, player);
        SendBattlefieldStatus(match, matchPlayer, player);

        // the client gives the control back by itself on the teleport out
        player->SetClientControl(player, false);
    }

    void Manager::SendScoreboard(Match& match, Player* target /*= nullptr*/)
    {
        WorldPackets::Battleground::PVPMatchStatistics statistics;
        if (match.Winner)
            statistics.Winner = *match.Winner == TEAM_ALLIANCE ? PVP_TEAM_ALLIANCE : *match.Winner == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_NEUTRAL;

        for (auto const& [guid, matchPlayer] : match.Players)
        {
            WorldPackets::Battleground::PVPLogData_Player& data = statistics.Players.emplace_back();
            data.PlayerGUID = guid;
            data.Kills = matchPlayer.KillingBlows;
            data.HonorOrFaction = WorldPackets::Battleground::PVPLogData_Honor{ matchPlayer.HonorableKills, matchPlayer.Deaths, 0 };
            data.DamageDone = matchPlayer.DamageDone;
            data.HealingDone = matchPlayer.HealingDone;
            data.Stats = { matchPlayer.Points };                    // Eye of the Storm column (Flag Captures), shown because of the battlefield status
        }

        WorldPacket const* packet = statistics.Write();
        if (target)
        {
            target->SendDirectMessage(packet);
            return;
        }

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(packet);
    }

    // the score frame takes "Time Elapsed" and "Battleground closing in" from an active battlefield status, as sent by
    // Battleground::EndBattleground. It goes in a queue slot the player is not using for a real battleground.
    void Manager::SendBattlefieldStatus(Match const& match, MatchPlayer& matchPlayer, Player* player) const
    {
        if (!matchPlayer.StatusSlot)
        {
            for (uint32 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
            {
                if (player->GetBattlegroundQueueTypeId(slot) == BATTLEGROUND_QUEUE_NONE)
                {
                    matchPlayer.StatusSlot = slot;
                    break;
                }
            }

            if (!matchPlayer.StatusSlot)
                return;
        }

        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());

        WorldPackets::Battleground::BattlefieldStatusActive status;
        status.Hdr.QueueSlot = *matchPlayer.StatusSlot;
        status.Hdr.QueueID = GetFakeQueueTypeId(player).GetPacked();
        status.Hdr.RangeMin = uint8(bracket ? bracket->MinLevel : player->GetLevel());
        status.Hdr.RangeMax = uint8(bracket ? bracket->MaxLevel : player->GetLevel());
        status.Hdr.InstanceID = match.Id;
        status.Mapid = WorldStates::FakeMapId;
        status.ShutdownTimer = match.StatusTimer;
        status.StartTimer = match.Status == MatchStatus::InProgress ? Timers::MatchDuration - match.StatusTimer : match.BattleTime;
        status.ArenaFaction = matchPlayer.Team == TEAM_HORDE ? PVP_TEAM_HORDE : PVP_TEAM_ALLIANCE;
        player->SendDirectMessage(status.Write());
        SetClientRelabel(player, true);
    }

    void Manager::ClearBattlefieldStatus(MatchPlayer& matchPlayer, Player* player)
    {
        if (!matchPlayer.StatusSlot)
            return;

        WorldPackets::Battleground::BattlefieldStatusNone status;
        status.QueueSlot = *matchPlayer.StatusSlot;
        player->SendDirectMessage(status.Write());
        matchPlayer.StatusSlot.reset();
        sAcherusOrbs->SetClientRelabel(player, false);
    }

    // fake queue id of the mode: Eye of the Storm's battleground list id, so the client shows its frames
    BattlegroundQueueTypeId Manager::GetFakeQueueTypeId(Player const* player)
    {
        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());
        return BattlegroundQueueTypeId{
            .BattlemasterListId = WorldStates::FakeBattlemasterListId,
            .BracketId = uint8(bracket ? bracket->GetBracketId() : 0),
            .TeamSize = 0
        };
    }

    // queued players get a battlefield status too, so the minimap button appears while waiting in the queue
    void Manager::SendQueueStatus(Player* player)
    {
        uint32 slot = 0;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            auto itr = _queueStatusSlots.find(player->GetGUID());
            if (itr != _queueStatusSlots.end())
                slot = itr->second;
            else
            {
                for (; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
                    if (player->GetBattlegroundQueueTypeId(slot) == BATTLEGROUND_QUEUE_NONE)
                        break;

                if (slot >= PLAYER_MAX_BATTLEGROUND_QUEUES)
                    return;

                _queueStatusSlots[player->GetGUID()] = slot;
            }
        }

        PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(WorldStates::FakeMapId, player->GetLevel());

        WorldPackets::Battleground::BattlefieldStatusQueued status;
        status.Hdr.QueueSlot = slot;
        status.Hdr.QueueID = GetFakeQueueTypeId(player).GetPacked();
        status.Hdr.RangeMin = uint8(bracket ? bracket->MinLevel : player->GetLevel());
        status.Hdr.RangeMax = uint8(bracket ? bracket->MaxLevel : player->GetLevel());
        status.AverageWaitTime = 0;
        status.WaitTime = 0;
        player->SendDirectMessage(status.Write());
        SetClientRelabel(player, true);
    }

    void Manager::ClearQueueStatus(ObjectGuid guid, Player* player)
    {
        uint32 slot = 0;
        {
            std::lock_guard<std::mutex> lock(_queueLock);
            auto itr = _queueStatusSlots.find(guid);
            if (itr == _queueStatusSlots.end())
                return;

            slot = itr->second;
            _queueStatusSlots.erase(itr);
        }

        if (!player)
            return;

        WorldPackets::Battleground::BattlefieldStatusNone status;
        status.QueueSlot = slot;
        player->SendDirectMessage(status.Write());
        SetClientRelabel(player, false);
    }

    void Manager::Announce(Match& match, ChatMsg type, std::string const& text)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(type, LANG_UNIVERSAL, nullptr, nullptr, text);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }

    // like Battleground::PlaySoundToAll
    void Manager::PlaySound(Match& match, uint32 soundId)
    {
        WorldPackets::Misc::PlaySound packet(soundId);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : match.Players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }

    Match* Manager::GetMatch(ObjectGuid guid) const
    {
        auto itr = _playerMatch.find(guid);
        return itr != _playerMatch.end() ? itr->second : nullptr;
    }
}
