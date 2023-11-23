#pragma once
#include <entity2/entityidentity.h>
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