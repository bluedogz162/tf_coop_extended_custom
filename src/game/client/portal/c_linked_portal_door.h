#ifndef _C_LINKED_PORTAL_DOOR_H_
#define _C_LINKED_PORTAL_DOOR_H_

#pragma once

#include "c_prop_portal.h"

class C_LinkedPortalDoor : public C_Prop_Portal
{
	public:
		DECLARE_CLASS(C_LinkedPortalDoor, C_Prop_Portal);
		DECLARE_CLIENTCLASS();
		DECLARE_DATADESC();

		C_LinkedPortalDoor();
		virtual ~C_LinkedPortalDoor() OVERRIDE;

		virtual void DrawSimplePortalMesh(const IMaterial *pMaterialOverride = NULL, float fForwardOffsetModifier = 0.25f) OVERRIDE;

	private:
		
};

#endif