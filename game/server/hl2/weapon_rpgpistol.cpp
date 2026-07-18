//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: RPG PISTOL - Independent Standalone Weapon
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "weapon_rpg.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "gamestats.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CWeaponRPGPistol : public CBaseHLCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponRPGPistol, CBaseHLCombatWeapon );

	CWeaponRPGPistol(void);

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void	Precache( void );
	void	PrimaryAttack( void );
	void	AddViewKick( void );
	void	Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }

	virtual const Vector& GetBulletSpread( void )
	{
		static Vector cone = VECTOR_CONE_1DEGREES;
		return cone;
	}
	
	virtual float GetFireRate( void ) { return 0.05f; }

	DECLARE_ACTTABLE();

private:
	float	m_flSoonestPrimaryAttack;
	float	m_flLastAttackTime;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponRPGPistol, DT_WeaponRPGPistol)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_rpgpistol, CWeaponRPGPistol );
PRECACHE_WEAPON_REGISTER( weapon_rpgpistol );

BEGIN_DATADESC( CWeaponRPGPistol )
	DEFINE_FIELD( m_flSoonestPrimaryAttack, FIELD_TIME ),
	DEFINE_FIELD( m_flLastAttackTime,		FIELD_TIME ),
END_DATADESC()

acttable_t	CWeaponRPGPistol::m_acttable[] = 
{
	{ ACT_IDLE,						ACT_IDLE_PISTOL,				true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_PISTOL,			true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_PISTOL,		true },
	{ ACT_RELOAD,					ACT_RELOAD_PISTOL,				true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_PISTOL,			true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_PISTOL,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_PISTOL,true },
};
IMPLEMENT_ACTTABLE( CWeaponRPGPistol );

CWeaponRPGPistol::CWeaponRPGPistol( void )
{
	m_flSoonestPrimaryAttack = gpGlobals->curtime;
	m_fMinRange1		= 24;
	m_fMaxRange1		= 1500;
	m_bFiresUnderwater	= true;
}

void CWeaponRPGPistol::Precache( void )
{
	BaseClass::Precache();
	UTIL_PrecacheOther( "rpg_missile" );
	PrecacheModel( "models/weapons/v_pistol.mdl" );
	PrecacheModel( "models/weapons/w_pistol.mdl" );
}

void CWeaponRPGPistol::PrimaryAttack( void )
{
	m_flLastAttackTime = gpGlobals->curtime;
	m_flNextPrimaryAttack = gpGlobals->curtime + 0.05f;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if( !pOwner ) return;

	Vector vecOrigin, vecForward;
	pOwner->EyeVectors( &vecForward );
	vecOrigin = pOwner->Weapon_ShootPosition();

	CMissile *pMissile = CMissile::Create(
		vecOrigin + vecForward * 32.0f,
		pOwner->EyeAngles(),
		pOwner->edict() );
	if ( pMissile )
	{
		pMissile->SetAbsVelocity( vecForward * 1500.0f );
	}
	
	WeaponSound( SINGLE );
	pOwner->DoMuzzleFlash();
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pOwner->SetAnimation( PLAYER_ATTACK1 );
	
	pOwner->ViewPunchReset();
}

void CWeaponRPGPistol::AddViewKick( void )
{
	CBasePlayer *pPlayer  = ToBasePlayer( GetOwner() );
	if ( pPlayer )
	{
		QAngle vp;
		vp.x = random->RandomFloat( 0.25f, 0.5f );
		vp.y = random->RandomFloat( -.6f, .6f );
		vp.z = 0.0f;
		pPlayer->ViewPunch( vp );
	}
}

void CWeaponRPGPistol::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
}
