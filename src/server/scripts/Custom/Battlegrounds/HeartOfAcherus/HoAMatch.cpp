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

#include "HoAMatch.h"
#include "HoABattlegroundUI.h"
#include "HoAClientUI.h"
#include "HoALayout.h"
#include "HoAMgr.h"
#include "HoAUtil.h"
#include "ChatPackets.h"
#include "Map.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <algorithm>
#include <vector>

namespace HeartOfAcherus
{
    namespace
    {
        constexpr float HonorableKillRange = 40.0f;

        char const* TeamName(TeamId team)
        {
            return team == TEAM_ALLIANCE ? "Alliance" : "Horde";
        }

        ChatMsg TeamChatMsg(TeamId team)
        {
            return team == TEAM_ALLIANCE ? CHAT_MSG_BG_SYSTEM_ALLIANCE : CHAT_MSG_BG_SYSTEM_HORDE;
        }
    }

    Match::Match(Manager& manager, uint32 id, uint32 phaseMask)
        : _manager(manager), _id(id), _phaseMask(phaseMask), _hall(*this), _runes(*this), _graveyards(*this), _raids(*this)
    {
    }

    Map* Match::GetMap() const
    {
        return sMapMgr->FindMap(Ids::MapId, 0);
    }

    ClientUI& Match::GetClientUI() const
    {
        return _manager.GetClientUI();
    }

    MatchPlayer* Match::GetPlayer(ObjectGuid guid)
    {
        auto itr = _players.find(guid);
        return itr != _players.end() ? &itr->second : nullptr;
    }

    MatchPlayer const* Match::GetPlayer(ObjectGuid guid) const
    {
        auto itr = _players.find(guid);
        return itr != _players.end() ? &itr->second : nullptr;
    }

    // ----------------------------------------------------------------- flow

    // the forges already glow, the runes can only be taken once the battle begins
    void Match::StartPreparation()
    {
        _status = MatchStatus::Preparation;
        _statusTimer = Timers::Preparation;

        if (Map* map = sMapMgr->CreateBaseMap(Ids::MapId))
        {
            _hall.SpawnPreparation(map);
            _graveyards.Spawn(map);
        }

        _hall.SetForgeVisuals(true);
        Announce(CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for the Heart of Acherus begins in 2 minutes.");
    }

    // like Battleground::SetStartDelayTime(0) for .bg start
    void Match::SkipPreparation()
    {
        if (_status == MatchStatus::Preparation)
            _statusTimer = 0;
    }

    void Match::Update(uint32 diff)
    {
        uint32 const oldTimer = _statusTimer;
        _statusTimer = _statusTimer > diff ? _statusTimer - diff : 0;

        if (_status != MatchStatus::Ended)
            _graveyards.Update(diff);

        _playerCheckTimer += diff;
        if (_playerCheckTimer >= Timers::PlayerCheck)
        {
            _playerCheckTimer = 0;
            CheckPlayers();
        }

        switch (_status)
        {
            case MatchStatus::Preparation:
                if (oldTimer > MINUTE * IN_MILLISECONDS && _statusTimer <= MINUTE * IN_MILLISECONDS)
                    Announce(CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for the Heart of Acherus begins in 1 minute.");
                else if (oldTimer > 30 * IN_MILLISECONDS && _statusTimer <= 30 * IN_MILLISECONDS)
                    Announce(CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for the Heart of Acherus begins in 30 seconds. Prepare yourselves!");

                if (!_statusTimer)
                    StartBattle();
                break;
            case MatchStatus::InProgress:
            {
                _runes.Update(diff);
                _hall.UpdateBerserkBuffs(diff);

                _tickTimer += diff;
                if (_tickTimer >= Scoring::TickInterval)
                {
                    _tickTimer -= Scoring::TickInterval;
                    ScoreTick();
                }

                // kills score from the map threads, so the victory is checked here
                uint32 const alliance = _score[TEAM_ALLIANCE];
                uint32 const horde = _score[TEAM_HORDE];
                if (alliance >= Scoring::MaxScore || horde >= Scoring::MaxScore)
                    End(alliance == horde ? TEAM_NEUTRAL : alliance > horde ? TEAM_ALLIANCE : TEAM_HORDE);
                else if (!_statusTimer)
                    End(alliance > horde ? TEAM_ALLIANCE : horde > alliance ? TEAM_HORDE : TEAM_NEUTRAL);
                break;
            }
            case MatchStatus::Ended:
                break;
        }

        _markerTimer += diff;
        if (_markerTimer >= Timers::RuneMarkers)
        {
            _markerTimer = 0;
            _runes.SendMarkers();
        }
    }

    // the preparation area leaves, its ghosts are revived on their spawn
    void Match::StartBattle()
    {
        _status = MatchStatus::InProgress;
        _statusTimer = Timers::MatchDuration;
        _tickTimer = 0;
        _graveyards.ResetWave();

        if (Map* map = GetMap())
        {
            _graveyards.DespawnPreparation(map);
            _hall.DespawnPreparationArea(map);
        }

        for (auto& [guid, matchPlayer] : _players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player || !IsInHall(player) || player->IsAlive() || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                continue;

            matchPlayer.HandledDeath = false;
            Revive(player);
            player->NearTeleportTo(Positions::Spawn[matchPlayer.Team]);
        }

        _hall.SetForgeVisuals(true);
        if (Map* map = GetMap())
            _hall.SpawnBattle(map);

        UpdateWorldStates();

        // the score frame timers switch from the preparation to the battle
        for (auto& [guid, matchPlayer] : _players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendBattlefieldStatus(matchPlayer, player);

        Announce(CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for the Heart of Acherus has begun! Claim the runes at the runeforges!");
        PlaySound(Sounds::BattleStart);
    }

    void Match::End(TeamId winner)
    {
        _battleTime = _status == MatchStatus::InProgress ? Timers::MatchDuration - _statusTimer : 0;
        _status = MatchStatus::Ended;
        _statusTimer = Timers::EndWait;
        _winner = winner;

        for (std::size_t rune = 0; rune < RuneCount; ++rune)
        {
            _runes.Drop(Rune(rune), false);
            _hall.SetForgeVisuals(Rune(rune), false);
        }

        for (auto const& [guid, matchPlayer] : _players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && IsInHall(player) && !player->IsAlive())
                Revive(player);
        }

        UpdateWorldStates();

        for (auto& [guid, matchPlayer] : _players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                SendEndState(matchPlayer, player);

        if (winner == TEAM_NEUTRAL)
            Announce(CHAT_MSG_BG_SYSTEM_NEUTRAL, "The battle for the Heart of Acherus ended in a draw.");
        else
        {
            Announce(TeamChatMsg(winner), Trinity::StringFormat("The {} wins the battle for the Heart of Acherus!", TeamName(winner)));
            PlaySound(winner == TEAM_ALLIANCE ? Sounds::AllianceWins : Sounds::HordeWins);
        }
    }

    // every second: match state, raid, deaths, portal; players that left the map or stayed offline too long go out
    void Match::CheckPlayers()
    {
        std::vector<ObjectGuid> left;
        std::vector<ObjectGuid> expired;

        for (auto& [guid, matchPlayer] : _players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!player)
            {
                // the client gets the real forge flags when it sees them again
                matchPlayer.ForgesLocked = false;

                // like battlegrounds, an offline player keeps the place for a while
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

            // a ghost may be sent to a graveyard elsewhere, the wave brings it back
            if (player->GetMapId() != Ids::MapId)
            {
                matchPlayer.ForgesLocked = false;
                if (player->IsAlive())
                    left.push_back(guid);
                continue;
            }

            ApplyPlayerState(player);
            _raids.Update(matchPlayer, player);
            _runes.UpdateForgeLock(matchPlayer, player);

            // the client shows the battleground button and asks for the scoreboard only with an active battlefield
            // status, which it drops while still loading the world
            if (!matchPlayer.WorldStatesSent)
            {
                matchPlayer.WorldStatesSent = true;
                player->SendInitWorldStates(player->GetZoneId(), player->GetAreaId());
                SendBattlefieldStatus(matchPlayer, player);
            }

            if (!player->IsAlive())
            {
                if (!matchPlayer.HandledDeath)
                {
                    matchPlayer.HandledDeath = true;
                    if (_status == MatchStatus::InProgress)
                        ++matchPlayer.Deaths;
                    _runes.DropCarried(guid);
                }

                // the spirit guides are upstairs: a ghost in the hall goes back up through the portal
                if (_status == MatchStatus::InProgress && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    _hall.UsePortal(player);

                continue;
            }

            matchPlayer.HandledDeath = false;

            if (player->IsMounted() && _runes.GetCarried(guid))
                Dismount(player);

            if (_status == MatchStatus::InProgress)
                _hall.UsePortal(player);
        }

        for (ObjectGuid const& guid : left)
            _manager.RemovePlayer(*this, guid, RemoveMode::Left);

        for (ObjectGuid const& guid : expired)
            _manager.RemovePlayer(*this, guid, RemoveMode::Logout);

        if (_status == MatchStatus::Preparation)
            _hall.KeepInPreparationArea();

        _raids.RemoveOutsiders();
    }

    // ----------------------------------------------------------------- score

    void Match::ScoreTick()
    {
        for (RuneState const& state : _runes.GetStates())
        {
            if (state.Carrier.IsEmpty())
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(state.Carrier);
            MatchPlayer* matchPlayer = GetPlayer(state.Carrier);
            if (!player || !player->IsAlive() || player->GetMapId() != Ids::MapId || !matchPlayer)
                continue;

            AwardPoints(*matchPlayer, player, GetPointsForPosition(*player));
        }

        UpdateWorldStates();
    }

    void Match::AwardPoints(MatchPlayer& matchPlayer, Player* player, uint32 points)
    {
        uint32& score = _score[matchPlayer.Team];
        uint32 const before = score;
        score = std::min(score + points, Scoring::MaxScore);
        matchPlayer.Points += score - before;
        if (score > before)
            player->GetSession()->SendAreaTriggerMessage("+%u points", score - before);
    }

    // any enemy player, carrying a rune or not
    void Match::OnKill(Player* killer, Player* killed)
    {
        MatchPlayer* killerData = GetPlayer(killer->GetGUID());
        MatchPlayer const* killedData = GetPlayer(killed->GetGUID());
        if (!killerData || !killedData || killerData->Team == killedData->Team)
            return;

        ++killerData->KillingBlows;

        for (auto& [guid, matchPlayer] : _players)
        {
            if (matchPlayer.Team != killerData->Team)
                continue;

            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && player->IsAlive() && player->GetMapId() == Ids::MapId && player->IsWithinDistInMap(killed, HonorableKillRange))
                ++matchPlayer.HonorableKills;
        }

        if (uint32 const bonus = _manager.GetSettings().KillBonus)
        {
            AwardPoints(*killerData, killer, bonus);
            UpdateWorldStates();
        }
    }

    void Match::AddDamageDone(ObjectGuid guid, uint32 damage)
    {
        if (MatchPlayer* matchPlayer = GetPlayer(guid))
            matchPlayer->DamageDone += damage;
    }

    void Match::AddHealingDone(ObjectGuid guid, uint32 healing)
    {
        if (MatchPlayer* matchPlayer = GetPlayer(guid))
            matchPlayer->HealingDone += healing;
    }

    // ----------------------------------------------------------------- players

    bool Match::AreHallRulesActive(Player const* player) const
    {
        return _status != MatchStatus::Ended && player->GetMapId() == Ids::MapId;
    }

    void Match::ApplyPlayerState(Player* player) const
    {
        if (!player->IsGameMaster() && player->GetPhaseMask() != _phaseMask)
        {
            player->SetPhaseMask(_phaseMask, true);
            player->GetSession()->SendSetPhaseShift(_phaseMask);
        }

        player->RemoveAurasDueToSpell(Spells::UndyingResolve);

        // spell_area gives it back on area changes; in a match players ride their mounts instead
        player->RemoveAurasDueToSpell(Spells::DominionOverAcherus);

        // the whole map is a sanctuary (AreaTableEntry::IsSanctuary), also for who was already in Acherus
        if (AreHallRulesActive(player) && player->IsInSanctuary())
        {
            player->RemovePvpFlag(UNIT_BYTE2_FLAG_SANCTUARY);
            player->pvpInfo.IsInNoPvPArea = false;
        }

        if (!player->IsPvP())
            player->UpdatePvP(true, true);
    }

    // ----------------------------------------------------------------- client frames

    void Match::FillInitWorldStates(Player* player, WorldPackets::WorldState::InitWorldStates& packet)
    {
        BattlegroundUI::FillInitWorldStates(packet, _score, _runes.GetHeldCounts());

        if (MatchPlayer* matchPlayer = GetPlayer(player->GetGUID()))
            matchPlayer->WorldStatesSent = true;
    }

    void Match::UpdateWorldStates()
    {
        std::array<uint32, PVP_TEAMS_COUNT> const runesHeld = _runes.GetHeldCounts();
        for (auto const& [guid, matchPlayer] : _players)
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (player && IsInHall(player))
                BattlegroundUI::SendWorldStates(player, _score, runesHeld);
        }
    }

    void Match::SendScoreboard(Player* target /*= nullptr*/)
    {
        std::vector<BattlegroundUI::ScoreRow> rows;
        std::vector<Player*> targets;
        for (auto const& [guid, matchPlayer] : _players)
        {
            rows.push_back({ guid, matchPlayer.KillingBlows, matchPlayer.HonorableKills, matchPlayer.Deaths,
                matchPlayer.DamageDone, matchPlayer.HealingDone, matchPlayer.Points });

            if (!target)
                if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                    targets.push_back(player);
        }

        if (target)
            targets.push_back(target);

        BattlegroundUI::SendScoreboard(targets, _winner, rows);
    }

    // like Battleground::EndBattleground: final score and the player can no longer move (the client gives the control
    // back by itself on the teleport out)
    void Match::SendEndState(MatchPlayer& matchPlayer, Player* player)
    {
        if (!IsInHall(player))
            return;

        SendScoreboard(player);
        SendBattlefieldStatus(matchPlayer, player);
        player->SetClientControl(player, false);
    }

    void Match::SendBattlefieldStatus(MatchPlayer& matchPlayer, Player* player)
    {
        if (!matchPlayer.StatusSlot)
        {
            matchPlayer.StatusSlot = BattlegroundUI::FindFreeStatusSlot(player);
            if (!matchPlayer.StatusSlot)
                return;
        }

        uint32 const elapsed = _status == MatchStatus::InProgress ? Timers::MatchDuration - _statusTimer : _battleTime;
        BattlegroundUI::SendStatusActive(player, *matchPlayer.StatusSlot, _id, _statusTimer, elapsed, matchPlayer.Team);
        GetClientUI().SetRelabel(player, true);
    }

    void Match::ClearBattlefieldStatus(MatchPlayer& matchPlayer, Player* player)
    {
        if (!matchPlayer.StatusSlot)
            return;

        BattlegroundUI::SendStatusNone(player, *matchPlayer.StatusSlot);
        matchPlayer.StatusSlot.reset();
        GetClientUI().SetRelabel(player, false);
    }

    void Match::Announce(ChatMsg type, std::string const& text)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(type, LANG_UNIVERSAL, nullptr, nullptr, text);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : _players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }

    // like Battleground::PlaySoundToAll
    void Match::PlaySound(uint32 soundId)
    {
        WorldPackets::Misc::PlaySound packet(soundId);
        WorldPacket const* data = packet.Write();

        for (auto const& [guid, matchPlayer] : _players)
            if (Player* player = ObjectAccessor::FindConnectedPlayer(guid))
                player->SendDirectMessage(data);
    }
}
