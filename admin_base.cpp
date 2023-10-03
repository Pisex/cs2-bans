#include <stdio.h>
#include "admin_base.h"
#include <string>
#include <ctime>
#include "KeyValues.h"
#include <algorithm>
#include <vector>
#include <unordered_map>


SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*, const char*, bool);

AdminBase g_AdminBase;
IServerGameClients* gameclients = NULL;
IVEngineServer2* engine = NULL;
IFileSystem* filesystem = NULL;
ICvar* icvar = NULL;

KeyValues* g_hKV;

std::vector<std::string> g_AdmList;
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

CON_COMMAND_EXTERN(mm_kick, KickCommand, "Kick Clients");
void KickCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	if (slot.Get() != -1)
	{
		char steamid[64];
		g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 1)
			{
				int slotid = std::stoi(args[1]);
				if (engine->GetPlayerUserId(slotid).Get() == -1)
				{
					g_SMAPI->ClientConPrintf(slot, "[Admin] No valid player at UserID (%d) \n", slotid);
				}
				else
				{
					const char* steamid_target = engine->GetPlayerNetworkIDString(slotid);
					const char* name = engine->GetClientConVarValue(slotid, "name");
					g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Kicked!\n", name, steamid_target, slot);
					engine->DisconnectClient(CEntityIndex(slotid), 41);
				}
			}
			else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_kick <userid> \n");
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
}

CON_COMMAND_EXTERN(mm_ban, BanCommand, "Ban Clients");
void BanCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();
	if (slot.Get() != -1)
	{
		char steamid[64];
		g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 2)
			{
				int slotid = std::stoi(args[1]);
				if (engine->GetPlayerUserId(slotid).Get() == -1)
				{
					g_SMAPI->ClientConPrintf(slot, "[Admin] No valid player at UserID (%d)\n", slotid);
				}
				else
				{
					int time = std::stoi(args[2]);
					char steamid_target[64];
					g_SMAPI->Format(steamid_target, sizeof(steamid_target), "%d", engine->GetClientXUID(slotid));
					auto name = engine->GetClientConVarValue(slotid, "name");
					g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Banned!\n", name, steamid_target);
					engine->DisconnectClient(CEntityIndex(slotid), 41);
					g_hKV->SetInt(steamid_target, time == 0 ? 0:(std::time(0) + time));
					g_hKV->SaveToFile(filesystem, "addons/configs/bans.ini");
				}
			}
			else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_ban <userid> <timeban_second> \n");
		}
		else
		{
			g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
		}
	}
}

CON_COMMAND_EXTERN(mm_unban, UnBanCommand, "UnBan Clients");
void UnBanCommand(const CCommandContext& context, const CCommand& args)
{
	auto slot = context.GetPlayerSlot();

	if (slot.Get() != -1)
	{
		char steamid[64];
		g_SMAPI->Format(steamid, sizeof(steamid), "%s", ConvertSteamID(engine->GetPlayerNetworkIDString(slot)));
		if (std::find(g_AdmList.begin(), g_AdmList.end(), steamid) != g_AdmList.end())
		{
			if (args.ArgC() > 1)
			{
				auto steamid_target = args[1];
				if (g_hKV->FindKey(steamid_target, false))
				{
					g_hKV->FindAndDeleteSubKey(steamid_target);
					g_SMAPI->ClientConPrintf(slot, "[Admin] Steamid % s successfully unbanned!\n", steamid_target);
					g_hKV->SaveToFile(filesystem, "addons/configs/bans.ini");
				}
				else
				{
					g_SMAPI->ClientConPrintf(slot, "[Admin] Invalid steamid (%s)\n", steamid_target);
				}
			}
			else
			{
				g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: mm_unban <steamid>\n");
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	}
}

PLUGIN_EXPOSE(AdminBase, g_AdminBase);
bool AdminBase::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);

	SH_ADD_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);

	g_pCVar = icvar;
	ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);

	KeyValues* kv = new KeyValues("Config");
	kv->LoadFromFile(filesystem, "addons/configs/admins.ini");
	FOR_EACH_VALUE(kv, subkey)
	{
		g_AdmList.emplace_back(subkey->GetName());
	}
	delete kv;

	g_hKV = new KeyValues("Config");
	g_hKV->LoadFromFile(filesystem, "addons/configs/bans.ini");

	return true;
}

bool AdminBase::Unload(char* error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);

	g_AdmList.clear();

	return true;
}

void AdminBase::Hook_OnClientConnected(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
	if (!bFakePlayer)
	{
		char steamid[64];
		g_SMAPI->Format(steamid, sizeof(steamid), "%d", xuid);
		if (g_hKV->FindKey(steamid, false))
		{
			int time = g_hKV->GetInt(steamid);
			if (time == 0 || time > std::time(0))
			{
				engine->DisconnectClient(CEntityIndex(slot.Get()), 41);
			}
			else
			{
				g_hKV->FindAndDeleteSubKey(steamid);
				g_hKV->SaveToFile(filesystem, "addons/configs/bans.ini");
			}
		}
	}
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
	return ".|.";
}

const char* AdminBase::GetVersion()
{
	return "1.1";
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
