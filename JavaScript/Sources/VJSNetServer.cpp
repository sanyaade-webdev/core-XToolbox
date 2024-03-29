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
#include "VJavaScriptPrecompiled.h"

#include "VJSNet.h"
#include "VJSNetServer.h"
#include "VJSNetSocket.h"

#include "VJSContext.h"
#include "VJSGlobalClass.h"

#include "VJSEvent.h"
#include "VJSEventEmitter.h"

USING_TOOLBOX_NAMESPACE

VJSNetServerObject::VJSNetServerObject (bool inIsSynchronous, bool inAllowHalfOpen)
{
	fIsSynchronous = inIsSynchronous;
	fAllowHalfOpen = inAllowHalfOpen;
	fIsSSL = false;
	fConnectionListener = NULL;
	fSockListener = NULL;	
}

VJSNetServerObject::~VJSNetServerObject ()
{
	xbox_assert(fConnectionListener == NULL && fSockListener == NULL);
}

void VJSNetServerObject::SetKeyAndCertificate (const XBOX::VString &inKey, const XBOX::VString &inCertificate)
{
	fIsSSL = true;
	fKey = inKey;
	fCertificate = inCertificate;	
}

void VJSNetServerObject::Close ()
{
	if (fIsSynchronous) {

		xbox_assert(fConnectionListener == NULL);	

		if (fSockListener != NULL) {

			fSockListener->StopListeningAndClearPorts();
			delete fSockListener;
			fSockListener = NULL;

		}

	} else {

		xbox_assert(fSockListener == NULL);

		if (fConnectionListener!= NULL) {

			if (fConnectionListener->IsListening()) 

				fConnectionListener->StopListening();

			ReleaseRefCountable<VTCPConnectionListener>(&fConnectionListener);

		}

	}
}

void VJSNetServerObject::Close (XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	inServer->Close();
}

void VJSNetServerObject::Address(XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	XBOX::VJSObject	object(ioParms.GetContext());
	
	object.MakeEmpty();
	object.SetProperty("address", inServer->fAddress);
	object.SetProperty("port", inServer->fPort);
}

VJSConnectionHandler::VJSConnectionHandler (VJSWorker *inWorker, VJSNetServerObject *inServerObject, bool inIsSSL)
{
	xbox_assert(inWorker != NULL && inServerObject != NULL);

	fWorker = inWorker;
	fServerObject = inServerObject;
	fIsSSL = inIsSSL;
}

XBOX::VError VJSConnectionHandler::SetEndPoint (XBOX::VEndPoint *inEndPoint)
{
	xbox_assert(inEndPoint != NULL);

	inEndPoint->Retain();
	fWorker->QueueEvent(VJSNetEvent::CreateConnection(fServerObject, (XBOX::VTCPEndPoint *) inEndPoint, fIsSSL));
	return XBOX::VE_OK;
}

VJSConnectionHandlerFactory::VJSConnectionHandlerFactory (VJSWorker *inWorker, VJSNetServerObject *inServerObject, bool inIsSSL)
{
	xbox_assert(inWorker != NULL && inServerObject != NULL);

	fPort = -1;
	fWorker = inWorker;
	fServerObject = inServerObject;
	fIsSSL = inIsSSL;
	fConnectionHandler = NULL;

	SetIsSSL(inIsSSL);
}

VJSConnectionHandlerFactory::~VJSConnectionHandlerFactory ()
{
	ReleaseRefCountable<VJSConnectionHandler>(&fConnectionHandler);
}

VConnectionHandler *VJSConnectionHandlerFactory::CreateConnectionHandler (XBOX::VError &outError)
{
	XBOX::StLocker<XBOX::VCriticalSection>	lock(&fMutex);

	if (fConnectionHandler == NULL && (fConnectionHandler = new VJSConnectionHandler(fWorker, fServerObject, fIsSSL)) == NULL) 

		outError = XBOX::VE_MEMORY_FULL;		
	
	else 
		
		outError = XBOX::VE_OK;	

	return fConnectionHandler;
}

XBOX::VError VJSConnectionHandlerFactory::AddNewPort (PortNumber iPort)
{
	xbox_assert(fPort == -1);	// Can only assign once.

	fPort = iPort;
	return XBOX::VE_OK;
}

XBOX::VError VJSConnectionHandlerFactory::GetPorts (std::vector<PortNumber> &oPorts)
{
	if (fPort != -1) 

		oPorts.push_back(fPort);

	return XBOX::VE_OK;
}
	
void VJSNetServerClass::GetDefinition (ClassDefinition &outDefinition)
{
	static inherited::StaticFunction functions[] =
	{
		{ "listen",		js_callStaticFunction<_listen>,						JS4D::PropertyAttributeDontDelete	},
		{ "pause",		js_callStaticFunction<_pause>,						JS4D::PropertyAttributeDontDelete	},		
		{ "close",		js_callStaticFunction<VJSNetServerObject::Close>,	JS4D::PropertyAttributeDontDelete	},
		{ "address",	js_callStaticFunction<VJSNetServerObject::Address>,	JS4D::PropertyAttributeDontDelete	},
		{ 0,			0,													0									},
	};

    outDefinition.className			= "Server";
	outDefinition.staticFunctions	= functions;
	outDefinition.callAsConstructor	= js_callAsConstructor<Construct>;
	outDefinition.initialize		= js_initialize<_Initialize>;
    outDefinition.finalize			= js_finalize<_Finalize>;
}

void VJSNetServerClass::Construct (XBOX::VJSParms_callAsConstructor &ioParms)
{
	VJSNetServerObject	*serverObject;
	
	if ((serverObject = new VJSNetServerObject(false)) != NULL) {

		XBOX::VJSObject	createdObject(ioParms.GetContext());

		createdObject = VJSNetServerClass::CreateInstance(ioParms.GetContext(), serverObject);

		ioParms.ReturnConstructedObject(createdObject);

	} else

		XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
}

void VJSNetServerClass::_Initialize (const XBOX::VJSParms_initialize &inParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	inServer->SetupEventEmitter(inParms);
}

void VJSNetServerClass::_Finalize (const XBOX::VJSParms_finalize &inParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	inServer->Close();
	inServer->Release();
}

void VJSNetServerClass::_listen (XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	XBOX::VString	address;
	sLONG			port;

	if (!ioParms.CountParams()) {

		XBOX::vThrowError(XBOX::VE_JVSC_EXPECTING_PARAMETER, "net.Server.listen()");
		return;

	}

	if (!ioParms.IsNumberParam(1) || !ioParms.GetLongParam(1, &port)) {
		
		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");
		return;

	}

	XBOX::VString	hostname;
	XBOX::VJSObject	callback(ioParms.GetContext());
	bool			hasCallback;

	hostname = "";
	hasCallback = false;
	if (ioParms.CountParams() >= 2) {

		if (ioParms.IsStringParam(2)) {
			
			if (!ioParms.GetStringParam(2, hostname)) {

				XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
				return;

			}

		} else if (ioParms.IsObjectParam(2) && ioParms.GetParamFunc(2, callback)) {

			hasCallback = true;

		} else {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_FUNCTION, "2");
			return;

		}

	} 

	if (ioParms.CountParams() == 3 ) {
	
		if ((hasCallback || !ioParms.IsObjectParam(3) || !ioParms.GetParamFunc(3, callback))) {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_FUNCTION, "3");
			return;

		} else

			hasCallback = true;

	}

	// We are already listening, stop previous listener.

	if (inServer->fConnectionListener != NULL)

		inServer->Close();

	// Set up listener.
	
	if ((inServer->fConnectionListener = new XBOX::VTCPConnectionListener())== NULL) {

		XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
		return;

	} 

	if (inServer->fIsSSL)

		inServer->fConnectionListener->SetSSLKeyAndCertificate(inServer->fCertificate, inServer->fKey);

	VJSNetSocketObject::sMutex.Lock();

	if (VJSNetSocketObject::sSelectIOPool == NULL)

		VJSNetSocketObject::sSelectIOPool = new XBOX::VTCPSelectIOPool();

	VJSNetSocketObject::sMutex.Unlock();

	inServer->fConnectionListener->AddSelectIOPool(VJSNetSocketObject::sSelectIOPool);

	VJSWorker	*worker;

	worker = VJSWorker::GetWorker(ioParms.GetContext());

	VJSConnectionHandlerFactory	*connectionHandlerFactory;
	
	if ((connectionHandlerFactory = new VJSConnectionHandlerFactory(worker, inServer, inServer->fIsSSL)) == NULL) {

		XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
		return;

	}

#if WITH_DEPRECATED_IPV4_API
	
	sLONG	resolvedAddress;

	if (hostname.GetLength()) {

		address = hostname;
		resolvedAddress = XBOX::ServerNetTools::ResolveAddress(hostname);

	} else {

		address = "0.0.0.0";

		std::vector<IP4>	localIPs;

		if (XBOX::ServerNetTools::GetLocalIPv4Addresses(localIPs))

			resolvedAddress = localIPs[0];

		else

			resolvedAddress = 0;	// Should be same as above.

	}

	connectionHandlerFactory->SetIP(resolvedAddress);

#elif DEPRECATED_IPV4_API_SHOULD_NOT_COMPILE

	XBOX::VNetAddrList	addrList;
	bool				isIPv6;

	if (hostname.GetLength()) {

		XBOX::VNetAddress	addr(hostname, port);

		isIPv6 = addr.IsV6();
		addrList.FromDnsQuery(hostname, port);
		
	} else {

		addrList.FromLocalInterfaces();
		isIPv6 = false;

	}

	// Always take first matching type address.

	XBOX::VNetAddrList::const_iterator	it;

	for (it = addrList.begin(); it != addrList.end(); it++)

		if ((*it).IsV6() == isIPv6)

			break;

	address = (*it).GetIP();
		
	connectionHandlerFactory->SetIP(address);

#endif	

	connectionHandlerFactory->AddNewPort(port);

	XBOX::VError	error;

	error = inServer->fConnectionListener->AddConnectionHandlerFactory(connectionHandlerFactory);

	connectionHandlerFactory->Release();	// AddConnectionHandlerFactory() has done a retain().
	if (error != XBOX::VE_OK) {
		
		XBOX::vThrowError(error);
		return;

	} 

	if ((error = inServer->fConnectionListener->StartListening()) != XBOX::VE_OK) {

		XBOX::vThrowError(error);
		return;

	}
	
	inServer->fAddress = address;
	inServer->fPort = port;
		
	if (hasCallback) 
		
		inServer->AddListener("listening", callback, false);
	
	worker->QueueEvent(VJSNetEvent::Create(inServer, "listening"));
}

void VJSNetServerClass::_pause (XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	sLONG	waitingTime;	// In milliseconds.

	if (!ioParms.CountParams())

		waitingTime = 1;

	else if (ioParms.CountParams() != 1 || !ioParms.IsNumberParam(1) || !ioParms.GetLongParam(1, &waitingTime)) {

		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");
		return;

	}

	// Need support from VTCPConnectionListener.

	XBOX::vThrowError(XBOX::VE_JVSC_UNSUPPORTED_FUNCTION, "net.Server.pause()");
}

void VJSNetServerSyncClass::GetDefinition (ClassDefinition &outDefinition)
{
	static inherited::StaticFunction functions[] =
	{
		{ "listen",		js_callStaticFunction<_listen>,						JS4D::PropertyAttributeDontDelete	},
		{ "accept",		js_callStaticFunction<_accept>,						JS4D::PropertyAttributeDontDelete	},
		{ "close",		js_callStaticFunction<VJSNetServerObject::Close>,	JS4D::PropertyAttributeDontDelete	},
		{ "address",	js_callStaticFunction<VJSNetServerObject::Address>,	JS4D::PropertyAttributeDontDelete	},
		{ 0,			0,													0									},
	};

    outDefinition.className			= "ServerSync";
	outDefinition.staticFunctions	= functions;
	outDefinition.callAsConstructor	= js_callAsConstructor<Construct>;
    outDefinition.finalize			= js_finalize<_Finalize>;
}

void VJSNetServerSyncClass::Construct (XBOX::VJSParms_callAsConstructor &ioParms)
{
	VJSNetServerObject	*serverObject;
	
	if ((serverObject = new VJSNetServerObject(true)) != NULL) {

		XBOX::VJSObject	createdObject(ioParms.GetContext());

		createdObject = VJSNetServerSyncClass::CreateInstance(ioParms.GetContext(), serverObject);

		ioParms.ReturnConstructedObject(createdObject);

	} else

		XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
}

void VJSNetServerSyncClass::_Finalize (const XBOX::VJSParms_finalize &inParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	inServer->Close();
	inServer->Release();
}

void VJSNetServerSyncClass::_listen (XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	sLONG	port;

	if (ioParms.CountParams() >= 1) {

		if (!ioParms.IsNumberParam(1) || !ioParms.GetLongParam(1, &port)) {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");
			return;

		}

	} else

		port = 0;		// Will use a random port (useful?).

// Re-use code of async listen().

	uLONG	address;

#if WITH_DEPRECATED_IPV4_API
	if (ioParms.CountParams() >= 2) {

		XBOX::VString	hostname;

		if (!ioParms.IsStringParam(2) || !ioParms.GetStringParam(2, hostname)) {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
			return;

		}

		address = XBOX::ServerNetTools::ResolveAddress(hostname);

	} else

		address = 0;	// Same as "localhost".

#elif DEPRECATED_IPV4_API_SHOULD_NOT_COMPILE
	#error NEED AN IP V6 UPDATE
#endif
	
	// We are already listening, stop previous listener.

	if (inServer->fConnectionListener != NULL)

		inServer->Close();

	// Setup new listener.

	if ((inServer->fSockListener = new XBOX::VSockListener()) == NULL) {

		XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
		return;

	}

	XBOX::VError	error;

	error = XBOX::VE_OK;
	if (inServer->fIsSSL)
		
		error = inServer->fSockListener->SetKeyAndCertificate(inServer->fKey, inServer->fCertificate);

	if (error != XBOX::VE_OK) {

		inServer->Close();
		XBOX::vThrowError(error);

	}
#if WITH_DEPRECATED_IPV4_API
	else if (!inServer->fSockListener->SetBlocking(true)
	|| !inServer->fSockListener->AddListeningPort(address, port, inServer->fIsSSL)	
	|| !inServer->fSockListener->StartListening()) {

		inServer->Close();
		XBOX::vThrowError(XBOX::VE_SRVR_FAILED_TO_CREATE_LISTENING_SOCKET);
		
	}
#elif DEPRECATED_IPV4_API_SHOULD_NOT_COMPILE
	#error NEED AN IP V6 UPDATE
#endif
	else {

		inServer->fAddress = address;	
		inServer->fPort = port;	

	}
}

void VJSNetServerSyncClass::_accept (XBOX::VJSParms_callStaticFunction &ioParms, VJSNetServerObject *inServer)
{
	xbox_assert(inServer != NULL);

	sLONG	timeOut;

	if (ioParms.CountParams() >= 1) {

		if (!ioParms.IsNumberParam(1) || !ioParms.GetLongParam(1, &timeOut)) {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");
			return;

		}

		if (timeOut < 0) {

			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");
			return;

		}

	} else

		timeOut = 0;

	XBOX::XTCPSock	*socket;

	// If socket returned is NULL, accept() has timed out or an error has occured.
	// An exception is thrown by GetNewConnectedSocket() if erroneous.

	if ((socket = inServer->fSockListener->GetNewConnectedSocket(timeOut)) == NULL) 

		ioParms.ReturnNullValue();

	else {

		XBOX::VTCPEndPoint	*endPoint		= NULL;
		VJSNetSocketObject	*socketObject	= NULL;
	
		if ((endPoint = new XBOX::VTCPEndPoint(socket)) == NULL 
		|| (socketObject = new VJSNetSocketObject(true, VJSNetSocketObject::eTYPE_TCP4, false)) == NULL) {

			if (endPoint != NULL)

				endPoint->Release();

			if (socketObject != NULL)

				socketObject->Release();

			socket->Close();
			delete socket;

			XBOX::vThrowError(XBOX::VE_MEMORY_FULL);
		
		} else {

			socketObject->fEndPoint = endPoint;
			socketObject->fEndPoint->SetNoDelay(false);

			XBOX::VJSObject	newSocketSync = VJSNetSocketSyncClass::CreateInstance(ioParms.GetContext(), socketObject);
			XBOX::VString	address;

			socketObject->fObjectRef = newSocketSync.GetObjectRef();
			socketObject->fWorker = VJSWorker::GetWorker(ioParms.GetContext());

			socketObject->fEndPoint->GetIP(address);
			newSocketSync.SetProperty("remoteAddress", address);
			newSocketSync.SetProperty("remotePort", (sLONG) socketObject->fEndPoint->GetPort() & 0xffff);

			ioParms.ReturnValue(newSocketSync);

		}

	}
}