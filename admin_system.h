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
#include "sdk/CGameRules.h"
#include "sdk/module.h"
#include "include/mysql_mm.h"
#include "include/menus.h"
#include "include/admin.h"
#include "sdk/ctimer.h"
#include "funchook.h"
#include <map>
#include <ctime>
#include <array>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

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

class AdminApi : public IAdminApi
{
public:
    std::map<std::string, AdminCategory> m_Categories;
    std::map<std::string, std::map<std::string, AdminItem>> m_Items;
	
	void BanPlayer(int iSlot, int iAdmin, int iTime, const char* szReason);
    void MutePlayer(int iSlot, int iAdmin, int iTime, const char* szReason);
    void GagPlayer(int iSlot, int iAdmin, int iTime, const char* szReason);
	void OfflineBanPlayer(uint64_t SteamID, const char* szNick, int iAdmin, int iTime, const char* szReason);
	bool ClientInMuted(int iSlot);
	bool ClientInGagged(int iSlot);
	bool IsAdminFlagSet(int iSlot, int iFlag);
	bool ClientIsAdmin(int iSlot);
    int GetClientAdminFlags(int iSlot);
	int GetClientImmunity(int iSlot);
	int ReadFlagString(const char* szFlags);
    void RegAdminCategory(const char* szBack, const char* szFront, AdminCategoryCallback callback) override {
		m_Categories[std::string(szBack)] = {std::string(szFront), callback};
	}
    void RegAdminItem(const char* szBackCategory, const char* szBack, const char* szFront, int iFlag, AdminItemCallback callback) override {
		m_Items[std::string(szBackCategory)][std::string(szBack)] = {std::string(szFront), iFlag, callback};
	}

};

class CPlayer
{
public:
	CPlayer(CPlayerSlot slot, bool m_bFakeClient = false) : 
		m_slot(slot), m_bFakeClient(m_bFakeClient)
	{
		m_bAuthenticated = false;
		m_iGagged = -1;
		m_iMuted = -1;
		m_iSteamID = 0;
		m_iFlags = 0;
		m_iImmunity = 0;
		m_sName = "\0";
		m_iEnd = 0;
	}

	bool IsFakeClient() { return m_bFakeClient; }
	bool IsAuthenticated() { return m_bAuthenticated; }
	CPlayerSlot GetPlayerSlot() { return m_slot; }

	void SetAuthenticated() { m_bAuthenticated = true; }
	void SetMuted(int iDuration, int iEnd) { m_iMuted = iDuration != 0?iEnd:0; }
	void SetGagged(int iDuration, int iEnd) { m_iGagged = iDuration != 0?iEnd:0; }

	int GetMuted() { return m_iMuted; }
	int GetGagged() { return m_iGagged; }
	bool IsMuted() { return (m_iMuted != -1 && (m_iMuted == 0 || std::time(0) < m_iMuted))?true:false; }
	bool IsGagged() { return (m_iGagged != -1 && (m_iGagged == 0 || std::time(0) < m_iGagged))?true:false; }

	void SetAdminImmunity(int iImmunity) { m_iImmunity = iImmunity; };
	void SetAdminEnd(int iEnd) { m_iEnd = iEnd; };
	void SetSteamID(uint64 iSteamID) { m_iSteamID = iSteamID; };
	void SetAdminFlags(uint64 iFlags) { m_iFlags = iFlags; };
	void SetAdminName(const char* sName) { m_sName = sName; };

	int GetAdminImmunity() { return m_iImmunity; };
	uint64_t GetAdminEnd() { return m_iEnd; };
	uint64 GetSteamID() { return m_iSteamID; };
	uint64 GetAdminFlags() { return m_iFlags; };
	const char* GetAdminName() { return m_sName; };
private:
	CPlayerSlot m_slot;
	bool m_bFakeClient;
	bool m_bAuthenticated;
	uint64_t m_iMuted;
	uint64_t m_iGagged;

	uint64 m_iSteamID;
	uint64 m_iFlags;
	int m_iImmunity;
	const char* m_sName;
	uint64_t m_iEnd;
};

class AdminSystem final : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
	bool Unload(char* error, size_t maxlen);
	bool LoadAdmins();
	void AllPluginsLoaded();
	void* OnMetamodQuery(const char* iface, int* ret);
	bool IsAdminFlagSet(int iSlot, uint64 iFlag);
	bool IsAdmin(int iSlot);
	int TargetPlayerString(const char* target);
	// void CreateTimer(std::function<void()> fn, uint64_t time);
	const char* Translate(const char* phrase);
	bool CheckImmunity(int iTarget, int iAdmin);
	void ParseChatCommand(int iSlot, const char *pMessage, CCSPlayerController *pController);
	void CheckInfractions(int PlayerSlot, int bAdmin);
	uint64 ParseFlags(const char* pszFlags);
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
	bool Hook_ClientConnect( CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID, bool unk1, CBufferString *pRejectReason );
	void Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick);
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
