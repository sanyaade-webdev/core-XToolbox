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
#ifndef __VGraphicsErrors__
#define __VGraphicsErrors__


#include "KernelIPC/VKernelIPC.h"

BEGIN_TOOLBOX_NAMESPACE

// Graphic context errors: 2000->2099
const VError VE_NOT_AVAILABLE = MAKE_VERROR(kCOMPONENT_XTOOLBOX, 2000);

END_TOOLBOX_NAMESPACE

#endif
