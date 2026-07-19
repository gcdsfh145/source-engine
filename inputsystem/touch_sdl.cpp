//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linux/Android touch implementation for inputsystem
//
//===========================================================================//

/* For force feedback testing. */
#include "inputsystem.h"
#include "tier1/convar.h"
#include "tier0/icommandline.h"
#include "SDL.h"
#include "SDL_touch.h"
// NOTE: This has to be the last file included!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Handle the events coming from the Touch SDL subsystem.
//-----------------------------------------------------------------------------
int TouchSDLWatcher( void *userInfo, SDL_Event *event )
{
	CInputSystem *pInputSystem = (CInputSystem *)userInfo;

	if( !event || !pInputSystem ) return 1;

	switch ( event->type ) {
	case SDL_FINGERDOWN:
		pInputSystem->FingerEvent( IE_FingerDown, event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	case SDL_FINGERUP:
		pInputSystem->FingerEvent( IE_FingerUp, event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	case SDL_FINGERMOTION:
		pInputSystem->FingerEvent( IE_FingerMotion ,event->tfinger.fingerId, event->tfinger.x, event->tfinger.y, event->tfinger.dx, event->tfinger.dy );
		break;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Initialize all joysticks
//-----------------------------------------------------------------------------
void CInputSystem::InitializeTouch( void )
{
	if ( m_bTouchInitialized )
		ShutdownTouch();

	// abort startup if user requests no touch
	if ( CommandLine()->FindParm("-notouch") ) return;

	memset( m_touchAccumX, 0, sizeof(m_touchAccumX) );
	memset( m_touchAccumY, 0, sizeof(m_touchAccumY) );
	for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
		m_touchFingerIds[i] = -1;

	m_bTouchInitialized = true;
	SDL_AddEventWatch(TouchSDLWatcher, this);
}

void CInputSystem::ShutdownTouch()
{
	if ( !m_bTouchInitialized )
		return;

	SDL_DelEventWatch( TouchSDLWatcher, this );
	for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
		m_touchFingerIds[i] = -1;
	m_bTouchInitialized = false;
}

bool CInputSystem::GetTouchAccumulators( int fingerId, float &dx, float &dy )
{
	if ( fingerId < 0 || fingerId >= TOUCH_FINGER_MAX_COUNT )
	{
		dx = dy = 0.f;
		return false;
	}

	dx = m_touchAccumX[fingerId];
	dy = m_touchAccumY[fingerId];

	m_touchAccumX[fingerId] = m_touchAccumY[fingerId] = 0.f;

	return true;
}

void CInputSystem::FingerEvent(int eventType, int64 fingerId, float x, float y, float dx, float dy)
{
	const int64 sdlFingerId = fingerId;
	int mappedFinger = -1;
	for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
	{
		if ( m_touchFingerIds[i] == sdlFingerId )
		{
			mappedFinger = i;
			break;
		}
	}

	if ( mappedFinger < 0 && eventType == IE_FingerDown )
	{
		for ( int i = 0; i < TOUCH_FINGER_MAX_COUNT; ++i )
		{
			if ( m_touchFingerIds[i] == -1 )
			{
				m_touchFingerIds[i] = sdlFingerId;
				mappedFinger = i;
				break;
			}
		}
	}

	if ( mappedFinger < 0 )
		return;

	if( eventType == IE_FingerUp )
	{
		m_touchAccumX[mappedFinger] = 0.f;
		m_touchAccumY[mappedFinger] = 0.f;
	}
	else
	{
		m_touchAccumX[mappedFinger] += dx;
		m_touchAccumY[mappedFinger] += dy;
	}

	int _x,_y;
	memcpy( &_x, &x, sizeof(float) );
	memcpy( &_y, &y, sizeof(float) );
	PostEvent(eventType, m_nLastSampleTick, mappedFinger, _x, _y);

	if ( eventType == IE_FingerUp )
		m_touchFingerIds[mappedFinger] = -1;
}
