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
#include "Chat.h"
#include "ChatCommand.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldStatePackets.h"

using namespace Trinity::ChatCommands;

enum AcherusOrbsGossip
{
    NPC_TEXT_GREETING   = 990000,
    NPC_TEXT_RULES      = 990001,

    ACTION_JOIN_QUEUE   = GOSSIP_ACTION_INFO_DEF + 1,
    ACTION_LEAVE_QUEUE  = GOSSIP_ACTION_INFO_DEF + 2,
    ACTION_RULES        = GOSSIP_ACTION_INFO_DEF + 3,
    ACTION_BACK         = GOSSIP_ACTION_INFO_DEF + 4
};

struct npc_acherus_orbs_battlemaster : public ScriptedAI
{
    npc_acherus_orbs_battlemaster(Creature* creature) : ScriptedAI(creature) { }

    // chat icons only: the client auto-selects a lone option with any other icon
    void SendMainMenu(Player* player)
    {
        ClearGossipMenuFor(player);

        std::array<std::size_t, PVP_TEAMS_COUNT> const queued = sAcherusOrbs->GetQueueSizes();
        if (sAcherusOrbs->IsQueued(player->GetGUID()))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I want to leave the queue.", GOSSIP_SENDER_MAIN, ACTION_LEAVE_QUEUE);
        else if (!sAcherusOrbs->IsInMatch(player->GetGUID()))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, Trinity::StringFormat("I want to join the battle for the Heart of Acherus. (Alliance {}, Horde {} queued)",
                queued[TEAM_ALLIANCE], queued[TEAM_HORDE]), GOSSIP_SENDER_MAIN, ACTION_JOIN_QUEUE);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "How does the battle work?", GOSSIP_SENDER_MAIN, ACTION_RULES);
        SendGossipMenuFor(player, NPC_TEXT_GREETING, me->GetGUID());
    }

    bool OnGossipHello(Player* player) override
    {
        SendMainMenu(player);
        return true;
    }

    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
    {
        uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

        if (action == ACTION_RULES)
        {
            ClearGossipMenuFor(player);
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back.", GOSSIP_SENDER_MAIN, ACTION_BACK);
            SendGossipMenuFor(player, NPC_TEXT_RULES, me->GetGUID());
            return true;
        }

        if (action == ACTION_BACK)
        {
            SendMainMenu(player);
            return true;
        }

        CloseGossipMenuFor(player);

        ChatHandler handler(player->GetSession());
        switch (action)
        {
            case ACTION_JOIN_QUEUE:
            {
                std::string error;
                if (sAcherusOrbs->Enqueue(player, error))
                    handler.SendSysMessage("You are now queued for the battle for the Heart of Acherus. You will be taken there when the battle is ready.");
                else if (!error.empty())
                    handler.SendSysMessage(error);
                break;
            }
            case ACTION_LEAVE_QUEUE:
                if (sAcherusOrbs->Dequeue(player->GetGUID()))
                    handler.SendSysMessage("You left the queue for the battle for the Heart of Acherus.");
                break;
            default:
                break;
        }
        return true;
    }
};

struct go_acherus_orbs_runeforge : public GameObjectAI
{
    go_acherus_orbs_runeforge(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        sAcherusOrbs->OnForgeUse(player, me);
        return true;
    }
};

class player_acherus_orbs : public PlayerScript
{
public:
    player_acherus_orbs() : PlayerScript("player_acherus_orbs") { }

    void OnPVPKill(Player* killer, Player* killed) override
    {
        sAcherusOrbs->OnPvPKill(killer, killed);
    }

    void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        sAcherusOrbs->OnUpdateZone(player);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        sAcherusOrbs->OnLogin(player);
    }

    void OnPVPLogDataRequest(Player* player) override
    {
        sAcherusOrbs->OnPVPLogDataRequest(player);
    }

    void OnRequestBattlefieldStatus(Player* player) override
    {
        sAcherusOrbs->OnRequestBattlefieldStatus(player);
    }

    void OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite, bool& handled) override
    {
        sAcherusOrbs->OnBattlefieldPort(player, queueID, acceptedInvite, handled);
    }

    void OnAddonMessage(Player* player, std::string const& msg, bool& handled) override
    {
        sAcherusOrbs->OnAddonMessage(player, msg, handled);
    }

    void OnWardenLuaExecuted(Player* player) override
    {
        sAcherusOrbs->OnWardenLuaExecuted(player);
    }

    void OnBeforeLogout(Player* player) override
    {
        sAcherusOrbs->OnBeforeLogout(player);
    }

    void OnLogout(Player* player) override
    {
        sAcherusOrbs->OnLogout(player);
    }

    void OnSendInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet) override
    {
        sAcherusOrbs->FillInitWorldStates(player, packet);
    }

    void OnLeaveBattlefield(Player* player) override
    {
        sAcherusOrbs->OnLeaveRequest(player);
    }

    void OnJoinBattlegroundQueue(Player* player) override
    {
        sAcherusOrbs->OnJoinRealBattlegroundQueue(player);
    }

    void OnCheckSanctuary(Player* player, bool& isSanctuary) override
    {
        if (isSanctuary && sAcherusOrbs->IsSanctuaryDisabled(player))
            isSanctuary = false;
    }

    void OnRepopAtGraveyard(Player* player, bool& handled) override
    {
        if (!handled)
            handled = sAcherusOrbs->OnRepop(player);
    }

    void OnSpiritHealerQuery(Player* player, Creature* spiritHealer, bool& handled) override
    {
        if (!handled)
            handled = sAcherusOrbs->OnSpiritHealerQuery(player, spiritHealer);
    }

    void OnSpiritHealerQueue(Player* player, Creature* spiritHealer, bool& handled) override
    {
        sAcherusOrbs->OnSpiritHealerQueue(player, spiritHealer, handled);
    }
};

class unit_acherus_orbs : public UnitScript
{
public:
    unit_acherus_orbs() : UnitScript("unit_acherus_orbs") { }

    void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
    {
        sAcherusOrbs->ModifyHealing(healer, reciever, gain);
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        sAcherusOrbs->TrackDamage(attacker, victim, damage);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        sAcherusOrbs->ModifyDamage(attacker, target, damage);
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        sAcherusOrbs->ModifyDamage(attacker, target, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        if (damage <= 0)
            return;

        uint32 amount = uint32(damage);
        sAcherusOrbs->ModifyDamage(attacker, target, amount);
        damage = int32(amount);
    }
};

class world_acherus_orbs : public WorldScript
{
public:
    world_acherus_orbs() : WorldScript("world_acherus_orbs") { }

    // scripts are loaded after the first config load, so the startup one is handled here
    void OnStartup() override
    {
        sAcherusOrbs->LoadConfig();
    }

    void OnConfigLoad(bool /*reload*/) override
    {
        sAcherusOrbs->LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        sAcherusOrbs->Update(diff);
    }
};

class acherus_orbs_commandscript : public CommandScript
{
public:
    acherus_orbs_commandscript() : CommandScript("acherus_orbs_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable acherusCommandTable =
        {
            // players reach this through the PvP frame "Join"; they already have the normal battleground
            // join permission, so no extra RBAC grant is needed (the other subcommands stay GM only)
            { "queue",  HandleQueueCommand,  rbac::RBAC_PERM_JOIN_NORMAL_BG, Console::No },
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

    // .acherus queue - GM: queues the selected player (or yourself) and toggles; players reach it through the
    // PvP frame "Join", so they only queue themselves and never toggle (the Leave Queue button still works)
    static bool HandleQueueCommand(ChatHandler* handler)
    {
        bool isGm = handler->HasPermission(rbac::RBAC_PERM_COMMAND_DEBUG);

        Player* player = isGm ? handler->getSelectedPlayerOrSelf() : handler->GetPlayer();
        if (!player)
            return false;

        if (isGm && sAcherusOrbs->Dequeue(player->GetGUID()))
        {
            handler->SendSysMessage(Trinity::StringFormat("{} left the Heart of Acherus queue.", player->GetName()));
            return true;
        }

        // a repeat click on Join is a no-op for players instead of an error
        if (!isGm && sAcherusOrbs->IsQueued(player->GetGUID()))
            return true;

        std::string error;
        if (!sAcherusOrbs->Enqueue(player, error))
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

    // .acherus start - starts a match with whoever is queued, ignoring the minimum per team
    static bool HandleStartCommand(ChatHandler* handler)
    {
        sAcherusOrbs->ForceStart();
        handler->SendSysMessage("The Heart of Acherus match will start with the current queue.");
        return true;
    }

    // .acherus begin - skips the preparation of your match, like .bg start (of every match when used outside of one)
    static bool HandleBeginCommand(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();                   // null from the console
        if (player && sAcherusOrbs->SkipPreparation(player->GetGUID()))
        {
            handler->SendSysMessage("Your Heart of Acherus match will begin now.");
            return true;
        }

        sAcherusOrbs->SkipPreparation(ObjectGuid::Empty);
        handler->SendSysMessage("Every Heart of Acherus match in preparation will begin now.");
        return true;
    }

    // .acherus stop - ends every running match as a draw
    static bool HandleStopCommand(ChatHandler* handler)
    {
        sAcherusOrbs->EndAll();
        handler->SendSysMessage("Every Heart of Acherus match will end as a draw.");
        return true;
    }

    static bool HandleStatusCommand(ChatHandler* handler)
    {
        std::array<std::size_t, PVP_TEAMS_COUNT> const queued = sAcherusOrbs->GetQueueSizes();
        handler->SendSysMessage(Trinity::StringFormat("{}\nQueue: Alliance {}, Horde {}", sAcherusOrbs->GetStatus(), queued[TEAM_ALLIANCE], queued[TEAM_HORDE]));
        return true;
    }
};

void AddSC_acherus_orbs()
{
    RegisterCreatureAI(npc_acherus_orbs_battlemaster);
    RegisterGameObjectAI(go_acherus_orbs_runeforge);
    new player_acherus_orbs();
    new unit_acherus_orbs();
    new world_acherus_orbs();
    new acherus_orbs_commandscript();
}
