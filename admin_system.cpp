#include <stdio.h>
#include "admin_system.h"
#include "metamod_oslink.h"

AdminSystem g_AdminSystem;
PLUGIN_EXPOSE(AdminSystem, g_AdminSystem);

INetworkGameServer *g_pNetworkGameServer = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
IGameEventManager2* gameeventmanager = nullptr;
CSchemaSystem* g_pCSchemaSystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
IVEngineServer2* engine = nullptr;
CGlobalVars *gpGlobals = nullptr;

CUtlMap<uint32, CChatCommand *> g_CommandList(0, 0, DefLessFunc(uint32));

IMySQLClient *g_pMysqlClient;
IMySQLConnection* g_pConnection;

int g_iLastTime;


class GameSessionConfiguration_t { };
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, int, const char *, uint64, const char *);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandHandle, const CCommandContext&, const CCommand&);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);

void (*UTIL_ClientPrint)(CBasePlayerController *player, int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;
void (*UTIL_ClientPrintAll)(int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;
bool (*UTIL_IsHearingClient)(void* serverClient, int index) = nullptr;

funchook_t* m_IsHearingClient;

bool FASTCALL IsHearingClient(void* serverClient, int index)
{
	CPlayer* player = g_AdminSystem.GetPlayer(index);
	if (player && player->IsMuted())
		return false;

	return UTIL_IsHearingClient(serverClient, index);
}

void ClientPrintAll(int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	UTIL_ClientPrintAll(hud_dest, buf, nullptr, nullptr, nullptr, nullptr);
	ConMsg("%s\n", buf);
}

void ClientPrint(CBasePlayerController *player, int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);

	va_end(args);

	if (player)
		UTIL_ClientPrint(player, hud_dest, buf, nullptr, nullptr, nullptr, nullptr);
	else
		ConMsg("%s\n", buf);
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

CPlayer *AdminSystem::GetPlayer(int slot)
{
	return m_vecPlayers[slot];
};

int AdminSystem::TargetPlayerString(const char* target)
{	
	if (*target == '#')
	{
		int userid = V_StringToUint16(target + 1, -1);
		CPlayerUserId PlayerUserID = engine->GetPlayerUserId(userid);
		if (PlayerUserID.Get() != -1)
		{
			return PlayerUserID.Get();
		}
	}
	else
	{
		for (int i = 0; i < gpGlobals->maxClients; i++)
		{
			if (m_vecPlayers[i] == nullptr)
				continue;

			CCSPlayerController* player = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

			if (!player)
				continue;

			if (V_stristr(player->m_iszPlayerName(), target))
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

	CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(iSlot);
	CAdmin& foundAdmin = *foundAdminPtr;
	if (foundAdminPtr == nullptr || !g_AdminSystem.IsAdminFlagSet(foundAdmin, flags, iSlot))
	{
		UTIL_ClientPrint(pPlayer, HUD_PRINTTALK, "You don't have access to this command.", nullptr, nullptr, nullptr, nullptr);
		return false;
	}

	return true;
}

void AdminSystem::Hook_DispatchConCommand(ConCommandHandle cmdHandle, const CCommandContext& ctx, const CCommand& args)
{
	if (!g_pEntitySystem)
		return;

	auto iCommandPlayerSlot = ctx.GetPlayerSlot();

	bool bSay = !V_strcmp(args.Arg(0), "say");
	bool bTeamSay = !V_strcmp(args.Arg(0), "say_team");

	if (iCommandPlayerSlot != -1 && (bSay || bTeamSay))
	{
		auto pController = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iCommandPlayerSlot.Get() + 1));
		bool bGagged = m_vecPlayers[iCommandPlayerSlot.Get()]->IsGagged();
		bool bSilent = *args[1] == '/';
		bool bCommand = *args[1] == '!' || *args[1] == '/';

		if (pController)
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent("player_chat");

			if (pEvent)
			{
				pEvent->SetBool("teamonly", bTeamSay);
				pEvent->SetInt("userid", iCommandPlayerSlot.Get());
				pEvent->SetString("text", args[1]);

				gameeventmanager->FireEvent(pEvent, true);
			}
		}

		if (!bGagged && !bSilent)
		{
			SH_CALL(g_pCVar, &ICvar::DispatchConCommand)(cmdHandle, ctx, args);
		}

		if (bCommand)
		{
			char *pszMessage = (char *)(args.ArgS() + 2);
			if (bSilent)
				pszMessage[V_strlen(pszMessage) - 1] = 0;

			ParseChatCommand(iCommandPlayerSlot.Get(), pszMessage, pController);
		}

		RETURN_META(MRES_SUPERCEDE);
	}
}

bool AdminSystem::IsAdminFlagSet(CAdmin aAdmin, uint64 iFlag, int slot)
{
	if(!m_vecPlayers[slot]->IsAuthenticated()) return 0;
	else return !iFlag || (aAdmin.GetFlags() & iFlag);
}

CAdmin* AdminSystem::FindAdmin(int slot)
{
	FOR_EACH_VEC(m_vecAdmins, i)
    {
        if (m_vecAdmins[i].GetSteamID() == engine->GetClientXUID(slot))
            return &m_vecAdmins[i];
    }

    return nullptr;
}

void AdminSystem::CheckInfractions(int PlayerSlot)
{
	char szQuery[128];
	uint64 iSteamID = m_vecPlayers[PlayerSlot]->GetSteamId64();
	g_SMAPI->Format(szQuery, sizeof(szQuery), "SELECT * FROM `as_bans` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", iSteamID, std::time(0));
	g_pConnection->Query(szQuery, [PlayerSlot, this](IMySQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->GetRowCount())
		{
			if(results->FetchRow())
			{
				engine->DisconnectClient(CPlayerSlot(PlayerSlot), 41);
			}
			//Типо бан
			// results->GetString(1); стим айди админа
			// results->GetString(4); ник админа
			// results->GetInt(6); время бана
			// results->GetInt(7); конец бана
			// results->GetString(8); причина бана
		}
	});
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
				//Типо мут
				// results->GetString(1); стим айди админа
				// results->GetString(4); ник админа
				// results->GetInt(6); время мута
				// results->GetInt(7); конец мута
				// results->GetString(8); причина мута
			}
		}
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
				//Типо мут
				// results->GetString(1); стим айди админа
				// results->GetString(4); ник админа
				// results->GetInt(6); время мута
				// results->GetInt(7); конец мута
				// results->GetString(8); причина мута
			}
		}
	});
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

void AdminSystem::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pMysqlClient = (IMySQLClient *)g_SMAPI->MetaFactory(MYSQLMM_INTERFACE, &ret, NULL);

	if (ret == META_IFACE_FAILED)
	{
		V_strncpy(error, "Missing MYSQL plugin", 64);
		return;
	}
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
	g_pConnection = g_pMysqlClient->CreateMySQLConnection(info);

	g_pConnection->Connect([this](bool connect)
	{
		if (!connect)
		{
			META_CONPRINT("Failed to connect the mysql database\n");
		} else {
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_bans`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_gags`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_mutes`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT,`admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
		}
	});
}

bool AdminSystem::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceServiceServer, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);

	// Get CSchemaSystem
	{
		HINSTANCE m_hModule = dlmount(WIN_LINUX("schemasystem.dll", "libschemasystem.so"));
		g_pCSchemaSystem = reinterpret_cast<CSchemaSystem*>(reinterpret_cast<CreateInterfaceFn>(dlsym(m_hModule, "CreateInterface"))(SCHEMASYSTEM_INTERFACE_VERSION, nullptr));
		dlclose(m_hModule);
	}

	if (!g_AdminSystem.LoadAdmins())
	{
		ConColorMsg(Color(255, 0, 0, 255), "[%s] Error config load\n", GetLogTag());
		
		return false;
	}
	CModule libserver(g_pSource2Server);
	CModule libengine(engine);
	UTIL_ClientPrint = libserver.FindPatternSIMD(WIN_LINUX("48 85 C9 0F 84 2A 2A 2A 2A 48 8B C4 48 89 58 18", "55 48 89 E5 41 57 49 89 CF 41 56 49 89 D6 41 55 41 89 F5 41 54 4C 8D A5 A0 FE FF FF")).RCast< decltype(UTIL_ClientPrint) >();
	if (!UTIL_ClientPrint)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrint", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);

		return false;
	}
	UTIL_ClientPrintAll = libserver.FindPatternSIMD(WIN_LINUX("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 70 01 2A 2A 8B E9", "55 48 89 E5 41 57 49 89 D7 41 56 49 89 F6 41 55 41 89 FD")).RCast< decltype(UTIL_ClientPrintAll) >();
	if (!UTIL_ClientPrintAll)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrintAll", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);

		return false;
	}
	UTIL_IsHearingClient = libengine.FindPatternSIMD(WIN_LINUX("40 53 48 83 EC 20 48 8B D9 3B 91 B8", "55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 08 3B B7 C8")).RCast< decltype(UTIL_IsHearingClient) >();
	if (!UTIL_IsHearingClient)
	{
		V_strncpy(error, "Failed to find function to get UTIL_IsHearingClient", maxlen);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);

		return false;
	}
	m_IsHearingClient = funchook_create();
	funchook_prepare(m_IsHearingClient, (void**)&UTIL_IsHearingClient, (void*)IsHearingClient);
	funchook_install(m_IsHearingClient, 0);

	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AdminSystem::StartupServer), true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &AdminSystem::Hook_OnClientDisconnect, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, g_pSource2GameClients, this, &AdminSystem::Hook_ClientPutInServer, true);
	SH_ADD_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &AdminSystem::Hook_DispatchConCommand, false);

	gameeventmanager = static_cast<IGameEventManager2*>(CallVFunc<IToolGameEventAPI*, 91>(g_pSource2Server));
	ConVar_Register(FCVAR_GAMEDLL);

	return true;
}

bool AdminSystem::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AdminSystem::StartupServer), true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &AdminSystem::Hook_OnClientDisconnect, true);
	SH_REMOVE_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &AdminSystem::Hook_DispatchConCommand, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, g_pSource2GameClients, this, &AdminSystem::Hook_ClientPutInServer, true);

	ConVar_Unregister();

	if (g_pConnection)
		g_pConnection->Destroy();
	
	return true;
}

bool AdminSystem::LoadAdmins()
{
	m_vecAdmins.RemoveAll();
	KeyValues* pKV = new KeyValues("Admins");
	KeyValues::AutoDelete autoDelete(pKV);

	const char *pszPath = "addons/configs/admins.cfg";

	if (!pKV->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return false;
	}
	FOR_EACH_VALUE(pKV, pKey)
	{
		const char *pszSteamID = pKey->GetName();
		const char *pszFlags = pKey->GetString(nullptr, nullptr);
		uint64 iFlags = ParseFlags(pszFlags);
		m_vecAdmins.AddToTail(CAdmin(atoll(pszSteamID), iFlags));
	}
	return true;
}

void AdminSystem::StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*)
{
	static bool bDone = false;
	if (!bDone)
	{
		g_pNetworkGameServer = g_pNetworkServerService->GetIGameServer();
		gpGlobals = g_pNetworkGameServer->GetGlobals();
		g_pGameEntitySystem = *reinterpret_cast<CGameEntitySystem**>(reinterpret_cast<uintptr_t>(g_pGameResourceServiceServer) + WIN_LINUX(0x58, 0x50));
		g_pEntitySystem = g_pGameEntitySystem;

		bDone = true;
	}
}

void AdminSystem::Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	if(m_vecPlayers[slot.Get()] == nullptr)
	{
		CCSPlayerController* pPlayer = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(slot.Get() + 1));
		if(!pPlayer || pPlayer->m_steamID() == 0) m_vecPlayers[slot.Get()] = new CPlayer(slot, true);
		else m_vecPlayers[slot.Get()] = new CPlayer(slot);
	}
}

void AdminSystem::Hook_OnClientDisconnect(CPlayerSlot slot, int reason, const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	delete m_vecPlayers[slot.Get()];
	m_vecPlayers[slot.Get()] = nullptr;
}

void AdminSystem::CreateTimer(std::function<void()> fn, uint64_t time)
{
	m_Timer.push_back(fn);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	m_TimerTime.push_back(time*1000+millis);
}

void AdminSystem::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	while (!m_Timer.empty())
	{
		uint64_t time = m_TimerTime.front();
		if(millis >= time)
		{
			m_Timer.front()();
			m_Timer.pop_front();
			m_TimerTime.pop_front();
		}
		else break;
	}

	if(g_iLastTime == 0) g_iLastTime = std::time(0);
	else if(std::time(0) - g_iLastTime >= 1)
	{
		g_iLastTime = std::time(0);
		for (int i = 0; i < 64; i++)
		{
			CCSPlayerController* pPlayerController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));
			if(!pPlayerController)
				continue;
			CPlayerSlot pSlot = CPlayerSlot(i);
			CPlayer* pPlayer = g_AdminSystem.GetPlayer(i);
			if(pPlayer == nullptr)
				continue;
			if (pPlayer->IsAuthenticated() || pPlayer->IsFakeClient())
				continue;

			if (engine->IsClientFullyAuthenticated(CPlayerSlot(i)))
			{
				pPlayer->SetAuthenticated();
				pPlayer->SetSteamId(engine->GetClientSteamID(i));
				g_AdminSystem.CheckInfractions(i);
			}
		}
	}
}

void PunishmentPlayer(int iSlot, const CCommand &args, CCSPlayerController *player, int iType)
{
	if (args.ArgC() < 4)
	{
		switch (iType)
		{
			case 0:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !ban <#userid|name> <duration(seconds)/0 (permanent)> <reason>");
				break;
			}
			case 1:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !mute <#userid|name> <duration(seconds)/0 (permanent)> <reason>");
				break;
			}
			case 2:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !gag <#userid|name> <duration(seconds)/0 (permanent)> <reason>");
				break;
			}
			case 3:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !silence <#userid|name> <duration(seconds)/0 (permanent)> <reason>");
				break;
			}
		}
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Invalid duration.");
		return;
	}

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(iTarget);

	if (pTargetPlayer->IsFakeClient())
		return;

	CPlayer* pPlayer = g_AdminSystem.GetPlayer(iSlot);
	
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
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, "Admin %s has permanently banned %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, "Admin %s has banned %s for %i minutes.", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration, std::time(0)+iDuration, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			engine->DisconnectClient(CPlayerSlot(iTarget), 41);
			break;
		}
		case 1:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, "Admin %s has permanently muted %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, "Admin %s has muted %s for %i minutes.", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration, std::time(0)+iDuration, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetMuted(iDuration, std::time(0)+iDuration);
			break;
		}
		case 2:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, "Admin %s has permanently gagged %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, "Admin %s has gagged %s for %i minutes.", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration, std::time(0)+iDuration, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetGagged(iDuration, std::time(0)+iDuration);
			break;
		}
		case 3:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, "Admin %s has permanently silence %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, "Admin %s has silence %s for %i minutes.", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration/60);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration, std::time(0)+iDuration, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration, std::time(0)+iDuration, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetMuted(iDuration, std::time(0)+iDuration);
			pTargetPlayer->SetGagged(iDuration, std::time(0)+iDuration);
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
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !unban <steamid>");
				break;
			}
			case 1:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !unmute <#userid|name>");
				break;
			}
			case 2:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !ungag <#userid|name>");
				break;
			}
			case 3:
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !unsilence <#userid|name>");
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
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
			return;
		}

		CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		if (!pTarget)
			return;
		CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(iTarget);
		if (pTargetPlayer->IsFakeClient())
			return;
		CPlayer* pPlayer = g_AdminSystem.GetPlayer(iSlot);
		switch (iType)
		{
			case 1:
			{
				ClientPrintAll(HUD_PRINTTALK, "Admin %s has unmuted %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetMuted(-1, -1);
				break;
			}
			case 2:
			{
				ClientPrintAll(HUD_PRINTTALK, "Admin %s has ungagged %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetGagged(-1, -1);
				break;
			}
			case 3:
			{
				ClientPrintAll(HUD_PRINTTALK, "Admin %s has silence %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
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

CON_COMMAND_CHAT(admin, "shows available admin commands")
{
	if(iSlot != -1)
	{
		CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(iSlot);
		CAdmin& foundAdmin = *foundAdminPtr;
		if(foundAdminPtr != nullptr)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Commands:\n");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_BAN, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!ban <#userid|name> <duration(seconds)/0 (permanent)> <reason> - ban");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_UNBAN, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!unban <steamid> - unban");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHAT, iSlot))
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!silence <#userid|name> <duration(seconds)/0 (permanent)> <reason> - mute && gag");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!unsilence <#userid|name> - unmute && ungag");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!mute <#userid|name> <duration(seconds)/0 (permanent)> <reason> - mute");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!unmute <#userid|name> - unmute");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!gag <#userid|name> <duration(seconds)/0 (permanent)> <reason> - gag");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!ungag <#userid|name> - ungag");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!csay <message> - say to all players (in center)");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!hsay <message> - say to all players (in hud)");
			}
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_KICK, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!kick <#userid|name> - kick");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_GENERIC, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!who <optional #userid|name> - recognize a player's rights");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_RCON, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!rcon <command> - send a command to server console");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_SLAY, iSlot))
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!freeze <#userid|name> <duration> - freeze a player");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!unfreeze <#userid|name> - unfreeze a player");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!slay <#userid|name> - slay a player");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!slap <#userid|name> <optional damage> - slap a player");
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!changeteam <#userid|name> <team (0-3)> - change team");
			}
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHANGEMAP, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!map <mapname> - change map");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHEATS, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!noclip <optional #userid|name> - noclip");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_ROOT, iSlot))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "!reload_admins - reload admin list");
		}
	}
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

CON_COMMAND_CHAT_FLAGS(kick, "kick a player", ADMFLAG_KICK)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !kick <#userid|name>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(HUD_PRINTTALK, "Admin %s has kicked %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	engine->DisconnectClient(CPlayerSlot(iTarget), 39);
}

CON_COMMAND_CHAT_FLAGS(who, "recognize a player", ADMFLAG_GENERIC)
{
	if (args.ArgC() < 2)
	{
		bool bFound = false;
		for (int i = 0; i < gpGlobals->maxClients; i++)
		{
			if(engine->GetPlayerUserId(i).Get() == -1) continue;
			CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(i + 1));

			CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(i);

			if (!pTargetPlayer || pTargetPlayer->IsFakeClient() || !pTarget)
				continue;

			CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(i);
			CAdmin& foundAdmin = *foundAdminPtr;
			std::string flag;
			if(foundAdminPtr == nullptr) flag = "none";
			else
			{
				uint64_t flags = foundAdmin.GetFlags();
				if(flags & ADMFLAG_ROOT) flag = "root";
				else FlagsToString(flag, flags);
			}
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"%s has access: %s", pTarget->m_iszPlayerName(), flag.c_str());
			bFound = true;
		}
		if(!bFound) ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"No players found");
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
			return;
		}

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(iTarget);

		if (!pTargetPlayer || pTargetPlayer->IsFakeClient() || !pTarget)
			return;

		CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(iTarget);
		CAdmin& foundAdmin = *foundAdminPtr;
		std::string flag;
		if(foundAdminPtr == nullptr) flag = "none";
		else
		{
			uint64_t flags = foundAdmin.GetFlags();
			if(flags & ADMFLAG_ROOT) flag = "root";
			else FlagsToString(flag, flags);
		}
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"%s has access: %s", pTarget->m_iszPlayerName(), flag.c_str());
	}
}

CON_COMMAND_CHAT_FLAGS(csay, "say to all players (in center)", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !csay <message>");
		return;
	}

	ClientPrintAll(HUD_PRINTCENTER, "%s", args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(hsay, "say to all players (in hud)", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !hsay <message>");
		return;
	}

	ClientPrintAll(HUD_PRINTALERT, "%s", args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(rcon, "send a command to server console", ADMFLAG_RCON)
{
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "You are already on the server console.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !rcon <command>");
		return;
	}

	engine->ServerCommand(args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(freeze, "freeze a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !freeze <#userid|name> <duration>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Invalid duration.");
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

	if(!pPlayer)
		return;

	pPlayer->m_MoveType() = MOVETYPE_NONE;
	g_AdminSystem.CreateTimer([pPlayer]()
	{
		pPlayer->m_MoveType() = MOVETYPE_WALK;
	}, iDuration);
	ClientPrintAll(HUD_PRINTTALK, "Admin %s has freezing %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
}

CON_COMMAND_CHAT_FLAGS(unfreeze, "unfreeze a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{	
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !unfreeze <#userid|name>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

	if(!pPlayer)
		return;

	pPlayer->m_MoveType() = MOVETYPE_WALK;
	ClientPrintAll(HUD_PRINTTALK, "Admin %s has unfreezing %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
}

CON_COMMAND_CHAT_FLAGS(noclip, "noclip a player", ADMFLAG_CHEATS)
{
	if (args.ArgC() < 2)
	{
		CBasePlayerPawn* pPlayer = player->m_hPawn();

		if(!pPlayer)
			return;

		if (pPlayer->m_iHealth() <= 0)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You cannot noclip while dead!");
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			ClientPrintAll(HUD_PRINTTALK, "Admin %s exited noclip.", iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_WALK;
		}
		else
		{
			ClientPrintAll(HUD_PRINTTALK, "Admin %s entered noclip.", iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
		}
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
			return;
		}

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		if (!pTarget)
			return;

		CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

		if(!pPlayer)
			return;

		if (pPlayer->m_iHealth() <= 0)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You cannot noclip while dead!");
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			pPlayer->m_MoveType() = MOVETYPE_WALK;
			ClientPrintAll(HUD_PRINTTALK, "Admin %s deactivated noclip %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
		else
		{
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
			ClientPrintAll(HUD_PRINTTALK, "Admin %s activated noclip %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
	}
}

CON_COMMAND_CHAT_FLAGS(changeteam, "set a player's team", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !changeteam <#userid|name> <team (0-3)>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	int iTeam = V_StringToInt32(args[2], -1);
	const char *teams[] = {"none", "spectators", "terrorists", "counter-terrorists"};
	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Invalid team specified, range is 0-3.");
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	pTarget->ChangeTeam(iTeam);
	
	ClientPrintAll(HUD_PRINTTALK, "Admin %s moved %s to %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), teams[iTeam]);
}

CON_COMMAND_CHAT_FLAGS(slap, "slap a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !slap <#userid|name> <optional damage>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

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
	
	ClientPrintAll(HUD_PRINTTALK, "Admin %s has slapped %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
}

CON_COMMAND_CHAT_FLAGS(slay, "slay a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !slay <#userid|name>");
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Target not found.");
		return;
	}

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(HUD_PRINTTALK, "Admin %s has slayed %s", iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	pTarget->m_hPlayerPawn().Get()->CommitSuicide(false, true);
}

CON_COMMAND_CHAT_FLAGS(map, "change map", ADMFLAG_CHANGEMAP)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Usage: !map <mapname>");
		return;
	}

	char szMapName[MAX_PATH];
	V_strncpy(szMapName, args[1], sizeof(szMapName));

	if (!engine->IsMapValid(szMapName))
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Invalid map specified.");
		return;
	}

	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "Changing map to %s...", szMapName);
	
	g_AdminSystem.CreateTimer([szMapName]()
	{
		engine->ChangeLevel(szMapName, nullptr);
	}, 5.0);
}

CON_COMMAND_CHAT_FLAGS(reload_admins, "Reload admin config", ADMFLAG_ROOT)
{
	if (!g_AdminSystem.LoadAdmins())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Error config load");
		return;
	}
	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX"Admins reloaded");
}

///////////////////////////////////////
const char* AdminSystem::GetLicense()
{
	return "GPL";
}

const char* AdminSystem::GetVersion()
{
	return "2.0.0";
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
