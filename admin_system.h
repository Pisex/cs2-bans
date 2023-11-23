#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_

#include <ISmmPlugin.h>
#include <sh_vector.h>
#include "utlvector.h"
#include "ehandle.h"
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "igameevents.h"
#include "vector.h"
#include "defines.h"
#include <deque>
#include <functional>
#include "sdk/utils.hpp"
#include <utlstring.h>
#include <KeyValues.h>
#include "sdk/schemasystem.h"
#include "sdk/CBaseEntity.h"
#include "sdk/CBasePlayerPawn.h"
#include "sdk/CCSPlayerController.h"
#include "sdk/module.h"
#include "include/mysql_mm.h"
#include "funchook.h"
#include <map>
#include <ctime>
#include <array>
#include <chrono>

class CChatCommand;

typedef void (*FnChatCommandCallback_t)(int iSlot, const CCommand &args, CCSPlayerController *player);

extern CUtlMap<uint32, CChatCommand*> g_CommandList;

class CChatCommand
{
public:
	CChatCommand(const char *cmd, FnChatCommandCallback_t callback, uint64 flags = ADMFLAG_NONE) :
		m_pfnCallback(callback), m_nFlags(flags)
	{
		g_CommandList.Insert(hash_32_fnv1a_const(cmd), this);
	}

	void operator()(int iSlot, const CCommand &args, CCSPlayerController *player)
	{
		if (player && !CheckCommandAccess(iSlot, player, m_nFlags))
			return;

		m_pfnCallback(iSlot, args, player);
	}

	static bool CheckCommandAccess(int iSlot, CBasePlayerController *pPlayer, uint64 flags);

private:
	FnChatCommandCallback_t m_pfnCallback;
	uint64 m_nFlags;
};

#define CON_COMMAND_CHAT_FLAGS(name, description, flags)																								\
	void name##_callback(int iSlot, const CCommand &args, CCSPlayerController *player);																	\
	static CChatCommand name##_chat_command(#name, name##_callback, flags);																				\
	static void name##_con_callback(const CCommandContext &context, const CCommand &args)																\
	{																																					\
		CCSPlayerController *pController = nullptr;																										\
		if (context.GetPlayerSlot().Get() != -1)																										\
			pController = (CCSPlayerController *)g_pEntitySystem->GetBaseEntity((CEntityIndex)(context.GetPlayerSlot().Get() + 1));						\
																																						\
		name##_chat_command(context.GetPlayerSlot().Get(), args, pController);																			\
	}																																					\
	static ConCommandRefAbstract name##_ref;																											\
	static ConCommand name##_command(&name##_ref, COMMAND_PREFIX#name, name##_con_callback,																\
									description, FCVAR_CLIENT_CAN_EXECUTE | FCVAR_LINKED_CONCOMMAND);													\
	void name##_callback(int iSlot, const CCommand &args, CCSPlayerController *player)

#define CON_COMMAND_CHAT(name, description) CON_COMMAND_CHAT_FLAGS(name, description, ADMFLAG_NONE)

class CPlayer
{
public:
	CPlayer(CPlayerSlot slot, bool m_bFakeClient = false) : 
		m_slot(slot), m_bFakeClient(m_bFakeClient)
	{
		m_bAuthenticated = false;
		m_iGagged = -1;
		m_iMuted = -1;
	}

	bool IsFakeClient() { return m_bFakeClient; }
	bool IsAuthenticated() { return m_bAuthenticated; }
	uint64 GetSteamId64() { return m_SteamID->ConvertToUint64(); }
	const CSteamID* GetSteamId() { return m_SteamID; }
	CPlayerSlot GetPlayerSlot() { return m_slot; }

	void SetAuthenticated() { m_bAuthenticated = true; }
	void SetMuted(int iDuration, int iEnd) { m_iMuted = iDuration != 0?iEnd:0; }
	void SetGagged(int iDuration, int iEnd) { m_iGagged = iDuration != 0?iEnd:0; }
	void SetSteamId(const CSteamID* steamID) { m_SteamID = steamID; }

	int GetMuted() { return m_iMuted; }
	int GetGagged() { return m_iGagged; }
	bool IsMuted() { return (m_iMuted != -1 && (m_iMuted == 0 || std::time(0) < m_iMuted))?true:false; }
	bool IsGagged() { return (m_iGagged != -1 && (m_iGagged == 0 || std::time(0) < m_iGagged))?true:false; }
private:
	CPlayerSlot m_slot;
	const CSteamID* m_SteamID;
	bool m_bFakeClient;
	bool m_bAuthenticated;
	uint64_t m_iMuted;
	uint64_t m_iGagged;
};

class CAdmin
{
public:
	CAdmin(uint64 iSteamID, uint64 iFlags) : 
		m_iSteamID(iSteamID), m_iFlags(iFlags)
	{}

	uint64 GetSteamID() { return m_iSteamID; };
	uint64 GetFlags() { return m_iFlags; };
private:
	uint64 m_iSteamID;
	uint64 m_iFlags;
};

class AdminSystem final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	bool LoadAdmins();
	void AllPluginsLoaded();
	CAdmin* FindAdmin(int slot);
	bool IsAdminFlagSet(CAdmin aAdmin, uint64 iFlag, int iSlot);
	int TargetPlayerString(const char* target);
	CPlayer* GetPlayer(int slot);
	void CreateTimer(std::function<void()> fn, uint64_t time);
private:
	const char* GetAuthor();
	const char* GetName();
	const char* GetDescription();
	const char* GetURL();
	const char* GetLicense();
	const char* GetVersion();
	const char* GetDate();
	const char* GetLogTag();

private: // Hooks
	void StartupServer(const GameSessionConfiguration_t& config, ISource2WorldSession*, const char*);
	void Hook_DispatchConCommand(ConCommandHandle cmd, const CCommandContext& ctx, const CCommand& args);
	void Hook_OnClientDisconnect(CPlayerSlot slot, int reason, const char *pszName, uint64 xuid, const char *pszNetworkID);
	void Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type, uint64 xuid);
	void Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick);

	void ParseChatCommand(int iSlot, const char *pMessage, CCSPlayerController *pController);
	void CheckInfractions(int slot);

	uint64 ParseFlags(const char* pszFlags);

	std::deque<std::function<void()>> m_Timer;
	std::deque<uint64_t> m_TimerTime;

	CUtlVector<CAdmin> m_vecAdmins;
	CPlayer *m_vecPlayers[MAXPLAYERS];
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
