/*
* This file is part of Wakanda software, licensed by 4D under
*  (i) the GNU General Public License version 3 (GNU GPL v3), or
*  (ii) the Affero General Public License version 3 (AGPL v3) or
*  (iii) a commercial license.
* This file remains the exclusive property of 4D and/or its licensors
* and is protected by national and international legislations.
* In any event, Licensee's compliance with the terms and conditions
* of the applicable license constitutes a prerequisite to any use of this file.
* Except as otherwise expressly stated in the applicable license,
* such license does not include any other license or rights on this file,
* 4D's and/or its licensors' trademarks and/or other proprietary rights.
* Consequently, no title, copyright or other proprietary rights
* other than those specified in the applicable license is granted.
*/
#ifndef __VFontMgr__
#define __VFontMgr__

#include "KernelIPC/VKernelIPC.h"
#include "Graphics/Sources/VGraphicsTypes.h"

// Native declarations
#if VERSIONWIN
#include "Graphics/Sources/XWinFontMgr.h"
#elif VERSIONMAC
#include "Graphics/Sources/XMacFontMgr.h"
#endif

BEGIN_TOOLBOX_NAMESPACE

// Needed declarations
class VFont;



class XTOOLBOX_API VFontMgr : public VObject
{
public:
			VFontMgr ();
	virtual	~VFontMgr ();
	
	xFontnameVector*	GetFontNameList (bool inWithScreenFonts = true);
	
	VFont*	RetainStdFont (StdFont inFont);
	VFont*	RetainFont (const VString& inFontFamilyName, const VFontFace& inStyle, GReal inSize, const GReal inDPI = 0, bool inReturnNULLIfNotExist = false);
	
private:
	mutable VCriticalSection	fCriticalSection;
	VArrayRetainedPtrOf<VFont*>	fFonts;
	
	XFontMgrImpl	fFontMgr;
};


END_TOOLBOX_NAMESPACE

#endif
