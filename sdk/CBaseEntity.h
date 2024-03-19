#pragma once
#include <entity2/entityidentity.h>
#include <utlsymbollarge.h>
#include "ehandle.h"
#include <baseentity.h>
#include "schemasystem.h"

inline CEntityInstance* UTIL_FindEntityByClassname(CEntityInstance* pStart, const char* name)
{
	extern CEntitySystem* g_pEntitySystem;
	CEntityIdentity* pEntity = pStart ? pStart->m_pEntity->m_pNext : g_pEntitySystem->m_EntityList.m_pFirstActiveEntity;

	for (; pEntity; pEntity = pEntity->m_pNext)
	{
		if (!strcmp(pEntity->m_designerName.String(), name))
			return pEntity->m_pInstance;
	};

	return nullptr;
}

class SC_CBaseEntity : public CBaseEntity
{
public:
	SCHEMA_FIELD(int32_t, CBaseEntity, m_iHealth);
	SCHEMA_FIELD(Vector, CBaseEntity, m_vecAbsVelocity);
	SCHEMA_FIELD(MoveType_t, CBaseEntity, m_MoveType);

	void SetAbsVelocity(Vector vecVelocity) { m_vecAbsVelocity() = vecVelocity; }

	void TakeDamage(int iDamage)
	{
		m_iHealth() = m_iHealth() - iDamage;
	}
};

class CCopyRecipientFilter
{
public:
	SCHEMA_FIELD(int32_t, CCopyRecipientFilter, m_Flags); // 0x8	
	SCHEMA_FIELD(CUtlVector<CPlayerSlot>, CCopyRecipientFilter, m_Recipients); // 0x10	
};

class CSoundEnvelope
{
public:
	SCHEMA_FIELD(float, CSoundEnvelope, m_current); // 0x0	
	SCHEMA_FIELD(float, CSoundEnvelope, m_target); // 0x4	
	SCHEMA_FIELD(float, CSoundEnvelope, m_rate); // 0x8	
	SCHEMA_FIELD(bool, CSoundEnvelope, m_forceupdate); // 0xc	
};

class CSoundPatch
{
public:
	SCHEMA_FIELD(CSoundEnvelope, CSoundPatch, m_pitch); // 0x8	
	SCHEMA_FIELD(CSoundEnvelope, CSoundPatch, m_volume); // 0x18
	SCHEMA_FIELD(float, CSoundPatch, m_shutdownTime); // 0x30	
	SCHEMA_FIELD(float, CSoundPatch, m_flLastTime); // 0x34	
	SCHEMA_FIELD(CUtlSymbolLarge, CSoundPatch, m_iszSoundScriptName); // 0x38	
	SCHEMA_FIELD(CHandle<SC_CBaseEntity>, CSoundPatch, m_hEnt); // 0x40	
	SCHEMA_FIELD(CEntityIndex, CSoundPatch, m_soundEntityIndex); // 0x44	
	SCHEMA_FIELD(Vector, CSoundPatch, m_soundOrigin); // 0x48	
	SCHEMA_FIELD(int32_t, CSoundPatch, m_isPlaying); // 0x54	
	SCHEMA_FIELD(CCopyRecipientFilter, CSoundPatch, m_Filter); // 0x58	
	SCHEMA_FIELD(float, CSoundPatch, m_flCloseCaptionDuration); // 0x80	
	SCHEMA_FIELD(bool, CSoundPatch, m_bUpdatedSoundOrigin); // 0x84	
	SCHEMA_FIELD(CUtlSymbolLarge, CSoundPatch, m_iszClassName); // 0x88	
};