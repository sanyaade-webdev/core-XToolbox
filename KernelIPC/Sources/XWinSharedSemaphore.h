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
#ifndef __XWinSharedSemaphore__
#define __XWinSharedSemaphore__

#include "Kernel/VKernel.h"

BEGIN_TOOLBOX_NAMESPACE


class XTOOLBOX_API XWinSharedSemaphore : public VObject
{
public:
  // Constructor
  XWinSharedSemaphore(uLONG inName);
  
  // Destructor
  ~XWinSharedSemaphore();
  
  inline bool IsValid();
  
  bool Lock(sLONG inTimeoutMilliseconds);
  
  bool Unlock();
  
private:
  HANDLE fSem;
};

typedef XWinSharedSemaphore XSharedSemaphoreImpl;

END_TOOLBOX_NAMESPACE

#endif
