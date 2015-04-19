

//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//
#include "cbase.h"
#include "decals.h"
#include "materialsystem/IMaterialVar.h"
#include "ieffects.h"
#include "fx.h"
#include "fx_impact.h"
#include "view.h"
#include "engine/IStaticPropMgr.h"
#include "datacache/imdlcache.h"
#include "debugoverlay_shared.h"
#include "c_impact_effects.h"
#include "tier0/vprof.h"
#ifdef INFESTED_DLL
#include "c_asw_fx.h"
#endif
#include "dlight.h"
#include "IEfx.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar  r_drawflecks( "r_drawflecks", "1" );
static ConVar  r_impacts_alt_orientation ( "r_impacts_alt_orientation", "1" );
extern ConVar r_drawmodeldecals;

ImpactSoundRouteFn g_pImpactSoundRouteFn = NULL;

//==========================================================================================================================
// RAGDOLL ENUMERATOR
//==========================================================================================================================
CRagdollEnumerator::CRagdollEnumerator( Ray_t& shot, int iDamageType )
{
	m_rayShot = shot;
	m_iDamageType = iDamageType;
	m_bHit = false;
}

IterationRetval_t CRagdollEnumerator::EnumElement( IHandleEntity *pHandleEntity )
{
	C_BaseEntity *pEnt = ClientEntityList().GetBaseEntityFromHandle( pHandleEntity->GetRefEHandle() );
	if ( pEnt == NULL )
		return ITERATION_CONTINUE;

	C_BaseAnimating *pModel = static_cast< C_BaseAnimating * >( pEnt );

	// If the ragdoll was created on this tick, then the forces were already applied on the server
	if ( pModel == NULL || WasRagdollCreatedOnCurrentTick( pEnt ) )
		return ITERATION_CONTINUE;

	IPhysicsObject *pPhysicsObject = pModel->VPhysicsGetObject();
	if ( pPhysicsObject == NULL )
		return ITERATION_CONTINUE;

	trace_t tr;
	enginetrace->ClipRayToEntity( m_rayShot, MASK_SHOT, pModel, &tr );

	if ( tr.fraction < 1.0 )
	{
		pModel->ImpactTrace( &tr, m_iDamageType, NULL );
		m_bHit = true;

		//FIXME: Yes?  No?
		return ITERATION_STOP;
	}

	return ITERATION_CONTINUE;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool FX_AffectRagdolls( Vector vecOrigin, Vector vecStart, int iDamageType )
{
	// don't do this when lots of ragdolls are simulating
	if ( s_RagdollLRU.CountRagdolls(true) > 1 )
		return false;
	Ray_t shotRay;
	shotRay.Init( vecStart, vecOrigin );

	CRagdollEnumerator ragdollEnum( shotRay, iDamageType );
	partition->EnumerateElementsAlongRay( PARTITION_CLIENT_RESPONSIVE_EDICTS, shotRay, false, &ragdollEnum );

	return ragdollEnum.Hit();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &data - 
//-----------------------------------------------------------------------------
void RagdollImpactCallback( const CEffectData &data )
{
	FX_AffectRagdolls( data.m_vOrigin, data.m_vStart, data.m_nDamageType );
}

DECLARE_CLIENT_EFFECT( RagdollImpact, RagdollImpactCallback );

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool Impact( Vector &vecOrigin, Vector &vecStart, int iMaterial, int iDamageType, int iHitbox, C_BaseEntity *pEntity, trace_t &tr, int nFlags, int maxLODToDecal )
{
	VPROF( "Impact" );

	Assert ( pEntity );

	MDLCACHE_CRITICAL_SECTION();

	// Clear out the trace
	memset( &tr, 0, sizeof(trace_t));
	tr.fraction = 1.0f;

	// Setup our shot information
	Vector shotDir = vecOrigin - vecStart;
	float flLength = VectorNormalize( shotDir );
	Vector traceExt;
	VectorMA( vecStart, flLength + 8.0f, shotDir, traceExt );

	// Attempt to hit ragdolls
	
	bool bHitRagdoll = false;
	
	//if ( !pEntity->IsClientCreated() )
	//{
		bHitRagdoll = FX_AffectRagdolls( vecOrigin, vecStart, iDamageType );
	//}

	if ( (nFlags & IMPACT_NODECAL) == 0 )
	{
		int decalNumber = decalsystem->GetDecalIndexForName( GetImpactDecal( pEntity, iMaterial, iDamageType ) );
		if ( decalNumber == -1 )
			return false;

		if ( (pEntity->entindex() == 0) && (iHitbox != 0) )
		{
			staticpropmgr->AddDecalToStaticProp( vecStart, traceExt, iHitbox - 1, decalNumber, true, tr );
		}
		else if ( pEntity )
		{
			// Here we deal with decals on entities.
			pEntity->AddDecal( vecStart, traceExt, vecOrigin, iHitbox, decalNumber, true, tr, maxLODToDecal );
		}
	}
	else
	{
		// Perform the trace ourselves
		Ray_t ray;
		ray.Init( vecStart, traceExt );

		if ( (pEntity->entindex() == 0) && (iHitbox != 0) )
		{
			// Special case for world entity with hitbox (that's a static prop)
			ICollideable *pCollideable = staticpropmgr->GetStaticPropByIndex( iHitbox - 1 ); 
			enginetrace->ClipRayToCollideable( ray, MASK_SHOT, pCollideable, &tr );
		}
		else
		{
			if ( !pEntity )
				return false;

			enginetrace->ClipRayToEntity( ray, MASK_SHOT, pEntity, &tr );
		}
	}

	// If we found the surface, emit debris flecks
	bool bReportRagdollImpacts = (nFlags & IMPACT_REPORT_RAGDOLL_IMPACTS) != 0;
	if ( ( tr.fraction == 1.0f ) || ( bHitRagdoll && !bReportRagdollImpacts ) )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
char const *GetImpactDecal( C_BaseEntity *pEntity, int iMaterial, int iDamageType )
{
	char const *decalName;
	if ( !pEntity )
	{
		decalName = "Impact.Concrete";
	}
	else
	{
		decalName = pEntity->DamageDecal( iDamageType, iMaterial );
	}

	// See if we need to offset the decal for material type
	return decalsystem->TranslateDecalForGameMaterial( decalName, iMaterial );
}

//-----------------------------------------------------------------------------
// Purpose: Perform custom effects based on the Decal index
//-----------------------------------------------------------------------------

struct ImpactEffect_t
{
	const char *m_pName;
	const char *m_pNameNoFlecks;
};

static ImpactEffect_t s_pImpactEffect[26] = 
{
	{ "impact_antlion",		NULL },							// CHAR_TEX_ANTLION
	{ NULL,					NULL },							// CHAR_TEX_BLOODYFLESH	
	{ "impact_concrete",	"impact_concrete_cheap" },		// CHAR_TEX_CONCRETE		
	{ "impact_dirt",		"impact_dirt_cheap" },			// CHAR_TEX_DIRT			
	{ NULL,					NULL },							// CHAR_TEX_EGGSHELL		
	{ NULL,					NULL },							// CHAR_TEX_FLESH			
	{ "impact_metal",		"impact_metal_cheap" },			// CHAR_TEX_GRATE			
	{ "blood_impact_yellow_01",	NULL },							// CHAR_TEX_ALIENFLESH		
	{ NULL,					NULL },							// CHAR_TEX_CLIP			
	{ "impact_grass",		"impact_grass_cheap" },			// CHAR_TEX_GRASS		
	{ "impact_mud",			"impact_mud_cheap" },			// CHAR_TEX_MUD		
	{ "impact_plastic",		"impact_plastic_cheap" },		// CHAR_TEX_PLASTIC		
	{ "impact_metal",		"impact_metal_cheap" },			// CHAR_TEX_METAL			
	{ "impact_dirt",		"impact_sand_cheap" },			// CHAR_TEX_SAND			
	{ "impact_leaves",		"impact_leaves_cheap" },		// CHAR_TEX_LEAVES		
	{ "impact_computer",	"impact_computer_cheap" },		// CHAR_TEX_COMPUTER		
	{ "impact_asphalt",		"impact_asphalt_cheap" },		// CHAR_TEX_ASPHALT		
	{ "impact_brick",		"impact_brick_cheap" },			// CHAR_TEX_BRICK		
	{ "impact_wet",			"impact_wet_cheap" },			// CHAR_TEX_SLOSH			
	{ "impact_concrete",	"impact_concrete_noflecks" },	// CHAR_TEX_TILE	//{ "impact_tile",		"impact_tile_cheap" },			// CHAR_TEX_TILE			
	{ "impact_cardboard",	"impact_cardboard_cheap" },		// CHAR_TEX_CARDBOARD		
	{ "impact_metal",		"impact_metal_cheap" },			// CHAR_TEX_VENT			
	{ "impact_wood",		"impact_wood_cheap" },			// CHAR_TEX_WOOD			
	{ NULL,					NULL },							// CHAR_TEX_FAKE		
	{ "impact_glass",		"impact_glass_cheap" },			// CHAR_TEX_GLASS			
	{ "warp_shield_impact", NULL },							// CHAR_TEX_WARPSHIELD	
};

static ImpactEffect_t s_pImpactEffect2[11] = 
{
	{ "impact_clay",		"impact_clay_cheap" },			// CHAR_TEX_CLAY
	{ "impact_plaster",		"impact_plaster_cheap" },		// CHAR_TEX_PLASTER	
	{ "impact_rock",		"impact_rock_cheap" },			// CHAR_TEX_ROCK		
	{ "impact_rubber",		"impact_rubber_cheap" },		// CHAR_TEX_RUBBER			
	{ "impact_sheetrock",	"impact_sheetrock_cheap" },		// CHAR_TEX_SHEETROCK		
	{ "impact_cloth",		"impact_cloth_cheap" },			// CHAR_TEX_CLOTH			
	{ "impact_carpet",		"impact_carpet_cheap" },		// CHAR_TEX_CARPET			
	{ "impact_paper",		"impact_paper_cheap" },			// CHAR_TEX_PAPER		
	{ "impact_upholstery",	"impact_upholstery_cheap" },	// CHAR_TEX_UPHOLSTERY				
	{ "impact_puddle",		"impact_puddle_cheap" },		// CHAR_TEX_PUDDLE
	{ "impact_metal",		"impact_metal_cheap" },			// CHAR_TEX_STEAM_PIPE
};

static int s_pImpactEffectIndex[ ARRAYSIZE( s_pImpactEffect ) ][2];
static int s_pImpactEffect2Index[ ARRAYSIZE( s_pImpactEffect2 ) ][2];

PRECACHE_REGISTER_BEGIN( GLOBAL, PrecacheImpacts )
	for ( int i = 0; i < ARRAYSIZE( s_pImpactEffect ); ++i )
	{
		if ( s_pImpactEffect[i].m_pName )
		{
			PRECACHE_INDEX( PARTICLE_SYSTEM, s_pImpactEffect[i].m_pName, s_pImpactEffectIndex[i][0] );
		}
		if ( s_pImpactEffect[i].m_pNameNoFlecks )
		{
			PRECACHE_INDEX( PARTICLE_SYSTEM, s_pImpactEffect[i].m_pNameNoFlecks, s_pImpactEffectIndex[i][1] );
		}
	}

	for ( int i = 0; i < ARRAYSIZE( s_pImpactEffect2 ); ++i )
	{
		if ( s_pImpactEffect2[i].m_pName )
		{
			PRECACHE_INDEX( PARTICLE_SYSTEM, s_pImpactEffect2[i].m_pName, s_pImpactEffect2Index[i][0] );
		}
		if ( s_pImpactEffect2[i].m_pNameNoFlecks )
		{
			PRECACHE_INDEX( PARTICLE_SYSTEM, s_pImpactEffect2[i].m_pNameNoFlecks, s_pImpactEffect2Index[i][1] );
		}
	}
PRECACHE_REGISTER_END()

static void SetImpactControlPoint( CNewParticleEffect *pEffect, int nPoint, const Vector &vecImpactPoint, const Vector &vecForward, C_BaseEntity *pEntity )
{
	Vector vecImpactY, vecImpactZ;
	VectorVectors( vecForward, vecImpactY, vecImpactZ ); 
	vecImpactY *= -1.0f;

	pEffect->SetControlPoint( nPoint, vecImpactPoint );

	if ( r_impacts_alt_orientation.GetBool() )
		pEffect->SetControlPointOrientation( nPoint, vecImpactZ, vecImpactY, vecForward );
	else
		pEffect->SetControlPointOrientation( nPoint, vecForward, vecImpactY, vecImpactZ );
	pEffect->SetControlPointEntity( nPoint, pEntity );
}

static void PerformNewCustomEffects( const Vector &vecOrigin, trace_t &tr, const Vector &shotDir, int iMaterial, int iScale, int nFlags )
{
	bool bNoFlecks = !r_drawflecks.GetBool();
	if ( !bNoFlecks )
	{
		bNoFlecks = ( ( nFlags & FLAGS_CUSTIOM_EFFECTS_NOFLECKS ) != 0  );
	}

	// Compute the impact effect name
	ImpactEffect_t *pEffectList;
	int *pEffectIndex;
	int nOffset;
	if ( iMaterial >= 1 && iMaterial <= 11 )
	{
		pEffectList = s_pImpactEffect2;
		nOffset = 1;
		pEffectIndex = s_pImpactEffect2Index[iMaterial - nOffset];
	}
	else
	{
		pEffectList = s_pImpactEffect;
		nOffset = 'A';
		pEffectIndex = s_pImpactEffectIndex[iMaterial - nOffset];
	}

	const ImpactEffect_t &effect = pEffectList[ iMaterial - nOffset ];

	const char *pImpactName = effect.m_pName;
	int nEffectIndex = pEffectIndex[0];
	if ( bNoFlecks && effect.m_pNameNoFlecks )
	{
		pImpactName = effect.m_pNameNoFlecks;
		nEffectIndex = pEffectIndex[1];
	}
	if ( !pImpactName )
		return;

	Vector	vecReflect;
	float	flDot = DotProduct( shotDir, tr.plane.normal );
	VectorMA( shotDir, -2.0f * flDot, tr.plane.normal, vecReflect );
		
	Vector vecShotBackward;
	VectorMultiply( shotDir, -1.0f, vecShotBackward );
		
	Vector vecImpactPoint = ( tr.fraction != 1.0f ) ? tr.endpos : vecOrigin;
	AssertMsg( VectorsAreEqual( vecOrigin, tr.endpos, 1e-1 ), "Impact decal drawn too far from the surface impacted." );

#ifdef INFESTED_DLL	// Randomly spawn a persistent jet of steam
	if ( iMaterial == CHAR_TEX_STEAM_PIPE )
	{
		FX_ASW_Potential_Burst_Pipe( vecImpactPoint, vecReflect, vecShotBackward, tr.plane.normal );
	}
#endif

	CSmartPtr<CNewParticleEffect> pEffect = CNewParticleEffect::CreateOrAggregatePrecached( NULL, nEffectIndex, vecImpactPoint );
	if ( !pEffect->IsValid() )
		return;

	SetImpactControlPoint( pEffect.GetObject(), 0, vecImpactPoint, tr.plane.normal, tr.m_pEnt ); 
	SetImpactControlPoint( pEffect.GetObject(), 1, vecImpactPoint, vecReflect,		tr.m_pEnt ); 
	SetImpactControlPoint( pEffect.GetObject(), 2, vecImpactPoint, vecShotBackward,	tr.m_pEnt ); 
	pEffect->SetControlPoint( 3, Vector( iScale, iScale, iScale ) );
	if ( pEffect->m_pDef->ReadsControlPoint( 4 ) )
	{
		Vector vecColor;
		GetColorForSurface( &tr, &vecColor );
		pEffect->SetControlPoint( 4, vecColor );
	}
}

void PerformCustomEffects( const Vector &vecOrigin, trace_t &tr, const Vector &shotDir, int iMaterial, int iScale, int nFlags )
{
	// Throw out the effect if any of these are true
	const int noEffectsFlags = (SURF_SKY|SURF_NODRAW|SURF_HINT|SURF_SKIP);
	if ( tr.surface.flags & noEffectsFlags )
		return;

	if ( iMaterial == CHAR_TEX_ANTLION )
	{
		FX_AntlionImpact( vecOrigin, &tr );
	}
	else if ( iMaterial == CHAR_TEX_WARPSHIELD )
	{
		QAngle vecAngles;
		VectorAngles( -shotDir, vecAngles );
		DispatchParticleEffect( "warp_shield_impact", vecOrigin, vecAngles );
	}
	else {
		PerformNewCustomEffects( vecOrigin, tr, shotDir, iMaterial, iScale, nFlags );
	
		if ( ( iMaterial == CHAR_TEX_METAL ) || ( iMaterial == CHAR_TEX_VENT ) )
		{
			Vector	reflect;
			float	dot = shotDir.Dot( tr.plane.normal );
			reflect = shotDir + ( tr.plane.normal * ( dot*-2.0f ) );
			
			if( random->RandomInt(0,2)==0 )
			{
				//Dynamic light
				dlight_t *dl = effects->CL_AllocDlight ( 0 );

				VectorCopy (vecOrigin, dl->origin);

				dl->origin = vecOrigin;
				dl->radius = random->RandomInt( 24, 24 ); // radius of flash
				dl->decay = dl->radius / 0.05f;  // original radius is 0.05f; **needed distance from a wall**
				dl->die = gpGlobals->curtime + 0.05f;  // FIX ME: time causes somewhat weird lighting please adjust
				dl->color.r = 147;
				dl->color.g = 118;
				dl->color.b = 66;
				dl->color.exponent = 5;
			}
	
			reflect[0] += random->RandomFloat( -0.2f, 0.2f );
			reflect[1] += random->RandomFloat( -0.2f, 0.2f );
			reflect[2] += random->RandomFloat( -0.2f, 0.2f );

			if ( random->RandomInt(0,2)==0 )
				FX_MetalSpark( vecOrigin, reflect, tr.plane.normal, iScale );

			reflect[0] += random->RandomFloat( -0.8f, 0.8f );
			reflect[1] += random->RandomFloat( -0.8f, 0.8f );
			reflect[2] += random->RandomFloat( -0.8f, 0.8f );

			if ( random->RandomInt(0,4)==0 )
				FX_MetalSpark( vecOrigin, reflect, tr.plane.normal, iScale );

			reflect[0] += random->RandomFloat( -1.6f, 1.6f );
			reflect[1] += random->RandomFloat( -1.6f, 1.6f );
			reflect[2] += random->RandomFloat( -1.6f, 1.6f );

			if ( random->RandomInt(0,8)==0 )
				FX_MetalSpark( vecOrigin, reflect, tr.plane.normal, iScale );

			Vector	origin=vecOrigin;

			QAngle vecAngles;
			VectorAngles( -shotDir, vecAngles );
			/*
			if ( random->RandomInt(0,20)==0 )
			{
				DispatchParticleEffect( "metal_impact_bullet", vecOrigin, vecAngles );
			}
			if ( random->RandomInt(0,30)==0 )
			{
				DispatchParticleEffect( "metal_spark_shower", vecOrigin, vecAngles );
			}*/
			//FX_ConcussiveExplosion ( origin, reflect ); Tres Rare : Grosse impact plein de spark
			//FX_MetalScrape( origin, reflect ); Rare Gicl� d'�tincelle fine et longue
			//FX_EnergySplash( vecOrigin, tr.plane.normal ); Jamais Bouclier Combine
			//FX_MicroExplosion ( origin, reflect ); Jamais Comme Bouclier Combine mais jaune et moche
			//FX_Explosion ( origin, reflect, CHAR_TEX_METAL ); Jamais Large Sparks with Large smoke
			if ( random->RandomInt(0,30)==0 )
				FX_MetalScrape( origin, reflect );

			if( random->RandomInt(0,20)==0 )
			{
				Vector	offset = vecOrigin + ( tr.plane.normal * 1.0f );
				g_pEffects->Sparks( offset );
			}
		}
		else if ( iMaterial == CHAR_TEX_COMPUTER )
		{
			Vector	offset = vecOrigin + ( tr.plane.normal * 1.0f );

			g_pEffects->Sparks( offset );

			Vector	reflect;

			reflect[0] += random->RandomFloat( -1.0f, 1.0f );
			reflect[1] += random->RandomFloat( -1.0f, 1.0f );
			reflect[2] += random->RandomFloat( -1.0f, 1.0f );

			if ( random->RandomInt(0,3)==0 )
				FX_MetalSpark( vecOrigin, reflect, tr.plane.normal, iScale );
		}
		else if ( iMaterial == CHAR_TEX_WARPSHIELD )
		{
			QAngle vecAngles;
			VectorAngles( -shotDir, vecAngles );
			DispatchParticleEffect( "warp_shield_impact", vecOrigin, vecAngles );
		}
		else if ( ( iMaterial == CHAR_TEX_FLESH ) || ( iMaterial == CHAR_TEX_BLOODYFLESH ) )
		{
			Vector	reflect;
			float	dot = shotDir.Dot( tr.plane.normal );
			reflect = shotDir + ( tr.plane.normal * ( dot*-2.0f ) );
			Vector	offset = vecOrigin + ( tr.plane.normal * 1.0f );

			QAngle vecAngles;
			VectorAngles( shotDir, vecAngles ); //ShotDir invert
			DispatchParticleEffect( "blood_impact_red_dead", vecOrigin, vecAngles );

			UTIL_TraceLine ( offset, offset + reflect * 64,  MASK_SOLID_BRUSHONLY, null, COLLISION_GROUP_NONE, &tr);

			if ( random->RandomInt(0,1)==0 )
				UTIL_BloodDecalTrace( &tr, BLOOD_COLOR_RED );
		}
		else if ( ( iMaterial == CHAR_TEX_CONCRETE ) || ( iMaterial == CHAR_TEX_TILE ) )
		{
			if( random->RandomInt(0,15)==0 )
			{
				Vector	reflect;

				reflect[0] += random->RandomFloat( -1.6f, 1.6f );
				reflect[1] += random->RandomFloat( -1.6f, 1.6f );
				reflect[2] += random->RandomFloat( -1.6f, 1.6f );

				FX_MetalSpark( vecOrigin, reflect, tr.plane.normal, iScale );
			}
		}

	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a sound for an impact. If tr contains a valid hit, use that. 
//			If not, use the passed in origin & surface.
//-----------------------------------------------------------------------------
void PlayImpactSound( CBaseEntity *pEntity, trace_t &tr, Vector &vecServerOrigin, int nServerSurfaceProp )
{
	VPROF( "PlayImpactSound" );
	surfacedata_t *pdata;
	Vector vecOrigin;

	// If the client-side trace hit a different entity than the server, or
	// the server didn't specify a surfaceprop, then use the client-side trace 
	// material if it's valid.
	if ( tr.DidHit() && (pEntity != tr.m_pEnt || nServerSurfaceProp == 0) )
	{
		nServerSurfaceProp = tr.surface.surfaceProps;
	}
	pdata = physprops->GetSurfaceData( nServerSurfaceProp );
	if ( tr.fraction < 1.0 )
	{
		vecOrigin = tr.endpos;
	}
	else
	{
		vecOrigin = vecServerOrigin;
	}

	// Now play the esound
	if ( pdata->sounds.bulletImpact )
	{
		const char *pbulletImpactSoundName = physprops->GetString( pdata->sounds.bulletImpact );
		
		if ( g_pImpactSoundRouteFn )
		{
			g_pImpactSoundRouteFn( pbulletImpactSoundName, vecOrigin );
		}
		else
		{
			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, NULL, pbulletImpactSoundName, pdata->soundhandles.bulletImpact, &vecOrigin );
		}

		return;
	}

#ifdef _DEBUG
	Msg("***ERROR: PlayImpactSound() on a surface with 0 bulletImpactCount!\n");
#endif //_DEBUG
}


void SetImpactSoundRoute( ImpactSoundRouteFn fn )
{
	g_pImpactSoundRouteFn = fn;
}


//-----------------------------------------------------------------------------
// Purpose: Pull the impact data out
// Input  : &data - 
//			*vecOrigin - 
//			*vecAngles - 
//			*iMaterial - 
//			*iDamageType - 
//			*iHitbox - 
//			*iEntIndex - 
//-----------------------------------------------------------------------------
C_BaseEntity *ParseImpactData( const CEffectData &data, Vector *vecOrigin, Vector *vecStart, 
	Vector *vecShotDir, short &nSurfaceProp, int &iMaterial, int &iDamageType, int &iHitbox )
{
	C_BaseEntity *pEntity = data.GetEntity( );
	*vecOrigin = data.m_vOrigin;
	*vecStart = data.m_vStart;
	nSurfaceProp = data.m_nSurfaceProp;
	iDamageType = data.m_nDamageType;
	iHitbox = data.m_nHitBox;

	*vecShotDir = (*vecOrigin - *vecStart);
	VectorNormalize( *vecShotDir );

	// Get the material from the surfaceprop
	surfacedata_t *psurfaceData = physprops->GetSurfaceData( data.m_nSurfaceProp );
	iMaterial = psurfaceData->game.material;

	return pEntity;
}

