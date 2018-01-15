/*
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "BattleGroundWS.h"
#include "BattleGround.h"
#include "BattleGroundMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "Language.h"
#include "MapManager.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldPacket.h"

BattleGroundWS::BattleGroundWS(const battlefield::bracket& bracket)
  : specification_(battlefield::warsong_gulch, bracket)
{
    SetMapId(489);
    SetTypeID(BATTLEGROUND_WS);
    SetArenaorBGType(false);
    SetMinPlayersPerTeam(5);
    SetMaxPlayersPerTeam(10);
    SetMinPlayers(10);
    SetMaxPlayers(20);
    SetName("Warsong Gulch");
    SetTeamStartLoc(ALLIANCE, 1523.81f, 1481.76f, 352.008f, 3.14159f);
    SetTeamStartLoc(HORDE, 933.331f, 1433.72f, 345.536f, 0.0f);
    SetLevelRange(10, 70);

    m_StartMessageIds[BG_STARTING_EVENT_FIRST] = 0;
    m_StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_WS_START_ONE_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_THIRD] = LANG_BG_WS_START_HALF_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_WS_HAS_BEGUN;

    // Stuff that was in BattleGroundWS::Reset previously:
    // spiritguides and flags not spawned at beginning
    m_ActiveEvents[WS_EVENT_SPIRITGUIDES_SPAWN] = BG_EVENT_NONE;
    m_ActiveEvents[WS_EVENT_FLAG_A] = BG_EVENT_NONE;
    m_ActiveEvents[WS_EVENT_FLAG_H] = BG_EVENT_NONE;

    for (uint32 i = 0; i < BG_TEAMS_COUNT; ++i)
    {
        m_DroppedFlagGuid[i].Clear();
        m_FlagKeepers[i].Clear();
        m_FlagState[i] = BG_WS_FLAG_STATE_ON_BASE;
        m_TeamScores[i] = 0;
    }
    bool isBGWeekend = BattleGroundMgr::IsBGWeekend(GetTypeID());
    m_ReputationCapture = (isBGWeekend) ? 45 : 35;
    m_HonorWinKills = (isBGWeekend) ? 3 : 1;
    m_HonorEndKills = (isBGWeekend) ? 4 : 2;
    // End of stuff that was in BattleGroundWS::Reset previously
}

BattleGroundWS::~BattleGroundWS()
{
}

void BattleGroundWS::Update(uint32 diff)
{
    BattleGround::Update(diff);

    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        if (m_FlagState[BG_TEAM_ALLIANCE] == BG_WS_FLAG_STATE_WAIT_RESPAWN)
        {
            m_FlagsTimer[BG_TEAM_ALLIANCE] -= diff;

            if (m_FlagsTimer[BG_TEAM_ALLIANCE] < 0)
            {
                m_FlagsTimer[BG_TEAM_ALLIANCE] = 0;
                RespawnFlag(ALLIANCE, true);
            }
        }
        if (m_FlagState[BG_TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_GROUND)
        {
            m_FlagsDropTimer[BG_TEAM_ALLIANCE] -= diff;

            if (m_FlagsDropTimer[BG_TEAM_ALLIANCE] < 0)
            {
                m_FlagsDropTimer[BG_TEAM_ALLIANCE] = 0;
                RespawnFlagAfterDrop(ALLIANCE);
            }
        }
        if (m_FlagState[BG_TEAM_HORDE] == BG_WS_FLAG_STATE_WAIT_RESPAWN)
        {
            m_FlagsTimer[BG_TEAM_HORDE] -= diff;

            if (m_FlagsTimer[BG_TEAM_HORDE] < 0)
            {
                m_FlagsTimer[BG_TEAM_HORDE] = 0;
                RespawnFlag(HORDE, true);
            }
        }
        if (m_FlagState[BG_TEAM_HORDE] == BG_WS_FLAG_STATE_ON_GROUND)
        {
            m_FlagsDropTimer[BG_TEAM_HORDE] -= diff;

            if (m_FlagsDropTimer[BG_TEAM_HORDE] < 0)
            {
                m_FlagsDropTimer[BG_TEAM_HORDE] = 0;
                RespawnFlagAfterDrop(HORDE);
            }
        }
    }
}

void BattleGroundWS::StartingEventCloseDoors()
{
}

void BattleGroundWS::StartingEventOpenDoors()
{
    OpenDoorEvent(BG_EVENT_DOOR);

    SpawnEvent(WS_EVENT_SPIRITGUIDES_SPAWN, 0, true);
    SpawnEvent(WS_EVENT_FLAG_A, 0, true);
    SpawnEvent(WS_EVENT_FLAG_H, 0, true);
}

void BattleGroundWS::AddPlayer(Player* plr)
{
    BattleGround::AddPlayer(plr);
    // create score and add it to map, default values are set in constructor
    auto sc = new BattleGroundWGScore;

    m_PlayerScores[plr->GetObjectGuid()] = sc;
}

void BattleGroundWS::RespawnFlag(Team team, bool captured)
{
    if (team == ALLIANCE)
    {
        LOG_DEBUG(logging, "Respawn Alliance flag");
        m_FlagState[BG_TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_BASE;
        SpawnEvent(WS_EVENT_FLAG_A, 0, true);
        if (Player* player =
                sObjectMgr::Instance()->GetPlayer(m_FlagKeepers[BG_TEAM_HORDE]))
        {
            // FIXME: Get XYZ from object and not hard-coded values
            if (player->IsWithinDist3d(1540.4f, 1481.3f, 351.8f, 6.0f))
                EventPlayerCapturedFlag(player);
        }
    }
    else
    {
        LOG_DEBUG(logging, "Respawn Horde flag");
        m_FlagState[BG_TEAM_HORDE] = BG_WS_FLAG_STATE_ON_BASE;
        SpawnEvent(WS_EVENT_FLAG_H, 0, true);
        if (Player* player = sObjectMgr::Instance()->GetPlayer(
                m_FlagKeepers[BG_TEAM_ALLIANCE]))
        {
            // FIXME: Get XYZ from object and not hard-coded values
            if (player->IsWithinDist3d(916.0f, 1434.4f, 345.4f, 6.0f))
                EventPlayerCapturedFlag(player);
        }
    }

    if (captured)
    {
        // when map_update will be allowed for battlegrounds this code will be
        // useless
        SpawnEvent(WS_EVENT_FLAG_A, 0, true);
        SpawnEvent(WS_EVENT_FLAG_H, 0, true);
        SendMessageToAll(LANG_BG_WS_F_PLACED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        PlaySoundToAll(BG_WS_SOUND_FLAGS_RESPAWNED); // flag respawned sound...
    }
}

void BattleGroundWS::RespawnFlagAfterDrop(Team team)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    RespawnFlag(team, false);
    if (team == ALLIANCE)
        SendMessageToAll(
            LANG_BG_WS_ALLIANCE_FLAG_RESPAWNED, CHAT_MSG_BG_SYSTEM_NEUTRAL);
    else
        SendMessageToAll(
            LANG_BG_WS_HORDE_FLAG_RESPAWNED, CHAT_MSG_BG_SYSTEM_NEUTRAL);

    PlaySoundToAll(BG_WS_SOUND_FLAGS_RESPAWNED);

    GameObject* obj = GetBgMap()->GetGameObject(GetDroppedFlagGuid(team));
    if (obj)
        obj->Delete();
    else
        logging.error("Unknown dropped flag bg: %s",
            GetDroppedFlagGuid(team).GetString().c_str());

    ClearDroppedFlagGuid(team);
}

void BattleGroundWS::EventPlayerCapturedFlag(Player* Source)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Team winner = TEAM_NONE;

    Source->remove_auras_if([](AuraHolder* h)
        {
            return h->GetSpellProto()->AuraInterruptFlags &
                   AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT;
        });
    if (Source->GetTeam() == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        ClearHordeFlagPicker(); // must be before aura remove to prevent 2
                                // events (drop+capture) at the same time
                                // horde flag in base (but not respawned yet)
        m_FlagState[BG_TEAM_HORDE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
        // Drop Horde Flag from Player
        Source->remove_auras(BG_WS_SPELL_WARSONG_FLAG);
        if (GetTeamScore(ALLIANCE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(ALLIANCE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_ALLIANCE);
        RewardReputationToTeam(890, m_ReputationCapture, ALLIANCE);
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        ClearAllianceFlagPicker(); // must be before aura remove to prevent 2
                                   // events (drop+capture) at the same time
        // alliance flag in base (but not respawned yet)
        m_FlagState[BG_TEAM_ALLIANCE] = BG_WS_FLAG_STATE_WAIT_RESPAWN;
        // Drop Alliance Flag from Player
        Source->remove_auras(BG_WS_SPELL_SILVERWING_FLAG);
        if (GetTeamScore(HORDE) < BG_WS_MAX_TEAM_SCORE)
            AddPoint(HORDE, 1);
        PlaySoundToAll(BG_WS_SOUND_FLAG_CAPTURED_HORDE);
        RewardReputationToTeam(889, m_ReputationCapture, HORDE);
    }
    // for flag capture is reward 2 honorable kills
    RewardHonorToTeam(GetBonusHonorFromKill(2), Source->GetTeam());

    // despawn flags
    SpawnEvent(WS_EVENT_FLAG_A, 0, false);
    SpawnEvent(WS_EVENT_FLAG_H, 0, false);

    if (Source->GetTeam() == ALLIANCE)
        SendMessageToAll(
            LANG_BG_WS_CAPTURED_HF, CHAT_MSG_BG_SYSTEM_ALLIANCE, Source);
    else
        SendMessageToAll(
            LANG_BG_WS_CAPTURED_AF, CHAT_MSG_BG_SYSTEM_HORDE, Source);

    UpdateFlagState(Source->GetTeam(), 1); // flag state none
    UpdateTeamScore(Source->GetTeam());
    // only flag capture should be updated
    UpdatePlayerScore(Source, SCORE_FLAG_CAPTURES, 1); // +1 flag captures

    if (GetTeamScore(ALLIANCE) == BG_WS_MAX_TEAM_SCORE)
        winner = ALLIANCE;

    if (GetTeamScore(HORDE) == BG_WS_MAX_TEAM_SCORE)
        winner = HORDE;

    if (winner)
    {
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 0);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 0);
        UpdateWorldState(BG_WS_FLAG_STATE_ALLIANCE, 1);
        UpdateWorldState(BG_WS_FLAG_STATE_HORDE, 1);

        EndBattleGround(winner);
    }
    else
    {
        m_FlagsTimer[GetOtherTeamIndex(
            GetTeamIndexByTeamId(Source->GetTeam()))] = BG_WS_FLAG_RESPAWN_TIME;
    }
}

void BattleGroundWS::EventPlayerDroppedFlag(Player* Source)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player (prevent
        // spawning the "dropped" flag), neither send unnecessary messages
        // just take off the aura
        if (Source->GetTeam() == ALLIANCE)
        {
            if (!IsHordeFlagPickedup())
                return;
            if (GetHordeFlagPickerGuid() == Source->GetObjectGuid())
            {
                ClearHordeFlagPicker();
                Source->remove_auras(BG_WS_SPELL_WARSONG_FLAG);
            }
        }
        else
        {
            if (!IsAllianceFlagPickedup())
                return;
            if (GetAllianceFlagPickerGuid() == Source->GetObjectGuid())
            {
                ClearAllianceFlagPicker();
                Source->remove_auras(BG_WS_SPELL_SILVERWING_FLAG);
            }
        }
        return;
    }

    bool set = false;

    if (Source->GetTeam() == ALLIANCE)
    {
        if (!IsHordeFlagPickedup())
            return;
        if (GetHordeFlagPickerGuid() == Source->GetObjectGuid())
        {
            ClearHordeFlagPicker();
            Source->remove_auras(BG_WS_SPELL_WARSONG_FLAG);
            m_FlagState[BG_TEAM_HORDE] = BG_WS_FLAG_STATE_ON_GROUND;
            Source->CastSpell(Source, BG_WS_SPELL_WARSONG_FLAG_DROPPED, true);
            set = true;
        }
    }
    else
    {
        if (!IsAllianceFlagPickedup())
            return;
        if (GetAllianceFlagPickerGuid() == Source->GetObjectGuid())
        {
            ClearAllianceFlagPicker();
            Source->remove_auras(BG_WS_SPELL_SILVERWING_FLAG);
            m_FlagState[BG_TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_GROUND;
            Source->CastSpell(
                Source, BG_WS_SPELL_SILVERWING_FLAG_DROPPED, true);
            set = true;
        }
    }

    if (set)
    {
        UpdateFlagState(Source->GetTeam(), 1);

        if (Source->GetTeam() == ALLIANCE)
        {
            SendMessageToAll(
                LANG_BG_WS_DROPPED_HF, CHAT_MSG_BG_SYSTEM_HORDE, Source);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, uint32(-1));
        }
        else
        {
            SendMessageToAll(
                LANG_BG_WS_DROPPED_AF, CHAT_MSG_BG_SYSTEM_ALLIANCE, Source);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, uint32(-1));
        }

        m_FlagsDropTimer[GetOtherTeamIndex(
            GetTeamIndexByTeamId(Source->GetTeam()))] = BG_WS_FLAG_DROP_TIME;
    }
}

void BattleGroundWS::EventPlayerClickedOnFlag(
    Player* Source, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    int32 message_id = 0;
    uint32 spell_id = 0;
    ChatMsg type;

    uint8 event = (sBattleGroundMgr::Instance()->GetGameObjectEventIndex(
                       target_obj->GetGUIDLow())).event1;

    // alliance flag picked up from base
    if (Source->GetTeam() == HORDE &&
        GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_BASE &&
        event == WS_EVENT_FLAG_A)
    {
        message_id = LANG_BG_WS_PICKEDUP_AF;
        type = CHAT_MSG_BG_SYSTEM_HORDE;
        PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
        SpawnEvent(WS_EVENT_FLAG_A, 0, false);
        SetAllianceFlagPicker(Source->GetObjectGuid());
        m_FlagState[BG_TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
        // update world state to show correct flag carrier
        UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
        spell_id = BG_WS_SPELL_SILVERWING_FLAG;
    }
    // horde flag picked up from base
    else if (Source->GetTeam() == ALLIANCE &&
             GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_BASE &&
             event == WS_EVENT_FLAG_H)
    {
        message_id = LANG_BG_WS_PICKEDUP_HF;
        type = CHAT_MSG_BG_SYSTEM_ALLIANCE;
        PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
        SpawnEvent(WS_EVENT_FLAG_H, 0, false);
        SetHordeFlagPicker(Source->GetObjectGuid());
        m_FlagState[BG_TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
        // update world state to show correct flag carrier
        UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
        UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
        spell_id = BG_WS_SPELL_WARSONG_FLAG;
    }
    // Alliance flag on ground(not in base) (returned or picked up again from
    // ground!)
    else if (GetFlagState(ALLIANCE) == BG_WS_FLAG_STATE_ON_GROUND &&
             Source->IsWithinDistInMap(target_obj, 10) &&
             target_obj->GetEntry() == BG_WS_FLAG_ALLIANCE_FIELD)
    {
        if (Source->GetTeam() == ALLIANCE)
        {
            message_id = LANG_BG_WS_RETURNED_AF;
            type = CHAT_MSG_BG_SYSTEM_ALLIANCE;
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(ALLIANCE, false);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdatePlayerScore(Source, SCORE_FLAG_RETURNS, 1);
        }
        else
        {
            message_id = LANG_BG_WS_PICKEDUP_AF;
            type = CHAT_MSG_BG_SYSTEM_HORDE;
            PlaySoundToAll(BG_WS_SOUND_ALLIANCE_FLAG_PICKED_UP);
            SpawnEvent(WS_EVENT_FLAG_A, 0, false);
            SetAllianceFlagPicker(Source->GetObjectGuid());
            m_FlagState[BG_TEAM_ALLIANCE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(HORDE, BG_WS_FLAG_STATE_ON_PLAYER);
            UpdateWorldState(BG_WS_FLAG_UNK_ALLIANCE, 1);
            spell_id = BG_WS_SPELL_SILVERWING_FLAG;
        }
        // called in HandleGameObjectUseOpcode:
        // target_obj->Delete();
    }
    // Horde flag on ground(not in base) (returned or picked up again)
    else if (GetFlagState(HORDE) == BG_WS_FLAG_STATE_ON_GROUND &&
             Source->IsWithinDistInMap(target_obj, 10) &&
             target_obj->GetEntry() == BG_WS_FLAG_HORDE_FIELD)
    {
        if (Source->GetTeam() == HORDE)
        {
            message_id = LANG_BG_WS_RETURNED_HF;
            type = CHAT_MSG_BG_SYSTEM_HORDE;
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_WAIT_RESPAWN);
            RespawnFlag(HORDE, false);
            PlaySoundToAll(BG_WS_SOUND_FLAG_RETURNED);
            UpdatePlayerScore(Source, SCORE_FLAG_RETURNS, 1);
        }
        else
        {
            message_id = LANG_BG_WS_PICKEDUP_HF;
            type = CHAT_MSG_BG_SYSTEM_ALLIANCE;
            PlaySoundToAll(BG_WS_SOUND_HORDE_FLAG_PICKED_UP);
            SpawnEvent(WS_EVENT_FLAG_H, 0, false);
            SetHordeFlagPicker(Source->GetObjectGuid());
            m_FlagState[BG_TEAM_HORDE] = BG_WS_FLAG_STATE_ON_PLAYER;
            UpdateFlagState(ALLIANCE, BG_WS_FLAG_STATE_ON_PLAYER);
            UpdateWorldState(BG_WS_FLAG_UNK_HORDE, 1);
            spell_id = BG_WS_SPELL_WARSONG_FLAG;
        }
        // called in HandleGameObjectUseOpcode:
        // target_obj->Delete();
    }

    if (!message_id)
        return;
    SendMessageToAll(message_id, type, Source);

    if (spell_id)
        Source->CastSpell(Source, spell_id, true);

    Source->remove_auras_if([](AuraHolder* h)
        {
            return h->GetSpellProto()->AuraInterruptFlags &
                   AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT;
        });
}

void BattleGroundWS::RemovePlayer(Player* plr, ObjectGuid guid)
{
    // sometimes flag aura not removed :(
    if (IsAllianceFlagPickedup() && m_FlagKeepers[BG_TEAM_ALLIANCE] == guid)
    {
        if (!plr)
        {
            logging.error(
                "BattleGroundWS: Removing offline player who has the FLAG!!");
            ClearAllianceFlagPicker();
            RespawnFlag(ALLIANCE, false);
        }
        else
            EventPlayerDroppedFlag(plr);
    }
    if (IsHordeFlagPickedup() && m_FlagKeepers[BG_TEAM_HORDE] == guid)
    {
        if (!plr)
        {
            logging.error(
                "BattleGroundWS: Removing offline player who has the FLAG!!");
            ClearHordeFlagPicker();
            RespawnFlag(HORDE, false);
        }
        else
            EventPlayerDroppedFlag(plr);
    }
}

void BattleGroundWS::UpdateFlagState(Team team, uint32 value)
{
    if (team == ALLIANCE)
        UpdateWorldState(BG_WS_FLAG_STATE_ALLIANCE, value);
    else
        UpdateWorldState(BG_WS_FLAG_STATE_HORDE, value);
}

void BattleGroundWS::UpdateTeamScore(Team team)
{
    if (team == ALLIANCE)
        UpdateWorldState(BG_WS_FLAG_CAPTURES_ALLIANCE, GetTeamScore(team));
    else
        UpdateWorldState(BG_WS_FLAG_CAPTURES_HORDE, GetTeamScore(team));
}

void BattleGroundWS::HandleAreaTrigger(Player* Source, uint32 Trigger)
{
    // this is wrong way to implement these things. On official it done by
    // gameobject spell cast.
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // uint32 SpellId = 0;
    // uint64 buff_guid = 0;
    switch (Trigger)
    {
    case 3686: // Alliance elixir of speed spawn. Trigger not working, because
               // located inside other areatrigger, can be replaced by
               // IsWithinDist(object, dist) in BattleGround::Update().
    case 3687: // Horde elixir of speed spawn. Trigger not working, because
               // located inside other areatrigger, can be replaced by
               // IsWithinDist(object, dist) in BattleGround::Update().
    case 3706: // Alliance elixir of regeneration spawn
    case 3708: // Horde elixir of regeneration spawn
    case 3707: // Alliance elixir of berserk spawn
    case 3709: // Horde elixir of berserk spawn
        break;
    case 3646: // Alliance Flag spawn
        if (m_FlagState[BG_TEAM_HORDE] && !m_FlagState[BG_TEAM_ALLIANCE])
            if (GetHordeFlagPickerGuid() == Source->GetObjectGuid())
                EventPlayerCapturedFlag(Source);
        break;
    case 3647: // Horde Flag spawn
        if (m_FlagState[BG_TEAM_ALLIANCE] && !m_FlagState[BG_TEAM_HORDE])
            if (GetAllianceFlagPickerGuid() == Source->GetObjectGuid())
                EventPlayerCapturedFlag(Source);
        break;
    case 3649: // unk1
    case 3688: // unk2
    case 4628: // unk3
    case 4629: // unk4
        break;
    default:
        logging.error(
            "WARNING: Unhandled AreaTrigger in Battleground: %u", Trigger);
        Source->GetSession()->SendAreaTriggerMessage(
            "Warning: Unhandled AreaTrigger in Battleground: %u", Trigger);
        break;
    }
}

bool BattleGroundWS::SetupBattleGround()
{
    return true;
}

void BattleGroundWS::EndBattleGround(Team winner)
{
    // win reward
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(m_HonorWinKills), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(m_HonorWinKills), HORDE);
    // complete map_end rewards (even if no team wins)
    RewardHonorToTeam(GetBonusHonorFromKill(m_HonorEndKills), ALLIANCE);
    RewardHonorToTeam(GetBonusHonorFromKill(m_HonorEndKills), HORDE);

    BattleGround::EndBattleGround(winner);
}

void BattleGroundWS::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    EventPlayerDroppedFlag(player);

    BattleGround::HandleKillPlayer(player, killer);
}

void BattleGroundWS::UpdatePlayerScore(
    Player* Source, uint32 type, uint32 value)
{
    auto itr = m_PlayerScores.find(Source->GetObjectGuid());
    if (itr == m_PlayerScores.end()) // player not found
        return;

    switch (type)
    {
    case SCORE_FLAG_CAPTURES: // flags captured
        ((BattleGroundWGScore*)itr->second)->FlagCaptures += value;
        break;
    case SCORE_FLAG_RETURNS: // flags returned
        ((BattleGroundWGScore*)itr->second)->FlagReturns += value;
        break;
    default:
        BattleGround::UpdatePlayerScore(Source, type, value);
        break;
    }
}

WorldSafeLocsEntry const* BattleGroundWS::GetClosestGraveYard(Player* player)
{
    // if status in progress, it returns main graveyards with spiritguides
    // else it will return the graveyard in the flagroom - this is especially
    // good
    // if a player dies in preparation phase - then the player can't cheat
    // and teleport to the graveyard outside the flagroom
    // and start running around, while the doors are still closed
    if (player->GetTeam() == ALLIANCE)
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_ALLIANCE);
        else
            return sWorldSafeLocsStore.LookupEntry(
                WS_GRAVEYARD_FLAGROOM_ALLIANCE);
    }
    else
    {
        if (GetStatus() == STATUS_IN_PROGRESS)
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_MAIN_HORDE);
        else
            return sWorldSafeLocsStore.LookupEntry(WS_GRAVEYARD_FLAGROOM_HORDE);
    }
}

void BattleGroundWS::FillInitialWorldStates(WorldPacket& data, uint32& count)
{
    FillInitialWorldState(
        data, count, BG_WS_FLAG_CAPTURES_ALLIANCE, GetTeamScore(ALLIANCE));
    FillInitialWorldState(
        data, count, BG_WS_FLAG_CAPTURES_HORDE, GetTeamScore(HORDE));

    if (m_FlagState[BG_TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_GROUND)
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_ALLIANCE, -1);
    else if (m_FlagState[BG_TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_PLAYER)
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_ALLIANCE, 1);
    else
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_ALLIANCE, 0);

    if (m_FlagState[BG_TEAM_HORDE] == BG_WS_FLAG_STATE_ON_GROUND)
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_HORDE, -1);
    else if (m_FlagState[BG_TEAM_HORDE] == BG_WS_FLAG_STATE_ON_PLAYER)
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_HORDE, 1);
    else
        FillInitialWorldState(data, count, BG_WS_FLAG_UNK_HORDE, 0);

    FillInitialWorldState(
        data, count, BG_WS_FLAG_CAPTURES_MAX, BG_WS_MAX_TEAM_SCORE);

    if (m_FlagState[BG_TEAM_HORDE] == BG_WS_FLAG_STATE_ON_PLAYER)
        FillInitialWorldState(data, count, BG_WS_FLAG_STATE_ALLIANCE, 2);
    else
        FillInitialWorldState(data, count, BG_WS_FLAG_STATE_ALLIANCE, 1);

    if (m_FlagState[BG_TEAM_ALLIANCE] == BG_WS_FLAG_STATE_ON_PLAYER)
        FillInitialWorldState(data, count, BG_WS_FLAG_STATE_HORDE, 2);
    else
        FillInitialWorldState(data, count, BG_WS_FLAG_STATE_HORDE, 1);
}