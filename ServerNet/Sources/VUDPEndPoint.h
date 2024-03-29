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
#ifndef __SNET_UDP_ENDPOINT__
#define __SNET_UDP_ENDPOINT__


#if VERSIONWIN
	#include "XWinSocket.h"
#else
	#include "XBsdSocket.h"
#endif


BEGIN_TOOLBOX_NAMESPACE


class XTOOLBOX_API VUDPEndPoint : public VObject
{
public :

	virtual ~VUDPEndPoint ();
	  
	void SetIsBlocking ( bool inIsBlocking );
	
	virtual VError Read(void* outBuffer, uLONG* ioLength, XNetAddr* outSenderInfo);	
	
	virtual VError WriteExactly (void* inBuffer, uLONG inLength /*, const XNetAddr* inInfo*/);
	
	virtual VError Close();

	virtual VError SetDestination(const XNetAddr& inReceiverInfo);

	
private:
	
	friend class VUDPEndPointFactory;
	
	VUDPEndPoint (XUDPSock* inSock);

	XUDPSock* fSock;

	XNetAddr fTmpPeer;
	bool fHaveTmpPeer;
	
	XNetAddr fMCastAddr;
	bool fIsMCast;
	
	XNetAddr fDestination;	
};


class XTOOLBOX_API VUDPEndPointFactory : public VObject
{
public:

	static VUDPEndPoint* CreateMulticastEndPoint(uLONG inMulticastIPv4, PortNumber inPort);

	static VUDPEndPoint* CreateMulticastEndPoint(uLONG inLocalIpv4, uLONG inMulticastIPv4, PortNumber inPort);
};	


END_TOOLBOX_NAMESPACE


#endif
