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

#include "VJSMessagePort.h"

#include "VJSContext.h"
#include "VJSGlobalClass.h"

#include "VJSEvent.h"
#include "VJSWorker.h"
#include "VJSStructuredClone.h"

USING_TOOLBOX_NAMESPACE

VJSMessagePort *VJSMessagePort::Create (VJSWorker *inOutsideWorker, VJSWorker *inInsideWorker)
{
	xbox_assert(inOutsideWorker != NULL && inInsideWorker != NULL && inOutsideWorker != inInsideWorker);

	VJSMessagePort	*messagePort	= new VJSMessagePort();

	messagePort->Retain();

	messagePort->fOutsideWorker = inOutsideWorker;	
	messagePort->fInsideWorker = inInsideWorker;

	inOutsideWorker->AddMessagePort(messagePort);
	inInsideWorker->AddMessagePort(messagePort);
		
	return messagePort;
}

VJSMessagePort *VJSMessagePort::Create (VJSWorker *inWorker, XBOX::VJSObject &inObject, const XBOX::VString &inCallbackName)
{
	xbox_assert(inWorker != NULL && inObject.GetObjectRef() != NULL && inCallbackName.GetLength());

	VJSMessagePort	*messagePort	= new VJSMessagePort();

	messagePort->fOutsideWorker = NULL;
	messagePort->fOutsideClosingFlag = true;

	messagePort->fInsideWorker = inWorker;
	messagePort->fInsideObject = inObject.GetObjectRef();
	messagePort->fInsideCallbackName = inCallbackName;

	inWorker->AddMessagePort(messagePort);

	return messagePort;
}

void VJSMessagePort::Close (VJSWorker *inWorker)
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));

	if (inWorker == fOutsideWorker)

		fOutsideClosingFlag = true;

	else
		
		fInsideClosingFlag = true;

	inWorker->RemoveMessagePort(this);

	Release();
}

void VJSMessagePort::CloseAll (VJSWorker *inWorker, std::list<VJSMessagePort *> *ioMessagePorts)
{
	xbox_assert(inWorker != NULL && ioMessagePorts != NULL);

	std::list<VJSMessagePort *>::iterator	i;

	for (i = ioMessagePorts->begin(); i != ioMessagePorts->end(); i++) {

		xbox_assert((inWorker == (*i)->fOutsideWorker && !(*i)->fOutsideClosingFlag) || (inWorker == (*i)->fInsideWorker && !(*i)->fInsideClosingFlag));

		if (inWorker == (*i)->fOutsideWorker)

			(*i)->fOutsideClosingFlag = true;

		else
		
			(*i)->fInsideClosingFlag = true;

		(*i)->Release();

	}
	ioMessagePorts->clear();
}

bool VJSMessagePort::IsInside (VJSWorker *inWorker)
{
	xbox_assert(inWorker != NULL);

	return inWorker == fInsideWorker;
}

VJSWorker *VJSMessagePort::GetOther (VJSWorker *inWorker)
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));

	return inWorker == fOutsideWorker ? fInsideWorker : fOutsideWorker;
}

VJSWorker *VJSMessagePort::GetOther ()
{
	xbox_assert(fOutsideWorker == NULL);

	return fInsideWorker;
}

bool VJSMessagePort::IsOtherClosed (VJSWorker *inWorker)
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));

	return inWorker == fOutsideWorker ? fInsideClosingFlag : fOutsideClosingFlag;
}

bool VJSMessagePort::IsOtherClosed ()
{
	xbox_assert(fOutsideWorker == NULL);

	return fInsideClosingFlag;
}

void VJSMessagePort::SetObject (VJSWorker *inWorker, XBOX::VJSObject &inObject)
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));
	xbox_assert(inObject.GetObjectRef() != NULL);

	if (inWorker == fOutsideWorker) 

		fOutsideObject = inObject.GetObjectRef();
	
	else 
		
		fInsideObject = inObject.GetObjectRef();
}

void VJSMessagePort::SetCallbackName (VJSWorker *inWorker, const XBOX::VString &inCallbackName)
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));
	xbox_assert(inCallbackName.GetLength());

	if (inWorker == fOutsideWorker) 

		fOutsideCallbackName = inCallbackName;

	else

		fInsideCallbackName = inCallbackName;
}

XBOX::VJSObject VJSMessagePort::GetCallback (VJSWorker *inWorker, XBOX::VJSContext &inContext) 
{
	xbox_assert(inWorker != NULL && (inWorker == fOutsideWorker || inWorker == fInsideWorker));
	xbox_assert((inWorker == fOutsideWorker && !fOutsideClosingFlag) || (inWorker == fInsideWorker && !fInsideClosingFlag));

	XBOX::VJSObject	object(inContext, inWorker == fOutsideWorker ? fOutsideObject : fInsideObject);

	return object.GetPropertyAsObject(inWorker == fOutsideWorker ? fOutsideCallbackName : fInsideCallbackName);
}

void VJSMessagePort::PostMessage (VJSWorker *inTargetWorker, VJSStructuredClone *inMessage)
{
	xbox_assert(inTargetWorker != NULL && (inTargetWorker == fOutsideWorker || inTargetWorker == fInsideWorker));
		
	if ((inTargetWorker == fOutsideWorker && !fOutsideClosingFlag)
	|| (inTargetWorker == fInsideWorker && !fInsideClosingFlag))

		inTargetWorker->QueueEvent(VJSMessageEvent::Create(this, inMessage));
}

void VJSMessagePort::PostError (VJSWorker *inTargetWorker, const XBOX::VString &inMessage, const XBOX::VString &inFileName, sLONG inLineNumber)
{
	xbox_assert(inTargetWorker != NULL && (inTargetWorker == fOutsideWorker || inTargetWorker == fInsideWorker));
		
	if ((inTargetWorker == fOutsideWorker && !fOutsideClosingFlag)
	|| (inTargetWorker == fInsideWorker && !fInsideClosingFlag))

		inTargetWorker->QueueEvent(VJSErrorEvent::Create(this, inMessage, inFileName, inLineNumber));
}

void VJSMessagePort::PostMessageMethod (XBOX::VJSParms_callStaticFunction &ioParms, VJSMessagePort *inMessagePort)
{
	xbox_assert(inMessagePort != NULL);

	if (ioParms.CountParams() < 1) {
	
		XBOX::vThrowError(XBOX::VE_INVALID_PARAMETER);
		return;

	}

	VJSWorker			*worker	= VJSWorker::GetWorker(ioParms.GetContext());
	XBOX::VJSValue		value = ioParms.GetParamValue(1);
	VJSStructuredClone	*message;
			
	message = VJSStructuredClone::RetainClone(value);
	inMessagePort->PostMessage(inMessagePort->GetOther(worker), message);
	message->Release();
}

VJSMessagePort::VJSMessagePort () 
{
	fOutsideClosingFlag = fInsideClosingFlag = false;
	fOutsideObject = fInsideObject = NULL;
}

VJSMessagePort::~VJSMessagePort () 
{
	xbox_assert(fOutsideClosingFlag && fInsideClosingFlag);
}

void VJSMessagePortClass::GetDefinition (ClassDefinition &outDefinition)
{
    static inherited::StaticFunction functions[] =
	{
		{ "postMessage",	js_callStaticFunction<_postMessage>,	JS4D::PropertyAttributeDontDelete },
		{ 0,				0,										0 },
	};

    outDefinition.className	= "MessagePort";    
	outDefinition.staticFunctions = functions;
}

void VJSMessagePortClass::_postMessage (XBOX::VJSParms_callStaticFunction &ioParms, VJSMessagePort *inMessagePort)
{
	VJSMessagePort::PostMessageMethod(ioParms, inMessagePort);
}