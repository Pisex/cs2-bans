#pragma once
#include "CBaseCombatCharacter.h"
#include "CBasePlayerController.h"
#include "ehandle.h"
#include "schemasystem.h"
#include "virtual.h"

class CBasePlayerPawn : public CBaseCombatCharacter
{
public:
	SCHEMA_FIELD(CHandle<CBasePlayerController>, CBasePlayerPawn, m_hController);
	void TakeDamage(int iDamage)
	{
		if (m_iHealth() - iDamage <= 0)
			CommitSuicide(false, true);
		else
			SC_CBaseEntity::TakeDamage(iDamage);
	}
	void CommitSuicide(bool bExplode, bool bForce)
	{
		CALL_VIRTUAL(void, 360, this, bExplode, bForce);
	}
};