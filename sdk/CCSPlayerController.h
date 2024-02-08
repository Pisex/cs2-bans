#pragma once
#include "CBasePlayerController.h"
#include "CCSPlayerPawn.h"
#include "ehandle.h"
#include "schemasystem.h"

class CCSPlayerController : public CBasePlayerController
{
public:
	SCHEMA_FIELD(CHandle<CCSPlayerPawn>, CCSPlayerController, m_hPlayerPawn);

	void ChangeTeam(int iTeam)
	{
		CALL_VIRTUAL(void, WIN_LINUX(93,92), this, iTeam);
	}
};