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
#include "ServerNetTypes.h"


#ifndef __SNET_SERVICE_DISCOVERY__
#define __SNET_SERVICE_DISCOVERY__


/* Implementation note(s):
 * 
 *	- No network security (vulnerable to flooding attacks, answer to anyone).
 *
 * Bonjour notes:
 *
 *	- Use <dns-sd service name>.<protocol> for service names (example: "_http._tcp").
 *
 *	- VValueBag must be encodable in a DNS TXT resource record:
 *
 *		+ A "key=value" attribute string should be less than 255 characters;
 *		+ Key must be all ASCII characters (case should be ignored);
 *		+ Everything must fit (along other resource record) in a signe UDP packet.
 *
 *	- VServiceDiscoveryClient is unable to send additional querie(s) for missing 
 *	  information.
 *
 *  - For HTTP use the following keys in VValueBag:
 *
 *		+ "u" for username;
 *		+ "p" for password;
 *		+ "path" for path to document.
 *
 * Links:
 *
 *	- http://www.dns-sd.org/
 *	- http://developer.apple.com/opensource/
 *	- http://files.dns-sd.org/draft-cheshire-dnsext-dns-sd.txt (Specification)
 */ 


#include <list>
#include <map>
#include <vector>

#include "ServerNetTypes.h"


BEGIN_TOOLBOX_NAMESPACE


class VUDPEndPoint;


class XTOOLBOX_API VServiceRecord : public VObject
{
public:
		
	VString	fServiceName;		// Service name (ex: "_http._tcp").
	VString	fProviderName;		// Name of service provider (ex: "MyWakandaSolution").
	VString	fHostName;			// Host name (ex: "PC-4DEng.local").
	uLONG			fAddress;			// IPv4 address.
	PortNumber	fPort;			// Port.			
	VValueBag	fValueBag;			// Used to store any additional information.
	
	// Retrieve hostname of machine and set it to fHostName, add ".local" suffix if needed.
	
	void	SetHostName ();
};


// Singleton instance.

class XTOOLBOX_API VServiceDiscoveryServer : public VTask
{
public:

	static VServiceDiscoveryServer	*GetInstance ();
		
	// Duplicates (same service and provider names) are forbidden.
	
	VError					AddServiceRecord (const VServiceRecord &inRecord);	
	
	// Remove a service record.
	
	VError					RemoveServiceRecord (const VString &inServiceName, const VString &inProviderName);
	
	// Network resources (socket) are not allocated until call to Start().
	
	VError					Start ();

	// Send a "broadcast" packet on Bonjour's multicast address. Clients maybe listening without explicitely send queries.
	// Server must be running.

	VError					PublishServiceRecord (const VString &inServiceName, const VString &inProviderName);

	// Kill server's VTask and wait until it actually terminates. Use VTask's Kill() method to just request termination.
	
	void							KillAndWaitTermination ();
	
private:
	
	// In milliseconds.
	
	static const uLONG	kTimeOut		= 500;	
	static const uLONG	kMaximumWait	= 5000;	
			
	static VServiceDiscoveryServer	*sSingletonInstance;
	static VCriticalSection	sCriticalSection;
	
	VUDPEndPoint					*fUDPEndPoint;
	std::list<VServiceRecord>		fServiceRecords;
		
	VServiceDiscoveryServer ();
	virtual ~VServiceDiscoveryServer ();

	virtual void	DoOnRefCountZero ();	
	virtual Boolean	DoRun ();
	
	VError	_HandlePacket (uBYTE *inPacket, uLONG inPacketSize, uLONG inBufferSize);	
	VError	_AnswerQuery (uBYTE *ioPacket, uLONG inBufferSize, uLONG inOffset, const VString &inQueriedService);
	VError	_SendServiceList (uBYTE *ioPacket, uLONG inBufferSize, uLONG inOffset);
	VError	_SendPTRAnswer (uBYTE *ioPacket, uLONG inBufferSize, uLONG inOffset, const VServiceRecord &inServiceRecord);
	VError	_SendAAnswer (uBYTE *ioPacket, uLONG inBufferSize, uLONG inOffset, const VString &inHostName, uLONG inIPv4);	
};


// Several instances can be created and used, but this is not efficient.

class XTOOLBOX_API VServiceDiscoveryClient : public VObject 
{
public:
	
	VServiceDiscoveryClient ();
	virtual ~VServiceDiscoveryClient ();
	
	// Reset networking.
	
	VError	Reset ();
	
	// Send request(s) to discover services with given name(s). Service requests are sent individually.
	// Last error is returned.

	VError	SendQuery (const VString &inServiceName);
	VError	SendQuery (const std::vector<VString> &inServiceNames, uLONG *outNumberRequestSent = NULL);
		
	// Set inAcceptBroadcast to true for accepting service records information not from an explicit query.
	
	VError	ReceiveServiceRecords (std::vector<VServiceRecord> *outServiceRecords, 
										   const std::vector<VString> &inServiceNames, 
										   bool inAcceptBroadcast = false);
	
private:
	
	static const uLONG	kMaximumNumberReceived	= 100;	// Maximum number of received packets per ReceiveServiceRecords() call.
	
	struct PendingRecord {
		
		VServiceRecord	fServiceRecord;			
		bool			fHasSRV, fHasA;	
		VString	fTarget;
		
	};	
	
	uLONG			fIPv4;
	PortNumber		fPort;
	uWORD			fIdentifier;
	VUDPEndPoint	*fUDPEndPoint;
	
	VError	_EncodePTRQuery (uBYTE *outBuffer, uLONG *ioSize, const VString &inServiceName, uWORD inIdentifier = 0);
	VError	_ParsePacket (const uBYTE *inPacket, uLONG inPacketSize, 
								  std::vector<VServiceRecord> *outServiceRecords, 
								  const std::vector<VString> &inServiceNames, 
								  sLONG inIdentifier = -1);
};


END_TOOLBOX_NAMESPACE


#endif
