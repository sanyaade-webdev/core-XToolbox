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

#include "VJSWorkerProxy.h"

#include "VJSGlobalClass.h"
#include "VJSContext.h"
#include "VJSWorker.h"
#include "VJSMessagePort.h"

USING_TOOLBOX_NAMESPACE

VJSWorkerLocationObject::VJSWorkerLocationObject (const XBOX::VString &inURL)
{
	XBOX::VURL	url	= XBOX::VURL(inURL, true);
		
	url.GetAbsoluteURL(fHRef, false);
	url.GetScheme(fProtocol);
	url.GetNetworkLocation(fHost, false);
	url.GetHostName(fHostName, false);
	url.GetPortNumber(fPort, false);
	url.GetPath(fPathName, eURL_POSIX_STYLE, false);
	url.GetQuery(fSearch, false);
	url.GetFragment(fHash, false);		
}

void VJSWorkerLocationClass::GetDefinition (ClassDefinition &outDefinition)
{
    outDefinition.className	= "WorkerLocation";
    outDefinition.initialize = js_initialize<_Initialize>;
}

void VJSWorkerLocationClass::_Initialize (const XBOX::VJSParms_initialize &inParms, VJSWorkerLocationObject *inWorkerLocation)
{
	xbox_assert(inWorkerLocation != NULL);

	XBOX::VJSObject	object(inParms.GetObject());

	object.SetProperty("href",		inWorkerLocation->fHRef,		JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("protocol",	inWorkerLocation->fProtocol,	JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("host",		inWorkerLocation->fHost,		JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("hostname",	inWorkerLocation->fHostName,	JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("port",		inWorkerLocation->fPort,		JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("pathname",	inWorkerLocation->fPathName,	JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("search",	inWorkerLocation->fSearch,		JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
	object.SetProperty("hash",		inWorkerLocation->fHash,		JS4D::PropertyAttributeReadOnly | JS4D::PropertyAttributeDontDelete);
}

VJSWebWorkerObject *VJSWebWorkerObject::_CreateWorker (XBOX::VJSContext &inParentContext, VJSWorker *inOutsideWorker, 
														bool inReUseContext, 
														const XBOX::VString &inUrl, bool inIsDedicated, const XBOX::VString &inName)
{
	xbox_assert(inOutsideWorker != NULL);
	
	bool		needToRun;
	VJSWorker	*insideWorker;
			
	if (inIsDedicated) {
	
		needToRun = true;
		insideWorker = new VJSWorker(inParentContext, inUrl, inReUseContext);
				
	} else 

		insideWorker = VJSWorker::GetSharedWorker(inParentContext, inUrl, inName, inReUseContext, &needToRun);

	if (insideWorker == NULL)

		return NULL;
	
	VJSWebWorkerObject	*webWorkerObject;

	webWorkerObject = new VJSWebWorkerObject();

	webWorkerObject->fOnMessagePort = VJSMessagePort::Create(inOutsideWorker, insideWorker);
	webWorkerObject->fOnMessagePort->SetCallbackName(inOutsideWorker, "onmessage");	
	webWorkerObject->fOnMessagePort->SetCallbackName(insideWorker, "onmessage");
	
	webWorkerObject->fOnErrorPort = VJSMessagePort::Create(inOutsideWorker, insideWorker);
	webWorkerObject->fOnErrorPort->SetCallbackName(inOutsideWorker, "onerror");	
	webWorkerObject->fOnErrorPort->SetCallbackName(insideWorker, "onerror");

	inOutsideWorker->AddErrorPort(webWorkerObject->fOnErrorPort);
	insideWorker->AddErrorPort(webWorkerObject->fOnErrorPort);

	if (inIsDedicated)

		insideWorker->SetMessagePorts(webWorkerObject->fOnMessagePort, webWorkerObject->fOnErrorPort);

	else

		insideWorker->Connect(webWorkerObject->fOnMessagePort, webWorkerObject->fOnErrorPort);
	
	if (needToRun) 

		insideWorker->Run();

	return webWorkerObject;
}

void VJSDedicatedWorkerClass::GetDefinition (ClassDefinition &outDefinition)
{
	static inherited::StaticFunction functions[] =
	{
		{ "terminate",		js_callStaticFunction<_terminate>,		JS4D::PropertyAttributeDontDelete	},		
		{ "postMessage",	js_callStaticFunction<_postMessage>,	JS4D::PropertyAttributeDontDelete	},
		{ 0,				0,										0									},
	};	
	
	outDefinition.className	= "Worker";
	outDefinition.staticFunctions = functions;
	outDefinition.finalize = js_finalize<_Finalize>;	
}

void VJSDedicatedWorkerClass::Construct (XBOX::VJSParms_construct &ioParms)
{
	if (!ioParms.CountParams() || !ioParms.IsStringParam(1)) {

		XBOX::vThrowError(XBOX::VE_INVALID_PARAMETER);
		ioParms.ReturnUndefined();
		return;

	}

	XBOX::VString		url;
	bool				reUseContext;
	VJSWorker			*outsideWorker;
	VJSWebWorkerObject	*workerProxy;
	XBOX::VJSContext	context(ioParms.GetContext());
	XBOX::VJSObject		constructedObject(context);	
	
	ioParms.GetStringParam(1, url);
	outsideWorker = VJSWorker::GetWorker(context);

	if (ioParms.CountParams() < 2 || !ioParms.IsBooleanParam(2) || !ioParms.GetBoolParam(2, &reUseContext))

		reUseContext = false;

	if ((workerProxy = VJSWebWorkerObject::_CreateWorker(context, outsideWorker, reUseContext, url, true)) != NULL) {

		constructedObject = VJSDedicatedWorkerClass::CreateInstance(context, workerProxy);
		workerProxy->fOnMessagePort->SetObject(outsideWorker, constructedObject);
		workerProxy->fOnErrorPort->SetObject(outsideWorker, constructedObject);
			
	} else 

		constructedObject.SetNull();

	ioParms.ReturnConstructedObject(constructedObject);
}

void VJSDedicatedWorkerClass::_Finalize (const XBOX::VJSParms_finalize &inParms, VJSWebWorkerObject *inDedicatedWorker)
{
	xbox_assert(inDedicatedWorker != NULL);

	delete inDedicatedWorker;
}

void VJSDedicatedWorkerClass::_terminate (XBOX::VJSParms_callStaticFunction &ioParms, VJSWebWorkerObject *inDedicatedWorker)
{
	xbox_assert(inDedicatedWorker != NULL);

	VJSWorker	*outsideWorker	= VJSWorker::GetWorker(ioParms.GetContext());
	VJSWorker	*insideWorker	= inDedicatedWorker->fOnMessagePort->GetOther(outsideWorker);

	insideWorker->Terminate();
}

void VJSDedicatedWorkerClass::_postMessage (XBOX::VJSParms_callStaticFunction &ioParms, VJSWebWorkerObject *inDedicatedWorker)
{
	xbox_assert(inDedicatedWorker != NULL);

	VJSMessagePort::PostMessageMethod(ioParms, inDedicatedWorker->fOnMessagePort);
}

void VJSSharedWorkerClass::GetDefinition (ClassDefinition &outDefinition)
{
	outDefinition.className	= "SharedWorker";
    outDefinition.finalize = js_finalize<_Finalize>;	
}

void VJSSharedWorkerClass::Construct (XBOX::VJSParms_construct &ioParms)
{
	if (!ioParms.CountParams()
	|| !ioParms.IsStringParam(1)
	|| (ioParms.CountParams() >= 2 && !ioParms.IsStringParam(2))) {
	
		XBOX::vThrowError(XBOX::VE_INVALID_PARAMETER);
		ioParms.ReturnUndefined();
		return;

	}

	XBOX::VString		url, name;
	bool				reUseContext;
	VJSWorker			*outsideWorker;
	VJSWebWorkerObject	*workerProxy;
	XBOX::VJSContext	context(ioParms.GetContext());
	XBOX::VJSObject		constructedObject(context);

	ioParms.GetStringParam(1, url);
	if (ioParms.CountParams() == 2)

		ioParms.GetStringParam(2, name);

	outsideWorker = VJSWorker::GetWorker(context);

	if (ioParms.CountParams() < 3 || !ioParms.IsBooleanParam(3) || !ioParms.GetBoolParam(3, &reUseContext))

		reUseContext = false;

	if ((workerProxy = VJSWebWorkerObject::_CreateWorker(context, outsideWorker, reUseContext, url, false, name)) != NULL) {
		
		constructedObject = VJSDedicatedWorkerClass::CreateInstance(context, workerProxy);
		workerProxy->fOnErrorPort->SetObject(outsideWorker, constructedObject);

		XBOX::VJSObject	messagePortObject(context);
		
		messagePortObject = VJSMessagePortClass::CreateInstance(context, workerProxy->fOnMessagePort);
		workerProxy->fOnMessagePort->SetObject(outsideWorker, messagePortObject);
		
		constructedObject.SetProperty("port", messagePortObject);
		
	} else 

		constructedObject.SetNull();

	ioParms.ReturnConstructedObject(constructedObject);
}

void VJSSharedWorkerClass::_Finalize (const XBOX::VJSParms_finalize &inParms, VJSWebWorkerObject *inSharedWorker)
{
	xbox_assert(inSharedWorker != NULL);

	delete inSharedWorker;
}