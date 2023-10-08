#include <stdio.h>
#include "admin_base.h"
#include <string>
#include <ctime>
#include <algorithm>
#include "KeyValues.h"
#include <vector>

SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*, const char*, bool);

AdminBase g_AdminBase;
IServerGameDLL* server = NULL;
IServerGameClients* gameclients = NULL;
IVEngineServer2* engine = NULL;
IFileSystem* filesystem = NULL;
ICvar* icvar = NULL;

KeyValues* g_hKV;

struct APlayer {
	bool IsAuthenticated = false;
	bool IsFakeClient = true;
};

APlayer APlayerDate[64];

int g_iLastCheck = 0;
std::vector<std::string> g_AdmList;

void ProcessBan(int slot, int time_minutes, const char* name, bool isConsole)
{
    char steamid_target[64];
    g_SMAPI->Format(steamid_target, sizeof(steamid_target), "%d", engine->GetClientXUID(slot));
    int time_seconds = (time_minutes > 0) ? (time_minutes * 60) : 0;

    if (isConsole) {
        META_CONPRINTF("[Admin] Player %s(%s) success Banned for %d minute(s)!\n", name, steamid_target, time_minutes);
    } else {
        g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Banned for %d minute(s)!\n", name, steamid_target, time_minutes);
    }
    engine->DisconnectClient(CEntityIndex(slot), 41);
    g_hKV->SetInt(steamid_target, time_seconds);
    g_hKV->SaveToFile(filesystem, "addons/configs/admin_base/bans.ini");
}

bool containsOnlyDigits(const std::string& str) {
	return str.find_first_not_of("0123456789") == std::string::npos;
}
char* ConvertSteamID(const char* usteamid) {
	char* steamid = new char[strlen(usteamid) + 1];
	strcpy(steamid, usteamid);

	for (char* ch = steamid; *ch; ++ch) {
		if (*ch == '[' || *ch == ']') {
			memmove(ch, ch + 1, strlen(ch));
		}
	}

	char* token = strtok(steamid, ":");
	char* usteamid_split[3];

	int i = 0;
	while (token != nullptr) {
		usteamid_split[i++] = token;
		token = strtok(nullptr, ":");
	}

	char steamid_parts[3][12] = { "STEAM_1:", "", "" };

	int z = atoi(usteamid_split[2]);

	if (z % 2 == 0) {
		strcpy(steamid_parts[1], "0:");
	}
	else {
		strcpy(steamid_parts[1], "1:");
	}

	int steamacct = z / 2;

	snprintf(steamid_parts[2], sizeof(steamid_parts[2]), "%d", steamacct);

	char* result = new char[strlen(steamid_parts[0]) + strlen(steamid_parts[1]) + strlen(steamid_parts[2]) + 1];
	strcpy(result, steamid_parts[0]);
	strcat(result, steamid_parts[1]);
	strcat(result, steamid_parts[2]);

	delete[] steamid;

	return result;
}

CON_COMMAND_EXTERN(mm_reload_admins, ReloadAdminsCommand, "Reload admins.ini");
CON_COMMAND_EXTERN(mm_kick, KickCommand, "Kick Clients");
CON_COMMAND_EXTERN(mm_ban, BanCommand, "Ban Clients");
CON_COMMAND_EXTERN(mm_unban, UnBanCommand, "UnBan Clients");
CON_COMMAND_EXTERN(mm_map, ChangeMapCommand, "Change map");

void ReloadAdminsCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	bool isConsole = slot.Get() == -1;
	if (isConsole)
	{
		g_AdmList.clear();
		KeyValues* kv = new KeyValues("Config");
		kv->LoadFromFile(filesystem, "addons/configs/admin_base/admins.ini");
		FOR_EACH_VALUE(kv, subkey)
		{
			g_AdmList.emplace_back(subkey->GetName());
		}
		delete kv;
		META_CONPRINT("[Admin] The list of administrators has been reloaded");
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
}

void KickCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	bool isConsole = slot.Get() == -1;

	if (isConsole || APlayerDate[slot.Get()].IsAuthenticated)
	{
		char steamid[64];
		if (!isConsole) g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (isConsole || std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 1 && args[1][0])
			{
				if (containsOnlyDigits(args[1]))
				{
					int slotid = std::stoi(args[1]);
					if (engine->GetPlayerUserId(slotid).Get() == -1)
					{
						if (isConsole) META_CONPRINTF("[Admin] No valid player at UserID (%d) \n", slotid);
						else g_SMAPI->ClientConPrintf(slot, "[Admin] No valid player at UserID (%d) \n", slotid);
					}
					else
					{
						const char* steamid_target = engine->GetPlayerNetworkIDString(slotid);
						const char* name = engine->GetClientConVarValue(slotid, "name");
						if (isConsole) META_CONPRINTF("[Admin] Player %s(%s) success Kicked!\n", name, steamid_target);
						else g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Kicked!\n", name, steamid_target);
						engine->DisconnectClient(CEntityIndex(slotid), 39);
					}
				}
				else
				{
					bool isFound = false;
					auto nick = args[1];
					for (int i = 0; i < 64; i++)
					{
						if (engine->GetPlayerUserId(i).Get() == -1)
							continue;
						if (APlayerDate[i].IsFakeClient == true)
							continue;
						const char* name = engine->GetClientConVarValue(i, "name");
						if (strstr(name, nick) != NULL)
						{
							isFound = true;
							const char* steamid_target = engine->GetPlayerNetworkIDString(i);
							if (isConsole) META_CONPRINTF("[Admin] Player %s(%s) success Kicked!\n", name, steamid_target);
							else g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Kicked!\n", name, steamid_target);
							engine->DisconnectClient(CEntityIndex(i), 39);
						}
					}
					if (!isFound)
					{
						if (isConsole) META_CONPRINTF("[Admin] Player not found\n");
						else g_SMAPI->ClientConPrintf(slot, "[Admin] Player not found\n");
					}
				}
			}
			else
			{
				if (isConsole) META_CONPRINTF("[Admin] Usage: mm_kick <userid|nickname> \n");
				else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_kick <userid|nickname> \n");
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] Please, await...\n");
}

void BanCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	bool isConsole = slot.Get() == -1;
	if (isConsole || APlayerDate[slot.Get()].IsAuthenticated)
	{
		char steamid[64];
		if(!isConsole) g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (isConsole || std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 2 && args[1][0] && args[2][0])
			{
				if (containsOnlyDigits(args[1]))
				{
					int slotid = std::stoi(args[1]);
					if (engine->GetPlayerUserId(slotid).Get() == -1)
					{
						if (isConsole) META_CONPRINTF("[Admin] No valid player at UserID (%d)\n", slotid);
						else g_SMAPI->ClientConPrintf(slot, "[Admin] No valid player at UserID (%d)\n", slotid);
					}
					else
					{
						int time = std::stoi(args[2]);
						auto name = engine->GetClientConVarValue(slotid, "name");
						ProcessBan(slotid, time, name, isConsole);
					}
				}
				else
				{
					bool isFound = false;
					auto nick = args[1];
					for (int i = 0; i < 64; i++)
					{
						if (engine->GetPlayerUserId(i).Get() == -1)
							continue;
						if (APlayerDate[i].IsFakeClient == true)
							continue;
						const char* name = engine->GetClientConVarValue(i, "name");
						if (strstr(name, nick) != NULL)
						{
							isFound = true;
							int time = std::stoi(args[2]);
							ProcessBan(i, time, name, isConsole);
						}
					}
					if (!isFound)
					{
						if (isConsole) META_CONPRINTF("[Admin] Player not found\n");
						else g_SMAPI->ClientConPrintf(slot, "[Admin] Player not found\n");
					}
				}
			}
			else
			{
				if (isConsole) META_CONPRINTF("[Admin] Usage: mm_ban <userid|nickname> <timeban_minute> \n");
				else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_ban <userid|nickname> <timeban_minute> \n");
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] Please, await...\n");
}

void UnBanCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	bool isConsole = slot.Get() == -1;
	if (isConsole || APlayerDate[slot.Get()].IsAuthenticated)
	{
		char steamid[64];
		if (!isConsole) g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (isConsole || std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 1 && args[1][0])
			{
				auto steamid_target = args[1];
				if (g_hKV->FindKey(steamid_target, false))
				{
					g_hKV->FindAndDeleteSubKey(steamid_target);
					if (isConsole) META_CONPRINTF("[Admin] Steamid % s successfully unbanned!\n", steamid_target);
					else g_SMAPI->ClientConPrintf(slot, "[Admin] Steamid % s successfully unbanned!\n", steamid_target);
					g_hKV->SaveToFile(filesystem, "addons/configs/admin_base/bans.ini");
				}
				else
				{
					if (isConsole) META_CONPRINTF("[Admin] Invalid steamid (%s)\n", steamid_target);
					else g_SMAPI->ClientConPrintf(slot, "[Admin] Invalid steamid (%s)\n", steamid_target);
				}
			}
			else
			{
				if (isConsole) META_CONPRINTF("[Admin] Usage: mm_unban <steamid>\n");
				else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_unban <steamid>\n");
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] Please, await...\n");
}

void ChangeMapCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	bool isConsole = slot.Get() == -1;
	if (isConsole || APlayerDate[slot.Get()].IsAuthenticated)
	{
		char steamid[64];
		if (!isConsole) g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (isConsole || std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 1 && args[1][0])
			{
				if (engine->IsMapValid(args[1]))
				{
					if (isConsole) META_CONPRINTF("[Admin] Changing the map to %s\n", args[1]);
					else g_SMAPI->ClientConPrintf(slot, "[Admin] Changing the map to %s\n", args[1]);
					char buf[MAX_PATH];
					g_SMAPI->Format(buf, sizeof(buf), "changelevel %s", args[1]);
					engine->ServerCommand(buf);
				}
				else
				{
					if (isConsole) META_CONPRINT("[Admin] Invalid map name\n");
					else g_SMAPI->ClientConPrintf(slot, "[Admin] Invalid map name\n");
				}
			}
			else
			{
				if (isConsole) META_CONPRINTF("[Admin] Usage: mm_map <mapname>\n");
				else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_map <mapname>\n");
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] Please, await...\n");
}

PLUGIN_EXPOSE(AdminBase, g_AdminBase);
bool AdminBase::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);

	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, server, this, &AdminBase::Hook_GameFrame, false);
	SH_ADD_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);

	g_pCVar = icvar;
	ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

	KeyValues* kv = new KeyValues("Config");
	kv->LoadFromFile(filesystem, "addons/configs/admin_base/admins.ini");
	FOR_EACH_VALUE(kv, subkey)
	{
		g_AdmList.emplace_back(subkey->GetName());
	}
	delete kv;

	g_hKV = new KeyValues("Config");
	g_hKV->LoadFromFile(filesystem, "addons/configs/admin_base/bans.ini");

	return true;
}

void AdminBase::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
	if (g_iLastCheck == 0) g_iLastCheck = std::time(0);
	else
	{
		if (std::time(0) - g_iLastCheck < 1) return;
	}

	for (int i = 0; i < 64; i++)
	{
		if(engine->GetPlayerUserId(i).Get() == -1)
			continue;
		if (APlayerDate[i].IsFakeClient == true)
			continue;
		if (APlayerDate[i].IsAuthenticated)
			continue;

		if (engine->IsClientFullyAuthenticated(CPlayerSlot(i)))
		{
			APlayerDate[i].IsAuthenticated = true;
			char steamid[64];
			g_SMAPI->Format(steamid, sizeof(steamid), "%d", engine->GetClientXUID(CPlayerSlot(i)));
			if (g_hKV->FindKey(steamid, false))
			{
				int time = g_hKV->GetInt(steamid);
				if (time == 0 || time > std::time(0))
				{
					engine->DisconnectClient(CEntityIndex(i), 41);
				}
				else
				{
					g_hKV->FindAndDeleteSubKey(steamid);
					g_hKV->SaveToFile(filesystem, "addons/configs/admin_base/bans.ini");
				}
			}
		}
	}
}

bool AdminBase::Unload(char* error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, server, this, &AdminBase::Hook_GameFrame, false);
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);

	g_AdmList.clear();

	ConVar_Unregister();

	return true;
}

void AdminBase::Hook_OnClientConnected(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
	APlayerDate[slot.Get()].IsFakeClient = bFakePlayer;
	APlayerDate[slot.Get()].IsAuthenticated = false;
}

bool AdminBase::Pause(char* error, size_t maxlen)
{
	return true;
}

bool AdminBase::Unpause(char* error, size_t maxlen)
{
	return true;
}

const char* AdminBase::GetLicense()
{
	return "Free";
}

const char* AdminBase::GetVersion()
{
	return "1.2";
}

const char* AdminBase::GetDate()
{
	return __DATE__;
}

const char* AdminBase::GetLogTag()
{
	return "ADMIN";
}

const char* AdminBase::GetAuthor()
{
	return "Pisex";
}

const char* AdminBase::GetDescription()
{
	return "Admin function plugin";
}

const char* AdminBase::GetName()
{
	return "Base Admin";
}

const char* AdminBase::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
