//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Base Object built by players
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "tf_player.h"
#include "tf_team.h"
#include "tf_obj.h"
#include "tf_weapon_wrench.h"
#include "tf_weaponbase.h"
#include "rope.h"
#include "rope_shared.h"
#include "bone_setup.h"
#include "ndebugoverlay.h"
#include "rope_helpers.h"
#include "IEffects.h"
#include "vstdlib/random.h"
#include "tier1/strtools.h"
#include "basegrenade_shared.h"
#include "tf_gamerules.h"
#include "engine/IEngineSound.h"
#include "tf_shareddefs.h"
#include "vguiscreen.h"
#include "hierarchy.h"
#include "func_no_build.h"
#include "func_respawnroom.h"
#include <KeyValues.h>
#include "ihasbuildpoints.h"
#include "utldict.h"
#include "filesystem.h"
#include "npcevent.h"
#include "tf_shareddefs.h"
#include "animation.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "tf_gamestats.h"
#include "tf_ammo_pack.h"
#include "tf_obj_sapper.h"
#include "particle_parse.h"
#include "tf_fx.h"
#include "world.h"
#include "ai_basenpc.h"
#include "lfe_buildpoint.h"
#include "vphysics/constraints.h"
#include "physics_saverestore.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Control panels
#define SCREEN_OVERLAY_MATERIAL "vgui/screens/vgui_overlay"

#define ROPE_HANG_DIST	150

ConVar tf_obj_gib_velocity_min( "tf_obj_gib_velocity_min", "100", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar tf_obj_gib_velocity_max( "tf_obj_gib_velocity_max", "450", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar tf_obj_gib_maxspeed( "tf_obj_gib_maxspeed", "800", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );


ConVar object_verbose( "object_verbose", "0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Debug object system." );
ConVar obj_damage_factor( "obj_damage_factor","0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Factor applied to all damage done to objects" );
ConVar obj_child_damage_factor( "obj_child_damage_factor","0.25", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Factor applied to damage done to objects that are built on a buildpoint" );
ConVar tf_fastbuild("tf_fastbuild", "0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar tf_obj_ground_clearance( "tf_obj_ground_clearance", "32", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Object corners can be this high above the ground" );

ConVar lfe_obj_physics( "lfe_obj_physics", "0", FCVAR_NOTIFY | FCVAR_REPLICATED, "Enable physics on buildings." );

extern short g_sModelIndexFireball;

// Minimum distance between 2 objects to ensure player movement between them
#define MINIMUM_OBJECT_SAFE_DISTANCE		100

// Maximum number of a type of objects on a single resource zone
#define MAX_OBJECTS_PER_ZONE			1

// Time it takes a fully healed object to deteriorate
ConVar  object_deterioration_time( "object_deterioration_time", "30", 0, "Time it takes for a fully-healed object to deteriorate." );

// Time after taking damage that an object will still drop resources on death
#define MAX_DROP_TIME_AFTER_DAMAGE			5

#define OBJ_BASE_THINK_CONTEXT				"BaseObjectThink"

BEGIN_DATADESC( CBaseObject )
	// keys 
	DEFINE_KEYFIELD_NOT_SAVED( m_SolidToPlayers,		FIELD_INTEGER, "SolidToPlayer" ),
	DEFINE_KEYFIELD( m_iDefaultUpgrade, FIELD_INTEGER, "defaultupgrade" ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "Show", InputShow ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Hide", InputHide ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetHealth", InputSetHealth ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "AddHealth", InputAddHealth ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "RemoveHealth", InputRemoveHealth ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetSolidToPlayer", InputSetSolidToPlayer ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetBuilder", InputSetBuilder ),

	// Outputs
	DEFINE_OUTPUT( m_OnDestroyed, "OnDestroyed" ),
	DEFINE_OUTPUT( m_OnDamaged, "OnDamaged" ),
	DEFINE_OUTPUT( m_OnRepaired, "OnRepaired" ),
	DEFINE_OUTPUT( m_OnBecomingDisabled, "OnDisabled" ),
	DEFINE_OUTPUT( m_OnBecomingReenabled, "OnReenabled" ),
	DEFINE_OUTPUT( m_OnObjectHealthChanged, "OnObjectHealthChanged" ),

	DEFINE_USEFUNC( BuildingUse ),
	DEFINE_THINKFUNC( FallThink ),

	DEFINE_PHYSPTR( m_pAntiFlipConstraint ),

END_DATADESC()


IMPLEMENT_SERVERCLASS_ST( CBaseObject, DT_BaseObject )
	SendPropInt( SENDINFO( m_iHealth ), 13 ),
	SendPropInt( SENDINFO( m_iMaxHealth ), 13 ),
	SendPropBool( SENDINFO( m_bHasSapper ) ),
	SendPropInt( SENDINFO( m_iObjectType ), Q_log2( OBJ_LAST ) + 1, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bBuilding ) ),
	SendPropBool( SENDINFO( m_bPlacing ) ),
	SendPropBool( SENDINFO( m_bCarried ) ),
	SendPropBool( SENDINFO( m_bCarryDeploy ) ),
	SendPropBool( SENDINFO( m_bMiniBuilding ) ),
	SendPropFloat( SENDINFO( m_flPercentageConstructed ), 8, 0, 0.0, 1.0f ),
	SendPropInt( SENDINFO( m_fObjectFlags ), OF_BIT_COUNT, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO( m_hBuiltOnEntity ) ),
	SendPropBool( SENDINFO( m_bDisabled ) ),
	SendPropEHandle( SENDINFO( m_hBuilder ) ),
	SendPropVector( SENDINFO( m_vecBuildMaxs ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO( m_vecBuildMins ), -1, SPROP_COORD ),
	SendPropInt( SENDINFO( m_iDesiredBuildRotations ), 2, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bServerOverridePlacement ) ),
	SendPropInt( SENDINFO( m_iUpgradeLevel ), 3 ),
	SendPropInt( SENDINFO( m_iUpgradeMetal ), 10 ),
	SendPropInt( SENDINFO( m_iUpgradeMetalRequired ), 10 ),
	SendPropInt( SENDINFO( m_iHighestUpgradeLevel ), 3 ),
	SendPropInt( SENDINFO( m_iObjectMode ), 2 ),
	SendPropBool( SENDINFO( m_bDisposableBuilding ) ),
	SendPropBool( SENDINFO( m_bWasMapPlaced ) )
END_SEND_TABLE();

bool PlayerIndexLessFunc( const int &lhs, const int &rhs )	
{ 
	return lhs < rhs; 
}

ConVar tf_obj_upgrade_per_hit( "tf_obj_upgrade_per_hit", "25", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );

extern ConVar tf_cheapobjects;

// This controls whether ropes attached to objects are transmitted or not. It's important that
// ropes aren't transmitted to guys who don't own them.
class CObjectRopeTransmitProxy : public CBaseTransmitProxy
{
public:
	CObjectRopeTransmitProxy( CBaseEntity *pRope ) : CBaseTransmitProxy( pRope )
	{
	}
	
	virtual int ShouldTransmit( const CCheckTransmitInfo *pInfo, int nPrevShouldTransmitResult )
	{
		// Don't transmit the rope if it's not even visible.
		if ( !nPrevShouldTransmitResult )
			return FL_EDICT_DONTSEND;

		// This proxy only wants to be active while one of the two objects is being placed.
		// When they're done being placed, the proxy goes away and the rope draws like normal.
		bool bAnyObjectPlacing = (m_hObj1 && m_hObj1->IsPlacing()) || (m_hObj2 && m_hObj2->IsPlacing());
		if ( !bAnyObjectPlacing )
		{
			Release();
			return nPrevShouldTransmitResult;
		}

		// Give control to whichever object is being placed.
		if ( m_hObj1 && m_hObj1->IsPlacing() )
			return m_hObj1->ShouldTransmit( pInfo );
		
		else if ( m_hObj2 && m_hObj2->IsPlacing() )
			return m_hObj2->ShouldTransmit( pInfo );
		
		else
			return FL_EDICT_ALWAYS;
	}


	CHandle<CBaseObject> m_hObj1;
	CHandle<CBaseObject> m_hObj2;
};

IMPLEMENT_AUTO_LIST( IBaseObjectAutoList )

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CBaseObject::CBaseObject()
{
	m_iHealth = m_iMaxHealth = m_flHealth = m_iGoalHealth = 0;
	m_flPercentageConstructed = 0;
	m_bPlacing = false;
	m_bBuilding = false;
	m_bCarried = false;
	m_bCarryDeploy = false;
	m_Activity = ACT_INVALID;
	m_bDisabled = false;
	m_SolidToPlayers = SOLID_TO_PLAYER_USE_DEFAULT;
	m_bPlacementOK = false;
	m_aGibs.Purge();
	m_iObjectMode = 0;
	m_iDefaultUpgrade = 0;
	m_bMiniBuilding = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::UpdateOnRemove( void )
{
	m_bDying = true;

	// Remove anything left on me
	/*IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>(this);
	if ( pBPInterface && pBPInterface->GetNumObjectsOnMe() )
	{
		pBPInterface->RemoveAllObjects();
	}*/

	//DestroyConstraint();

	DestroyObject();

	/*if ( TFGameRules() && TFGameRules()->IsMannVsMachineMode() && GetTeamNumber() == TF_TEAM_BLUE) 
	{
		for (int i = 0; i < IBaseObjectAutoList::AutoList().Count(); ++i)
		{
			CBaseObject *obj = static_cast<CBaseObject *>( IBaseObjectAutoList::AutoList()[i] );
			if ( obj == this )
				continue;
			
			if ( obj->GetType() == GetType() )
			{
				if ( !obj->IsDying() && obj->GetBuilder() == nullptr )
				{
					obj->DetonateObject();
				}
			}
		}
	}*/

	if ( GetTeam() )
		GetTFTeam()->RemoveObject( this );

	DetachObjectFromObject();

	// Make sure the object isn't in either team's list of objects...
	//Assert( !GetGlobalTFTeam(1)->IsObjectOnTeam( this ) );
	//Assert( !GetGlobalTFTeam(2)->IsObjectOnTeam( this ) );

	// Chain at end to mimic destructor unwind order
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseObject::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_FULLCHECK );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseObject::ShouldTransmit( const CCheckTransmitInfo *pInfo )
{
	// Always transmit to owner
	if ( GetBuilder() && pInfo->m_pClientEnt == GetBuilder()->edict() )
		return FL_EDICT_ALWAYS;

	// Placement models only transmit to owners
	if ( IsPlacing() )
		return FL_EDICT_DONTSEND;

	return BaseClass::ShouldTransmit( pInfo );
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
bool CBaseObject::CanBeUpgraded( CTFPlayer *pPlayer )
{
	// only engineers
	if ( !ClassCanBuild( pPlayer->GetPlayerClass()->GetClassIndex(), GetType() ) )
	{
		return false;
	}

	// max upgraded
	if ( GetUpgradeLevel() >= GetMaxUpgradeLevel() )
	{
		return false;
	}

	if ( IsPlacing() )
	{
		return false;
	}

	if ( IsBuilding() )
	{
		return false;
	}

	if ( IsUpgrading() )
	{
		return false;
	}

	if ( IsRedeploying() )
	{
		return false;
	}

	if ( IsMiniBuilding() )
	{
		return false;
	}

	return true;
}

void CBaseObject::SetTransmit( CCheckTransmitInfo *pInfo, bool bAlways )
{
	// Are we already marked for transmission?
	if ( pInfo->m_pTransmitEdict->Get( entindex() ) )
		return;

	BaseClass::SetTransmit( pInfo, bAlways );

	// Force our screens to be sent too.
	int nTeam = CBaseEntity::Instance( pInfo->m_pClientEnt )->GetTeamNumber();
	for ( int i=0; i < m_hScreens.Count(); i++ )
	{
		CVGuiScreen *pScreen = m_hScreens[i].Get();
		if ( pScreen && pScreen->IsVisibleToTeam( nTeam ) )
			pScreen->SetTransmit( pInfo, bAlways );
	}
}

void CBaseObject::DoWrenchHitEffect( Vector vecHitPos, bool bRepair, bool bUpgrade )
{
	CPVSFilter filter( vecHitPos );
	if ( bRepair )
	{
		TE_TFParticleEffect( filter, 0.0f, "nutsnbolts_repair", vecHitPos, QAngle( 0, 0, 0 ) );
	}
	else if ( bUpgrade )
	{
		TE_TFParticleEffect( filter, 0.0f, "nutsnbolts_upgrade", vecHitPos, QAngle( 0, 0, 0 ) );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::Precache()
{
	PrecacheMaterial( SCREEN_OVERLAY_MATERIAL );

	PrecacheScriptSound( GetObjectInfo( ObjectType() )->m_pExplodeSound );

	const char *pEffect = GetObjectInfo( ObjectType() )->m_pExplosionParticleEffect;

	if ( pEffect && pEffect[0] != '\0' )
	{
		PrecacheParticleSystem( pEffect );
	}

	PrecacheParticleSystem( "nutsnbolts_build" );
	PrecacheParticleSystem( "nutsnbolts_upgrade" );
	PrecacheParticleSystem( "nutsnbolts_repair" );
	CBaseEntity::PrecacheModel( "models/weapons/w_models/w_toolbox.mdl" );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::Spawn( void )
{
	Precache();

	CollisionProp()->SetSurroundingBoundsType( USE_BEST_COLLISION_BOUNDS );
	SetSolidToPlayers( m_SolidToPlayers, true );

	m_bHasSapper = false;
	m_takedamage = DAMAGE_YES;
	m_flHealth = m_iMaxHealth = m_iHealth;
	m_iKills = 0;

	m_iUpgradeLevel = 1;
	m_iGoalUpgradeLevel = 1;
	m_iUpgradeMetal = 0;

	m_iUpgradeMetalRequired = GetObjectInfo( ObjectType() )->m_UpgradeCost;
	m_iHighestUpgradeLevel = GetMaxUpgradeLevel();
	//m_iHighestUpgradeLevel = GetObjectInfo(ObjectType())->m_MaxUpgradeLevel;

	SetContextThink( &CBaseObject::BaseObjectThink, gpGlobals->curtime + 0.1, OBJ_BASE_THINK_CONTEXT );

	AddFlag( FL_OBJECT ); // So NPCs will notice it
	SetViewOffset( WorldSpaceCenter() - GetAbsOrigin() );

	m_RepairerList.SetLessFunc( PlayerIndexLessFunc );

	m_iDesiredBuildRotations = 0;
	m_flCurrentBuildRotation = 0;

	if ( MustBeBuiltOnAttachmentPoint() )
	{
		AddEffects( EF_NODRAW );
	}

	// assume valid placement
	m_bServerOverridePlacement = true;

	SetNextThink( gpGlobals->curtime + 0.1f );

	SetUse( NULL );

	if ( MustBeBuiltOnAttachmentPoint() )
	{
		if ( !VPhysicsGetObject() )
			VPhysicsInitStatic();
	}
	else
	{
		if ( lfe_obj_physics.GetBool() )
		{
			VPhysicsInitNormal( SOLID_BBOX, 0, false );
		}
		else
		{
			SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
			SetElasticity( 0.01f );
			SetGravity( 1.0f );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Gunslinger's Buildings
//-----------------------------------------------------------------------------
void CBaseObject::MakeMiniBuilding( void )
{
	m_bMiniBuilding = true;

	// Set the skin
	switch ( GetTeamNumber() )
	{
	case TF_TEAM_RED:
		m_nSkin = 2;
		break;

	case TF_TEAM_BLUE:
		m_nSkin = 3;
		break;

	default:
		m_nSkin = 2;
		break;
	}

	SetModelScale( 0.75f );
}

void CBaseObject::MakeCarriedObject( CTFPlayer *pPlayer )
{
	if ( pPlayer )
	{
		m_bCarried = true;
		m_bCarryDeploy = false;
		DestroyScreens();
		//FollowEntity( pPlayer, true );

		// Save health amount building had before getting picked up. It will only heal back up to it.
		m_iGoalHealth = GetHealth();

		// Save current upgrade level and reset it. Building will automatically upgrade back once re-deployed.
		m_iGoalUpgradeLevel = GetUpgradeLevel();
		m_iUpgradeLevel = 1;

		// Reset placement rotation.
		m_iDesiredBuildRotations = 0;

		SetModel( GetPlacementModel() );

		pPlayer->m_Shared.SetCarriedObject( this );

		//AddEffects( EF_NODRAW );
		// StartPlacement already does this but better safe than sorry.
		AddSolidFlags( FSOLID_NOT_SOLID );

		SetContextThink( NULL, 0, "FallThink" );

		IGameEvent * event = gameeventmanager->CreateEvent( "player_carryobject" );
		if ( event )
		{
			event->SetInt( "userid", pPlayer->GetUserID() );
			event->SetInt( "object", ObjectType() );
			event->SetInt( "index", entindex() );	// object entity index
			gameeventmanager->FireEvent( event, true );	// don't send to clients
		}
	}

}

void CBaseObject::DropCarriedObject( CTFPlayer *pPlayer )
{
	m_bCarried = false;
	m_bCarryDeploy = true;

	if ( pPlayer )
	{
		pPlayer->m_Shared.SetCarriedObject( NULL );
	}

	SetContextThink( &CBaseObject::FallThink, gpGlobals->curtime + 0.1, "FallThink" );

	//StopFollowingEntity();
}

//-----------------------------------------------------------------------------
// Returns information about the various control panels
//-----------------------------------------------------------------------------
void CBaseObject::GetControlPanelInfo( int nPanelIndex, const char *&pPanelName )
{
	pPanelName = NULL;
}

//-----------------------------------------------------------------------------
// Returns information about the various control panels
//-----------------------------------------------------------------------------
void CBaseObject::GetControlPanelClassName( int nPanelIndex, const char *&pPanelName )
{
	pPanelName = "vgui_screen";
}

//-----------------------------------------------------------------------------
// This is called by the base object when it's time to spawn the control panels
//-----------------------------------------------------------------------------
void CBaseObject::SpawnControlPanels()
{
	char buf[64];

	// FIXME: Deal with dynamically resizing control panels?

	// If we're attached to an entity, spawn control panels on it instead of use
	CBaseAnimating *pEntityToSpawnOn = this;
	char *pOrgLL = "controlpanel%d_ll";
	char *pOrgUR = "controlpanel%d_ur";
	char *pAttachmentNameLL = pOrgLL;
	char *pAttachmentNameUR = pOrgUR;
	if ( IsBuiltOnAttachment() )
	{
		pEntityToSpawnOn = dynamic_cast<CBaseAnimating*>((CBaseEntity*)m_hBuiltOnEntity.Get());
		if ( pEntityToSpawnOn )
		{
			char sBuildPointLL[64];
			char sBuildPointUR[64];
			Q_snprintf( sBuildPointLL, sizeof( sBuildPointLL ), "bp%d_controlpanel%%d_ll", m_iBuiltOnPoint );
			Q_snprintf( sBuildPointUR, sizeof( sBuildPointUR ), "bp%d_controlpanel%%d_ur", m_iBuiltOnPoint );
			pAttachmentNameLL = sBuildPointLL;
			pAttachmentNameUR = sBuildPointUR;
		}
		else
		{
			pEntityToSpawnOn = this;
		}
	}

	Assert( pEntityToSpawnOn );

	// Lookup the attachment point...
	int nPanel;
	for ( nPanel = 0; true; ++nPanel )
	{
		Q_snprintf( buf, sizeof( buf ), pAttachmentNameLL, nPanel );
		int nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
		if (nLLAttachmentIndex <= 0)
		{
			// Try and use my panels then
			pEntityToSpawnOn = this;
			Q_snprintf( buf, sizeof( buf ), pOrgLL, nPanel );
			nLLAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nLLAttachmentIndex <= 0)
				return;
		}

		Q_snprintf( buf, sizeof( buf ), pAttachmentNameUR, nPanel );
		int nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
		if (nURAttachmentIndex <= 0)
		{
			// Try and use my panels then
			Q_snprintf( buf, sizeof( buf ), pOrgUR, nPanel );
			nURAttachmentIndex = pEntityToSpawnOn->LookupAttachment(buf);
			if (nURAttachmentIndex <= 0)
				return;
		}

		const char *pScreenName = NULL;
		GetControlPanelInfo( nPanel, pScreenName );
		if (!pScreenName)
			continue;

		const char *pScreenClassname;
		GetControlPanelClassName( nPanel, pScreenClassname );
		if ( !pScreenClassname )
			continue;

		// Compute the screen size from the attachment points...
		matrix3x4_t	panelToWorld;
		pEntityToSpawnOn->GetAttachment( nLLAttachmentIndex, panelToWorld );

		matrix3x4_t	worldToPanel;
		MatrixInvert( panelToWorld, worldToPanel );

		// Now get the lower right position + transform into panel space
		Vector lr, lrlocal;
		pEntityToSpawnOn->GetAttachment( nURAttachmentIndex, panelToWorld );
		MatrixGetColumn( panelToWorld, 3, lr );
		VectorTransform( lr, worldToPanel, lrlocal );

		float flWidth = lrlocal.x;
		float flHeight = lrlocal.y;

		CVGuiScreen *pScreen = CreateVGuiScreen( pScreenClassname, pScreenName, pEntityToSpawnOn, this, nLLAttachmentIndex );
		pScreen->ChangeTeam( GetTeamNumber() );
		pScreen->SetActualSize( flWidth, flHeight );
		pScreen->SetActive( false );
		pScreen->MakeVisibleOnlyToTeammates( true );
		pScreen->SetOverlayMaterial( SCREEN_OVERLAY_MATERIAL );
		pScreen->SetTransparency( true );

		// for now, only input by the owning player
		pScreen->SetPlayerOwner( GetBuilder(), true );

		int nScreen = m_hScreens.AddToTail( );
		m_hScreens[nScreen].Set( pScreen );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called in case was not built by a player but placed by a mapper.
//-----------------------------------------------------------------------------
void CBaseObject::InitializeMapPlacedObject( void )
{
	m_bWasMapPlaced = true;

	if ( ( GetObjectFlags() & OF_IS_CART_OBJECT ) == 0 )
		SpawnControlPanels();

	// Spawn with full health.
	SetHealth( GetMaxHealth() );

	// Go active.
	FinishedBuilding();
	//AlignToGround( GetAbsOrigin() );

	// Add it to team.
	CTFTeam *pTFTeam = GetGlobalTFTeam( GetTeamNumber() );

	if ( pTFTeam && !pTFTeam->IsObjectOnTeam( this ) )
	{
		pTFTeam->AddObject( this );
	}

	// Set the skin
	switch ( GetTeamNumber() )
	{
	case TF_TEAM_RED:
		m_nSkin = 0;
		break;

	case TF_TEAM_BLUE:
		m_nSkin = 1;
		break;

	default:
		m_nSkin = 1;
		break;
	}
}

//-----------------------------------------------------------------------------
// Handle commands sent from vgui panels on the client 
//-----------------------------------------------------------------------------
bool CBaseObject::ClientCommand( CTFPlayer *pSender, const CCommand &args )
{
	//const char *pCmd = args[0];
	return false;
}

#define BASE_OBJECT_THINK_DELAY	0.1
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::BaseObjectThink( void )
{
	SetNextThink( gpGlobals->curtime + BASE_OBJECT_THINK_DELAY, OBJ_BASE_THINK_CONTEXT );

	// Make sure animation is up to date
	DetermineAnimation();

	DeterminePlaybackRate();

	// Do nothing while we're being placed
	if ( IsPlacing() )
	{
		if ( MustBeBuiltOnAttachmentPoint() )
		{
			UpdateAttachmentPlacement();	
			m_bServerOverridePlacement = true;
		}
		else
		{
			m_bServerOverridePlacement = EstimateValidBuildPos();

			UpdateDesiredBuildRotation( BASE_OBJECT_THINK_DELAY );
			//AlignToGround( GetAbsOrigin() );
		}

		return;
	}

	// If we're building, keep going
	if ( IsBuilding() )
	{
		BuildingThink();
		return;
	}
	else if ( IsUpgrading() )
	{
		UpgradeThink();
		return;
	}

	if ( m_bCarryDeploy )
	{
		if ( m_iUpgradeLevel < m_iGoalUpgradeLevel )
		{
			// Keep upgrading until we hit our previous upgrade level.
			StartUpgrading();
		}
		else
		{
			// Finished.
			m_bCarryDeploy = false;
		}
	}
	
	if ( IsMiniBuilding() )
	{
		MakeMiniBuilding();
		return;
	}
}

bool CBaseObject::UpdateAttachmentPlacement( CBaseObject *pObject /*= NULL*/ )
{
	// See if we should snap to a build position
	// finding one implies it is a valid position
	if ( FindSnapToBuildPos( pObject ) )
	{
		m_bPlacementOK = true;

		Teleport( &m_vecBuildOrigin, &GetLocalAngles(), NULL );
	}
	else
	{
		m_bPlacementOK = false;

		// Clear out previous parent 
		if ( m_hBuiltOnEntity.Get() )
		{
			m_hBuiltOnEntity = NULL;
			m_iBuiltOnPoint = 0;
			SetParent( NULL );
		}

		// teleport to builder's origin
		CTFPlayer *pPlayer = GetOwner();

		if ( pPlayer )
		{
			Teleport( &pPlayer->WorldSpaceCenter(), &GetLocalAngles(), NULL );
		}
	}

	return m_bPlacementOK;
}

//-----------------------------------------------------------------------------
// Purpose: Cheap check to see if we are in any server-defined No-build areas.
//-----------------------------------------------------------------------------
bool CBaseObject::EstimateValidBuildPos( void )
{
	CTFPlayer *pPlayer = GetOwner();

	if ( !pPlayer )
		return false;

	// Calculate build angles
	Vector forward;
	QAngle vecAngles = vec3_angle;
	vecAngles.y = pPlayer->EyeAngles().y;

	QAngle objAngles = vecAngles;

	//SetAbsAngles( objAngles );
	//SetLocalAngles( objAngles );
	AngleVectors(vecAngles, &forward );

	// Adjust build distance based upon object size
	Vector2D vecObjectRadius;
	vecObjectRadius.x = max( fabs( m_vecBuildMins.m_Value.x ), fabs( m_vecBuildMaxs.m_Value.x ) );
	vecObjectRadius.y = max( fabs( m_vecBuildMins.m_Value.y ), fabs( m_vecBuildMaxs.m_Value.y ) );

	Vector2D vecPlayerRadius;
	Vector vecPlayerMins = pPlayer->WorldAlignMins();
	Vector vecPlayerMaxs = pPlayer->WorldAlignMaxs();
	vecPlayerRadius.x = max( fabs( vecPlayerMins.x ), fabs( vecPlayerMaxs.x ) );
	vecPlayerRadius.y = max( fabs( vecPlayerMins.y ), fabs( vecPlayerMaxs.y ) );

	float flDistance = vecObjectRadius.Length() + vecPlayerRadius.Length() + 4; // small safety buffer
	Vector vecBuildOrigin = pPlayer->WorldSpaceCenter() + forward * flDistance;

	//NDebugOverlay::Cross3D( vecBuildOrigin, 10, 255, 0, 0, false, 0.1 );

	// Cannot build inside a nobuild brush
	if ( PointInNoBuild( vecBuildOrigin, this ) )
		return false;

	if ( PointInRespawnRoom( NULL, vecBuildOrigin ) )
		return false;

	Vector vecBuildFarEdge = vecBuildOrigin + forward * ( flDistance + 8.0f );
	if ( TestAgainstRespawnRoomVisualizer( pPlayer, vecBuildFarEdge ) )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseObject::TestAgainstRespawnRoomVisualizer( CTFPlayer *pPlayer, const Vector &vecEnd )
{
	// Setup the ray.
	Ray_t ray;
	ray.Init( pPlayer->WorldSpaceCenter(), vecEnd );

	CBaseEntity *pEntity = NULL;
	while ( ( pEntity = gEntList.FindEntityByClassnameWithin( pEntity, "func_respawnroomvisualizer", pPlayer->WorldSpaceCenter(), ray.m_Delta.Length() ) ) != NULL )
	{
		trace_t trace;
		enginetrace->ClipRayToEntity( ray, MASK_ALL, pEntity, &trace );
		if ( trace.fraction < 1.0f )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::DeterminePlaybackRate( void )
{
	if ( IsBuilding() )
	{
		// Default half rate, author build anim as if one player is building
		SetPlaybackRate( GetConstructionMultiplier() * 0.5 );	
	}
	else
	{
		SetPlaybackRate( 1.0 );
	}

	StudioFrameAdvance();
}

#define OBJ_UPGRADE_DURATION	1.5f

//-----------------------------------------------------------------------------
// Raises the Sentrygun one level
//-----------------------------------------------------------------------------
void CBaseObject::StartUpgrading( void )
{
	// Increase level
	m_iUpgradeLevel++;

	// more health
	if ( !IsRedeploying() )
	{
		int iMaxHealth = GetMaxHealth();
		SetMaxHealth( iMaxHealth * 1.2 );
		SetHealth( iMaxHealth * 1.2 );
	}

	// No ear raping for map placed buildings.
	if ( !m_iDefaultUpgrade )
	{
		EmitSound( GetObjectInfo( ObjectType() )->m_pUpgradeSound );
	}

	m_flUpgradeCompleteTime = gpGlobals->curtime + GetObjectInfo( ObjectType() )->m_flUpgradeDuration;

	SetUse( NULL );

	SetContextThink( &CBaseObject::FallThink, gpGlobals->curtime + 0.1, "FallThink" );

	SetNextThink( gpGlobals->curtime + 0.1f );
}

void CBaseObject::FinishUpgrading( void )
{
	SetUse( &CBaseObject::BuildingUse );

	// No ear raping for map placed buildings.
	if ( !m_iDefaultUpgrade )
	{
		EmitSound( GetObjectInfo( ObjectType() )->m_pUpgradeSound );
	}
}

//-----------------------------------------------------------------------------
// Playing the upgrade animation
//-----------------------------------------------------------------------------
void CBaseObject::UpgradeThink(void)
{
	if ( gpGlobals->curtime > m_flUpgradeCompleteTime )
	{
		FinishUpgrading();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CTFPlayer *CBaseObject::GetOwner()
{ 
	return m_hBuilder; 
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::Activate( void )
{
	BaseClass::Activate();

	if ( GetBuilder() == NULL )
		InitializeMapPlacedObject();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::SetBuilder( CTFPlayer *pBuilder )
{
	TRACE_OBJECT( UTIL_VarArgs( "%0.2f CBaseObject::SetBuilder builder %s\n", gpGlobals->curtime, 
		pBuilder ? pBuilder->GetPlayerName() : "NULL" ) );

	m_hBuilder = pBuilder;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int	CBaseObject::ObjectType( ) const
{
	return m_iObjectType;
}

//-----------------------------------------------------------------------------
// Purpose: Destroys the object, gives a chance to spawn an explosion
//-----------------------------------------------------------------------------
void CBaseObject::DetonateObject( void )
{
	CTakeDamageInfo info( this, this, vec3_origin, GetAbsOrigin(), 0, DMG_GENERIC );

	Killed( info );
}

//-----------------------------------------------------------------------------
// Purpose: Remove this object from it's team and mark for deletion
//-----------------------------------------------------------------------------
void CBaseObject::DestroyObject( void )
{
	TRACE_OBJECT( UTIL_VarArgs( "%0.2f CBaseObject::DestroyObject %p:%s\n", gpGlobals->curtime, this, GetClassname() ) );


	if ( m_bCarried )
	{
		DropCarriedObject( GetBuilder() );
	}

	if ( GetBuilder() )
	{
		GetBuilder()->OwnedObjectDestroyed( this );
	}

	UTIL_Remove( this );

	DestroyScreens();
}

//-----------------------------------------------------------------------------
// Purpose: Remove any screens that are active on this object
//-----------------------------------------------------------------------------
void CBaseObject::DestroyScreens( void )
{
	// Kill the control panels
	int i;
	for ( i = m_hScreens.Count(); --i >= 0; )
	{
		DestroyVGuiScreen( m_hScreens[i].Get() );
	}
	m_hScreens.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Get the total time it will take to build this object
//-----------------------------------------------------------------------------
float CBaseObject::GetTotalTime( void )
{
	float flBuildTime = GetObjectInfo( ObjectType() )->m_flBuildTime;

	if ( tf_fastbuild.GetInt() )
		return ( min( 2.f, flBuildTime ) );

	return flBuildTime;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseObject::GetMaxHealthForCurrentLevel( void )
{
	return GetBaseHealth() * pow( 1.2, GetUpgradeLevel() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Start placing the object
//-----------------------------------------------------------------------------
void CBaseObject::StartPlacement( CTFPlayer *pPlayer )
{
	AddSolidFlags( FSOLID_NOT_SOLID );

	m_bPlacing = true;
	m_bBuilding = false;
	if ( pPlayer )
	{
		SetBuilder( pPlayer );
		ChangeTeam( pPlayer->GetTeamNumber() );
	}

	// needed?
	m_nRenderMode = kRenderNormal;
	
	// Set my build size
	CollisionProp()->WorldSpaceAABB( &m_vecBuildMins.GetForModify(), &m_vecBuildMaxs.GetForModify() );
	m_vecBuildMins -= Vector( 4,4,0 );
	m_vecBuildMaxs += Vector( 4,4,0 );
	m_vecBuildMins -= GetAbsOrigin();
	m_vecBuildMaxs -= GetAbsOrigin();

	// Set the skin
	switch ( GetTeamNumber() )
	{
	case TF_TEAM_RED:
		m_nSkin = 0;
		break;

	case TF_TEAM_BLUE:
		m_nSkin = 1;
		break;

	default:
		m_nSkin = 1;
		break;
	}

	if ( IsMiniBuilding() )
	{
		MakeMiniBuilding();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Stop placing the object
//-----------------------------------------------------------------------------
void CBaseObject::StopPlacement( void )
{
	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// Purpose: Find the nearest buildpoint on the specified entity
//-----------------------------------------------------------------------------
bool CBaseObject::FindNearestBuildPoint( CBaseEntity *pEntity, CBasePlayer *pBuilder, float &flNearestPoint, Vector &vecNearestBuildPoint, bool bIgnoreLOS /*= false*/ )
{
	bool bFoundPoint = false;

	IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>( pEntity );
	Assert( pBPInterface );

	// Any empty buildpoints?
	for ( int i = 0; i < pBPInterface->GetNumBuildPoints(); i++ )
	{
		// Can this object build on this point?
		if ( pBPInterface->CanBuildObjectOnBuildPoint( i, GetType() ) )
		{
			// Close to this point?
			Vector vecBPOrigin;
			QAngle vecBPAngles;
			if ( pBPInterface->GetBuildPoint(i, vecBPOrigin, vecBPAngles) )
			{
				// If set to ignore LOS, distance, etc, just pick the first point available.
				if ( !bIgnoreLOS )
				{
					// ignore build points outside our view
					if ( !pBuilder->FInViewCone( vecBPOrigin ) )
						continue;

					// Do a trace to make sure we don't place attachments through things (players, world, etc...)
					Vector vecStart = pBuilder->EyePosition();
					trace_t trace;
					Ray_t ray; ray.Init( vecStart, vecBPOrigin );
					UTIL_Portal_TraceRay( ray, MASK_SOLID, pBuilder, COLLISION_GROUP_NONE, &trace );
					if ( trace.m_pEnt != pEntity && trace.fraction != 1.0 )
						continue;
				}

				float flDist = (vecBPOrigin - pBuilder->GetAbsOrigin()).Length();

				// if this is closer, or is the first one in our view, check it out
				if ( bIgnoreLOS || flDist < min(flNearestPoint, pBPInterface->GetMaxSnapDistance( i )) )
				{
					flNearestPoint = flDist;
					vecNearestBuildPoint = vecBPOrigin;
					m_hBuiltOnEntity = pEntity;
					m_iBuiltOnPoint = i;

					// Set our angles to the buildpoint's angles
					SetAbsAngles( vecBPAngles );

					bFoundPoint = true;

					if ( bIgnoreLOS )
						break;
				}
			}
		}
	}

	return bFoundPoint;
}

//-----------------------------------------------------------------------------
// Purpose: Find the nearest buildpoint on the specified entity
//-----------------------------------------------------------------------------
bool CBaseObject::FindBuildPointOnPlayer( CTFPlayer *pPlayer, CBasePlayer *pBuilder, float &flNearestPoint, Vector &vecNearestBuildPoint )
{
	if ( pPlayer->m_Shared.InCond( TF_COND_SAPPED ) )
		return false;

	bool bFoundPoint = false;

	// Close to this point?
	Vector vecBPOrigin = pPlayer->GetAbsOrigin();
	QAngle vecBPAngles = pPlayer->GetAbsAngles();

	int iAttachment = pPlayer->LookupAttachment( "head" );
	if ( iAttachment > 0 )
		pPlayer->GetAttachment( iAttachment, vecBPOrigin, vecBPAngles );

	// If set to ignore LOS, distance, etc, just pick the first point available.
	// ignore build points outside our view
	if ( !pBuilder->FInViewCone( vecBPOrigin ) )
		return false;

	// Do a trace to make sure we don't place attachments through things (players, world, etc...)
	Vector vecStart = pBuilder->EyePosition();
	trace_t trace;
	Ray_t ray; ray.Init( vecStart, vecBPOrigin );
	UTIL_Portal_TraceRay( ray, MASK_SOLID, pBuilder, COLLISION_GROUP_NONE, &trace );
	if ( trace.m_pEnt != pPlayer && trace.fraction != 1.0 )
		return false;

	float flDist = (vecBPOrigin - pBuilder->GetAbsOrigin()).Length();

	// if this is closer, or is the first one in our view, check it out
	if ( flDist < min( flNearestPoint, GetMaxSnapDistance( 1 ) ) )
	{
		flNearestPoint = flDist;
		vecNearestBuildPoint = vecBPOrigin;
		m_hBuiltOnEntity = pPlayer;
		m_iBuiltOnPoint = 1;

		// Set our angles to the buildpoint's angles
		SetAbsAngles( vecBPAngles );

		bFoundPoint = true;
	}

	return bFoundPoint;
}

//-----------------------------------------------------------------------------
// Purpose: Find the nearest buildpoint on the specified entity
//-----------------------------------------------------------------------------
bool CBaseObject::FindBuildPointOnNPC( CAI_BaseNPC *pNPC, CBasePlayer *pBuilder, float &flNearestPoint, Vector &vecNearestBuildPoint )
{
	bool bFoundPoint = false;

	// Close to this point?
	Vector vecBPOrigin = pNPC->GetAbsOrigin();
	QAngle vecBPAngles = pNPC->GetAbsAngles();

	int iAttachment = pNPC->LookupAttachment( "eyes" );
	if ( iAttachment > 0 )
		pNPC->GetAttachment( iAttachment, vecBPOrigin, vecBPAngles );

	// If set to ignore LOS, distance, etc, just pick the first point available.
	// ignore build points outside our view
	if ( !pBuilder->FInViewCone( vecBPOrigin ) )
		return false;

	// Do a trace to make sure we don't place attachments through things (players, world, etc...)
	Vector vecStart = pBuilder->EyePosition();
	trace_t trace;
	Ray_t ray; ray.Init( vecStart, vecBPOrigin );
	UTIL_Portal_TraceRay( ray, MASK_SOLID, pBuilder, COLLISION_GROUP_NONE, &trace );
	if ( trace.m_pEnt != pNPC && trace.fraction != 1.0 )
		return false;

	float flDist = (vecBPOrigin - pBuilder->GetAbsOrigin()).Length();

	// if this is closer, or is the first one in our view, check it out
	if ( flDist < min( flNearestPoint, GetMaxSnapDistance( 1 ) ) )
	{
		flNearestPoint = flDist;
		vecNearestBuildPoint = vecBPOrigin;
		m_hBuiltOnEntity = pNPC;
		m_iBuiltOnPoint = 1;

		// Set our angles to the buildpoint's angles
		SetAbsAngles( vecBPAngles );

		bFoundPoint = true;
	}

	return bFoundPoint;
}

//-----------------------------------------------------------------------------
// Purpose: Align myself to the ground below the specified point
//-----------------------------------------------------------------------------
void CBaseObject::AlignToGround( Vector vecOrigin )
{
	//if ( !(m_fObjectFlags & OF_ALIGN_TO_GROUND) )
	//	return;

	trace_t tr;
	Vector vecWorldMins, vecWorldMaxs;
	CollisionProp()->WorldSpaceAABB( &vecWorldMins, &vecWorldMaxs );
	float flHeight = max( vecWorldMaxs.z - vecWorldMins.z, 60 );
	UTIL_TraceLine( vecOrigin, vecOrigin + Vector(0,0,-flHeight), MASK_SOLID, this, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
	if ( tr.fraction != 1.0 )
	{
		// Orient the *up* axis to be along the plane normal
		Vector perp( 1, 0, 0 );
		Vector forward, right;
		CrossProduct( perp, tr.plane.normal, forward );
		if (forward.LengthSqr() < 0.1f)
		{
			perp.Init( 0, 1, 0 );
			CrossProduct( perp, tr.plane.normal, forward );
		}
		VectorNormalize( forward );
		CrossProduct( tr.plane.normal, forward, right );

		VMatrix orientation( forward, right, tr.plane.normal );

		QAngle angles;
		MatrixToAngles( orientation, angles );
		SetAbsAngles( angles );
		/*UpdateDesiredBuildRotation( gpGlobals->frametime );
		angles.y = angles.y + m_flCurrentBuildRotation;
		SetLocalAngles( angles );*/
	}
}

//-----------------------------------------------------------------------------
// Exit points for mounted vehicles....
//-----------------------------------------------------------------------------
void CBaseObject::GetExitPoint( CBaseEntity *pPlayer, int nBuildPoint, Vector *pAbsPosition, QAngle *pAbsAngles )
{
	// Deal with hierarchy...
	IHasBuildPoints *pMount = dynamic_cast<IHasBuildPoints*>( GetMoveParent() );
	if (pMount)
	{
		int nBuildPoint = pMount->FindObjectOnBuildPoint( this );
		if (nBuildPoint >= 0)
		{
			pMount->GetExitPoint( pPlayer, nBuildPoint, pAbsPosition, pAbsAngles );
			return;
		}
	}

	// FIXME: In future, we may well want to use specific exit attachments here...
	GetBuildPoint( nBuildPoint, *pAbsPosition, *pAbsAngles );

	// Move back along the forward direction a bit...
	Vector vecForward, vecUp;
	AngleVectors( *pAbsAngles, &vecForward, NULL, &vecUp );
	*pAbsPosition -= vecForward * 60;
	*pAbsPosition += vecUp * 30;

	// Now select a good spot to drop onto
	Vector vNewPos;
	if ( !EntityPlacementTest(pPlayer, *pAbsPosition, vNewPos, true) )
	{
		Warning( "Can't find valid place to exit object.\n" );
		return;
	}

	*pAbsPosition = vNewPos;
}

//-----------------------------------------------------------------------------
// Purpose: Move the placement model to the current position. Return false if it's an invalid position
//-----------------------------------------------------------------------------
bool CBaseObject::UpdatePlacement( void )
{
	if ( MustBeBuiltOnAttachmentPoint() )
	{
		return UpdateAttachmentPlacement();
	}

	// Finds bsp-valid place for building to be built
	// Checks for validity, nearby to other entities, in line of sight
	m_bPlacementOK = IsPlacementPosValid();

	//AlignToGround( m_vecBuildOrigin );
	Teleport( &m_vecBuildOrigin, &GetLocalAngles(), NULL );

	return m_bPlacementOK;
}

//-----------------------------------------------------------------------------
// Purpose: See if we should be snapping to a build position
//-----------------------------------------------------------------------------
bool CBaseObject::FindSnapToBuildPos( CBaseObject *pObject /*= NULL*/ )
{
	if ( !MustBeBuiltOnAttachmentPoint() && !CanBeBuiltOnAttachmentPoint() )
		return false;

	CTFPlayer *pPlayer = GetBuilder();
	if ( !pPlayer )
		return false;

	bool bSnappedToPoint = false;
	bool bShouldAttachToParent = false;

	Vector vecNearestBuildPoint = vec3_origin;

	// See if there are any nearby build positions to snap to
	float flNearestPoint = 9999;

	bool bHostileAttachment = IsHostileUpgrade();
	int iMyTeam = GetTeamNumber();

	// If we have an object specified then use that, don't search.
	if ( pObject )
	{
		if ( !pObject->IsPlacing() )
		{
			if ( FindNearestBuildPoint( pObject, pPlayer, flNearestPoint, vecNearestBuildPoint, true ) )
			{
				bSnappedToPoint = true;
				bShouldAttachToParent = true;
			}
		}
	}
	else
	{
		int nTeamCount = TFTeamMgr()->GetTeamCount();
		for ( int iTeam = FIRST_GAME_TEAM; iTeam < nTeamCount; ++iTeam )
		{
			// Hostile attachments look for enemy objects only
			if ( bHostileAttachment )
			{
				if ( iTeam == iMyTeam )
				{
					continue;
				}
			}
			// Friendly attachments look for friendly objects only
			else if ( iTeam != iMyTeam )
			{
				continue;
			}

			CTFTeam *pTeam = GetGlobalTFTeam( iTeam );
			if ( !pTeam )
				continue;

			// look for nearby buildpoints on other objects
			for ( int iObject = 0; iObject < pTeam->GetNumObjects(); iObject++ )
			{
				CBaseObject *pTempObject = pTeam->GetObject( iObject );
				if ( pTempObject && !pTempObject->IsPlacing() )
				{
					if ( FindNearestBuildPoint( pTempObject, pPlayer, flNearestPoint, vecNearestBuildPoint ) )
					{
						bSnappedToPoint = true;
						bShouldAttachToParent = true;
					}
				}
			}

			for ( int iPlayer = 0; iPlayer < pTeam->GetNumPlayers(); iPlayer++ )
			{
				CTFPlayer *pTargetPlayer = ToTFPlayer( pTeam->GetPlayer( iPlayer ) );
				if ( pTargetPlayer && pTargetPlayer->IsConnected() && pTargetPlayer->IsAlive() && TFGameRules()->IsMannVsMachineMode() )
				{
					if ( FindBuildPointOnPlayer( pTargetPlayer, pPlayer, flNearestPoint, vecNearestBuildPoint ) )
					{
						bSnappedToPoint = true;
						//bShouldAttachToParent = true;
					}
				}
			}

			// look for nearby buildpoints on npcs
			for ( int iNPC = 0; iNPC < pTeam->GetNumNPCs(); iNPC++ )
			{
				CAI_BaseNPC *pNPC = pTeam->GetNPC( iNPC );
				if ( pNPC && pNPC->IsAlive() && pNPC->CanSap() && !pNPC->InCond( TF_COND_SAPPED ) )
				{
					if ( FindBuildPointOnNPC( pNPC, pPlayer, flNearestPoint, vecNearestBuildPoint ) )
					{
						bSnappedToPoint = true;
						//bShouldAttachToParent = true;
					}
				}
			}

			// See if there's any mapdefined build points near me
			int iMapCount = g_MapDefinedBuildPoints.Count();
			for ( int iMap = 0; iMap < iMapCount; iMap++ )
			{
				if ( FindNearestBuildPoint( g_MapDefinedBuildPoints[iMap], pPlayer, flNearestPoint, vecNearestBuildPoint ) )
				{
					bSnappedToPoint = true;
					bShouldAttachToParent = true;
				}
			}
		}
	}

	if ( !bSnappedToPoint )
	{
		AddEffects( EF_NODRAW );
	}
	else
	{
		RemoveEffects( EF_NODRAW );

		if ( bShouldAttachToParent )
		{
			AttachObjectToObject( m_hBuiltOnEntity.Get(), m_iBuiltOnPoint, vecNearestBuildPoint );
		}

		m_vecBuildOrigin = vecNearestBuildPoint;
	}

	return bSnappedToPoint;
}

//-----------------------------------------------------------------------------
// Purpose: Are we currently in a buildable position
//-----------------------------------------------------------------------------
bool CBaseObject::IsValidPlacement( void ) const
{
	return m_bPlacementOK;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CBaseObject::GetResponseRulesModifier( void )
{
	switch ( GetType() )
	{
	case OBJ_DISPENSER: return "objtype:dispenser"; break;
	case OBJ_TELEPORTER: return "objtype:teleporter"; break;
	case OBJ_SENTRYGUN: return "objtype:sentrygun"; break;
	case OBJ_ATTACHMENT_SAPPER: return "objtype:sapper"; break;
	default:
		break;
	}
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Start building the object
//-----------------------------------------------------------------------------
bool CBaseObject::StartBuilding( CBaseEntity *pBuilder )
{
	/*
	// find any tf_ammo_boxes that we are colliding with and destroy them ?
	// enable if we need to do this
	CBaseEntity	*pList[8];
	Vector vecMins = m_vecBuildOrigin + m_vecBuildMins;
	Vector vecMaxs = m_vecBuildOrigin + m_vecBuildMaxs;

	int count = UTIL_EntitiesInBox( pList, ARRAYSIZE(pList), vecMins, vecMaxs, 0 );
	for ( int i = 0; i < count; i++ )
	{
		if ( pList[i] == this )
			continue;

		// if its a tf_ammo_box, remove it
		CTFAmmoPack *pAmmo = dynamic_cast< CTFAmmoPack * >( pList[i] );

		if ( pAmmo )
		{
			UTIL_Remove( pAmmo );
		}
	}
	*/

	// Need to add the object to the team now...
	CTFTeam *pTFTeam = ( CTFTeam * )GetGlobalTeam( GetTeamNumber() );

	// Deduct the cost from the player
	if ( pBuilder && pBuilder->IsPlayer() )
	{
		CTFPlayer *pTFBuilder = ToTFPlayer( pBuilder );

		if ( IsRedeploying() )
		{
			pTFBuilder->SpeakConceptIfAllowed( MP_CONCEPT_REDEPLOY_BUILDING, GetResponseRulesModifier() );
		}
		else
		{
			/*
			if ( ((CTFPlayer*)pBuilder)->IsPlayerClass( TF_CLASS_ENGINEER ) )
			{
			((CTFPlayer*)pBuilder)->HintMessage( HINT_ENGINEER_USE_WRENCH_ONOWN );
			}
			*/

			int iAmountPlayerPaidForMe = pTFBuilder->StartedBuildingObject( m_iObjectType );
			if ( !iAmountPlayerPaidForMe )
			{
				// Player couldn't afford to pay for me, so abort
				ClientPrint( pTFBuilder, HUD_PRINTCENTER, "#TF_Not_Enough_Resources" );
				StopPlacement();
				return false;
			}

			pTFBuilder->SpeakConceptIfAllowed( MP_CONCEPT_BUILDING_OBJECT, GetResponseRulesModifier() );
		}
	}

	// Add this object to the team's list (because we couldn't add it during
	// placement mode)
	if ( pTFTeam && !pTFTeam->IsObjectOnTeam( this ) )
	{
		pTFTeam->AddObject( this );
	}

	m_bPlacing = false;
	m_bBuilding = true;

	if ( !IsRedeploying() )
	{
		float flBuildingHealth = IsMiniBuilding() ? GetMiniBuildingStartingHealth() : OBJECT_CONSTRUCTION_STARTINGHEALTH;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( GetBuilder(), flBuildingHealth, mult_engy_building_health );
		SetHealth( flBuildingHealth );
	}
	m_flPercentageConstructed = 0;

	m_nRenderMode = kRenderNormal; 
	RemoveSolidFlags( FSOLID_NOT_SOLID );

	// NOTE: We must spawn the control panels now, instead of during
	// Spawn, because until placement is started, we don't actually know
	// the position of the control panel because we don't know what it's
	// been attached to (could be a vehicle which supplies a different
	// place for the control panel)
	// NOTE: We must also spawn it before FinishedBuilding can be called
	SpawnControlPanels();

	// Tell the object we've been built on that we exist
	if ( IsBuiltOnAttachment() )
	{
		IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>((CBaseEntity*)m_hBuiltOnEntity.Get());
		if ( pBPInterface )
			pBPInterface->SetObjectOnBuildPoint( m_iBuiltOnPoint, this );
	}

	// Start the build animations
	m_flTotalConstructionTime = m_flConstructionTimeLeft = GetTotalTime();

	if ( !IsRedeploying() && pBuilder && pBuilder->IsPlayer() )
	{
		CTFPlayer *pTFBuilder = ToTFPlayer( pBuilder );
		pTFBuilder->FinishedObject( this );
		IGameEvent * event = gameeventmanager->CreateEvent( "player_builtobject" );
		if ( event )
		{
			event->SetInt( "userid", pTFBuilder->GetUserID() );
			event->SetInt( "object", ObjectType() );
			event->SetInt( "index", entindex() );	// object entity index
			gameeventmanager->FireEvent( event, true );	// don't send to clients
		}
	}

	m_vecBuildOrigin = GetAbsOrigin();

	int contents = UTIL_PointContents( m_vecBuildOrigin );
	if ( contents & MASK_WATER )
	{
		SetWaterLevel( 3 );
	}

	SetUse( NULL );

	// instantly play the build anim
	DetermineAnimation();

	SetContextThink( &CBaseObject::FallThink, gpGlobals->curtime + 0.1, "FallThink" );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Continue construction of this object
//-----------------------------------------------------------------------------
void CBaseObject::BuildingThink( void )
{
	// Continue construction
	float flBuildingHealth = OBJECT_CONSTRUCTION_STARTINGHEALTH;
	CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( GetBuilder(), flBuildingHealth, mult_engy_building_health );
	Repair( (GetMaxHealth() - flBuildingHealth) / m_flTotalConstructionTime * OBJECT_CONSTRUCTION_INTERVAL );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::SetControlPanelsActive( bool bState )
{
	// Activate control panel screens
	for ( int i = m_hScreens.Count(); --i >= 0; )
	{
		if (m_hScreens[i].Get())
		{
			m_hScreens[i]->SetActive( bState );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::FinishedBuilding( void )
{
	SetControlPanelsActive( true );

	// Only make a shadow if the object doesn't use vphysics
	if ( MustBeBuiltOnAttachmentPoint() )
	{
		if ( !VPhysicsGetObject() )
			VPhysicsInitStatic();
	}
	else
	{
		if ( lfe_obj_physics.GetBool() )
		{
			VPhysicsInitNormal( SOLID_BBOX, 0, false );
			//CreateAntiFlipConstraint();
		}
		else
		{
			SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
			SetElasticity( 0.01f );
			SetGravity( 1.0f );
		}
	}

	m_bBuilding = false;

	AttemptToGoActive();

	SetUse( &CBaseObject::BuildingUse );

	// We're done building, add in the stat...
	////TFStats()->IncrementStat( (TFStatId_t)(TF_STAT_FIRST_OBJECT_BUILT + ObjectType()), 1 );

	// Spawn any objects on this one
	SpawnObjectPoints();
}

//-----------------------------------------------------------------------------
// Purpose: Objects store health in hacky ways
//-----------------------------------------------------------------------------
void CBaseObject::SetHealth( float flHealth )
{
	bool changed = m_flHealth != flHealth;

	m_flHealth = flHealth;
	m_iHealth = ceil(m_flHealth);

	/*
	// If we a pose parameter, set the pose parameter to reflect our health
	if ( LookupPoseParameter( "object_health") >= 0 && GetMaxHealth() > 0 )
	{
		SetPoseParameter( "object_health", 100 * ( GetHealth() / (float)GetMaxHealth() ) );
	}
	*/

	if ( changed )
	{
		// Set value and fire output
		m_OnObjectHealthChanged.Set( m_flHealth, this, this );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Override base traceattack to prevent visible effects from team members shooting me
//-----------------------------------------------------------------------------
void CBaseObject::TraceAttack( const CTakeDamageInfo &inputInfo, const Vector &vecDir, trace_t *ptr )
{
	// Prevent team damage here so blood doesn't appear
	if ( inputInfo.GetAttacker() )
	{
		if ( InSameTeam(inputInfo.GetAttacker()) )
		{
			// Pass Damage to enemy attachments
			int iNumObjects = GetNumObjectsOnMe();
			for ( int iPoint=iNumObjects-1;iPoint >= 0; --iPoint )
			{
				CBaseObject *pObject = GetBuildPointObject( iPoint );

				if ( pObject && pObject->IsHostileUpgrade() )
				{
					pObject->TraceAttack(inputInfo, vecDir, ptr );
				}
			}
			return;
		}
	}

	SpawnBlood( ptr->endpos, vecDir, BloodColor(), inputInfo.GetDamage() );

	AddMultiDamage( inputInfo, this );
}

//-----------------------------------------------------------------------------
// Purpose: Prevent Team Damage
//-----------------------------------------------------------------------------
ConVar object_show_damage( "obj_show_damage", "0", 0, "Show all damage taken by objects." );
ConVar object_capture_damage( "obj_capture_damage", "0", 0, "Captures all damage taken by objects for dumping later." );

CUtlDict<int,int> g_DamageMap;

void Cmd_DamageDump_f(void)
{	
	CUtlDict<bool,int> g_UniqueColumns;
	int idx;

	// Build the unique columns:
	for( idx = g_DamageMap.First(); idx != g_DamageMap.InvalidIndex(); idx = g_DamageMap.Next(idx) )
	{
		char* szColumnName = strchr(g_DamageMap.GetElementName(idx),',') + 1;

		int ColumnIdx = g_UniqueColumns.Find( szColumnName );

		if( ColumnIdx == g_UniqueColumns.InvalidIndex() ) 
		{
			g_UniqueColumns.Insert( szColumnName, false );
		}
	}

	// Dump the column names:
	FileHandle_t f = filesystem->Open("damage.txt","wt+");

	for( idx = g_UniqueColumns.First(); idx != g_UniqueColumns.InvalidIndex(); idx = g_UniqueColumns.Next(idx) )
	{
		filesystem->FPrintf(f,"\t%s",g_UniqueColumns.GetElementName(idx));
	}

	filesystem->FPrintf(f,"\n");

 
	CUtlDict<bool,int> g_CompletedRows;

	// Dump each row:
	bool bDidRow;

	do
	{
		bDidRow = false;

		for( idx = g_DamageMap.First(); idx != g_DamageMap.InvalidIndex(); idx = g_DamageMap.Next(idx) )
		{
			char szRowName[256];

			// Check the Row name of each entry to see if I've done this row yet.
			Q_strncpy(szRowName, g_DamageMap.GetElementName(idx), sizeof( szRowName ) );
			*strchr(szRowName,',') = '\0';

			char szRowNameComma[256];
			Q_snprintf( szRowNameComma, sizeof( szRowNameComma ), "%s,", szRowName );

			if( g_CompletedRows.Find(szRowName) == g_CompletedRows.InvalidIndex() )
			{
				bDidRow = true;
				g_CompletedRows.Insert(szRowName,false);


				// Output the row name:
				filesystem->FPrintf(f,szRowName);

				for( int ColumnIdx = g_UniqueColumns.First(); ColumnIdx != g_UniqueColumns.InvalidIndex(); ColumnIdx = g_UniqueColumns.Next( ColumnIdx ) )
				{
					char szRowNameCommaColumn[256];
					Q_strncpy( szRowNameCommaColumn, szRowNameComma, sizeof( szRowNameCommaColumn ) );					
					Q_strncat( szRowNameCommaColumn, g_UniqueColumns.GetElementName( ColumnIdx ), sizeof( szRowNameCommaColumn ), COPY_ALL_CHARACTERS );

					int nDamageAmount = 0;
					// Fine to reuse idx since we are going to break anyways.
					for( idx = g_DamageMap.First(); idx != g_DamageMap.InvalidIndex(); idx = g_DamageMap.Next(idx) )
					{
						if( !stricmp( g_DamageMap.GetElementName(idx), szRowNameCommaColumn ) )
						{
							nDamageAmount = g_DamageMap[idx];
							break;
						}
					}

					filesystem->FPrintf(f,"\t%i",nDamageAmount);

				}

				filesystem->FPrintf(f,"\n");
				break;
			}
		}
		// Grab the row name:

	} while(bDidRow);

	// close the file:
	filesystem->Close(f);
}

static ConCommand obj_dump_damage( "obj_dump_damage", Cmd_DamageDump_f );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ReportDamage( const char* szInflictor, const char* szVictim, float fAmount, int nCurrent, int nMax )
{
	int iAmount = (int)fAmount;

	if( object_show_damage.GetBool() && iAmount )
	{
		Msg( "ShowDamage: Object %s taking %0.1f damage from %s ( %i / %i )\n", szVictim, fAmount, szInflictor, nCurrent, nMax );
	}

	if( object_capture_damage.GetBool() )
	{
		char szMangledKey[256];

		Q_snprintf(szMangledKey,sizeof(szMangledKey)/sizeof(szMangledKey[0]),"%s,%s",szInflictor,szVictim);
		int idx = g_DamageMap.Find( szMangledKey );

		if( idx == g_DamageMap.InvalidIndex() )
		{
			g_DamageMap.Insert( szMangledKey, iAmount );

		} else
		{
			g_DamageMap[idx] += iAmount;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Return the first non-hostile object build on this object
//-----------------------------------------------------------------------------
CBaseEntity *CBaseObject::GetFirstFriendlyObjectOnMe( void )
{
	CBaseObject *pFirstObject = NULL;

	IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>(this);
	int iNumObjects = pBPInterface->GetNumObjectsOnMe();
	for ( int iPoint=0;iPoint<iNumObjects;iPoint++ )
	{
		CBaseObject *pObject = GetBuildPointObject( iPoint );

		if ( pObject && !pObject->IsHostileUpgrade() )
		{
			pFirstObject = pObject;
			break;
		}
	}

	return pFirstObject;
}

//-----------------------------------------------------------------------------
// Purpose: Pass the specified amount of damage through to any objects I have built on me
//-----------------------------------------------------------------------------
bool CBaseObject::PassDamageOntoChildren( const CTakeDamageInfo &info, float *flDamageLeftOver )
{
	float flDamage = info.GetDamage();

	// Double the amount of damage done (and get around the child damage modifier)
	flDamage *= 2;
	if ( obj_child_damage_factor.GetFloat() )
	{
		flDamage *= (1 / obj_child_damage_factor.GetFloat());
	}

	// Remove blast damage because child objects (well specifically upgrades)
	// want to ignore direct blast damage but still take damage from parent
	CTakeDamageInfo childInfo = info;
	childInfo.SetDamage( flDamage );
	childInfo.SetDamageType( info.GetDamageType() & (~DMG_BLAST) );

	CBaseEntity *pEntity = GetFirstFriendlyObjectOnMe();
	while ( pEntity )
	{
		Assert( pEntity->m_takedamage != DAMAGE_NO );
		// Do damage to the next object
		float flDamageTaken = pEntity->OnTakeDamage( childInfo );
		// If we didn't kill it, abort
		CBaseObject *pObject = dynamic_cast<CBaseObject*>(pEntity);
		if ( !pObject || !pObject->IsDying() )
		{
			char* szInflictor = "unknown";
			if( info.GetInflictor() )
				szInflictor = (char*)info.GetInflictor()->GetClassname();

			ReportDamage( szInflictor, GetClassname(), flDamageTaken, GetHealth(), GetMaxHealth() );

			*flDamageLeftOver = flDamage;
			return true;
		}
		// Reduce the damage and move on to the next
		flDamage -= flDamageTaken;
		pEntity = GetFirstFriendlyObjectOnMe();
	}

	*flDamageLeftOver = flDamage;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CBaseObject::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( !IsAlive() )
		return info.GetDamage();

	if ( m_takedamage == DAMAGE_NO )
		return 0;

	if ( HasSpawnFlags( SF_OBJ_INVULNERABLE ) )
		return 0;

	if ( IsPlacing() )
		return 0;

	// Check teams
	if ( info.GetAttacker() )
	{
		if ( InSameTeam(info.GetAttacker()) )
			return 0;
	}

	IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>(this);

	float flDamage = info.GetDamage();

	// Check weapon attributes if damages change when we hit a building.
	CBaseEntity *pWeapon = info.GetWeapon();
	if ( pWeapon )
	{
		int iEnergyWeaponNoDamage = 0;
		CALL_ATTRIB_HOOK_INT_ON_OTHER( pWeapon, iEnergyWeaponNoDamage, energy_weapon_no_hurt_building );
		if ( iEnergyWeaponNoDamage != 0 )
			flDamage *= 0.2f;

		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( pWeapon, flDamage, mult_dmg_vs_buildings );
	}

    if ( info.GetAttacker() && info.GetAttacker()->IsPlayer() )
	{
		CTFPlayer *pTFPlayer = ToTFPlayer( info.GetAttacker() );
		if ( pTFPlayer && pTFPlayer->m_Shared.InCond( TF_COND_RUNE_KNOCKOUT ) )
			flDamage *= 4.0f;
	}

	// Objects build on other objects take less damage
	if ( !IsAnUpgrade() && GetParentObject() )
	{
		flDamage *= obj_child_damage_factor.GetFloat();
	}

	if ( obj_damage_factor.GetFloat() )
	{
		flDamage *= obj_damage_factor.GetFloat();
	}

	bool bFriendlyObjectsAttached = false;
	int iNumObjects = pBPInterface->GetNumObjectsOnMe();
	for ( int iPoint=0;iPoint<iNumObjects;iPoint++ )
	{
		CBaseObject *pObject = GetBuildPointObject( iPoint );

		if ( !pObject || pObject->IsHostileUpgrade() )
		{
			continue;
		}

		bFriendlyObjectsAttached = true;
		break;
	}

	// If I have objects on me, I can't be destroyed until they're gone. Ditto if I can't be killed.
	bool bWillDieButCant = ( bFriendlyObjectsAttached ) && (( m_flHealth - flDamage ) < 1);
	if ( bWillDieButCant )
	{
		// Soak up the damage it would take to drop us to 1 health
		flDamage = flDamage - m_flHealth;
		SetHealth( 1 );

		// Pass leftover damage 
		if ( flDamage )
		{
			if ( PassDamageOntoChildren( info, &flDamage ) )
				return flDamage;
		}
	}

	//int iOldHealth = m_iHealth;

	if ( flDamage )
	{
		// Recheck our death possibility, because our objects may have all been blown off us by now
		bWillDieButCant = ( bFriendlyObjectsAttached ) && (( m_flHealth - flDamage ) < 1);
		if ( !bWillDieButCant )
		{
			// Reduce health
			SetHealth( m_flHealth - flDamage );
		}
	}
/*
	IGameEvent *event = gameeventmanager->CreateEvent( "npc_hurt" );

	if ( event )
	{
		CTFPlayer *pTFAttacker = ToTFPlayer( info.GetAttacker() );
		CTFWeaponBase *pTFWeapon = dynamic_cast<CTFWeaponBase *>( info.GetWeapon() );

		event->SetInt( "entindex", entindex() );
		event->SetInt( "attacker_player", pTFAttacker ? pTFAttacker->GetUserID() : 0 );
		event->SetInt( "weaponid", pTFWeapon ? pTFWeapon->GetWeaponID() : TF_WEAPON_NONE );
		event->SetInt( "damageamount", iOldHealth - m_iHealth );
		event->SetInt( "health", max( 0, m_iHealth ) );
		event->SetBool( "crit", false );
		event->SetBool( "boss", false );

		gameeventmanager->FireEvent( event );
	}
*/
	m_OnDamaged.FireOutput(info.GetAttacker(), this);

	if ( GetHealth() <= 0 )
	{
		if ( info.GetAttacker() )
		{
			//TFStats()->IncrementTeamStat( info.GetAttacker()->GetTeamNumber(), TF_TEAM_STAT_DESTROYED_OBJECT_COUNT, 1 );
			//TFStats()->IncrementPlayerStat( info.GetAttacker(), TF_PLAYER_STAT_DESTROYED_OBJECT_COUNT, 1 );
		}

		m_lifeState = LIFE_DEAD;
		m_OnDestroyed.FireOutput( info.GetAttacker(), this);
		Killed( info );

		// Tell our builder to speak about it
		if ( m_hBuilder )
		{
			m_hBuilder->SpeakConceptIfAllowed( MP_CONCEPT_LOST_OBJECT, GetResponseRulesModifier() );
		}
	}

	char* szInflictor = "unknown";
	if( info.GetInflictor() )
		szInflictor = (char*)info.GetInflictor()->GetClassname();

	ReportDamage( szInflictor, GetClassname(), flDamage, GetHealth(), GetMaxHealth() );

	return flDamage;
}

//-----------------------------------------------------------------------------
// Purpose: Repair / Help-Construct this object the specified amount
//-----------------------------------------------------------------------------
bool CBaseObject::Repair( float flHealth )
{
	// Multiply it by the repair rate
	flHealth *= GetConstructionMultiplier();
	if ( !flHealth )
		return false;

	if ( IsBuilding() )
	{
		// Reduce the construction time by the correct amount for the health passed in
		float flBuildingHealth = OBJECT_CONSTRUCTION_STARTINGHEALTH;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( GetBuilder(), flBuildingHealth, mult_engy_building_health );
		float flConstructionTime = flHealth / ((GetMaxHealth() - flBuildingHealth) / m_flTotalConstructionTime);
		m_flConstructionTimeLeft = max( 0, m_flConstructionTimeLeft - flConstructionTime);
		m_flConstructionTimeLeft = clamp( m_flConstructionTimeLeft, 0.0f, m_flTotalConstructionTime );
		m_flPercentageConstructed = 1 - (m_flConstructionTimeLeft / m_flTotalConstructionTime);
		m_flPercentageConstructed = clamp( m_flPercentageConstructed, 0.0f, 1.0f );

		// Increase health.
		// Only regenerate up to previous health while re-deploying.
		int iMaxHealth = IsRedeploying() ? m_iGoalHealth : GetMaxHealth();
		SetHealth( min( iMaxHealth, m_flHealth + flHealth ) );

		// Return true if we're constructed now
		if ( m_flConstructionTimeLeft <= 0.0f )
		{
			FinishedBuilding();
			return true;
		}
	}
	else
	{
		// Return true if we're already fully healed
		if ( GetHealth() >= GetMaxHealth() )
			return true;

		// Increase health.
		SetHealth( min( GetMaxHealth(), m_flHealth + flHealth ) );

		m_OnRepaired.FireOutput( this, this);

		// Return true if we're fully healed now
		if ( GetHealth() == GetMaxHealth() )
			return true;
	}

	return false;
}

void CBaseObject::OnConstructionHit( CTFPlayer *pPlayer, CTFWrench *pWrench, Vector vecHitPos )
{
	// Get the player index
	int iPlayerIndex = pPlayer->entindex();

	// The time the repair is going to expire
	float flRepairExpireTime = gpGlobals->curtime + 1.0;

	// Update or Add the expire time to the list
	int index = m_RepairerList.Find( iPlayerIndex );
	if ( index == m_RepairerList.InvalidIndex() )
	{
		m_RepairerList.Insert( iPlayerIndex, flRepairExpireTime );
	}
	else
	{
		m_RepairerList[index] = flRepairExpireTime;
	}

	CPVSFilter filter( vecHitPos );
	TE_TFParticleEffect( filter, 0.0f, "nutsnbolts_build", vecHitPos, QAngle( 0,0,0 ) );
}


float CBaseObject::GetConstructionMultiplier( void )
{
	float flMultiplier = 1.0f;

	// Minis deploy faster.
	if ( IsMiniBuilding() )
		flMultiplier *= 1.7f;

	// Re-deploy twice as fast.
	if ( IsRedeploying() )
		flMultiplier *= 2.0f;

	// expire all the old 
	int i = m_RepairerList.LastInorder();
	while ( i != m_RepairerList.InvalidIndex() )
	{
		int iThis = i;
		i = m_RepairerList.PrevInorder( i );
		if ( m_RepairerList[iThis] < gpGlobals->curtime )
		{
			m_RepairerList.RemoveAt( iThis );
		}
		else
		{
			// Each player hitting it builds twice as fast
			flMultiplier *= 2.5;

			// Check if this weapon has a build modifier
			CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( UTIL_PlayerByIndex( m_RepairerList.Key( iThis ) ), flMultiplier, mult_construction_value );
		}
	}

	return flMultiplier;
}

//-----------------------------------------------------------------------------
// Purpose: Object is exploding because it was killed or detonate
//-----------------------------------------------------------------------------
void CBaseObject::Explode( void )
{
	const char *pExplodeSound = GetObjectInfo( ObjectType() )->m_pExplodeSound;

	if ( pExplodeSound && Q_strlen(pExplodeSound) > 0 )
	{
		EmitSound( pExplodeSound );
	}

	const char *pExplodeEffect = GetObjectInfo( ObjectType() )->m_pExplosionParticleEffect;
	if ( pExplodeEffect && pExplodeEffect[0] != '\0' )
	{
		// Send to everyone - we're inside prediction for the engy who hit this off, but we 
		// don't predict that the hit will kill this object.
		CDisablePredictionFiltering disabler;

		Vector origin = GetAbsOrigin();
		QAngle up(-90,0,0);

		CPVSFilter filter( origin );
		TE_TFParticleEffect( filter, 0.0f, pExplodeEffect, origin, up );
	}

	// create some delicious, metal filled gibs
	CreateObjectGibs();
}

void CBaseObject::CreateObjectGibs( void )
{
	if ( m_aGibs.Count() <= 0 )
	{
		return;
	}

	const CObjectInfo *pObjectInfo = GetObjectInfo( ObjectType() );

	int nMetalPerGib = pObjectInfo->m_iMetalToDropInGibs / m_aGibs.Count();
	if ( GetType() == OBJ_SENTRYGUN && !IsMiniBuilding() )
		nMetalPerGib = 65;

	int i;
	for ( i=0; i<m_aGibs.Count(); i++ )
	{
		const char *szGibModel = m_aGibs[i].modelName;

		CTFAmmoPack *pAmmoPack = CTFAmmoPack::Create( GetAbsOrigin(), GetAbsAngles(), this, szGibModel );
		Assert( pAmmoPack );
		if ( pAmmoPack )
		{
			pAmmoPack->ActivateWhenAtRest();

			// Fill up the ammo pack.
			pAmmoPack->GiveAmmo( nMetalPerGib, TF_AMMO_METAL );

			// Calculate the initial impulse on the weapon.
			Vector vecImpulse( random->RandomFloat( -0.5, 0.5 ), random->RandomFloat( -0.5, 0.5 ), random->RandomFloat( 0.75, 1.25 ) );
			VectorNormalize( vecImpulse );
			vecImpulse *= random->RandomFloat( tf_obj_gib_velocity_min.GetFloat(), tf_obj_gib_velocity_max.GetFloat() );

			QAngle angImpulse( random->RandomFloat ( -100, -500 ), 0, 0 );

			// Cap the impulse.
			float flSpeed = vecImpulse.Length();
			if ( flSpeed > tf_obj_gib_maxspeed.GetFloat() )
			{
				VectorScale( vecImpulse, tf_obj_gib_maxspeed.GetFloat() / flSpeed, vecImpulse );
			}

			if ( pAmmoPack->VPhysicsGetObject() )
			{
				// We can probably remove this when the mass on the weapons is correct!
				//pAmmoPack->VPhysicsGetObject()->SetMass( 25.0f );
				AngularImpulse angImpulse( 0, random->RandomFloat( 0, 100 ), 0 );
				pAmmoPack->VPhysicsGetObject()->SetVelocityInstantaneous( &vecImpulse, &angImpulse );
			}

			pAmmoPack->SetInitialVelocity( vecImpulse );

			switch ( GetTeamNumber() )
			{
				case TF_TEAM_RED:
					pAmmoPack->m_nSkin = 0;
					break;

				case TF_TEAM_BLUE:
					pAmmoPack->m_nSkin = 1;
					break;

				default:
					pAmmoPack->m_nSkin = 1;
					break;

			}

			// Give the ammo pack some health, so that trains can destroy it.
			pAmmoPack->SetCollisionGroup( COLLISION_GROUP_DEBRIS );
			pAmmoPack->m_takedamage = DAMAGE_YES;		
			pAmmoPack->SetHealth( 900 );

			if ( IsMiniBuilding() )
			{
				pAmmoPack->SetModelScale( 0.75f );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Object has been blown up. Drop resource chunks upto the value of my max health.
//-----------------------------------------------------------------------------
void CBaseObject::Killed( const CTakeDamageInfo &info )
{
	m_bDying = true;

	// Find the killer & the scorer
	CBaseEntity *pInflictor = info.GetInflictor();
	CBaseEntity *pKiller = info.GetAttacker();
	CTFPlayer *pScorer = ToTFPlayer( TFGameRules()->GetDeathScorer( pKiller, pInflictor, this ) );
	CTFPlayer *pAssister = NULL;

	// if this object has a sapper on it, and was not killed by the sapper (killed by damage other than crush, since sapper does crushing damage),
	// award an assist to the owner of the sapper since it probably contributed to destroying this object
	if ( HasSapper() && !( DMG_CRUSH & info.GetDamageType() ) )
	{
		CObjectSapper *pSapper = dynamic_cast<CObjectSapper *>( FirstMoveChild() );
		if ( pSapper )
		{
			// give an assist to the sapper's owner
			pAssister = pSapper->GetOwner();
			CTF_GameStats.Event_AssistDestroyBuilding( pAssister, this );

			CTFWeaponBase *pRevolver = pAssister->Weapon_OwnsThisID( TF_WEAPON_REVOLVER );
			if ( pRevolver )
			{
				int iKillsCollectCrit = 0;
				CALL_ATTRIB_HOOK_INT_ON_OTHER( pRevolver, iKillsCollectCrit, sapper_kills_collect_crits );
				if ( iKillsCollectCrit > 0 )
				{
					pAssister->m_Shared.SetDecapitationCount( Min( pAssister->m_Shared.GetDecapitationCount() + 1, 35 ) );
				}
			}
		}
	}

	// Don't do anything if we were detonated or dismantled
	if ( pScorer && pInflictor != this )
	{
		IGameEvent * event = gameeventmanager->CreateEvent( "object_destroyed" );
		int iWeaponID = TF_WEAPON_NONE;

		// Work out what killed the player, and send a message to all clients about it
		const char *killer_weapon_name = TFGameRules()->GetKillingWeaponName( info, NULL, iWeaponID );
		const char *killer_weapon_log_name = NULL;

		if ( iWeaponID && pScorer )
		{
			CTFWeaponBase *pWeapon = pScorer->Weapon_OwnsThisID( iWeaponID );
			if ( pWeapon )
			{
				CEconItemDefinition *pItemDef = pWeapon->GetItem()->GetStaticData();
				if ( pItemDef )
				{
					if ( pItemDef->item_iconname[0] )
						killer_weapon_name = pItemDef->item_iconname;

					if ( pItemDef->item_logname[0] )
						killer_weapon_log_name = pItemDef->item_logname;
				}
			}
		}

		CTFPlayer *pTFPlayer = GetOwner();

		if ( event )
		{
			if ( pTFPlayer )
			{
				event->SetInt( "userid", pTFPlayer->GetUserID() );
				event->SetInt( "victim_index", pTFPlayer->entindex() );
			}
			if ( pAssister && ( pAssister != pScorer ) )
			{
				event->SetInt( "assister", pAssister->GetUserID() );
				event->SetInt( "assister_index", pAssister->entindex() );
			}
			
			event->SetInt( "attacker", pScorer->GetUserID() );	// attacker
			event->SetInt( "attacker_index", pScorer->entindex() );
			event->SetString( "weapon", killer_weapon_name );
			event->SetString( "weapon_logclassname", killer_weapon_log_name );
			event->SetInt( "priority", 6 );		// HLTV event priority, not transmitted
			event->SetInt( "objecttype", GetType() );
			event->SetInt( "index", entindex() );	// object entity index

			gameeventmanager->FireEvent( event );
		}
		CTFPlayer *pPlayerScorer = ToTFPlayer( pScorer );
		if ( pPlayerScorer )
		{
			CTF_GameStats.Event_PlayerDestroyedBuilding( pPlayerScorer, this );
			pPlayerScorer->Event_KilledOther(this, info);
		}	
	}

	// Do an explosion.
	Explode();

	UTIL_Remove( this );
}

//-----------------------------------------------------------------------------
// Purpose: Indicates this NPC's place in the relationship table.
//-----------------------------------------------------------------------------
Class_T	CBaseObject::Classify( void )
{
	switch (GetTeamNumber())
	{
	case TF_TEAM_RED:
		return CLASS_PLAYER_ALLY;
		break;
	case TF_TEAM_BLUE:
		return CLASS_COMBINE;
		break;
	case TF_TEAM_GREEN:
		return CLASS_ZOMBIE;
		break;
	case TF_TEAM_YELLOW:
		return CLASS_ANTLION;
		break;
	default:
		return CLASS_NONE;
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Get the type of this object
//-----------------------------------------------------------------------------
int	CBaseObject::GetType()
{
	return m_iObjectType;
}

//-----------------------------------------------------------------------------
// Purpose: Get the builder of this object
//-----------------------------------------------------------------------------
CTFPlayer *CBaseObject::GetBuilder( void )
{
	return m_hBuilder;
}

//-----------------------------------------------------------------------------
// Purpose: Return true if the Owning CTeam should clean this object up automatically
//-----------------------------------------------------------------------------
bool CBaseObject::ShouldAutoRemove( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : iTeamNum - 
//-----------------------------------------------------------------------------
void CBaseObject::ChangeTeam( int iTeamNum )
{
	CTFTeam *pTeam = ( CTFTeam * )GetGlobalTeam( iTeamNum );
	CTFTeam *pExisting = ( CTFTeam * )GetTeam();

	TRACE_OBJECT( UTIL_VarArgs( "%0.2f CBaseObject::ChangeTeam old %s new %s\n", gpGlobals->curtime, 
		pExisting ? pExisting->GetName() : "NULL",
		pTeam ? pTeam->GetName() : "NULL" ) );

	// Already on this team
	if ( GetTeamNumber() == iTeamNum )
		return;

	if ( pExisting )
	{
		// Remove it from current team ( if it's in one ) and give it to new team
		pExisting->RemoveObject( this );
	}
		
	// Change to new team
	BaseClass::ChangeTeam( iTeamNum );
	
	// Add this object to the team's list
	// But only if we're not placing it
	if ( pTeam && (!m_bPlacing) )
	{
		pTeam->AddObject( this );
	}

	// Setup for our new team's model
	CreateBuildPoints();
}

//-----------------------------------------------------------------------------
// Purpose: Return true if I have at least 1 sapper on me
//-----------------------------------------------------------------------------
bool CBaseObject::HasSapper( void )
{
	return m_bHasSapper;
}

void CBaseObject::OnAddSapper( void )
{
	// Assume we can only build 1 sapper per object
	Assert( m_bHasSapper == false );

	m_bHasSapper = true;

	CTFPlayer *pPlayer = GetBuilder();

	if ( pPlayer )
	{
		//pPlayer->HintMessage( HINT_OBJECT_YOUR_OBJECT_SAPPED, true );
		pPlayer->SpeakConceptIfAllowed( MP_CONCEPT_SPY_SAPPER, GetResponseRulesModifier() );
	}

	UpdateDisabledState();
}

void CBaseObject::OnRemoveSapper( void )
{
	m_bHasSapper = false;

	UpdateDisabledState();
}

bool CBaseObject::ShowVGUIScreen( int panelIndex, bool bShow )
{
	Assert( panelIndex >= 0 && panelIndex < m_hScreens.Count() );
	if ( m_hScreens[panelIndex].Get() )
	{
		m_hScreens[panelIndex]->SetActive( bShow );
		return true;
	}
	else
	{
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::InputShow( inputdata_t &inputdata )
{
	RemoveFlag( EF_NODRAW );
	SetDisabled( false );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::InputHide( inputdata_t &inputdata )
{
	AddFlag( EF_NODRAW );
	SetDisabled( true );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::InputEnable( inputdata_t &inputdata )
{
	SetDisabled( false );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::InputDisable( inputdata_t &inputdata )
{
	AddFlag( EF_NODRAW );
	SetDisabled( true );
}


//-----------------------------------------------------------------------------
// Purpose: Set the health of the object
//-----------------------------------------------------------------------------
void CBaseObject::InputSetHealth( inputdata_t &inputdata )
{
	m_iMaxHealth = inputdata.value.Int();
	SetHealth( m_iMaxHealth );
}

//-----------------------------------------------------------------------------
// Purpose: Add health to the object
//-----------------------------------------------------------------------------
void CBaseObject::InputAddHealth( inputdata_t &inputdata )
{
	int iHealth = inputdata.value.Int();
	SetHealth( min( GetMaxHealth(), m_flHealth + iHealth ) );
}

//-----------------------------------------------------------------------------
// Purpose: Remove health from the object
//-----------------------------------------------------------------------------
void CBaseObject::InputRemoveHealth( inputdata_t &inputdata )
{
	int iDamage = inputdata.value.Int();

	SetHealth( m_flHealth - iDamage );
	if ( GetHealth() <= 0 )
	{
		m_lifeState = LIFE_DEAD;
		m_OnDestroyed.FireOutput(this, this);

		CTakeDamageInfo info( inputdata.pCaller, inputdata.pActivator, vec3_origin, GetAbsOrigin(), iDamage, DMG_GENERIC );
		Killed( info );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CBaseObject::InputSetSolidToPlayer( inputdata_t &inputdata )
{
	int ival = inputdata.value.Int();
	ival = clamp( ival, (int)SOLID_TO_PLAYER_USE_DEFAULT, (int)SOLID_TO_PLAYER_NO );
	OBJSOLIDTYPE stp = (OBJSOLIDTYPE)ival;
	SetSolidToPlayers( stp );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CBaseObject::InputSetBuilder( inputdata_t &inputdata )
{
	// Find the matching netname
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CTFPlayer *pPlayer = ToTFPlayer( UTIL_PlayerByIndex(i) );
		if ( pPlayer )
		{
			if ( Q_strstr( pPlayer->GetPlayerName(), inputdata.value.String() ) )
			{
				SetBuilder( pPlayer );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : 
// Output : did this wrench hit do any work on the object?
//-----------------------------------------------------------------------------
bool CBaseObject::InputWrenchHit( CTFPlayer *pPlayer, CTFWrench *pWrench, Vector vecHitPos )
{
	Assert( pPlayer );
	if ( !pPlayer )
		return false;

	bool bDidWork = false;

	if ( HasSapper() )
	{
		// do damage to any attached buildings

		#define WRENCH_DMG_VS_SAPPER	65

		CTakeDamageInfo info( pPlayer, pPlayer, WRENCH_DMG_VS_SAPPER, DMG_CLUB, TF_DMG_WRENCH_FIX );

		IHasBuildPoints *pBPInterface = dynamic_cast< IHasBuildPoints * >( this );
		int iNumObjects = pBPInterface->GetNumObjectsOnMe();
		for ( int iPoint=0;iPoint<iNumObjects;iPoint++ )
		{
			CBaseObject *pObject = GetBuildPointObject( iPoint );

			if ( pObject && pObject->IsHostileUpgrade() )
			{
				int iBeforeHealth = pObject->GetHealth();

				pObject->TakeDamage( info );

				// This should always be true
				if ( iBeforeHealth != pObject->GetHealth() )
				{
					bDidWork = true;
					Assert( bDidWork );
				}
			}
		}
	}
	else if ( IsBuilding() )
	{
		OnConstructionHit( pPlayer, pWrench, vecHitPos );
		bDidWork = true;
	}
	else
	{
		// upgrade, refill, repair damage
		bDidWork = OnWrenchHit( pPlayer, pWrench, vecHitPos );
	}

	return bDidWork;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseObject::OnWrenchHit( CTFPlayer *pPlayer, CTFWrench *pWrench, Vector vecHitPos )
{
	bool bRepair = false;
	bool bUpgrade = false;

	// If the player repairs it at all, we're done
	bRepair = Command_Repair( pPlayer/*, pWrench->GetRepairValue()*/ );

	if ( !bRepair )
	{
		// no building upgrade for minis.
		if ( !IsMiniBuilding() )
		{
			// Don't put in upgrade metal until the object is fully healed
			if ( CanBeUpgraded( pPlayer ) )
			{
				bUpgrade = CheckUpgradeOnHit( pPlayer );
			}
		}
	}

	DoWrenchHitEffect( vecHitPos, bRepair, bUpgrade );

	return ( bRepair || bUpgrade );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBaseObject::CheckUpgradeOnHit( CTFPlayer *pPlayer )
{
	bool bUpgrade = false;

	int iPlayerMetal = pPlayer->GetAmmoCount( TF_AMMO_METAL );
	int iAmountToAdd = min( tf_obj_upgrade_per_hit.GetInt(), iPlayerMetal );

	if ( iAmountToAdd > ( m_iUpgradeMetalRequired - m_iUpgradeMetal ) )
		iAmountToAdd = ( m_iUpgradeMetalRequired - m_iUpgradeMetal );

	if ( tf_cheapobjects.GetBool() == false )
	{
		pPlayer->RemoveAmmo( iAmountToAdd, TF_AMMO_METAL );
	}
	m_iUpgradeMetal += iAmountToAdd;

	if ( iAmountToAdd > 0 )
	{
		bUpgrade = true;
	}

	if ( m_iUpgradeMetal >= m_iUpgradeMetalRequired )
	{
		StartUpgrading();

		IGameEvent * event = gameeventmanager->CreateEvent( "player_upgradedobject" );
		if ( event )
		{
			event->SetInt( "userid", pPlayer->GetUserID() );
			event->SetInt( "object", ObjectType() );
			event->SetInt( "index", entindex() );	// object entity index
			event->SetBool( "isbuilder", pPlayer == GetBuilder() );
			gameeventmanager->FireEvent( event, true );	// don't send to clients
		}

		m_iUpgradeMetal = 0;
	}

	return bUpgrade;
}

//-----------------------------------------------------------------------------
// Purpose: Separated so it can be triggered by wrench hit or by vgui screen
//-----------------------------------------------------------------------------
bool CBaseObject::Command_Repair( CTFPlayer *pActivator )
{
	if ( GetHealth() < GetMaxHealth() )
	{
		float flRepairRate = 1;
		CALL_ATTRIB_HOOK_FLOAT_ON_OTHER( pActivator, flRepairRate, mult_repair_value );

		int	iAmountToHeal = min( (int)(flRepairRate * 100) , GetMaxHealth() - GetHealth() );

		// repair the building
		int iRepairCost = ceil( (float)( iAmountToHeal ) * 0.33f );	
	
		TRACE_OBJECT( UTIL_VarArgs( "%0.2f CObjectDispenser::Command_Repair ( %d / %d ) - cost = %d\n", gpGlobals->curtime, 
			GetHealth(),
			GetMaxHealth(),
			iRepairCost ) );

		if ( iRepairCost > 0 )
		{
			if ( iRepairCost > pActivator->GetBuildResources() )
			{
				iRepairCost = pActivator->GetBuildResources();
			}

			pActivator->RemoveBuildResources( iRepairCost );

			float flNewHealth = min( GetMaxHealth(), m_flHealth + ( iRepairCost * 5 ) );
			SetHealth( flNewHealth );
	
			return ( iRepairCost > 0 );
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::PlayStartupAnimation( void )
{
	SetActivity( ACT_OBJ_STARTUP );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::DetermineAnimation( void )
{
	Activity desiredActivity = m_Activity;

	switch ( m_Activity )
	{
	default:
		{
			if ( IsUpgrading() )
			{
				desiredActivity = ACT_OBJ_UPGRADING;
			}
			else if ( IsPlacing() )
			{
				/*
				if (1 || m_bPlacementOK )
				{
					desiredActivity = ACT_OBJ_PLACING;
				}
				else
				{
					desiredActivity = ACT_OBJ_IDLE;
				}
				*/
			}
			else if ( IsBuilding() )
			{
				desiredActivity = ACT_OBJ_ASSEMBLING;
			}
			else
			{
				desiredActivity = ACT_OBJ_RUNNING;
			}
		}
		break;
	case ACT_OBJ_STARTUP:
		{
			if ( IsActivityFinished() )
			{
				desiredActivity = ACT_OBJ_RUNNING;
			}
		}
		break;
	}

	if ( desiredActivity == m_Activity )
		return;

	SetActivity( desiredActivity );
}

//-----------------------------------------------------------------------------
// Purpose: Attach this object to the specified object
//-----------------------------------------------------------------------------
void CBaseObject::AttachObjectToObject( CBaseEntity *pEntity, int iPoint, Vector &vecOrigin )
{
	m_hBuiltOnEntity = pEntity;
	m_iBuiltOnPoint = iPoint;

	if ( m_hBuiltOnEntity.Get() )
	{
		// Parent ourselves to the object
		CBaseAnimating *pAnimating = dynamic_cast<CBaseAnimating *>( pEntity );
		if ( pAnimating && pAnimating->LookupBone( "weapon_bone" ) > 0 )
		{
			FollowEntity( m_hBuiltOnEntity.Get(), true );
		}

		int iAttachment = 0;
		IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>( pEntity );
		if ( pBPInterface )
		{
			iAttachment = pBPInterface->GetBuildPointAttachmentIndex( iPoint );

			// re-link to the build points if the sapper is already built
			if ( !( IsPlacing() || IsBuilding() ) )
			{
				pBPInterface->SetObjectOnBuildPoint( m_iBuiltOnPoint, this );
			}
		}		

		SetParent( m_hBuiltOnEntity.Get(), iAttachment );

		if ( iAttachment >= 1 )
		{
			// Stick right onto the attachment point.
			vecOrigin.Init();
			SetLocalOrigin( vecOrigin );
			SetLocalAngles( QAngle(0,0,0) );
		}
		else
		{
			SetAbsOrigin( vecOrigin );
			vecOrigin = GetLocalOrigin();
		}

		SetupAttachedVersion();
	}

	Assert( m_hBuiltOnEntity.Get() == GetMoveParent() );
}

//-----------------------------------------------------------------------------
// Purpose: Detach this object from its parent, if it has one
//-----------------------------------------------------------------------------
void CBaseObject::DetachObjectFromObject( void )
{
	if ( !GetParentObject() || !GetParentEntity() )
		return;

	// Clear the build point
	IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>( GetParentObject() );
	if ( pBPInterface )
		pBPInterface->SetObjectOnBuildPoint( m_iBuiltOnPoint, NULL );

	SetParent( NULL );
	m_hBuiltOnEntity = NULL;
	m_iBuiltOnPoint = 0;
}


//-----------------------------------------------------------------------------
// Purpose: Spawn any objects specified inside the mdl
//-----------------------------------------------------------------------------
void CBaseObject::SpawnEntityOnBuildPoint( const char *pEntityName, int iAttachmentNumber )
{
	// Try and spawn the object
	CBaseEntity *pEntity = CreateEntityByName( pEntityName );
	if ( !pEntity )
		return;

	Vector vecOrigin;
	QAngle vecAngles;
	GetAttachment( iAttachmentNumber, vecOrigin, vecAngles );
	pEntity->SetAbsOrigin( vecOrigin );
	pEntity->SetAbsAngles( vecAngles );
	pEntity->Spawn();
	
	// If it's an object, finish setting it up
	CBaseObject *pObject = dynamic_cast<CBaseObject*>(pEntity);
	if ( !pObject )
		return;

	// Add a buildpoint here
	int iPoint = AddBuildPoint( iAttachmentNumber );
	AddValidObjectToBuildPoint( iPoint, pObject->GetType() );
	pObject->SetBuilder( GetBuilder() );
	pObject->ChangeTeam( GetTeamNumber() );
	pObject->SpawnControlPanels();
	pObject->SetHealth( pObject->GetMaxHealth() );
	pObject->FinishedBuilding();
	pObject->AttachObjectToObject( this, iPoint, vecOrigin );
	//pObject->m_fObjectFlags |= OF_CANNOT_BE_DISMANTLED;

	IHasBuildPoints *pBPInterface = dynamic_cast<IHasBuildPoints*>(this);
	if ( pBPInterface )
		pBPInterface->SetObjectOnBuildPoint( iPoint, pObject );
}


//-----------------------------------------------------------------------------
// Purpose: Spawn any objects specified inside the mdl
//-----------------------------------------------------------------------------
void CBaseObject::SpawnObjectPoints( void )
{
	KeyValues *modelKeyValues = new KeyValues("");
	if ( !modelKeyValues->LoadFromBuffer( modelinfo->GetModelName( GetModel() ), modelinfo->GetModelKeyValueText( GetModel() ) ) )
	{
		modelKeyValues->deleteThis();
		return;
	}

	// Do we have a build point section?
	KeyValues *pkvAllObjectPoints = modelKeyValues->FindKey("object_points");
	if ( !pkvAllObjectPoints )
	{
		modelKeyValues->deleteThis();
		return;
	}

	// Start grabbing the sounds and slotting them in
	KeyValues *pkvObjectPoint;
	for ( pkvObjectPoint = pkvAllObjectPoints->GetFirstSubKey(); pkvObjectPoint; pkvObjectPoint = pkvObjectPoint->GetNextKey() )
	{
		// Find the attachment first
		const char *sAttachment = pkvObjectPoint->GetName();
		int iAttachmentNumber = LookupAttachment( sAttachment );
		if ( iAttachmentNumber == 0 )
		{
			Msg( "ERROR: Model %s specifies object point %s, but has no attachment named %s.\n", STRING(GetModelName()), pkvObjectPoint->GetString(), pkvObjectPoint->GetString() );
			continue;
		}

		// Now see what we're supposed to spawn there
		// The count check is because it seems wrong to emit multiple entities on the same point
		int nCount = 0;
		KeyValues *pkvObject;
		for ( pkvObject = pkvObjectPoint->GetFirstSubKey(); pkvObject; pkvObject = pkvObject->GetNextKey() )
		{
			SpawnEntityOnBuildPoint( pkvObject->GetName(), iAttachmentNumber );
			++nCount;
			Assert( nCount <= 1 );
		}
	}

	modelKeyValues->deleteThis();
}

bool CBaseObject::IsSolidToPlayers( void ) const
{
	switch ( m_SolidToPlayers )
	{
	default:
		break;
	case SOLID_TO_PLAYER_USE_DEFAULT:
		{
			if ( GetObjectInfo( ObjectType() ) )
			{
				return GetObjectInfo( ObjectType() )->m_bSolidToPlayerMovement;
			}
		}
		break;
	case SOLID_TO_PLAYER_YES:
		return true;
	case SOLID_TO_PLAYER_NO:
		return false;
	}

	return false;
}

void CBaseObject::SetSolidToPlayers( OBJSOLIDTYPE stp, bool force )
{
	bool changed = stp != m_SolidToPlayers;
	m_SolidToPlayers = stp;

	if ( changed || force )
	{
		SetCollisionGroup( 
			IsSolidToPlayers() ? 
				TFCOLLISION_GROUP_OBJECT_SOLIDTOPLAYERMOVEMENT : 
				TFCOLLISION_GROUP_OBJECT );
	}
}

int CBaseObject::DrawDebugTextOverlays(void) 
{
	int text_offset = BaseClass::DrawDebugTextOverlays();

	if (m_debugOverlays & OVERLAY_TEXT_BIT) 
	{
		char tempstr[512];

		Q_snprintf( tempstr, sizeof( tempstr ),"Health: %d / %d ( %.1f )", GetHealth(), GetMaxHealth(), (float)GetHealth() / (float)GetMaxHealth() );
		EntityText(text_offset,tempstr,0);
		text_offset++;

		CTFPlayer *pBuilder = GetBuilder();

		Q_snprintf( tempstr, sizeof( tempstr ),"Built by: (%d) %s",
			pBuilder ? pBuilder->entindex() : -1,
			pBuilder ? pBuilder->GetPlayerName() : "invalid builder" );
		EntityText(text_offset,tempstr,0);
		text_offset++;

		if ( IsBuilding() )
		{
			Q_snprintf( tempstr, sizeof( tempstr ),"Build Rate: %.1f", GetConstructionMultiplier() );
			EntityText(text_offset,tempstr,0);
			text_offset++;
		}
	}
	return text_offset;

}

//-----------------------------------------------------------------------------
// Purpose: Change build orientation
//-----------------------------------------------------------------------------
void CBaseObject::RotateBuildAngles( void )
{
	// rotate the build angles by 90 degrees ( final angle calculated after we network this )
	m_iDesiredBuildRotations++;
	m_iDesiredBuildRotations = m_iDesiredBuildRotations % 4;
}

//-----------------------------------------------------------------------------
// Purpose: called on edge cases to see if we need to change our disabled state
//-----------------------------------------------------------------------------
void CBaseObject::UpdateDisabledState( void )
{
	SetDisabled( HasSapper() );
}

//-----------------------------------------------------------------------------
// Purpose: called when our disabled state changes
//-----------------------------------------------------------------------------
void CBaseObject::SetDisabled( bool bDisabled )
{
	if ( bDisabled && !m_bDisabled )
	{
		OnStartDisabled();
	}
	else if ( !bDisabled && m_bDisabled )
	{
		OnEndDisabled();
	}

	m_bDisabled = bDisabled;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::OnStartDisabled( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::OnEndDisabled( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Called when the model changes, find new attachments for the children
//-----------------------------------------------------------------------------
void CBaseObject::ReattachChildren( void )
{
	int iNumBuildPoints = GetNumBuildPoints();
	for (CBaseEntity *pChild = FirstMoveChild(); pChild; pChild = pChild->NextMovePeer())
	{
		//CBaseObject *pObject = GetBuildPointObject( iPoint );
		CBaseObject *pObject = dynamic_cast<CBaseObject *>( pChild );

		if ( !pObject )
		{
			continue;
		}

		Assert( pObject->GetParent() == this );

		// get the type
		int iObjectType = pObject->GetType();

		bool bReattached = false;

		Vector vecDummy;

		for ( int i = 0; i < iNumBuildPoints && bReattached == false; i++ )
		{
			// Can this object build on this point?
			if ( CanBuildObjectOnBuildPoint( i, iObjectType ) )
			{
				pObject->AttachObjectToObject( this, i, vecDummy );
				bReattached = true;
			}
		}

		// if we can't find an attach for the child, remove it and print an error
		if ( bReattached == false )
		{
			pObject->DestroyObject();
			Assert( !"Couldn't find attachment point on upgraded object for existing child.\n" );
		}
	}
}

void CBaseObject::SetModel( const char *pModel )
{
	BaseClass::SetModel( pModel );

	// Clear out the gib list and create a new one.
	m_aGibs.Purge();
	BuildGibList( m_aGibs, GetModelIndex(), 1.0f, COLLISION_GROUP_NONE );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CBaseObject::BuildingUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	/*CTFPlayer *pPlayer = ToTFPlayer( pActivator );
	if ( pPlayer )
	{
		m_OnPlayerUse.FireOutput( pActivator, pCaller );
	}*/
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CBaseObject::FallThink( void )
{
	SetContextThink( &CBaseObject::FallThink, gpGlobals->curtime + 0.1, "FallThink" );

	//if ( GetGroundEntity() == NULL )
		SetAbsVelocity( Vector( 0, 0, -64 ) );
}

//-----------------------------------------------------------------------------
// Purpose: Create a ragdoll constraint that prevents us from flipping.
//-----------------------------------------------------------------------------
void CBaseObject::CreateAntiFlipConstraint()
{
	if ( m_pAntiFlipConstraint != NULL )
		return;

	constraint_ragdollparams_t ragdoll;
	ragdoll.Defaults();

	// Don't prevent the boat from moving, just flipping.
	ragdoll.onlyAngularLimits = true;

	// Put the ragdoll constraint in the space of the airboat.
	SetIdentityMatrix( ragdoll.constraintToAttached );
	BuildObjectRelativeXform( g_PhysWorldObject, VPhysicsGetObject(), ragdoll.constraintToReference );

	ragdoll.axes[0].minRotation = -100;
	ragdoll.axes[0].maxRotation = 100;
	ragdoll.axes[1].minRotation = -100;
	ragdoll.axes[1].maxRotation = 100;
	ragdoll.axes[2].minRotation = -180;
	ragdoll.axes[2].maxRotation = 180;

	m_pAntiFlipConstraint = physenv->CreateRagdollConstraint( g_PhysWorldObject, VPhysicsGetObject(), NULL, ragdoll );

	//NDebugOverlay::Cross3DOriented( ragdoll.constraintToReference, 128, 255, true, 100 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBaseObject::DestroyConstraint( void )
{
	// Destroy the constraint
	if ( m_pAntiFlipConstraint != NULL )
	{
		physenv->DestroyConstraint( m_pAntiFlipConstraint );
		m_pAntiFlipConstraint = NULL;
	}
}