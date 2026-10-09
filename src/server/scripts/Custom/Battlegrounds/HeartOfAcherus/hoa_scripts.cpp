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
#include "Chat.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "WorldStatePackets.h"

enum HeartOfAcherusGossip
{
    NPC_TEXT_GREETING   = 990000,
    NPC_TEXT_RULES      = 990001,

    ACTION_JOIN_QUEUE   = GOSSIP_ACTION_INFO_DEF + 1,
    ACTION_LEAVE_QUEUE  = GOSSIP_ACTION_INFO_DEF + 2,
    ACTION_RULES        = GOSSIP_ACTION_INFO_DEF + 3,
    ACTION_BACK         = GOSSIP_ACTION_INFO_DEF + 4
};

struct npc_heart_of_acherus_battlemaster : public ScriptedAI
{
    npc_heart_of_acherus_battlemaster(Creature* creature) : ScriptedAI(creature) { }

    // chat icons only: the client auto-selects a lone option with any other icon
    void SendMainMenu(Player* player)
    {
        ClearGossipMenuFor(player);

        std::array<std::size_t, PVP_TEAMS_COUNT> const queued = sHeartOfAcherusMgr->GetQueueSizes();
        if (sHeartOfAcherusMgr->IsQueued(player->GetGUID()))
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "I want to leave the queue.", GOSSIP_SENDER_MAIN, ACTION_LEAVE_QUEUE);
        else if (!sHeartOfAcherusMgr->IsInMatch(player->GetGUID()))
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
                if (sHeartOfAcherusMgr->Enqueue(player, error))
                    handler.SendSysMessage("You are now queued for the battle for the Heart of Acherus. You will be taken there when the battle is ready.");
                else if (!error.empty())
                    handler.SendSysMessage(error);
                break;
            }
            case ACTION_LEAVE_QUEUE:
                if (sHeartOfAcherusMgr->Dequeue(player->GetGUID()))
                    handler.SendSysMessage("You left the queue for the battle for the Heart of Acherus.");
                break;
            default:
                break;
        }
        return true;
    }
};

struct go_heart_of_acherus_runeforge : public GameObjectAI
{
    go_heart_of_acherus_runeforge(GameObject* go) : GameObjectAI(go) { }

    bool OnGossipHello(Player* player) override
    {
        sHeartOfAcherusMgr->OnForgeUse(player, me);
        return true;
    }
};

class player_heart_of_acherus : public PlayerScript
{
public:
    player_heart_of_acherus() : PlayerScript("player_heart_of_acherus") { }

    void OnPVPKill(Player* killer, Player* killed) override
    {
        sHeartOfAcherusMgr->OnPvPKill(killer, killed);
    }

    void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
    {
        sHeartOfAcherusMgr->OnUpdateZone(player);
    }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        sHeartOfAcherusMgr->OnLogin(player);
    }

    void OnPVPLogDataRequest(Player* player) override
    {
        sHeartOfAcherusMgr->OnPVPLogDataRequest(player);
    }

    void OnRequestBattlefieldStatus(Player* player) override
    {
        sHeartOfAcherusMgr->OnRequestBattlefieldStatus(player);
    }

    void OnBattlefieldPort(Player* player, uint64 queueID, bool acceptedInvite, bool& handled) override
    {
        if (sHeartOfAcherusMgr->OnBattlefieldPort(player, queueID, acceptedInvite))
            handled = true;
    }

    void OnAddonMessage(Player* player, std::string const& msg, bool& handled) override
    {
        sHeartOfAcherusMgr->OnAddonMessage(player, msg, handled);
    }

    void OnWardenLuaExecuted(Player* player) override
    {
        sHeartOfAcherusMgr->OnWardenLuaExecuted(player);
    }

    void OnBeforeLogout(Player* player) override
    {
        sHeartOfAcherusMgr->OnBeforeLogout(player);
    }

    void OnLogout(Player* player) override
    {
        sHeartOfAcherusMgr->OnLogout(player);
    }

    void OnSendInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet) override
    {
        sHeartOfAcherusMgr->FillInitWorldStates(player, packet);
    }

    void OnLeaveBattlefield(Player* player) override
    {
        sHeartOfAcherusMgr->OnLeaveRequest(player);
    }

    void OnJoinBattlegroundQueue(Player* player) override
    {
        sHeartOfAcherusMgr->OnJoinRealBattlegroundQueue(player);
    }

    void OnCheckSanctuary(Player* player, bool& isSanctuary) override
    {
        if (isSanctuary && sHeartOfAcherusMgr->IsSanctuaryDisabled(player))
            isSanctuary = false;
    }

    void OnCheckOutdoors(Player* player, bool& isOutdoors) override
    {
        if (!isOutdoors && sHeartOfAcherusMgr->IsOutdoorsForced(player))
            isOutdoors = true;
    }

    void OnRepopAtGraveyard(Player* player, bool& handled) override
    {
        if (!handled)
            handled = sHeartOfAcherusMgr->OnRepop(player);
    }

    void OnSpiritHealerQuery(Player* player, Creature* spiritHealer, bool& handled) override
    {
        if (!handled)
            handled = sHeartOfAcherusMgr->OnSpiritHealerQuery(player, spiritHealer);
    }

    void OnSpiritHealerQueue(Player* player, Creature* spiritHealer, bool& handled) override
    {
        if (sHeartOfAcherusMgr->OnSpiritHealerQueue(player, spiritHealer))
            handled = true;
    }
};

// rune damage and healing modifiers, and the damage and healing columns of the scoreboard
class unit_heart_of_acherus : public UnitScript
{
public:
    unit_heart_of_acherus() : UnitScript("unit_heart_of_acherus") { }

    void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
    {
        sHeartOfAcherusMgr->ModifyHealing(healer, reciever, gain);
    }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        sHeartOfAcherusMgr->TrackDamage(attacker, victim, damage);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage) override
    {
        sHeartOfAcherusMgr->ModifyDamage(attacker, target, damage);
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        sHeartOfAcherusMgr->ModifyDamage(attacker, target, damage);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage) override
    {
        if (damage <= 0)
            return;

        uint32 amount = uint32(damage);
        sHeartOfAcherusMgr->ModifyDamage(attacker, target, amount);
        damage = int32(amount);
    }
};

class world_heart_of_acherus : public WorldScript
{
public:
    world_heart_of_acherus() : WorldScript("world_heart_of_acherus") { }

    // scripts are loaded after the first config load
    void OnStartup() override
    {
        sHeartOfAcherusMgr->LoadConfig();
    }

    void OnConfigLoad(bool /*reload*/) override
    {
        sHeartOfAcherusMgr->LoadConfig();
    }

    void OnUpdate(uint32 diff) override
    {
        sHeartOfAcherusMgr->Update(diff);
    }
};

void AddSC_heart_of_acherus_commandscript();

void AddSC_heart_of_acherus()
{
    RegisterCreatureAI(npc_heart_of_acherus_battlemaster);
    RegisterGameObjectAI(go_heart_of_acherus_runeforge);
    new player_heart_of_acherus();
    new unit_heart_of_acherus();
    new world_heart_of_acherus();
    AddSC_heart_of_acherus_commandscript();
}
