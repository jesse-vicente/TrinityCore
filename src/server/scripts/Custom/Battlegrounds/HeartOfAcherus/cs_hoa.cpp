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

#include "HoAMgr.h"
#include "HoABattlegroundUI.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include <string_view>

using namespace Trinity::ChatCommands;

class heart_of_acherus_commandscript : public CommandScript
{
public:
    heart_of_acherus_commandscript() : CommandScript("heart_of_acherus_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable acherusCommandTable =
        {
            // the PvP frame "Join" sends "join" (every player, with the normal join permission); "queue" is the GM toggle
            { "join",   HandleJoinCommand,   rbac::RBAC_PERM_JOIN_NORMAL_BG, Console::No },
            { "queue",  HandleQueueCommand,  rbac::RBAC_PERM_COMMAND_DEBUG, Console::No },
            { "start",  HandleStartCommand,  rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "begin",  HandleBeginCommand,  rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "stop",   HandleStopCommand,   rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "status", HandleStatusCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "acherus", acherusCommandTable },
        };
        return commandTable;
    }

    // the PvP frame "Join" sends it; it always queues the player himself (never toggles, never the selected target).
    // With "group" it queues the whole party/raid, which then enters the same match. Errors go to the client's error
    // frame as a BattlefieldStatusFailed (never to chat); a success is silent (the queue UI shows it)
    static bool HandleJoinCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;

        std::string_view mode(args ? args : "");
        while (!mode.empty() && mode.front() == ' ')
            mode.remove_prefix(1);

        GroupJoinBattlegroundResult reason = ERR_BATTLEGROUND_JOIN_FAILED;
        std::string error;

        if (mode == "group")
        {
            if (!sHeartOfAcherusMgr->EnqueueGroup(player, error, reason))
                HeartOfAcherus::BattlegroundUI::SendStatusFailed(player, reason);

            return true;
        }

        // a repeated Join click is a no-op
        if (sHeartOfAcherusMgr->IsQueued(player->GetGUID()))
            return true;

        if (!sHeartOfAcherusMgr->Enqueue(player, error, reason))
            HeartOfAcherus::BattlegroundUI::SendStatusFailed(player, reason);

        return true;
    }

    // GM: toggles the selected player (or yourself) in the queue; the PvP frame no longer uses it
    static bool HandleQueueCommand(ChatHandler* handler)
    {
        bool const isGm = handler->HasPermission(rbac::RBAC_PERM_COMMAND_DEBUG);

        Player* player = isGm ? handler->getSelectedPlayerOrSelf() : handler->GetPlayer();
        if (!player)
            return false;

        if (isGm && sHeartOfAcherusMgr->Dequeue(player->GetGUID()))
        {
            handler->SendSysMessage(Trinity::StringFormat("{} left the Heart of Acherus queue.", player->GetName()));
            return true;
        }

        // a repeated Join click is a no-op for players
        if (!isGm && sHeartOfAcherusMgr->IsQueued(player->GetGUID()))
            return true;

        std::string error;
        GroupJoinBattlegroundResult reason = ERR_BATTLEGROUND_JOIN_FAILED;
        if (!sHeartOfAcherusMgr->Enqueue(player, error, reason))
        {
            handler->SendSysMessage(error.empty() ? "Cannot queue this player." : error);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (isGm)
            handler->SendSysMessage(Trinity::StringFormat("{} queued for the Heart of Acherus.", player->GetName()));
        else
            handler->SendSysMessage("You queued for the battle for the Heart of Acherus.");
        return true;
    }

    // starts a match with whoever is queued, ignoring the minimum per team
    static bool HandleStartCommand(ChatHandler* handler)
    {
        sHeartOfAcherusMgr->ForceStart();
        handler->SendSysMessage("The Heart of Acherus match will start with the current queue.");
        return true;
    }

    // skips the preparation of your match, like .bg start; outside of a match (or from the console) of every match
    static bool HandleBeginCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (player && sHeartOfAcherusMgr->SkipPreparation(player->GetGUID()))
        {
            handler->SendSysMessage("Your Heart of Acherus match will begin now.");
            return true;
        }

        sHeartOfAcherusMgr->SkipPreparation(ObjectGuid::Empty);
        handler->SendSysMessage("Every Heart of Acherus match in preparation will begin now.");
        return true;
    }

    // ends every running match as a draw
    static bool HandleStopCommand(ChatHandler* handler)
    {
        sHeartOfAcherusMgr->EndAll();
        handler->SendSysMessage("Every Heart of Acherus match will end as a draw.");
        return true;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        std::array<std::size_t, PVP_TEAMS_COUNT> const queued = sHeartOfAcherusMgr->GetQueueSizes();
        handler->SendSysMessage(Trinity::StringFormat("{}\nQueue: Alliance {}, Horde {}", sHeartOfAcherusMgr->GetStatus(), queued[TEAM_ALLIANCE], queued[TEAM_HORDE]));
        return true;
    }
};

void AddSC_heart_of_acherus_commandscript()
{
    new heart_of_acherus_commandscript();
}
