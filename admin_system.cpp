#include <stdio.h>
#include "admin_system.h"
#include "metamod_oslink.h"

AdminSystem g_AdminSystem;
PLUGIN_EXPOSE(AdminSystem, g_AdminSystem);

CGameEntitySystem* g_pGameEntitySystem = nullptr;
CSchemaSystem* g_pCSchemaSystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
IVEngineServer2* engine = nullptr;
CGlobalVars *gpGlobals = nullptr;

CUtlMap<uint32, CChatCommand *> g_CommandList(0, 0, DefLessFunc(uint32));

IMySQLClient *g_pMysqlClient;
IMySQLConnection* g_pConnection;

IUtilsApi* g_pUtils;
IMenusApi* g_pMenus;

char g_szCategory[64][64];
char g_szItem[64][64];

float g_flUniversalTime;
float g_flLastTickedTime;
bool g_bHasTicked;

int g_iTarget[64];
int g_iReason[64];

AdminApi* g_pAdminApi = nullptr;
IAdminApi* g_pAdminCore = nullptr;

int g_iLastTime;
std::map<std::string, std::string> g_vecMaps;

std::map<std::string, std::string> g_vecPhrases;

std::vector<std::string> g_vecBanReasons;
std::vector<std::string> g_vecMuteReasons;

std::map<int, std::string> g_Times;

SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandHandle, const CCommandContext&, const CCommand&);
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK6(IServerGameClients, ClientConnect, SH_NOATTRIB, 0, bool, CPlayerSlot, const char*, uint64, const char *, bool, CBufferString *);

bool (*UTIL_IsHearingClient)(void* serverClient, int index) = nullptr;
void (*UTIL_SwitchTeam)(CCSPlayerController* pPlayer, int iTeam) = nullptr;

funchook_t* m_IsHearingClient;

CPlayer *m_vecPlayers[64];

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
};

void AdminMenu(int iSlot);
void ReasonMenu(int iSlot);
void ShowPlayerList(int iSlot);

bool containsOnlyDigits(const std::string& str) {
	return str.find_first_not_of("0123456789") == std::string::npos;
}

void SwitchTeam(CCSPlayerController* pPlayer, int iTeam)
{
	if (iTeam == CS_TEAM_SPECTATOR)
	{
		pPlayer->ChangeTeam(iTeam);
	}
	else
	{
		UTIL_SwitchTeam(pPlayer, iTeam);
	}
}

bool FASTCALL IsHearingClient(void* serverClient, int index)
{
	CPlayer* player = m_vecPlayers[index];
	if (player != nullptr && player->IsMuted())
		return false;

	return UTIL_IsHearingClient(serverClient, index);
}

void ClientPrintAll(const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	g_SMAPI->Format(buf2, sizeof(buf2), "%s%s", g_AdminSystem.Translate("Prefix"), buf);
	va_end(args);

	g_pUtils->PrintToChatAll(buf2);
	ConMsg("%s\n", buf2);
}

void ClientPrint(int iSlot, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	g_SMAPI->Format(buf2, sizeof(buf2), "%s%s", g_AdminSystem.Translate("Prefix"), buf);

	va_end(args);

	g_pUtils->PrintToChat(iSlot, buf2, nullptr, nullptr, nullptr, nullptr);
	if(iSlot < 0) ConMsg("%s\n", buf2);
}

bool CustomFlagsToString(std::string& flag, uint64_t flags)
{
    int total = 0;

    for (int i = 14; i <= 24; ++i)
    {
        if (flags & (1 << i))
        {
            if(total == 0)
			{
				if(size(flag) != 0) flag += ", custom(";
				else flag += "custom(";
			}
			else
                flag += ", ";
            flag += std::to_string(i - 14 + 1);
            total++;
        }
    }
    return total;
}

void FlagsToString(std::string& flag, uint64_t flags)
{
    int total = 0;
    for (int i = 0; i < FLAG_STRINGS; ++i)
    {
        if (flags & (1 << i))
        {
            if(total != 0)
                flag += ", ";
            flag += g_FlagNames[i];
            total++;
        }
    }
    if (CustomFlagsToString(flag, flags))
    {
        flag += ")";
    }
}

int AdminSystem::TargetPlayerString(const char* target)
{	
	if (*target == '#')
	{
		int userid = V_StringToUint16(target + 1, -1);
		if(userid < 64)
		{
			if (m_vecPlayers[userid] == nullptr || m_vecPlayers[userid]->IsFakeClient())
				return -1;

			CCSPlayerController* player = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(userid + 1));

			if(!player || !player->m_hPawn() || player->m_steamID() <= 0)
				return -1;

			return userid;
		}
		return -1;
	}
	else
	{
		for (int i = 0; i < 64; i++)
		{
			if (m_vecPlayers[i] == nullptr || m_vecPlayers[i]->IsFakeClient())
				continue;

			CCSPlayerController* player = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

			if (!player)
				continue;

			if (strstr(engine->GetClientConVarValue(i, "name"), target) || (containsOnlyDigits(target) && player->m_steamID() == std::stoull(target)))
			{
				return i;
			}
		}
	}
	return -1;
}

void AdminSystem::ParseChatCommand(int iSlot, const char *pMessage, CCSPlayerController *pController)
{
	if (!pController)
		return;

	CCommand args;
	args.Tokenize(pMessage);
	uint16 index = g_CommandList.Find(hash_32_fnv1a_const(args[0]));

	if (g_CommandList.IsValidIndex(index))
	{
		(*g_CommandList[index])(iSlot, args, pController);
	}
}

bool CChatCommand::CheckCommandAccess(int iSlot, CBasePlayerController *pPlayer, uint64 flags)
{
	if (!pPlayer)
		return false;

	if (!(!flags || (m_vecPlayers[iSlot]->GetAdminFlags() & flags)))
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("DenyAccess"));
		return false;
	}

	return true;
}

bool AdminSystem::IsAdmin(int iSlot)
{
	if(!m_vecPlayers[iSlot]->GetAdminFlags())
		return false;
	else return true;
}

bool AdminSystem::IsAdminFlagSet(int iSlot, uint64 iFlag)
{
	if(m_vecPlayers[iSlot] == nullptr) return 0;
	else return !iFlag || (m_vecPlayers[iSlot]->GetAdminFlags() & iFlag);
}

uint64 AdminSystem::ParseFlags(const char* pszFlags)
{
	uint64 flags = 0;
	size_t length = V_strlen(pszFlags);

	for (size_t i = 0; i < length; i++)
	{
		char c = tolower(pszFlags[i]);
		if (c < 'a' || c > 'z')
			continue;

		if (c == 'z')
			return -1;

		flags |= ((uint64)1 << (c - 'a'));
	}

	return flags;
}

void AdminSystem::CheckInfractions(int PlayerSlot, int bAdmin = true)
{
	char szQuery[128];
	uint64 iSteamID = m_vecPlayers[PlayerSlot]->GetSteamID();
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT * FROM `as_bans` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", iSteamID, std::time(0));
	g_pConnection->Query(szQuery, [PlayerSlot, this, iSteamID, bAdmin](IMySQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->GetRowCount())
		{
			if(results->FetchRow())
			{
				engine->DisconnectClient(CPlayerSlot(PlayerSlot), NETWORK_DISCONNECT_KICKBANADDED);
			}
		}
		else
		{
			char szQuery[128];
			g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT * FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", iSteamID, std::time(0));
			g_pConnection->Query(szQuery, [PlayerSlot, this](IMySQLQuery* test)
			{
				auto results = test->GetResultSet();
				if(results->GetRowCount())
				{
					while(results->FetchRow())
					{
						int iDuration = results->GetInt(6);
						int iEnd = results->GetInt(7);
						m_vecPlayers[PlayerSlot]->SetMuted(iDuration, iEnd);
					}
				}
				else
					m_vecPlayers[PlayerSlot]->SetMuted(-1, -1);
			});
			g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT * FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", iSteamID, std::time(0));
			g_pConnection->Query(szQuery, [PlayerSlot, this](IMySQLQuery* test)
			{
				auto results = test->GetResultSet();
				if(results->GetRowCount())
				{
					while(results->FetchRow())
					{
						int iDuration = results->GetInt(6);
						int iEnd = results->GetInt(7);
						m_vecPlayers[PlayerSlot]->SetGagged(iDuration, iEnd);
					}
				}
				else
					m_vecPlayers[PlayerSlot]->SetGagged(-1, -1);
			});
			if(bAdmin)
			{
				g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT `immunity`, `end`, `flags`, `name` FROM `as_admins` WHERE `steamid` = '%lld'", iSteamID);
				g_pConnection->Query(szQuery, [PlayerSlot, this](IMySQLQuery* test)
				{
					auto results = test->GetResultSet();
					if(results->GetRowCount())
					{
						if(results->FetchRow())
						{
							m_vecPlayers[PlayerSlot]->SetAdminImmunity(results->GetInt(0));
							m_vecPlayers[PlayerSlot]->SetAdminEnd(results->GetInt(1));
							m_vecPlayers[PlayerSlot]->SetAdminFlags(ParseFlags(results->GetString(2)));
							m_vecPlayers[PlayerSlot]->SetAdminName(results->GetString(3));
						}
					}
				});
			}
		}
	});
}

bool ChatListener(int iSlot, const char* szContent)
{
	CPlayer* player = m_vecPlayers[iSlot];
	return (player != nullptr && !player->IsGagged());
}

void TimeMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_pMenus->ClosePlayerMenu(iSlot);
		int iTime = std::stoi(szBack);
		char sReason[128], szQuery[512];
		auto pController = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
		auto pController2 = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(g_iTarget[iSlot] + 1));
		if (!pController2 || pController2->m_steamID() == 0 || !pController2->m_hPawn().Get())
		{
			g_pUtils->PrintToChat(iSlot, g_vecPhrases[std::string("Player Leave")].c_str());
			return;
		}
		if(!strcmp(g_szItem[iSlot], "ban"))
			g_SMAPI->Format(sReason, sizeof(sReason), "%s", g_vecBanReasons[g_iReason[iSlot]].c_str());
		else
			g_SMAPI->Format(sReason, sizeof(sReason), "%s", g_vecMuteReasons[g_iReason[iSlot]].c_str());

		if(!strcmp(g_szItem[iSlot], "ban"))
		{
			if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("BanPermanent"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Ban"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), iTime/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pController->m_steamID(), pController2->m_steamID(), iSlot == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), g_pConnection->Escape(pController2->m_iszPlayerName()).c_str(), std::time(0), iTime, std::time(0)+iTime, sReason);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			engine->DisconnectClient(CPlayerSlot(g_iTarget[iSlot]), NETWORK_DISCONNECT_KICKBANADDED);
		}
		else if(!strcmp(g_szItem[iSlot], "mute"))
		{
			if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("MutePermanent"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Mute"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), iTime/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pController->m_steamID(), pController2->m_steamID(), iSlot == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), g_pConnection->Escape(pController2->m_iszPlayerName()).c_str(), std::time(0), iTime, std::time(0)+iTime, sReason);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			m_vecPlayers[g_iTarget[iSlot]]->SetMuted(iTime, std::time(0)+iTime);
		}
		else if(!strcmp(g_szItem[iSlot], "gag"))
		{
			if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("GagPermanent"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Gag"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), iTime/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pController->m_steamID(), pController2->m_steamID(), iSlot == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), g_pConnection->Escape(pController2->m_iszPlayerName()).c_str(), std::time(0), iTime, std::time(0)+iTime, sReason);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			m_vecPlayers[g_iTarget[iSlot]]->SetGagged(iTime, std::time(0)+iTime);
		}
		else if(!strcmp(g_szItem[iSlot], "silence"))
		{
			if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("SilencePermanent"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Silence"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), iTime/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pController->m_steamID(), pController2->m_steamID(), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), std::time(0), iTime, std::time(0)+iTime, sReason);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pController->m_steamID(), pController2->m_steamID(), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName(), std::time(0), iTime, std::time(0)+iTime, sReason);
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			m_vecPlayers[g_iTarget[iSlot]]->SetMuted(iTime, std::time(0)+iTime);
			m_vecPlayers[g_iTarget[iSlot]]->SetGagged(iTime, std::time(0)+iTime);
		}
	}
	else if(iItem == 7) ReasonMenu(iSlot);
}

void ReasonMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_iReason[iSlot] = std::stoi(szBack);
		
		Menu hMenu;
		g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SelectTime")].c_str());
		
		char sBuff[32];
		for (auto it = g_Times.begin(); it != g_Times.end(); ++it) {
			g_SMAPI->Format(sBuff, sizeof(sBuff), "%i", it->first);
			g_pMenus->AddItemMenu(hMenu, sBuff, it->second.c_str());
		}
		
		g_pMenus->SetExitMenu(hMenu, true);
		g_pMenus->SetBackMenu(hMenu, true);
		g_pMenus->SetCallback(hMenu, TimeMenuHandle);
		g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
	}
	else if(iItem == 7) ShowPlayerList(iSlot);
}

void ReasonMenu(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SelectReason")].c_str());
	
	char sBuff[16];
	if(!strcmp(g_szItem[iSlot], "ban"))
	{
		for(int i=0; i < g_vecBanReasons.size(); i++){
			g_SMAPI->Format(sBuff, sizeof(sBuff), "%i", i);
			g_pMenus->AddItemMenu(hMenu, sBuff, g_vecBanReasons[i].c_str());
		}
	}
	else
	{
		for(int i=0; i < g_vecMuteReasons.size(); i++){
			g_SMAPI->Format(sBuff, sizeof(sBuff), "%i", i);
			g_pMenus->AddItemMenu(hMenu, sBuff, g_vecMuteReasons[i].c_str());
		}
	}
	
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, ReasonMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void SlapMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		int iDamage = std::stoi(szBack);
		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(g_iTarget[iSlot] + 1));

		CBasePlayerPawn *pPawn = pTarget->m_hPawn();

		if (!pPawn)
			return;

		Vector velocity = pPawn->m_vecAbsVelocity();
		velocity.x += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
		velocity.y += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
		velocity.z += rand() % 200 + 100;
		pPawn->SetAbsVelocity(velocity);
			
		if (iDamage > 0)
			pPawn->TakeDamage(iDamage);
		
		ClientPrintAll(g_AdminSystem.Translate("Slapped"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pTarget->m_iszPlayerName());
		if(pPawn->m_iHealth() > 0) g_pUtils->SetStateChanged(pPawn, "CBaseEntity", "m_iHealth");
	}
	else if(iItem == 7) ShowPlayerList(iSlot);
}

void SlapMenu(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SlapDamage")].c_str());

	g_pMenus->AddItemMenu(hMenu, "0", "0");
	g_pMenus->AddItemMenu(hMenu, "1", "1");
	g_pMenus->AddItemMenu(hMenu, "5", "5");
	g_pMenus->AddItemMenu(hMenu, "10", "10");
	g_pMenus->AddItemMenu(hMenu, "20", "20");
	g_pMenus->AddItemMenu(hMenu, "50", "50");
	g_pMenus->AddItemMenu(hMenu, "99", "99");
	
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, SlapMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void TeamMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_pMenus->ClosePlayerMenu(iSlot);
		int iTeam = std::stoi(szBack);

		const char *teams[] = {"none", g_vecPhrases[std::string("TeamSpec")].c_str(), g_vecPhrases[std::string("TeamT")].c_str(), g_vecPhrases[std::string("TeamCT")].c_str()};
		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(g_iTarget[iSlot] + 1));

		if (!pTarget)
			return;

		SwitchTeam(pTarget, iTeam);
		
		ClientPrintAll(g_AdminSystem.Translate("Moved"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pTarget->m_iszPlayerName(), teams[iTeam]);
	}
	else if(iItem == 7) ShowPlayerList(iSlot);
}

void TeamMenu(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SelectTeam")].c_str());

	g_pMenus->AddItemMenu(hMenu, "1", g_vecPhrases[std::string("TeamSpec")].c_str());
	g_pMenus->AddItemMenu(hMenu, "2", g_vecPhrases[std::string("TeamT")].c_str());
	g_pMenus->AddItemMenu(hMenu, "3", g_vecPhrases[std::string("TeamCT")].c_str());
	
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, TeamMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void PlayersMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_iTarget[iSlot] = std::stoi(szBack);
		auto pController2 = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(g_iTarget[iSlot] + 1));
		if (!pController2 || pController2->m_steamID() == 0 || !pController2->m_hPawn().Get())
		{
			g_pUtils->PrintToChat(iSlot, g_vecPhrases[std::string("Player Leave")].c_str());
			return;
		}
		if(!strcmp(g_szItem[iSlot], "ban") || !strcmp(g_szItem[iSlot], "mute") || !strcmp(g_szItem[iSlot], "gag") || !strcmp(g_szItem[iSlot], "silence"))
		{
			ReasonMenu(iSlot);
		}
		else if(!strcmp(g_szItem[iSlot], "kick"))
		{
			ClientPrintAll(g_AdminSystem.Translate("Kick"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			engine->DisconnectClient(CPlayerSlot(g_iTarget[iSlot]), NETWORK_DISCONNECT_KICKED);
			g_pMenus->ClosePlayerMenu(iSlot);
		}
		else if(!strcmp(g_szItem[iSlot], "slay"))
		{
			ClientPrintAll(g_AdminSystem.Translate("Slayed"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			pController2->m_hPlayerPawn().Get()->CommitSuicide(false, true);
		}
		else if(!strcmp(g_szItem[iSlot], "slap"))
		{
			SlapMenu(iSlot);
		}
		else if(!strcmp(g_szItem[iSlot], "who"))
		{
			CPlayer* pTargetPlayer = m_vecPlayers[g_iTarget[iSlot]];

			if (!pController2 || pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
				return;
				
			std::string flag;
			char szBuffer[256];
			uint64_t flags = pTargetPlayer->GetAdminFlags();
			if(!flags)
			{
				flag = "none";
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Access"), pController2->m_iszPlayerName(), flag.c_str());
			}
			else
			{
				if(flags & ADMFLAG_ROOT) flag = "root";
				else FlagsToString(flag, flags);
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AccessAdmin"), pController2->m_iszPlayerName(), pTargetPlayer->GetAdminName(), flag.c_str());
			}
			ClientPrint(iSlot, "%s", szBuffer);
		}
		else if(!strcmp(g_szItem[iSlot], "team"))
		{
			TeamMenu(iSlot);
		}
		else if(!strcmp(g_szItem[iSlot], "noclip"))
		{
			if (pController2->m_hPlayerPawn()->m_iHealth() <= 0)
			{
				ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("NoclipDead"));
				return;
			}
			if(pController2->m_hPlayerPawn()->m_MoveType() == MOVETYPE_NOCLIP)
			{
				pController2->m_hPlayerPawn()->m_MoveType() = MOVETYPE_WALK;
				ClientPrintAll(g_AdminSystem.Translate("NoclipDisable"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			}
			else
			{
				pController2->m_hPlayerPawn()->m_MoveType() = MOVETYPE_NOCLIP;
				ClientPrintAll(g_AdminSystem.Translate("NoclipEnable"), iSlot == -1?"Console":engine->GetClientConVarValue(iSlot, "name"), pController2->m_iszPlayerName());
			}
		}
	}
	else if(iItem == 7) AdminMenu(iSlot);
}

void ShowPlayerList(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SelectPlayer")].c_str());
	
	int iCount = 0;
	for (size_t i = 0; i < 64; i++)
	{
		CCSPlayerController* pController = static_cast<CCSPlayerController*>(g_pEntitySystem->GetBaseEntity(static_cast<CEntityIndex>(i + 1)));
		if (!pController)
			continue;
		
		uint32 m_steamID = pController->m_steamID();
		if (m_steamID == 0)
			continue;

		if(!pController->m_hPlayerPawn().Get())
			continue;
		
		if(g_pAdminCore->GetClientImmunity(iSlot) < g_pAdminCore->GetClientImmunity(i))
			continue;

		char sBuff[16], sBuff2[100];
		g_SMAPI->Format(sBuff, sizeof(sBuff), "%i", i);
		g_SMAPI->Format(sBuff2, sizeof(sBuff2), "%s", pController->m_iszPlayerName());
		g_pMenus->AddItemMenu(hMenu, sBuff, sBuff2);
		iCount++;
	}
	if(iCount)
	{
		g_pMenus->SetExitMenu(hMenu, true);
		g_pMenus->SetBackMenu(hMenu, true);
		g_pMenus->SetCallback(hMenu, PlayersMenuHandle);
		g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
	}
	else
	{
		g_pUtils->PrintToChat(iSlot, "Нет доступных \x0Cигроков!");
	}
}

void MapMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_pMenus->ClosePlayerMenu(iSlot);
		if (!engine->IsMapValid(szBack))
		{
			char sCommand[128];
			g_SMAPI->Format(sCommand, sizeof(sCommand), "ds_workshop_changelevel %s", szBack);
			char szBuffer[256];
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map workshop"), szFront);
			ClientPrint(iSlot,  "%s", szBuffer);
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), szFront);
			ClientPrintAll( "%s", szBuffer);

			new CTimer(5.0, [sCommand]()
			{		
				engine->ServerCommand(sCommand);
				return -1.0f;
			});
			return;
		}

		char szBuffer[256];
		g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), szBack);
		ClientPrintAll( "%s", szBuffer);
		
		new CTimer(5.0, [szBack]()
		{		
			engine->ChangeLevel(szBack, nullptr);
			return -1.0f;
		});
	}
	else if(iItem == 7) AdminMenu(iSlot);
}

void MapMenu(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("SelectMap")].c_str());
	
	char sBuff[32];
	for (auto it = g_vecMaps.begin(); it != g_vecMaps.end(); ++it) {
		g_pMenus->AddItemMenu(hMenu, it->first.c_str(), it->second.c_str());
	}
	
	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, true);
	g_pMenus->SetCallback(hMenu, MapMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void OnPlayerCommands(const char* szName, int iSlot)
{
	g_SMAPI->Format(g_szItem[iSlot], sizeof(g_szItem), szName);
	ShowPlayerList(iSlot);
}

void OnServerCommands(const char* szName, int iSlot)
{
	g_SMAPI->Format(g_szItem[iSlot], sizeof(g_szItem), szName);
	if(!strcmp(szName, "map"))
	{
		MapMenu(iSlot);
	}
}

void StartupServer()
{
	gpGlobals = g_pUtils->GetCGlobalVars();
	g_bHasTicked = false;
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
}

void AdminSystem::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pMysqlClient = (IMySQLClient *)g_SMAPI->MetaFactory(MYSQLMM_INTERFACE, &ret, NULL);

	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing MYSQL plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);

	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing Utils system plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pMenus = (IMenusApi *)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, NULL);

	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing Menus system plugin", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}

	const char* g_pszLanguage = g_pUtils->GetLanguage();
	{
		KeyValues* kvPhrases = new KeyValues("Phrases");
		const char *pszPath = "addons/translations/admin_system.phrases.txt";

		if (!kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			Warning("Failed to load %s\n", pszPath);
			return;
		}

		for (KeyValues *pKey = kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
			g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
		
		delete kvPhrases;
	}

	{
		KeyValues* kvMaps = new KeyValues("Maps");
		const char *pszPath = "addons/configs/maplist.ini";

		if (!kvMaps->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			Warning("Failed to load %s\n", pszPath);
			return;
		}

		FOR_EACH_VALUE(kvMaps, pValue)
		{
			g_vecMaps[std::string(pValue->GetName())] = std::string(pValue->GetString(nullptr, nullptr));
		}

		delete kvMaps;
	}

	
	{
		KeyValues* g_kvSettings = new KeyValues("Settings");
		const char *pszPath = "addons/configs/admin.ini";

		if (!g_kvSettings->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			Warning("Failed to load %s\n", pszPath);
			return;
		}

		KeyValues* pKVRule = g_kvSettings->FindKey("ban_reasons", false);
		if(pKVRule)
		{
			FOR_EACH_VALUE(pKVRule, pValue)
			{
				g_vecBanReasons.push_back(std::string(pValue->GetName()));
			}
		}
		pKVRule = g_kvSettings->FindKey("mute_gag_reasons", false);
		if(pKVRule)
		{
			FOR_EACH_VALUE(pKVRule, pValue)
			{
				g_vecMuteReasons.push_back(std::string(pValue->GetName()));
			}
		}
		pKVRule = g_kvSettings->FindKey("times", false);
		if(pKVRule)
		{
			FOR_EACH_VALUE(pKVRule, pValue)
			{
				g_Times[std::stoi(pValue->GetName())] = std::string(pValue->GetString(nullptr, nullptr));
			}
		}

		delete pKVRule;
		delete g_kvSettings;
	}

	g_pUtils->StartupServer(g_PLID, StartupServer);
	g_pUtils->AddChatListenerPre(g_PLID, ChatListener);
	g_pUtils->RegCommand(g_PLID, {}, {"!admin","!ban","!unban","!mute","!unmute","!gag","!ungag","!silence","!unsilence","!status","!kick","!who","!csay","!hsay","!rcon","!freeze","!unfreeze","!noclip","!setteam","!changeteam","!slap","!slay","!map","!reload_admins","!add_admin","!remove_admin"}, [](int iSlot, const char* szContent){
		CCSPlayerController* pPlayerController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
		g_AdminSystem.ParseChatCommand(iSlot, szContent+1, pPlayerController);
		return false;
	});

	g_pAdminCore->RegAdminCategory("PlayerCommands", g_vecPhrases[std::string("PlayerCommands")].c_str());
	g_pAdminCore->RegAdminCategory("ServerCommands", g_vecPhrases[std::string("ServerCommands")].c_str());
	// g_pAdminCore->RegAdminCategory("VotingCommands", g_vecPhrases[std::string("VotingCommands")].c_str());

	
	g_pAdminCore->RegAdminItem("PlayerCommands", "ban", g_vecPhrases[std::string("BanPlayer")].c_str(), 		ADMFLAG_BAN, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "mute", g_vecPhrases[std::string("MutePlayer")].c_str(), 		ADMFLAG_CHAT, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "gag", g_vecPhrases[std::string("GagPlayer")].c_str(), 		ADMFLAG_CHAT, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "silence", g_vecPhrases[std::string("SilencePlayer")].c_str(), ADMFLAG_CHAT, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "kick", g_vecPhrases[std::string("KickPlayer")].c_str(), 		ADMFLAG_KICK, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "slay", g_vecPhrases[std::string("SlayPlayer")].c_str(), 		ADMFLAG_SLAY, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "slap", g_vecPhrases[std::string("SlapPlayer")].c_str(), 		ADMFLAG_SLAY, 		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "who", g_vecPhrases[std::string("WhoPlayer")].c_str(), 		ADMFLAG_GENERIC,	OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "team", g_vecPhrases[std::string("ChangeTeamPlayer")].c_str(), ADMFLAG_SLAY,		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("PlayerCommands", "noclip", g_vecPhrases[std::string("NoclipPlayer")].c_str(),	ADMFLAG_CHEATS,		OnPlayerCommands);
	g_pAdminCore->RegAdminItem("ServerCommands", "map", g_vecPhrases[std::string("ChangeMap")].c_str(),			ADMFLAG_CHANGEMAP,	OnServerCommands);

	KeyValues* pKVConfig = new KeyValues("Databases");
	
	if (!pKVConfig->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg"))
	{
		V_strncpy(error, "Failed to load admin_system config 'addons/config/databases.cfg'", 64);
		return;
	}
	pKVConfig = pKVConfig->FindKey("admin_system", false);
	if(!pKVConfig)
	{
		V_strncpy(error, "No databases.cfg 'admin_system'", 64);
		return;
	}
	MySQLConnectionInfo info;
	info.host = pKVConfig->GetString("host", nullptr);
	info.user = pKVConfig->GetString("user", nullptr);
	info.pass = pKVConfig->GetString("pass", nullptr);
	info.database = pKVConfig->GetString("database", nullptr);
	info.port = pKVConfig->GetInt("port");
	g_pConnection = g_pMysqlClient->CreateMySQLConnection(info);

	g_pConnection->Connect([this](bool connect)
	{
		if (!connect)
		{
			META_CONPRINT("Failed to connect the mysql database\n");
		} else {
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_admins`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `flags` VARCHAR(64) NOT NULL, `immunity` INTEGER NOT NULL, `end` INTEGER NOT NULL, `comment` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_bans`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_gags`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_mutes`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT,`admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
		}
	});
}

const char *AdminSystem::Translate(const char* phrase)
{
    return g_vecPhrases[std::string(phrase)].c_str();
}

void* AdminSystem::OnMetamodQuery(const char* iface, int* ret)
{
	if (!strcmp(iface, Admin_INTERFACE))
	{
		*ret = META_IFACE_OK;
		return g_pAdminCore;
	}

	*ret = META_IFACE_FAILED;
	return nullptr;
}

bool AdminSystem::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pCSchemaSystem, CSchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceServiceServer, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientConnect, g_pSource2GameClients, this, &AdminSystem::Hook_ClientConnect, false );
	
	ConVar_Register(FCVAR_GAMEDLL);

	CModule libserver(g_pSource2Server);
	CModule libengine(engine);
	UTIL_SwitchTeam = libserver.FindPatternSIMD(WIN_LINUX("40 56 57 48 81 EC 2A 2A 2A 2A 48 8B F9 8B F2 8B CA", "55 48 89 E5 41 55 49 89 FD 89 F7")).RCast< decltype(UTIL_SwitchTeam) >();
	if (!UTIL_SwitchTeam)
	{
		V_strncpy(error, "Failed to find function to get UTIL_SwitchTeam", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return false;
	}
	UTIL_IsHearingClient = libengine.FindPatternSIMD(WIN_LINUX("40 53 48 83 EC 20 48 8B D9 3B 91 C0 00 00 00", "55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 08 3B B7")).RCast< decltype(UTIL_IsHearingClient) >();
	if (!UTIL_IsHearingClient)
	{
		V_strncpy(error, "Failed to find function to get UTIL_IsHearingClient", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return false;
	}
	m_IsHearingClient = funchook_create();
	funchook_prepare(m_IsHearingClient, (void**)&UTIL_IsHearingClient, (void*)IsHearingClient);
	funchook_install(m_IsHearingClient, 0);

	g_pAdminApi = new AdminApi();
	g_pAdminCore = g_pAdminApi;

	return true;
}

bool AdminSystem::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientConnect, g_pSource2GameClients, this, &AdminSystem::Hook_ClientConnect, false);

	ConVar_Unregister();
	RemoveTimers();

	if (g_pConnection)
		g_pConnection->Destroy();
	
	return true;
}

bool AdminSystem::Hook_ClientConnect( CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason )
{
	if(m_vecPlayers[slot.Get()] != nullptr)
	{
		delete m_vecPlayers[slot.Get()];
		m_vecPlayers[slot.Get()] = nullptr;
	}
	if(xuid <= 0) m_vecPlayers[slot.Get()] = new CPlayer(slot, true);
	else m_vecPlayers[slot.Get()] = new CPlayer(slot, false);

	m_vecPlayers[slot.Get()]->SetSteamID(xuid);
	CheckInfractions(slot.Get(), false);

	RETURN_META_VALUE(MRES_IGNORED, true);
}

void AdminSystem::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
    if (simulating && g_bHasTicked)
	{
		g_flUniversalTime += gpGlobals->curtime - g_flLastTickedTime;
	}
	else
	{
		g_flUniversalTime += gpGlobals->interval_per_tick;
	}

	g_flLastTickedTime = gpGlobals->curtime;
	g_bHasTicked = true;

	for (int i = g_timers.Tail(); i != g_timers.InvalidIndex();)
	{
		auto timer = g_timers[i];

		int prevIndex = i;
		i = g_timers.Previous(i);

		if (timer->m_flLastExecute == -1)
			timer->m_flLastExecute = g_flUniversalTime;

		// Timer execute 
		if (timer->m_flLastExecute + timer->m_flInterval <= g_flUniversalTime)
		{
			if (!timer->Execute())
			{
				delete timer;
				g_timers.Remove(prevIndex);
			}
			else
			{
				timer->m_flLastExecute = g_flUniversalTime;
			}
		}
	}

	if(g_iLastTime == 0) g_iLastTime = std::time(0);
	else if(std::time(0) - g_iLastTime >= 1 && g_pEntitySystem)
	{
		g_iLastTime = std::time(0);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pPlayerController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));
			if(!pPlayerController)
				continue;
			if(pPlayerController->m_steamID() == 0)
				continue;
			if(!pPlayerController->m_hPlayerPawn())
				continue;
			if(!pPlayerController->m_hPawn())
				continue;
			CPlayerSlot pSlot = CPlayerSlot(i);
			CPlayer* pPlayer = m_vecPlayers[i];
			if(pPlayer == nullptr)
				continue;
			if (pPlayer->IsAuthenticated() || pPlayer->IsFakeClient())
				continue;

			if (engine->IsClientFullyAuthenticated(CPlayerSlot(i)))
			{
				pPlayer->SetAuthenticated();
				pPlayer->SetSteamID(engine->GetClientXUID(i));
				g_AdminSystem.CheckInfractions(i, true);
			}
		}
	}
}

bool AdminSystem::CheckImmunity(int iTarget, int iAdmin)
{
	if(m_vecPlayers[iTarget] && m_vecPlayers[iAdmin] && m_vecPlayers[iTarget]->GetAdminImmunity() > m_vecPlayers[iAdmin]->GetAdminImmunity())
	{
		ClientPrint(iAdmin, "%s", g_AdminSystem.Translate("Target immunity"));
		return false;
	}
	return true;
}

void PunishmentPlayer(int iSlot, const CCommand &args, CCSPlayerController *player, int iType)
{
	if (args.ArgC() < 4)
	{
		switch (iType)
		{
			case 0:
			{
				ClientPrint(iSlot, "%s!ban <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 1:
			{
				ClientPrint(iSlot, "%s!mute <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 2:
			{
				ClientPrint(iSlot,  "%s!gag <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 3:
			{
				ClientPrint(iSlot,  "%s!silence <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
		}
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Invalid duration"));
		return;
	}

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CPlayer* pTargetPlayer = m_vecPlayers[iTarget];

	if (pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
		return;

	CPlayer* pPlayer = m_vecPlayers[iSlot];
	
	if (pPlayer == nullptr || pPlayer->IsFakeClient())
		return;

	std::string sReason = std::string(args.ArgS());
	int val = sReason.find(args[1]);
    if(val != -1) sReason.replace(val, strlen(args[1]), "");
	val = sReason.find(args[2]);
    if(val != -1)
	{
		sReason.replace(val, strlen(args[2]), "");
		sReason.erase(0, 2);
	}
	else sReason.erase(0, 1);

	char szQuery[512];
	switch (iType)
	{
		case 0:
		{
			if(iDuration == 0) ClientPrintAll(g_AdminSystem.Translate("BanPermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Ban"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamID(), pTargetPlayer->GetSteamID(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			engine->DisconnectClient(CPlayerSlot(iTarget), NETWORK_DISCONNECT_KICKBANADDED);
			break;
		}
		case 1:
		{
			if(iDuration == 0) ClientPrintAll(g_AdminSystem.Translate("MutePermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Mute"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamID(), pTargetPlayer->GetSteamID(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetMuted(iDuration*60, std::time(0)+iDuration*60);
			break;
		}
		case 2:
		{
			if(iDuration == 0) ClientPrintAll(g_AdminSystem.Translate("GagPermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Gag"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamID(), pTargetPlayer->GetSteamID(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetGagged(iDuration*60, std::time(0)+iDuration*60);
			break;
		}
		case 3:
		{
			if(iDuration == 0) ClientPrintAll(g_AdminSystem.Translate("SilencePermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(g_AdminSystem.Translate("Silence"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamID(), pTargetPlayer->GetSteamID(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamID(), pTargetPlayer->GetSteamID(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetMuted(iDuration*60, std::time(0)+iDuration*60);
			pTargetPlayer->SetGagged(iDuration*60, std::time(0)+iDuration*60);
			break;
		}
	}
}

void UnPunishmentPlayer(int iSlot, const CCommand &args, CCSPlayerController *player, int iType)
{
	if (args.ArgC() < 2)
	{
		switch (iType)
		{
			case 0:
			{
				ClientPrint(iSlot, " %s!unban <steamid>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 1:
			{
				ClientPrint(iSlot, "%s!unmute <#userid|name>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 2:
			{
				ClientPrint(iSlot,  "%s!ungag <#userid|name>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 3:
			{
				ClientPrint(iSlot,  "%s!unsilence <#userid|name>", g_AdminSystem.Translate("Usage"));
				break;
			}
		}
		return;
	}

	char szQuery[512];
	if(iType != 0)
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
			return;
		}

		CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		if (!pTarget)
			return;
		CPlayer* pTargetPlayer = m_vecPlayers[iTarget];
		if (pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
			return;
		CPlayer* pPlayer = m_vecPlayers[iSlot];
		if(pPlayer == nullptr || pPlayer->IsFakeClient())
			return;

		switch (iType)
		{
			case 1:
			{
				ClientPrintAll(g_AdminSystem.Translate("UnMuted"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamID(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetMuted(-1, -1);
				break;
			}
			case 2:
			{
				ClientPrintAll(g_AdminSystem.Translate("UnGagged"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamID(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetGagged(-1, -1);
				break;
			}
			case 3:
			{
				ClientPrintAll(g_AdminSystem.Translate("UnSilence"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamID(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamID(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetMuted(-1, -1);
				pTargetPlayer->SetGagged(-1, -1);
				break;
			}
		}
	}
	else
	{
		//Message
		g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_bans` WHERE `steamid` = '%s' AND (`end` > %i OR `duration` = 0)", args[1], std::time(0));
		g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
	}
}

void AdminItemMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		if(g_pAdminApi->m_Items[std::string(g_szCategory[iSlot])][std::string(szBack)].callback)
			g_pAdminApi->m_Items[std::string(g_szCategory[iSlot])][std::string(szBack)].callback(szBack, iSlot);
	}
}

void DisplayCategoryItems(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string(g_szCategory[iSlot])].c_str());

	for (auto it = g_pAdminApi->m_Items[std::string(g_szCategory[iSlot])].begin(); it != g_pAdminApi->m_Items[std::string(g_szCategory[iSlot])].end(); ++it) {
		AdminItem& adminItem = it->second;
		
		if(g_AdminSystem.IsAdminFlagSet(iSlot, adminItem.iFlag)) {
			g_pMenus->AddItemMenu(hMenu, it->first.c_str(), adminItem.szFront.c_str());
		}
	}

	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, false);
	g_pMenus->SetCallback(hMenu, AdminItemMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

void AdminCategoryMenuHandle(const char* szBack, const char* szFront, int iItem, int iSlot)
{
	if(iItem < 7)
	{
		g_SMAPI->Format(g_szCategory[iSlot], sizeof(g_szCategory), szBack);
		if(g_pAdminApi->m_Categories[std::string(szBack)].callback)
			g_pAdminApi->m_Categories[std::string(szBack)].callback(szBack, iSlot);
		DisplayCategoryItems(iSlot);
	}
}

void AdminMenu(int iSlot)
{
	Menu hMenu;
	g_pMenus->SetTitleMenu(hMenu, g_vecPhrases[std::string("Admin Menu")].c_str());

	for (auto it = g_pAdminApi->m_Categories.begin(); it != g_pAdminApi->m_Categories.end(); ++it) {
		AdminCategory& adminCategory = it->second;
		g_pMenus->AddItemMenu(hMenu, it->first.c_str(), adminCategory.szFront.c_str());
	}

	g_pMenus->SetExitMenu(hMenu, true);
	g_pMenus->SetBackMenu(hMenu, false);
	g_pMenus->SetCallback(hMenu, AdminCategoryMenuHandle);
	g_pMenus->DisplayPlayerMenu(hMenu, iSlot);
}

CON_COMMAND_CHAT(admin, "shows available admin commands")
{
	if(iSlot != -1 && g_AdminSystem.IsAdmin(iSlot))
	{
		AdminMenu(iSlot);
	}
	else 
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("DenyAccess"));
}

CON_COMMAND_CHAT_FLAGS(offban, "offban", ADMFLAG_BAN)
{
	if (args.ArgC() < 4)
	{
		ClientPrint(iSlot, "%s!offban <steam64> <nick> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
		return;
	}
	g_pAdminCore->OfflineBanPlayer(std::stoull(args[1]), args[2], iSlot, std::stoull(args[3]), args[4]);
	if(iSlot >= 0)
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("OfflineBan"));
}

CON_COMMAND_CHAT_FLAGS(rename, "rename", ADMFLAG_KICK)
{
	if (args.ArgC() < 3)
	{
		ClientPrint(iSlot,  "%s!rename <#userid|name> <name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1 || !m_vecPlayers[iTarget] || !m_vecPlayers[iSlot])
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(g_AdminSystem.Translate("RenameChat"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), args[2]);
	g_SMAPI->Format(pTarget->m_iszPlayerName(), 128, args[2]);
	g_pUtils->SetStateChanged(pTarget, "CBasePlayerController", "m_iszPlayerName");
}

CON_COMMAND_CHAT_FLAGS(ban, "ban a player", ADMFLAG_BAN)
{
	PunishmentPlayer(iSlot, args, player, 0);
}

CON_COMMAND_CHAT_FLAGS(unban, "unban a player", ADMFLAG_UNBAN)
{
	UnPunishmentPlayer(iSlot, args, player, 0);
}

CON_COMMAND_CHAT_FLAGS(mute, "mutes a player", ADMFLAG_CHAT)
{
	PunishmentPlayer(iSlot, args, player, 1);
}

CON_COMMAND_CHAT_FLAGS(unmute, "unmutes a player", ADMFLAG_CHAT)
{
	
	UnPunishmentPlayer(iSlot, args, player, 1);
}

CON_COMMAND_CHAT_FLAGS(gag, "gag a player", ADMFLAG_CHAT)
{
	PunishmentPlayer(iSlot, args, player, 2);
}

CON_COMMAND_CHAT_FLAGS(ungag, "ungags a player", ADMFLAG_CHAT)
{
	
	UnPunishmentPlayer(iSlot, args, player, 2);
}

CON_COMMAND_CHAT_FLAGS(silence, "silence a player", ADMFLAG_CHAT)
{
	PunishmentPlayer(iSlot, args, player, 3);
}

CON_COMMAND_CHAT_FLAGS(unsilence, "unsilence a player", ADMFLAG_CHAT)
{
	UnPunishmentPlayer(iSlot, args, player, 3);
}

CON_COMMAND_CHAT(status, "status")
{
	bool bFound = false;
	char szBuffer[256];
	for (int i = 0; i < 64; i++)
	{
		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

		if(!pTarget)
			continue;

		CPlayer* pTargetPlayer = m_vecPlayers[i];

		if (pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
			continue;

		g_SMAPI->Format(szBuffer, sizeof(szBuffer), "%i.%s", i, pTarget->m_iszPlayerName());
		ClientPrint(iSlot, "%s", szBuffer);
		bFound = true;
	}
	if(!bFound) ClientPrint(iSlot, "%s", g_AdminSystem.Translate("AccessNoPlayers"));
}

CON_COMMAND_CHAT_FLAGS(kick, "kick a player", ADMFLAG_KICK)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%s!kick <#userid|name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1 || !m_vecPlayers[iTarget] || !m_vecPlayers[iSlot])
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(g_AdminSystem.Translate("Kick"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	engine->DisconnectClient(CPlayerSlot(iTarget), NETWORK_DISCONNECT_KICKED);
}

CON_COMMAND_CHAT_FLAGS(who, "recognize a player", ADMFLAG_GENERIC)
{
	if (args.ArgC() < 2)
	{
		bool bFound = false;
		char szBuffer[256];
		for (int i = 0; i < gpGlobals->maxClients; i++)
		{
			if(engine->GetPlayerUserId(i).Get() == -1) continue;
			CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

			CPlayer* pTargetPlayer = m_vecPlayers[i];

			if(!pTarget || pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
				continue;

			std::string flag;
			uint64_t flags = pTargetPlayer->GetAdminFlags();
			if(!flags)
			{
				flag = "none";
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Access"), pTarget->m_iszPlayerName(), flag.c_str());
			}
			else
			{
				if(flags & ADMFLAG_ROOT) flag = "root";
				else FlagsToString(flag, flags);
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AccessAdmin"), pTarget->m_iszPlayerName(), pTargetPlayer->GetAdminName(), flag.c_str());
			}
			ClientPrint(iSlot, "%s", szBuffer);
			bFound = true;
		}
		if(!bFound) ClientPrint(iSlot, "%s", g_AdminSystem.Translate("AccessNoPlayers"));
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
			return;
		}

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		CPlayer* pTargetPlayer = m_vecPlayers[iTarget];

		if (!pTarget || pTargetPlayer == nullptr || pTargetPlayer->IsFakeClient())
			return;
			
		std::string flag;
		char szBuffer[256];
		uint64_t flags = pTargetPlayer->GetAdminFlags();
		if(!flags)
		{
			flag = "none";
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Access"), pTarget->m_iszPlayerName(), flag.c_str());
		}
		else
		{
			if(flags & ADMFLAG_ROOT) flag = "root";
			else FlagsToString(flag, flags);
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AccessAdmin"), pTarget->m_iszPlayerName(), pTargetPlayer->GetAdminName(), flag.c_str());
		}
		ClientPrint(iSlot, "%s", szBuffer);
	}
}

CON_COMMAND_CHAT_FLAGS(rcon, "send a command to server console", ADMFLAG_RCON)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
	CBasePlayerPawn* pPlayerPawn = pController->m_hPlayerPawn().Get();
	META_CONPRINTF("%i | %i | %i\n", player, pController, pPlayerPawn);
	if (!player)
	{
		ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("You console"));
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%s!rcon <command>", g_AdminSystem.Translate("Usage"));
		return;
	}

	engine->ServerCommand(args.ArgS());
}

// CON_COMMAND_CHAT_FLAGS(freeze, "freeze a player", ADMFLAG_SLAY)
// {
// 	if (args.ArgC() < 3)
// 	{	
// 		ClientPrint(iSlot,  "%s!freeze <#userid|name> <duration>", g_AdminSystem.Translate("Usage"));
// 		return;
// 	}

// 	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

// 	if (iTarget == -1)
// 	{
// 		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
// 		return;
// 	}

// 	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
// 		return;

// 	int iDuration = V_StringToInt32(args[2], -1);

// 	if (iDuration == -1)
// 	{
// 		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Invalid duration"));
// 		return;
// 	}

// 	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

// 	if (!pTarget)
// 		return;

// 	CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

// 	if(!pPlayer)
// 		return;

// 	pPlayer->m_MoveType() = MOVETYPE_NONE;
// 	g_AdminSystem.CreateTimer([pPlayer]()
// 	{
// 		pPlayer->m_MoveType() = MOVETYPE_WALK;
// 	}, iDuration);
// 	ClientPrintAll(g_AdminSystem.Translate("Freeze"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
// }

// CON_COMMAND_CHAT_FLAGS(unfreeze, "unfreeze a player", ADMFLAG_SLAY)
// {
// 	if (args.ArgC() < 2)
// 	{	
// 		ClientPrint(iSlot,  "%s!unfreeze <#userid|name>", g_AdminSystem.Translate("Usage"));
// 		return;
// 	}

// 	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

// 	if (iTarget == -1)
// 	{
// 		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
// 		return;
// 	}

// 	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
// 		return;

// 	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

// 	if (!pTarget)
// 		return;

// 	CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

// 	if(!pPlayer)
// 		return;

// 	pPlayer->m_MoveType() = MOVETYPE_WALK;
// 	ClientPrintAll(g_AdminSystem.Translate("Unfreeze"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
// }

CON_COMMAND_CHAT_FLAGS(noclip, "noclip a player", ADMFLAG_CHEATS)
{
	if (args.ArgC() < 2)
	{
		CBasePlayerPawn* pPlayer = player->m_hPawn();

		if(!pPlayer)
			return;

		if (pPlayer->m_iHealth() <= 0)
		{
			ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("NoclipDead"));
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			ClientPrintAll(g_AdminSystem.Translate("ANoclipDisable"), iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_WALK;
		}
		else
		{
			ClientPrintAll(g_AdminSystem.Translate("ANoclipEnable"), iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
		}
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
			return;
		}

		if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
			return;

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		if (!pTarget)
			return;

		CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

		if(!pPlayer)
			return;

		if (pPlayer->m_iHealth() <= 0)
		{
			ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("NoclipDead"));
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			pPlayer->m_MoveType() = MOVETYPE_WALK;
			ClientPrintAll(g_AdminSystem.Translate("NoclipDisable"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
		else
		{
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
			ClientPrintAll(g_AdminSystem.Translate("NoclipEnable"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
	}
}

CON_COMMAND_CHAT_FLAGS(setteam, "set a player's team(without death)", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(iSlot,  "%s!setteam <#userid|name> <team (0-3)>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	int iTeam = V_StringToInt32(args[2], -1);
	const char *teams[] = {"none", "spectators", "terrorists", "counter-terrorists"};
	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("Invalid Team"));
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	SwitchTeam(pTarget, iTeam);
	
	ClientPrintAll(g_AdminSystem.Translate("Moved"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), teams[iTeam]);
}

CON_COMMAND_CHAT_FLAGS(changeteam, "set a player's team(with death)", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(iSlot,  "%s!changeteam <#userid|name> <team (0-3)>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	int iTeam = V_StringToInt32(args[2], -1);
	const char *teams[] = {"none", "spectators", "terrorists", "counter-terrorists"};
	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("Invalid Team"));
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	pTarget->ChangeTeam(iTeam);
	
	ClientPrintAll(g_AdminSystem.Translate("Moved"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), teams[iTeam]);
}

CON_COMMAND_CHAT_FLAGS(slap, "slap a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%s!slap <#userid|name> <optional damage>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	CBasePlayerController *pTarget = (CBasePlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CBasePlayerPawn *pPawn = pTarget->m_hPawn();

	if (!pPawn)
		return;

	Vector velocity = pPawn->m_vecAbsVelocity();
	velocity.x += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
	velocity.y += ((rand() % 180) + 50) * (((rand() % 2) == 1) ? -1 : 1);
	velocity.z += rand() % 200 + 100;
	pPawn->SetAbsVelocity(velocity);

	int iDamage = V_StringToInt32(args[2], 0);
		
	if (iDamage > 0)
		pPawn->TakeDamage(iDamage);
	
	ClientPrintAll(g_AdminSystem.Translate("Slapped"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	if(pPawn->m_iHealth() > 0) g_pUtils->SetStateChanged(pPawn, "CBaseEntity", "m_iHealth");
}

CON_COMMAND_CHAT_FLAGS(slay, "slay a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%s!slay <#userid|name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot))
		return;

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(g_AdminSystem.Translate("Slayed"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	pTarget->m_hPlayerPawn().Get()->CommitSuicide(false, true);
}

CON_COMMAND_CHAT_FLAGS(map, "change map", ADMFLAG_CHANGEMAP)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot, "%s!map <mapname>", g_AdminSystem.Translate("Usage"));
		return;
	}

	char szMapName[MAX_PATH];
	V_strncpy(szMapName, args[1], sizeof(szMapName));

	if (!engine->IsMapValid(szMapName))
	{
		char sCommand[128];
		g_SMAPI->Format(sCommand, sizeof(sCommand), "host_workshop_map %s", args[1]);
		char szBuffer[256];
		g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map workshop"), args[1]);
		ClientPrint(iSlot,  "%s", szBuffer);
		g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), args[1]);
		ClientPrintAll( "%s", szBuffer);
		new CTimer(5.0, [sCommand]()
		{		
			engine->ServerCommand(sCommand);
			return -1.0f;
		});
		return;
	}

	char szBuffer[256];
	g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), szMapName);
	ClientPrintAll( "%s", szBuffer);
	
	new CTimer(5.0, [szMapName]()
	{		
		engine->ChangeLevel(szMapName, nullptr);
		return -1.0f;
	});
}

CON_COMMAND_CHAT_FLAGS(add_admin, "add admin", ADMFLAG_ROOT)
{
	if (args.ArgC() < 6)
	{
		ClientPrint(iSlot,  "%s!add_admin <admin_name> <steamid64> <duration(minutes)/0 (permanent)> <flags> <immunity> <optional comment>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iDuration = V_StringToInt32(args[3], -1);

	if (iDuration == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Invalid duration"));
		return;
	}

	int iImmunity = V_StringToInt32(args[5], -1);

	if (iImmunity == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Invalid immunity"));
		return;
	}

	std::string sComment;
	if(args[6][0])
	{
		sComment = std::string(args.ArgS());
		int val;
		for (size_t i = 0; i < args.ArgC()-1; i++)
		{
			val = sComment.find(args[i]);
			if(val != -1) sComment.replace(val, strlen(args[i]), "");
		}
		sComment.erase(0, args.ArgC()-2);
	}

	char szQuery[512], szQuery2[512], szQuery3[512];
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT * FROM `as_admins` WHERE `steamid` = '%s';", args[2]);
	g_SMAPI->Format(szQuery2, sizeof(szQuery2), "UPDATE `as_admins` SET `name` = '%s', `flags` = '%s', `immunity` = '%i', `end` = '%lld', `comment` = '%s' WHERE steamid = '%s';", g_pConnection->Escape(args[1]).c_str(), args[4], iImmunity, iDuration == 0?0:std::time(0)+iDuration*60, sComment.c_str(), args[2]);
	g_SMAPI->Format(szQuery3, sizeof(szQuery3), "INSERT INTO `as_admins` (`steamid`, `name`, `flags`, `immunity`, `end`, `comment`) VALUES ('%s', '%s', '%s', '%i', '%lld', '%s');", args[2], g_pConnection->Escape(args[1]).c_str(), args[4], iImmunity, iDuration == 0?0:std::time(0)+iDuration*60, sComment.c_str());
	g_pConnection->Query(szQuery, [arg = args[1], szQuery2, szQuery3, iSlot](IMySQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->GetRowCount())
		{
			ClientPrint(iSlot,  "%s", g_AdminSystem.Translate("UpdateAdmin"));
			g_pConnection->Query(szQuery2, [](IMySQLQuery* test){});
		}
		else
		{
			char szBuffer[256];
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AddAdmin"), arg);
			ClientPrint(iSlot,  "%s", szBuffer);
			g_pConnection->Query(szQuery3, [](IMySQLQuery* test){});
		}
	});
}

CON_COMMAND_CHAT_FLAGS(remove_admin, "remove admin", ADMFLAG_ROOT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%s!remove_admin <steamid64>", g_AdminSystem.Translate("Usage"));
		return;
	}

	char szQuery[512], szBuffer[256];
	g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("RemoveAdmin"), args[1]);
	ClientPrint(iSlot,  "%s", szBuffer);

	g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_admins` WHERE `steamid` = '%s';", args[1]);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
}

CON_COMMAND_CHAT_FLAGS(reload_infractions, "reload infractions", ADMFLAG_ROOT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(iSlot,  "%smm_reload_infractions <steamid64>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(iSlot, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	g_AdminSystem.CheckInfractions(iTarget, true);
}

bool AdminApi::ClientIsAdmin(int iSlot)
{
	return g_AdminSystem.IsAdmin(iSlot);
}

int AdminApi::GetClientAdminFlags(int iSlot)
{
	return m_vecPlayers[iSlot]->GetAdminFlags();
}

int AdminApi::ReadFlagString(const char* szFlags)
{
	return g_AdminSystem.ParseFlags(szFlags);
}

bool AdminApi::IsAdminFlagSet(int iSlot, int iFlag)
{
	return g_AdminSystem.IsAdminFlagSet(iSlot, iFlag);
}

int AdminApi::GetClientImmunity(int iSlot) {
	return m_vecPlayers[iSlot]->GetAdminImmunity();
}

void AdminApi::BanPlayer(int iSlot, int iAdmin, int iTime, const char* szReason)
{
	char szQuery[512];
	if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("BanPermanent"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"));
	else ClientPrintAll(g_AdminSystem.Translate("Ban"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"), iTime/60);
	g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iAdmin == -1?0:m_vecPlayers[iAdmin]->GetSteamID(), m_vecPlayers[iSlot]->GetSteamID(), iAdmin == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iAdmin, "name")).c_str(), g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), std::time(0), iTime, std::time(0)+iTime, szReason);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
	engine->DisconnectClient(CPlayerSlot(iSlot), NETWORK_DISCONNECT_KICKBANADDED);
}

void AdminApi::MutePlayer(int iSlot, int iAdmin, int iTime, const char* szReason)
{
	char szQuery[512];
	if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("MutePermanent"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"));
	else ClientPrintAll(g_AdminSystem.Translate("Mute"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"), iTime/60);
	g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iAdmin == -1?0:m_vecPlayers[iAdmin]->GetSteamID(), m_vecPlayers[iSlot]->GetSteamID(), iAdmin == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iAdmin, "name")).c_str(), g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), std::time(0), iTime, std::time(0)+iTime, szReason);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
	m_vecPlayers[iSlot]->SetMuted(iTime, std::time(0)+iTime);
}

void AdminApi::GagPlayer(int iSlot, int iAdmin, int iTime, const char* szReason)
{
	char szQuery[512];
	if(iTime == 0) ClientPrintAll(g_AdminSystem.Translate("GagPermanent"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"));
	else ClientPrintAll(g_AdminSystem.Translate("Gag"), iAdmin == -1?"Console":engine->GetClientConVarValue(iAdmin, "name"), engine->GetClientConVarValue(iSlot, "name"), iTime/60);
	g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iAdmin == -1?0:m_vecPlayers[iAdmin]->GetSteamID(), m_vecPlayers[iSlot]->GetSteamID(), iAdmin == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iAdmin, "name")).c_str(), g_pConnection->Escape(engine->GetClientConVarValue(iSlot, "name")).c_str(), std::time(0), iTime, std::time(0)+iTime, szReason);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
	m_vecPlayers[iSlot]->SetGagged(iTime*60, std::time(0)+iTime*60);
}

bool AdminApi::ClientInMuted(int iSlot)
{
	return m_vecPlayers[iSlot]->IsMuted();
}

bool AdminApi::ClientInGagged(int iSlot)
{
	return m_vecPlayers[iSlot]->IsGagged();
}

void AdminApi::OfflineBanPlayer(uint64_t SteamID, const char* szNick, int iAdmin, int iTime, const char* szReason)
{
	char szQuery[512];
	g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iAdmin == -1?0:m_vecPlayers[iAdmin]->GetSteamID(), SteamID, iAdmin == -1?"Console":g_pConnection->Escape(engine->GetClientConVarValue(iAdmin, "name")).c_str(), g_pConnection->Escape(szNick).c_str(), std::time(0), iTime, std::time(0)+iTime, szReason);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
}

///////////////////////////////////////
const char* AdminSystem::GetLicense()
{
	return "GPL";
}

const char* AdminSystem::GetVersion()
{
	return "2.5.5";
}

const char* AdminSystem::GetDate()
{
	return __DATE__;
}

const char *AdminSystem::GetLogTag()
{
	return "Admin System";
}

const char* AdminSystem::GetAuthor()
{
	return "Pisex";
}

const char* AdminSystem::GetDescription()
{
	return "Simple Admin System";
}

const char* AdminSystem::GetName()
{
	return "AdminSystem";
}

const char* AdminSystem::GetURL()
{
	return "";
}
