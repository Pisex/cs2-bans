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
const char *g_pszLanguage;
std::map<std::string, std::string> g_vecPhrases;

class GameSessionConfiguration_t { };
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);
SH_DECL_HOOK3_void(INetworkServerService, StartupServer, SH_NOATTRIB, 0, const GameSessionConfiguration_t&, ISource2WorldSession*, const char*);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0, CPlayerSlot, int, const char *, uint64, const char *);
SH_DECL_HOOK3_void(ICvar, DispatchConCommand, SH_NOATTRIB, 0, ConCommandHandle, const CCommandContext&, const CCommand&);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0, CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);

void (*UTIL_ClientPrint)(CBasePlayerController *player, int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;
void (*UTIL_ClientPrintAll)(int msg_dest, const char* msg_name, const char* param1, const char* param2, const char* param3, const char* param4) = nullptr;
bool (*UTIL_IsHearingClient)(void* serverClient, int index) = nullptr;
void (*UTIL_SwitchTeam)(CCSPlayerController* pPlayer, int iTeam) = nullptr;
void (*UTIL_Say)(const CCommandContext& ctx, CCommand& args) = nullptr;
void (*UTIL_SayTeam)(const CCommandContext& ctx, CCommand& args) = nullptr;

funchook_t* m_IsHearingClient;
funchook_t* m_SayHook;
funchook_t* m_SayTeamHook;

void SayTeamHook(const CCommandContext& ctx, CCommand& args)
{
	auto iCommandPlayerSlot = ctx.GetPlayerSlot();
	auto pController = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iCommandPlayerSlot.Get() + 1));
	bool bGagged = g_AdminSystem.GetPlayer(iCommandPlayerSlot.Get())->IsGagged();
	if(!bGagged) UTIL_SayTeam(ctx, args);
}
void SayHook(const CCommandContext& ctx, CCommand& args)
{
	auto iCommandPlayerSlot = ctx.GetPlayerSlot();
	auto pController = (CCSPlayerController*)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iCommandPlayerSlot.Get() + 1));
	bool bGagged = g_AdminSystem.GetPlayer(iCommandPlayerSlot.Get())->IsGagged();
	if(!bGagged) UTIL_Say(ctx, args);
}

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

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	g_SMAPI->Format(buf2, sizeof(buf2), "%s%s", g_AdminSystem.Translate("Prefix"), buf);
	va_end(args);

	UTIL_ClientPrintAll(hud_dest, buf2, nullptr, nullptr, nullptr, nullptr);
	ConMsg("%s\n", buf2);
}

void ClientPrint(CBasePlayerController *player, int hud_dest, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);

	char buf[256], buf2[256];
	V_vsnprintf(buf, sizeof(buf), msg, args);
	g_SMAPI->Format(buf2, sizeof(buf2), "%s%s", g_AdminSystem.Translate("Prefix"), buf);

	va_end(args);

	if (player)
		UTIL_ClientPrint(player, hud_dest, buf2, nullptr, nullptr, nullptr, nullptr);
	else
		ConMsg("%s\n", buf2);
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
		ClientPrint(pPlayer, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("DenyAccess"));
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
			if (bSilent || bGagged)
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
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_admins`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `flags` VARCHAR(64) NOT NULL, `immunity` INTEGER NOT NULL, `end` INTEGER NOT NULL, `comment` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_bans`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_gags`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT, `admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});
			g_pConnection->Query("CREATE TABLE IF NOT EXISTS `as_mutes`(`id` INTEGER NOT NULL PRIMARY KEY AUTO_INCREMENT,`admin_steamid` VARCHAR(32) NOT NULL, `steamid` VARCHAR(32) NOT NULL, `name` VARCHAR(64) NOT NULL, `admin_name` VARCHAR(64) NOT NULL, `created` INTEGER NOT NULL, `duration` INTEGER NOT NULL, `end` INTEGER NOT NULL, `reason` VARCHAR(64) NOT NULL);", [this](IMySQLQuery* test){});

			if (!g_AdminSystem.LoadAdmins())
				ConColorMsg(Color(255, 0, 0, 255), "[%s] Error config load\n", GetLogTag());
		}
	});
}

const char *AdminSystem::Translate(const char* phrase)
{
    return g_vecPhrases[std::string(phrase)].c_str();
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

	{
		KeyValues* g_kvPhrases = new KeyValues("Phrases");
		const char *pszPath = "addons/translations/admin_system.phrases.txt";

		if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
		{
			Warning("Failed to load %s\n", pszPath);
			return false;
		}

		const char* g_pszLanguage = g_kvPhrases->GetString("language", "en");
		for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
			g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
	}

	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_ADD_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AdminSystem::StartupServer), true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &AdminSystem::Hook_OnClientDisconnect, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, g_pSource2GameClients, this, &AdminSystem::Hook_ClientPutInServer, true);
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, this, &AdminSystem::Hook_GameServerSteamAPIActivated, false);
	SH_ADD_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &AdminSystem::Hook_DispatchConCommand, false);

	gameeventmanager = static_cast<IGameEventManager2*>(CallVFunc<IToolGameEventAPI*, 91>(g_pSource2Server));
	ConVar_Register(FCVAR_GAMEDLL);

	return true;
}

void AdminSystem::Hook_GameServerSteamAPIActivated()
{
	char error[128];
	CModule libserver(g_pSource2Server);
	CModule libengine(engine);
	UTIL_ClientPrint = libserver.FindPatternSIMD(WIN_LINUX("48 85 C9 0F 84 2A 2A 2A 2A 48 8B C4 48 89 58 18", "55 48 89 E5 41 57 49 89 CF 41 56 49 89 D6 41 55 41 89 F5 41 54 4C 8D A5 A0 FE FF FF")).RCast< decltype(UTIL_ClientPrint) >();
	if (!UTIL_ClientPrint)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrint", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	UTIL_ClientPrintAll = libserver.FindPatternSIMD(WIN_LINUX("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 70 01 2A 2A 8B E9", "55 48 89 E5 41 57 49 89 D7 41 56 49 89 F6 41 55 41 89 FD")).RCast< decltype(UTIL_ClientPrintAll) >();
	if (!UTIL_ClientPrintAll)
	{
		V_strncpy(error, "Failed to find function to get UTIL_ClientPrintAll", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	UTIL_SwitchTeam = libserver.FindPatternSIMD(WIN_LINUX("40 56 57 48 81 EC 2A 2A 2A 2A 48 8B F9 8B F2 8B CA", "55 48 89 E5 41 55 49 89 FD 89 F7")).RCast< decltype(UTIL_SwitchTeam) >();
	if (!UTIL_SwitchTeam)
	{
		V_strncpy(error, "Failed to find function to get UTIL_SwitchTeam", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	UTIL_IsHearingClient = libengine.FindPatternSIMD(WIN_LINUX("40 53 48 83 EC 20 48 8B D9 3B 91 B8", "55 48 89 E5 41 55 41 54 53 48 89 FB 48 83 EC 08 3B B7 C8")).RCast< decltype(UTIL_IsHearingClient) >();
	if (!UTIL_IsHearingClient)
	{
		V_strncpy(error, "Failed to find function to get UTIL_IsHearingClient", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	m_IsHearingClient = funchook_create();
	funchook_prepare(m_IsHearingClient, (void**)&UTIL_IsHearingClient, (void*)IsHearingClient);
	funchook_install(m_IsHearingClient, 0);

	UTIL_SayTeam = libserver.FindPatternSIMD("55 48 89 E5 41 56 41 55 49 89 F5 41 54 49 89 FC 53 48 83 EC 10 48 8D 05 8C 62 AA 00").RCast< decltype(UTIL_SayTeam) >();
	if (!UTIL_SayTeam)
	{
		V_strncpy(error, "Failed to find function to get UTIL_SayTeam", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	m_SayTeamHook = funchook_create();
	funchook_prepare(m_SayTeamHook, (void**)&UTIL_SayTeam, (void*)SayTeamHook);
	funchook_install(m_SayTeamHook, 0);

	UTIL_Say = libserver.FindPatternSIMD("55 48 89 E5 41 56 41 55 49 89 F5 41 54 49 89 FC 53 48 83 EC 10 48 8D 05 7C 61 AA 00").RCast< decltype(UTIL_Say) >();
	if (!UTIL_Say)
	{
		V_strncpy(error, "Failed to find function to get UTIL_Say", sizeof(error));
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	m_SayHook = funchook_create();
	funchook_prepare(m_SayHook, (void**)&UTIL_Say, (void*)SayHook);
	funchook_install(m_SayHook, 0);
}

bool AdminSystem::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(INetworkServerService, StartupServer, g_pNetworkServerService, SH_MEMBER(this, &AdminSystem::StartupServer), true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pSource2Server, this, &AdminSystem::Hook_GameFrame, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientDisconnect, g_pSource2GameClients, this, &AdminSystem::Hook_OnClientDisconnect, true);
	SH_REMOVE_HOOK_MEMFUNC(ICvar, DispatchConCommand, g_pCVar, this, &AdminSystem::Hook_DispatchConCommand, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, ClientPutInServer, g_pSource2GameClients, this, &AdminSystem::Hook_ClientPutInServer, true);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, this, &AdminSystem::Hook_GameServerSteamAPIActivated, false);

	ConVar_Unregister();

	if (g_pConnection)
		g_pConnection->Destroy();
	
	return true;
}

bool AdminSystem::LoadAdmins()
{
	m_vecAdmins.RemoveAll();
	g_pConnection->Query("SELECT * FROM as_admins;", [this](IMySQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->GetRowCount())
		{
			while(results->FetchRow())
			{
				uint64 iFlags = ParseFlags(results->GetString(3));
				m_vecAdmins.AddToTail(CAdmin(atoll(results->GetString(1)), iFlags, results->GetInt(4), results->GetString(2), results->GetInt(5)));
			}
		}
	});
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

bool AdminSystem::CheckImmunity(int iTarget, int iAdmin, CCSPlayerController *player)
{
	CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(iTarget);
	CAdmin& foundAdmin = *foundAdminPtr;
	if (foundAdminPtr != nullptr)
	{
		CAdmin* foundAdminPtr2 = g_AdminSystem.FindAdmin(iAdmin);
		CAdmin& foundAdmin2 = *foundAdminPtr2;
		if (foundAdminPtr2 != nullptr)
		{
			if(foundAdmin.GetImmunity() > foundAdmin2.GetImmunity())
			{
				ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target immunity"));
				return false;
			}
		}
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
				ClientPrint(player, HUD_PRINTTALK, "%s!ban <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 1:
			{
				ClientPrint(player, HUD_PRINTTALK, "%s!mute <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 2:
			{
				ClientPrint(player, HUD_PRINTTALK,  "%s!gag <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 3:
			{
				ClientPrint(player, HUD_PRINTTALK,  "%s!silence <#userid|name> <duration(minutes)/0 (permanent)> <reason>", g_AdminSystem.Translate("Usage"));
				break;
			}
		}
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Invalid duration"));
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
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("BanPermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Ban"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_bans` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			engine->DisconnectClient(CPlayerSlot(iTarget), 41);
			break;
		}
		case 1:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("MutePermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Mute"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetMuted(iDuration*60, std::time(0)+iDuration*60);
			break;
		}
		case 2:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("GagPermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Gag"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":g_pConnection->Escape(player->m_iszPlayerName()).c_str(), g_pConnection->Escape(pTarget->m_iszPlayerName()).c_str(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			pTargetPlayer->SetGagged(iDuration*60, std::time(0)+iDuration*60);
			break;
		}
		case 3:
		{
			if(iDuration == 0) ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("SilencePermanent"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
			else ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Silence"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), iDuration);
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_mutes` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
			g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
			g_SMAPI->Format(szQuery, sizeof(szQuery), "INSERT INTO `as_gags` (`admin_steamid`, `steamid`, `admin_name`, `name`, `created`, `duration`, `end`, `reason`) VALUES ('%lld', '%lld', '%s', '%s', '%lld', '%i', '%lld', '%s');", iSlot == -1?0:pPlayer->GetSteamId64(), pTargetPlayer->GetSteamId64(), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), std::time(0), iDuration*60, std::time(0)+iDuration*60, sReason.c_str());
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
				ClientPrint(player, HUD_PRINTTALK, " %s!unban <steamid>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 1:
			{
				ClientPrint(player, HUD_PRINTTALK, "%s!unmute <#userid|name>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 2:
			{
				ClientPrint(player, HUD_PRINTTALK,  "%s!ungag <#userid|name>", g_AdminSystem.Translate("Usage"));
				break;
			}
			case 3:
			{
				ClientPrint(player, HUD_PRINTTALK,  "%s!unsilence <#userid|name>", g_AdminSystem.Translate("Usage"));
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
			ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
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
				ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("UnMuted"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_mutes` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetMuted(-1, -1);
				break;
			}
			case 2:
			{
				ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("UnGagged"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
				g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_gags` WHERE `steamid` = '%lld' AND (`end` > %i OR `duration` = 0)", pTargetPlayer->GetSteamId64(), std::time(0));
				g_pConnection->Query(szQuery, [](IMySQLQuery* test){});
				pTargetPlayer->SetGagged(-1, -1);
				break;
			}
			case 3:
			{
				ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("UnSilence"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
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
			ClientPrint(player, HUD_PRINTTALK,  "Commands:\n");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_BAN, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!ban <#userid|name> <duration(minutes)/0 (permanent)> <reason> - ban");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_UNBAN, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!unban <steamid> - unban");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHAT, iSlot))
			{
				ClientPrint(player, HUD_PRINTTALK,  "!silence <#userid|name> <duration(minutes)/0 (permanent)> <reason> - mute && gag");
				ClientPrint(player, HUD_PRINTTALK,  "!unsilence <#userid|name> - unmute && ungag");
				ClientPrint(player, HUD_PRINTTALK,  "!mute <#userid|name> <duration(minutes)/0 (permanent)> <reason> - mute");
				ClientPrint(player, HUD_PRINTTALK,  "!unmute <#userid|name> - unmute");
				ClientPrint(player, HUD_PRINTTALK,  "!gag <#userid|name> <duration(minutes)/0 (permanent)> <reason> - gag");
				ClientPrint(player, HUD_PRINTTALK,  "!ungag <#userid|name> - ungag");
				ClientPrint(player, HUD_PRINTTALK,  "!csay <message> - say to all players (in center)");
				ClientPrint(player, HUD_PRINTTALK,  "!hsay <message> - say to all players (in hud)");
			}
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_KICK, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!kick <#userid|name> - kick");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_GENERIC, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!who <optional #userid|name> - recognize a player's rights");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_RCON, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!rcon <command> - send a command to server console");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_SLAY, iSlot))
			{
				ClientPrint(player, HUD_PRINTTALK,  "!freeze <#userid|name> <duration> - freeze a player");
				ClientPrint(player, HUD_PRINTTALK,  "!unfreeze <#userid|name> - unfreeze a player");
				ClientPrint(player, HUD_PRINTTALK,  "!slay <#userid|name> - slay a player");
				ClientPrint(player, HUD_PRINTTALK,  "!slap <#userid|name> <optional damage> - slap a player");
				ClientPrint(player, HUD_PRINTTALK,  "!setteam <#userid|name> <team (0-3)> - change team(without death)");
				ClientPrint(player, HUD_PRINTTALK,  "!changeteam <#userid|name> <team (0-3)> - change team(with death)");
			}
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHANGEMAP, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!map <mapname> - change map");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_CHEATS, iSlot))
				ClientPrint(player, HUD_PRINTTALK,  "!noclip <optional #userid|name> - noclip");
			if(g_AdminSystem.IsAdminFlagSet(foundAdmin, ADMFLAG_ROOT, iSlot))
			{
				ClientPrint(player, HUD_PRINTTALK,  "!reload_admins - reload admin list");
				ClientPrint(player, HUD_PRINTTALK,  "!add_admin <admin_name> <steamid64> <duration(minutes)/0 (permanent)> <flags> <immunity> <optional comment> - add admin");
				ClientPrint(player, HUD_PRINTTALK,  "!remove_admin <steamid64> - remove admin");
			}
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
		ClientPrint(player, HUD_PRINTTALK,  "%s!kick <#userid|name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Kick"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	engine->DisconnectClient(CPlayerSlot(iTarget), 39);
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

			CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(i);

			if (!pTargetPlayer || pTargetPlayer->IsFakeClient() || !pTarget)
				continue;

			CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(i);
			CAdmin& foundAdmin = *foundAdminPtr;
			std::string flag;
			if(foundAdminPtr == nullptr)
			{
				flag = "none";
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Access"), pTarget->m_iszPlayerName(), flag.c_str());
			}
			else
			{
				uint64_t flags = foundAdmin.GetFlags();
				if(flags & ADMFLAG_ROOT) flag = "root";
				else FlagsToString(flag, flags);
				g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AccessAdmin"), pTarget->m_iszPlayerName(), foundAdmin.GetName(), flag.c_str());
			}
			ClientPrint(player, HUD_PRINTTALK, "%s", szBuffer);
			bFound = true;
		}
		if(!bFound) ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("AccessNoPlayers"));
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
			return;
		}

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		CPlayer* pTargetPlayer = g_AdminSystem.GetPlayer(iTarget);

		if (!pTargetPlayer || pTargetPlayer->IsFakeClient() || !pTarget)
			return;

		CAdmin* foundAdminPtr = g_AdminSystem.FindAdmin(iTarget);
		CAdmin& foundAdmin = *foundAdminPtr;
		std::string flag;
		char szBuffer[256];
		if(foundAdminPtr == nullptr)
		{
			flag = "none";
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Access"), pTarget->m_iszPlayerName(), flag.c_str());
		}
		else
		{
			uint64_t flags = foundAdmin.GetFlags();
			if(flags & ADMFLAG_ROOT) flag = "root";
			else FlagsToString(flag, flags);
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AccessAdmin"), pTarget->m_iszPlayerName(), foundAdmin.GetName(), flag.c_str());
		}
		ClientPrint(player, HUD_PRINTTALK, "%s", szBuffer);
	}
}

CON_COMMAND_CHAT_FLAGS(csay, "say to all players (in center)", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!csay <message>", g_AdminSystem.Translate("Usage"));
		return;
	}

	ClientPrintAll(HUD_PRINTCENTER, "%s", args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(hsay, "say to all players (in hud)", ADMFLAG_CHAT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!hsay <message>", g_AdminSystem.Translate("Usage"));
		return;
	}

	ClientPrintAll(HUD_PRINTALERT, "%s", args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(rcon, "send a command to server console", ADMFLAG_RCON)
{
	CCSPlayerController* pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iSlot + 1));
	CBasePlayerPawn* pPlayerPawn = pController->m_hPlayerPawn().Get();
	META_CONPRINTF("%i | %i | %i\n", player, pController, pPlayerPawn);
	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE,  "%s", g_AdminSystem.Translate("You console"));
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!rcon <command>", g_AdminSystem.Translate("Usage"));
		return;
	}

	engine->ServerCommand(args.ArgS());
}

CON_COMMAND_CHAT_FLAGS(freeze, "freeze a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(player, HUD_PRINTTALK,  "%s!freeze <#userid|name> <duration>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	int iDuration = V_StringToInt32(args[2], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Invalid duration"));
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
	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Freeze"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
}

CON_COMMAND_CHAT_FLAGS(unfreeze, "unfreeze a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{	
		ClientPrint(player, HUD_PRINTTALK,  "%s!unfreeze <#userid|name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

	if(!pPlayer)
		return;

	pPlayer->m_MoveType() = MOVETYPE_WALK;
	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Unfreeze"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
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
			ClientPrint(player, HUD_PRINTTALK,  "%s", g_AdminSystem.Translate("NoclipDead"));
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("ANoclipDisable"), iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_WALK;
		}
		else
		{
			ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("ANoclipEnable"), iSlot == -1?"Console":player->m_iszPlayerName());
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
		}
	}
	else
	{
		int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

		if (iTarget == -1)
		{
			ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
			return;
		}

		if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
			return;

		CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

		if (!pTarget)
			return;

		CBasePlayerPawn* pPlayer = pTarget->m_hPawn();

		if(!pPlayer)
			return;

		if (pPlayer->m_iHealth() <= 0)
		{
			ClientPrint(player, HUD_PRINTTALK,  "%s", g_AdminSystem.Translate("NoclipDead"));
			return;
		}

		if(pPlayer->m_MoveType() == MOVETYPE_NOCLIP)
		{
			pPlayer->m_MoveType() = MOVETYPE_WALK;
			ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("NoclipDisable"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
		else
		{
			pPlayer->m_MoveType() = MOVETYPE_NOCLIP;
			ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("NoclipEnable"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
		}
	}
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

CON_COMMAND_CHAT_FLAGS(setteam, "set a player's team(without death)", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(player, HUD_PRINTTALK,  "%s!setteam <#userid|name> <team (0-3)>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	int iTeam = V_StringToInt32(args[2], -1);
	const char *teams[] = {"none", "spectators", "terrorists", "counter-terrorists"};
	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s", g_AdminSystem.Translate("Invalid Team"));
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	SwitchTeam(pTarget, iTeam);
	
	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Moved"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), teams[iTeam]);
}

CON_COMMAND_CHAT_FLAGS(changeteam, "set a player's team(with death)", ADMFLAG_SLAY)
{
	if (args.ArgC() < 3)
	{	
		ClientPrint(player, HUD_PRINTTALK,  "%s!changeteam <#userid|name> <team (0-3)>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	int iTeam = V_StringToInt32(args[2], -1);
	const char *teams[] = {"none", "spectators", "terrorists", "counter-terrorists"};
	if (iTeam < CS_TEAM_NONE || iTeam > CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s", g_AdminSystem.Translate("Invalid Team"));
		return;
	}

	CCSPlayerController *pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	pTarget->ChangeTeam(iTeam);
	
	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Moved"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName(), teams[iTeam]);
}

CON_COMMAND_CHAT_FLAGS(slap, "slap a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!slap <#userid|name> <optional damage>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
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
	
	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Slapped"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
}

CON_COMMAND_CHAT_FLAGS(slay, "slay a player", ADMFLAG_SLAY)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!slay <#userid|name>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iTarget = g_AdminSystem.TargetPlayerString(args[1]);

	if (iTarget == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Target not found"));
		return;
	}

	if(!g_AdminSystem.CheckImmunity(iTarget, iSlot, player))
		return;

	CCSPlayerController* pTarget = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(iTarget + 1));

	if (!pTarget)
		return;

	ClientPrintAll(HUD_PRINTTALK, g_AdminSystem.Translate("Slayed"), iSlot == -1?"Console":player->m_iszPlayerName(), pTarget->m_iszPlayerName());
	pTarget->m_hPlayerPawn().Get()->CommitSuicide(false, true);
}

CON_COMMAND_CHAT_FLAGS(map, "change map", ADMFLAG_CHANGEMAP)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s!map <mapname>", g_AdminSystem.Translate("Usage"));
		return;
	}

	char szMapName[MAX_PATH];
	V_strncpy(szMapName, args[1], sizeof(szMapName));

	if (!engine->IsMapValid(szMapName))
	{
		char sCommand[128];
		g_SMAPI->Format(sCommand, sizeof(sCommand), "ds_workshop_changelevel %s", args[1]);
		char szBuffer[256];
		g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map workshop"), args[1]);
		ClientPrint(player, HUD_PRINTTALK,  "%s", szBuffer);
		g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), args[1]);
		ClientPrintAll(HUD_PRINTTALK,  "%s", szBuffer);

		g_AdminSystem.CreateTimer([sCommand, szMapName]()
		{
			engine->ServerCommand(sCommand);
		}, 5.0);
		return;
	}

	char szBuffer[256];
	g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("Changing map"), szMapName);
	ClientPrintAll(HUD_PRINTTALK,  "%s", szBuffer);
	
	g_AdminSystem.CreateTimer([szMapName]()
	{
		engine->ChangeLevel(szMapName, nullptr);
	}, 5.0);
}

CON_COMMAND_CHAT_FLAGS(reload_admins, "Reload admin config", ADMFLAG_ROOT)
{
	if (!g_AdminSystem.LoadAdmins())
	{
		ClientPrint(player, HUD_PRINTTALK, "Error config load");
		return;
	}
	
	ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Admins reloaded"));
}

CON_COMMAND_CHAT_FLAGS(add_admin, "add admin", ADMFLAG_ROOT)
{
	if (args.ArgC() < 6)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!add_admin <admin_name> <steamid64> <duration(minutes)/0 (permanent)> <flags> <immunity> <optional comment>", g_AdminSystem.Translate("Usage"));
		return;
	}

	int iDuration = V_StringToInt32(args[3], -1);

	if (iDuration == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Invalid duration"));
		return;
	}

	int iImmunity = V_StringToInt32(args[5], -1);

	if (iImmunity == -1)
	{
		ClientPrint(player, HUD_PRINTTALK, "%s", g_AdminSystem.Translate("Invalid immunity"));
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
	g_pConnection->Query(szQuery, [args, szQuery2, szQuery3, player](IMySQLQuery* test)
	{
		auto results = test->GetResultSet();
		if(results->GetRowCount())
		{
			char szBuffer[256];
			g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("AddAdmin"), args[2]);
			ClientPrint(player, HUD_PRINTTALK,  "%s", szBuffer);
			g_pConnection->Query(szQuery2, [](IMySQLQuery* test){});
		}
		else
		{
			ClientPrint(player, HUD_PRINTTALK,  "%s", g_AdminSystem.Translate("UpdateAdmin"));
			g_pConnection->Query(szQuery3, [](IMySQLQuery* test){});
		}
		if (!g_AdminSystem.LoadAdmins())
		{
			ClientPrint(player, HUD_PRINTTALK, "Error config load");
			return;
		}
	});
}

CON_COMMAND_CHAT_FLAGS(remove_admin, "remove admin", ADMFLAG_ROOT)
{
	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK,  "%s!remove_admin <steamid64>", g_AdminSystem.Translate("Usage"));
		return;
	}

	char szQuery[512], szBuffer[256];
	g_SMAPI->Format(szBuffer, sizeof(szBuffer), g_AdminSystem.Translate("RemoveAdmin"), args[1]);
	ClientPrint(player, HUD_PRINTTALK,  "%s", szBuffer);

	g_SMAPI->Format(szQuery, sizeof(szQuery), "DELETE FROM `as_admins` WHERE `steamid` = '%s';", args[1]);
	g_pConnection->Query(szQuery, [](IMySQLQuery* test){});

	if (!g_AdminSystem.LoadAdmins())
	{
		ClientPrint(player, HUD_PRINTTALK, "Error config load");
		return;
	}
}

///////////////////////////////////////
const char* AdminSystem::GetLicense()
{
	return "GPL";
}

const char* AdminSystem::GetVersion()
{
	return "2.1.1";
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
