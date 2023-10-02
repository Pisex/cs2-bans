#include <stdio.h>
#include "admin_base.h"
#include <string>
#include <ctime>
#include "KeyValues.h"

SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*, const char*, bool);

AdminBase g_AdminBase;
IServerGameClients *gameclients = NULL;
IVEngineServer *engine = NULL;
IFileSystem* filesystem = NULL;
ICvar* icvar = NULL;

using namespace std;

CON_COMMAND_EXTERN(mm_ban, BanCommand, "Ban Clients");
void BanCommand(const CCommandContext& context, const CCommand& args)
{
	auto cmd = args.GetCommandString();
	auto first = args[0];
	auto slot = context.GetPlayerSlot();
	auto steamid = engine->GetPlayerNetworkIDString(slot);
	KeyValues* kv = new KeyValues("Config");
	kv->LoadFromFile(filesystem, "addons/configs/admins.ini");
	if(kv->GetBool(steamid))
	{
		if (args.ArgC() > 2)
		{
			int slotid = std::stoi(args[1]);
			if (engine->GetPlayerUserId(slotid).Get() == -1)
			{
				g_SMAPI->ClientConPrintf(slot, "[Admin] No valid player at UserID (%d) \n", slotid);
			}
			else
			{
				auto steamid_target = engine->GetPlayerNetworkIDString(slotid);
				auto name = engine->GetClientConVarValue(slotid, "name");
				g_SMAPI->ClientConPrintf(slot, "[Admin] Player %s(%s) success Banned!\n", name, steamid_target);
				engine->DisconnectClient(CEntityIndex(slotid), 41);
				KeyValues* kv = new KeyValues("Config");
				kv->LoadFromFile(filesystem, "addons/configs/bans.ini");
				kv->SetInt(steamid_target, std::time(0) + std::stoi(args[2]));
				kv->SaveToFile(filesystem, "addons/configs/bans.ini");
				delete kv;
			}
		}
		else g_SMAPI->ClientConPrintf(slot, "[Admin] Usage: %s <userid> <timeban_second> \n", first);
	}
	else g_SMAPI->ClientConPrintf(slot, "[Admin] You are denied access\n");
	delete kv;
}

PLUGIN_EXPOSE(AdminBase, g_AdminBase);
bool AdminBase::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_CURRENT(GetEngineFactory, icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, filesystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);

	SH_ADD_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);

	g_pCVar = icvar;
	ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_GAMEDLL);
	return true;
}

bool AdminBase::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameClients, OnClientConnected, gameclients, this, &AdminBase::Hook_OnClientConnected, false);
	return true;
}

void AdminBase::Hook_OnClientConnected(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
	if(!bFakePlayer)
	{
		auto steamid = engine->GetPlayerNetworkIDString(slot);
		KeyValues* kv = new KeyValues("Config");
		kv->LoadFromFile(filesystem, "addons/configs/bans.ini");
		if (kv->GetInt(steamid) > std::time(0)) engine->DisconnectClient(CEntityIndex(slot.Get()), 41);
		delete kv;
	}
}

bool AdminBase::Pause(char *error, size_t maxlen)
{
	return true;
}

bool AdminBase::Unpause(char *error, size_t maxlen)
{
	return true;
}

const char *AdminBase::GetLicense()
{
	return ".|.";
}

const char *AdminBase::GetVersion()
{
	return "1.0";
}

const char *AdminBase::GetDate()
{
	return __DATE__;
}

const char *AdminBase::GetLogTag()
{
	return "ADMIN";
}

const char *AdminBase::GetAuthor()
{
	return "Pisex";
}

const char *AdminBase::GetDescription()
{
	return "Admin function plugin";
}

const char *AdminBase::GetName()
{
	return "Base Admin";
}

const char *AdminBase::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
