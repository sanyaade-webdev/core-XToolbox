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
#ifndef __VKernelIPCErrors__
#define __VKernelIPCErrors__


#include "Kernel/VKernel.h"



BEGIN_TOOLBOX_NAMESPACE



// Component Errors: 1000->1099
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1000, VE_COMP_UNITIALISED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1001, VE_COMP_LIBRARY_NOT_FOUND);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1002, VE_COMP_ALLREADY_REGISTRED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1003, VE_COMP_NOT_REGISTRED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1004, VE_COMP_BAD_LIBRARY_TYPE);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1005, VE_COMP_PROC_PTR_NULL);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1006, VE_COMP_INVALID_DESTINATOR);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1007, VE_COMP_UNRESOLVED_DEPENDENCIES);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1008, VE_COMP_CANNOT_LOAD_LIBRARY);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1009, VE_COMP_CANNOT_EXPORT_LIBRARY);

DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1010, VE_LIBRARY_CANNOT_LOAD);

DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1120, VE_SHM_INIT_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1121, VE_SHM_ATTACH_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1122, VE_SHM_DETACH_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1123, VE_SHM_REMOVE_FAILED);

DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1130, VE_SEM_INIT_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1131, VE_SEM_LOCK_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1132, VE_SEM_UNLOCK_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1133, VE_SEM_REMOVE_FAILED);

DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1140, VE_START_WATCHING_FOLDER_FAILED);
DECLARE_VERROR( kCOMPONENT_XTOOLBOX, 1141, VE_STOP_WATCHING_FOLDER_FAILED);


END_TOOLBOX_NAMESPACE

#endif


