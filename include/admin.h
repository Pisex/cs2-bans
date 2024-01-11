#pragma once

#include <functional>
#include <string>

#define Admin_INTERFACE "IAdminApi"

#define ADMFLAG_NONE		(0)
#define ADMFLAG_RESERVATION (1 << 0)  // a
#define ADMFLAG_GENERIC		(1 << 1)  // b
#define ADMFLAG_KICK		(1 << 2)  // c
#define ADMFLAG_BAN			(1 << 3)  // d
#define ADMFLAG_UNBAN		(1 << 4)  // e
#define ADMFLAG_SLAY		(1 << 5)  // f
#define ADMFLAG_CHANGEMAP	(1 << 6)  // g
#define ADMFLAG_CONVARS		(1 << 7)  // h
#define ADMFLAG_CONFIG		(1 << 8)  // i
#define ADMFLAG_CHAT		(1 << 9)  // j
#define ADMFLAG_VOTE		(1 << 10) // k
#define ADMFLAG_PASSWORD	(1 << 11) // l
#define ADMFLAG_RCON		(1 << 12) // m
#define ADMFLAG_CHEATS		(1 << 13) // n
#define ADMFLAG_CUSTOM1		(1 << 14) // o
#define ADMFLAG_CUSTOM2		(1 << 15) // p
#define ADMFLAG_CUSTOM3		(1 << 16) // q
#define ADMFLAG_CUSTOM4		(1 << 17) // r
#define ADMFLAG_CUSTOM5		(1 << 18) // s
#define ADMFLAG_CUSTOM6		(1 << 19) // t
#define ADMFLAG_CUSTOM7		(1 << 20) // u
#define ADMFLAG_CUSTOM8		(1 << 21) // v
#define ADMFLAG_CUSTOM9		(1 << 22) // w
#define ADMFLAG_CUSTOM10	(1 << 23) // x
#define ADMFLAG_CUSTOM11	(1 << 24) // y
#define ADMFLAG_ROOT		(1 << 25) // z

// typedef std::function<bool(int iSlot, const char* szContent)> CommandCallback;
// typedef std::function<void(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)> EventCallback;
typedef std::function<void(const char* szName, int iSlot)> AdminCategoryCallback;
typedef std::function<void(const char* szName, int iSlot)> AdminItemCallback;

struct AdminCategory
{
    std::string szFront;
    AdminCategoryCallback callback;
};

struct AdminItem
{
    std::string szFront;
    int iFlag;
    AdminItemCallback callback;
};



class IAdminApi
{
public:
    virtual bool IsAdminFlagSet(int iSlot, int iFlag) = 0;
    virtual int ReadFlagString(const char* szFlags) = 0;
    virtual bool ClientIsAdmin(int iSlot) = 0;
    virtual int GetClientAdminFlags(int iSlot) = 0;
    virtual int GetClientImmunity(int iSlot) = 0;
    virtual bool ClientInMuted(int iSlot) = 0;
    virtual bool ClientInGagged(int iSlot) = 0;
    virtual void RegAdminCategory(const char* szBack, const char* szFront, AdminCategoryCallback callback = nullptr) = 0;
    virtual void RegAdminItem(const char* szBackCategory, const char* szBack, const char* szFront, int iFlag, AdminItemCallback callback) = 0;
    virtual void OfflineBanPlayer(uint64_t SteamID, const char* szNick, int iAdmin, int iTime, const char* szReason) = 0;
    virtual void BanPlayer(int iSlot, int iAdmin, int iTime, const char* szReason) = 0;
    virtual void MutePlayer(int iSlot, int iAdmin, int iTime, const char* szReason) = 0;
    virtual void GagPlayer(int iSlot, int iAdmin, int iTime, const char* szReason) = 0;
};