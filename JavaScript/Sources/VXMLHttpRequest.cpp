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
#include "VJavaScriptPrecompiled.h" //Fait plaisir a VS

#include <string.h>

#include "JavaScript/VJavaScript.h"
#include "CurlWrapper.h"
#include "VXMLHttpRequest.h"



class XMLHttpRequest::CWImpl
{
public :
    CWImpl() : fReq(NULL)/*, fMethod(CW::HttpRequest::CUSTOM)*/ {};
    ~CWImpl(){ if(fReq) delete fReq; }

    CW::HttpRequest*        fReq;
    //CW::HttpRequest::Method fMethod;
};





class MethodHelper
{
public :

    typedef CW::HttpRequest::Method Meth;

    MethodHelper(const XBOX::VString& inMethod) :
        fIsValid(false), fIsDangerous(false), fDoesSupportData(false), fMethod(CW::HttpRequest::CUSTOM)
    {
        XBOX::VString METHOD(inMethod);
        METHOD.ToUpperCase();

        if(METHOD.EqualToUSASCIICString("GET"))
            fMethod=CW::HttpRequest::GET, fIsValid=true;
        else if(METHOD.EqualToUSASCIICString("POST"))
            fMethod=CW::HttpRequest::POST, fIsValid=true;
        else if(METHOD.EqualToUSASCIICString("PUT"))
            fMethod=CW::HttpRequest::PUT, fDoesSupportData=true, fIsValid=true;
        else if (METHOD.EqualToUSASCIICString("HEAD"))
            fMethod=CW::HttpRequest::HEAD, fIsValid=true;
    }

    bool IsValid()          { return fIsValid; }
    bool IsDangerous()      { return fIsDangerous; }
    bool DoesSupportData()  { return fDoesSupportData; }
    Meth GetMethod()        { return fMethod; }


private :

    bool    fIsValid;
    bool    fIsDangerous;
    bool    fDoesSupportData;
    Meth    fMethod;
};



class UrlHelper
{
public :

    UrlHelper(const XBOX::VString inUrl) : fUrl(inUrl) {}

    XBOX::VString   DropFragment()              { return fUrl; }
    bool            IsSupportedScheme()         { return true; }
    bool            IsUserInfoSet()             { return false; }
    bool            DoesSchemeSupportUserInfo() { return false; }

private :

    XBOX::VString fUrl;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XMLHttpRequest : Implements xhr logic on top of curl wrapper. Setters and getters return VE_OK and fill params on
//                  success or return an error and don't touch params on failure. Errors are not thrown.
//
// - VE_XHRQ_IMPL_FAIL_ERROR
// - VE_XHRQ_INVALID_STATE_ERROR
// - VE_XHRQ_SYNTAX_ERROR
// - VE_XHRQ_SECURITY_ERROR
// - VE_XHRQ_NOT_SUPPORTED_ERROR
// - VE_XHRQ_SEND_ERROR
// - VE_XHRQ_NO_HEADER_ERROR
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//XMLHttpRequest::XMLHttpRequest(const XBOX::VString& inProxy, uLONG inProxyPort) :
XMLHttpRequest::XMLHttpRequest() :
    fAsync(true),
    fReadyState(UNSENT),
    fStatus(0),
    fErrorFlag(false),
    fPort(0),
    fChangeHandler(NULL)
{
    fCWImpl=new CWImpl();
};


XMLHttpRequest::~XMLHttpRequest()
{
    if(fCWImpl)
        delete fCWImpl;
}


XBOX::VError XMLHttpRequest::Open(const XBOX::VString& inMethod, const XBOX::VString& inUrl, bool inAsync,
                                  const XBOX::VString& inUser, const XBOX::VString& inPasswd)
{
    if(fReadyState!=UNSENT)
        return VE_XHRQ_INVALID_STATE_ERROR;   //Doesn't follow the spec, but behave roughly as FireFox

    if(!fCWImpl)
        return VE_XHRQ_IMPL_FAIL_ERROR;


    MethodHelper mh(inMethod);

    if(!mh.IsValid())
        return VE_XHRQ_SYNTAX_ERROR;

    if(mh.IsDangerous())
        return VE_XHRQ_SECURITY_ERROR;


    UrlHelper uh(inUrl);

    if(!uh.IsSupportedScheme())
        return VE_XHRQ_NOT_SUPPORTED_ERROR;

    if(uh.IsUserInfoSet() && !uh.DoesSchemeSupportUserInfo())
        return VE_XHRQ_SYNTAX_ERROR;

    if(fCWImpl->fReq)
        delete fCWImpl->fReq;

    fCWImpl->fReq=new CW::HttpRequest(uh.DropFragment(), mh.GetMethod());
    if(!fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    fReadyState=OPENED;

    if(fChangeHandler)
        fChangeHandler->Execute();

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::SetProxy(const XBOX::VString& inHost, const uLONG inPort)
{
    if(fReadyState!=UNSENT && fReadyState!=OPENED)
        return VE_XHRQ_INVALID_STATE_ERROR;

    fProxy=inHost;
    fPort=inPort;

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::SetRequestHeader(const XBOX::VString& inKey, const XBOX::VString& inValue)
{
    if(fReadyState!=OPENED)
        return VE_XHRQ_INVALID_STATE_ERROR;

    if(!fCWImpl || !fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;


    bool res=fCWImpl->fReq->SetRequestHeader(inKey, inValue);

    if(!res)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::OnReadyStateChange(XBOX::VJSObject inReceiver, const XBOX::VJSObject& inHandler)
{
    //No need for state tests here...

    if(fChangeHandler)
        delete fChangeHandler;

    fChangeHandler=NULL;

    fChangeHandler=new Command(inReceiver, inHandler);
    if(!fChangeHandler)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::Send(const XBOX::VString& inData, XBOX::VError* outImplErr)
{
    if(fReadyState!=OPENED)
        return VE_XHRQ_INVALID_STATE_ERROR;

    if(!fCWImpl || !fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    fErrorFlag=false;

    if(!fProxy.IsEmpty())
        fCWImpl->fReq->SetProxy(fProxy, fPort);

    if(/*(fCWImpl->fMethod==CW::HttpRequest::POST || fCWImpl->fMethod==CW::HttpRequest::PUT) && */!inData.IsEmpty())
    {
        fCWImpl->fReq->SetData(inData);
    }

    //if(fCWImpl->fMethod==CW::HttpRequest::GET || fCWImpl->fMethod==CW::HttpRequest::POST || fCWImpl->fMethod==CW::HttpRequest::HEAD || fCWImpl->fMethod==CW::HttpRequest::PUT)
    //{
    //parametrer la req de facon appropriee
    //}

    if(!fUser.IsEmpty())
    {
        //parametrer la req de facon appropriee

        if(!fPasswd.IsEmpty())
        {
            //parametrer la req de facon appropriee
        }
    }

    bool res=fCWImpl->fReq->Perform(outImplErr);

    if(res)
    {
        fReadyState=HEADERS_RECEIVED;

        if(fChangeHandler)
            fChangeHandler->Execute();

        fReadyState=LOADING;

        if(fChangeHandler)
            fChangeHandler->Execute();
    }

    fReadyState=DONE;

    if(fChangeHandler)
        fChangeHandler->Execute();

    if(!res)
        return VE_XHRQ_SEND_ERROR;

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::Abort()
{
    //No need for state tests here...

    fErrorFlag=true;
    fReadyState=UNSENT;

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::GetReadyState(XBOX::VLong* outValue) const
{
    //No need for state tests here...

    *outValue=(uLONG)(fReadyState);

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::GetStatus(XBOX::VLong* outValue) const
{
    if(fReadyState!=UNSENT && fReadyState!=OPENED && !fErrorFlag)
    {
        if(!fCWImpl || !fCWImpl->fReq)
            return VE_XHRQ_IMPL_FAIL_ERROR;

        uLONG code=0;
        bool hasCode=fCWImpl->fReq->HasValidResponseCode(&code);

        if(hasCode)
        {
            *outValue=(uLONG)(code);
            return XBOX::VE_OK;
        }

        return VE_XHRQ_NO_RESPONSE_CODE_ERROR;
    }

    return VE_XHRQ_INVALID_STATE_ERROR;
}


XBOX::VError XMLHttpRequest::GetStatusText(XBOX::VString* outValue) const
{
    XBOX::VLong code;
    XBOX::VError res=GetStatus(&code);

    if(res!=XBOX::VE_OK)
        return res;

    switch((uLONG)(code))
    {
    case 100 : *outValue=XBOX::VString("Continue") ; break;
    case 101 : *outValue=XBOX::VString("Switching Protocols") ; break;
        //102   Processing (webdav)
    case 200 : *outValue=XBOX::VString("OK") ; break;
    case 201 : *outValue=XBOX::VString("Created") ; break;
    case 202 : *outValue=XBOX::VString("Accepted") ; break;
    case 203 : *outValue=XBOX::VString("Non-Authoritative Information") ; break;
    case 204 : *outValue=XBOX::VString("No Content") ; break;
    case 205 : *outValue=XBOX::VString("Reset Content") ; break;
    case 206 : *outValue=XBOX::VString("Partial Content") ; break;
        //207   Multi-Status (webdav)
        //210   Content Different (webdav)
    case 300 : *outValue=XBOX::VString("Multiple Choices") ; break;
    case 301 : *outValue=XBOX::VString("Moved Permanently") ; break;
    case 302 : *outValue=XBOX::VString("Moved Temporarily") ; break;
    case 303 : *outValue=XBOX::VString("See Other") ; break;
    case 304 : *outValue=XBOX::VString("Not Modified") ; break;
    case 305 : *outValue=XBOX::VString("Use Proxy") ; break;
    case 307 : *outValue=XBOX::VString("Temporary Redirect") ; break;
    case 400 : *outValue=XBOX::VString("Bad Request") ; break;
    case 401 : *outValue=XBOX::VString("Unauthorized") ; break;
    case 402 : *outValue=XBOX::VString("Payment Required") ; break;
    case 403 : *outValue=XBOX::VString("Forbidden") ; break;
    case 404 : *outValue=XBOX::VString("Not Found") ; break;
    case 405 : *outValue=XBOX::VString("Method Not Allowed") ; break;
    case 406 : *outValue=XBOX::VString("Not Acceptable") ; break;
    case 407 : *outValue=XBOX::VString("Proxy Authentication Required") ; break;
    case 408 : *outValue=XBOX::VString("Request Time-out") ; break;
    case 409 : *outValue=XBOX::VString("Conflict") ; break;
    case 410 : *outValue=XBOX::VString("Gone") ; break;
    case 411 : *outValue=XBOX::VString("Length Required") ; break;
    case 412 : *outValue=XBOX::VString("Precondition Failed") ; break;
    case 413 : *outValue=XBOX::VString("Request Entity Too Large") ; break;
    case 414 : *outValue=XBOX::VString("Request-URI Too Long") ; break;
    case 415 : *outValue=XBOX::VString("Unsupported Media Type") ; break;
    case 416 : *outValue=XBOX::VString("Requested range unsatisfiable") ; break;
    case 417 : *outValue=XBOX::VString("Expectation failed") ; break;
        //422   Unprocessable entity (webdav)
        //423   Locked (webdav)
        //424   Method failure (webdav)
    case 500 : *outValue=XBOX::VString("Internal Server Error") ; break;
    case 501 : *outValue=XBOX::VString("Not Implemented") ; break;
    case 502 : *outValue=XBOX::VString("Bad Gateway") ; break;
    case 503 : *outValue=XBOX::VString("Service Unavailable") ; break;
    case 504 : *outValue=XBOX::VString("Gateway Time-out") ; break;
    case 505 : *outValue=XBOX::VString("HTTP Version not supported") ; break;
        //507   Insufficient storage (webdav)
    case 509 : *outValue=XBOX::VString("Bandwidth Limit Exceeded") ; break;

    default :
        *outValue=XBOX::VString("");
    }

    return XBOX::VE_OK;
}


XBOX::VError XMLHttpRequest::GetResponseHeader(const XBOX::VString& inKey, XBOX::VString* outValue) const
{
    if(fReadyState==UNSENT || fReadyState==OPENED || fErrorFlag)
        return VE_XHRQ_INVALID_STATE_ERROR;

    if(!fCWImpl || !fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    bool found=fCWImpl->fReq->GetResponseHeader(inKey, outValue);

    if(found)
        return XBOX::VE_OK;

    return VE_XHRQ_NO_HEADER_ERROR;
}


XBOX::VError XMLHttpRequest::GetAllResponseHeaders(XBOX::VString* outValue) const
{
    if(fReadyState==UNSENT || fReadyState==OPENED || fErrorFlag)
        return VE_XHRQ_INVALID_STATE_ERROR;

    if(!fCWImpl || !fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    bool found=fCWImpl->fReq->GetAllResponseHeaders(outValue);

    if(found)
        return XBOX::VE_OK;

    return VE_XHRQ_NO_HEADER_ERROR;
}


XBOX::VError XMLHttpRequest::GetResponseText(XBOX::VString* outValue) const
{
    if(fReadyState!=LOADING && fReadyState!=DONE)
        return VE_XHRQ_INVALID_STATE_ERROR;

    if(!fCWImpl || !fCWImpl->fReq)
        return VE_XHRQ_IMPL_FAIL_ERROR;

    const char* ptr=NULL;
    int len=-1;

    ptr=fCWImpl->fReq->GetContentCPointer();
    len=fCWImpl->fReq->GetContentLength();

	if(ptr==NULL || len==0)
		return XBOX::VE_OK;
	
	if(outValue==NULL)
		return XBOX::VE_INVALID_PARAMETER;
				
    XBOX::CharSet cs=GuessCharSet();

    outValue->FromBlock(ptr, len, cs);

    return XBOX::VE_OK;
}


// XBOX::CharSet XMLHttpRequest::GetRequestCharSet() const
// {
//     const char* c_header=fCWImpl->fReq->GetRequestHeader("Content-Type");

//     XBOX::CharSet cs=GetCharSet(c_header);

//     return cs/*!=XBOX::VTC_UNKNOWN ? cs : XBOX::VTC_US_ASCII*/;
// }


// XBOX::CharSet XMLHttpRequest::GuessResponseCharSet() const
// {
//     const char* c_header=fCWImpl->fReq->GetResponseHeader("Content-Type");

//     XBOX::CharSet cs=GetCharSet(c_header);

//     return cs!=XBOX::VTC_UNKNOWN ? cs : XBOX::VTC_UTF_8 ;
// }


// XBOX::CharSet XMLHttpRequest::GetCharSet(const char* inHeader) const
// {
//     if(c_header)
//     {
//         const char* pos=strstr(c_header, "charset=");

//         if(pos && strlen(pos)>sizeof("charset="))
//         {
//             pos+=sizeof("charset=")-1;
//             sLONG len=strlen(pos);

//             XBOX::VString charSetName(pos, len, XBOX::VTC_US_ASCII);

//             if(!charSetName.IsEmpty())
//             {
//                 XBOX::CharSet cs=XBOX::VTextConverters::Get()->GetCharSetFromName(charSetName);

//                 if(cs!=XBOX::VTC_UNKNOWN)
//                     return cs;
//             }
//         }
//     }

//     return XBOX::VTC_UNKNOWN;
// }


XBOX::CharSet XMLHttpRequest::GuessCharSet() const
{
    const char* c_header=fCWImpl->fReq->GetResponseHeader("Content-Type");

    if(c_header)
    {
        const char* pos=strstr(c_header, "charset=");

        if(pos && strlen(pos)>sizeof("charset="))
        {
            pos+=sizeof("charset=")-1;
            sLONG len=strlen(pos);

            XBOX::VString charSetName(pos, len, XBOX::VTC_US_ASCII);

            if(!charSetName.IsEmpty())
            {
                XBOX::CharSet cs=XBOX::VTextConverters::Get()->GetCharSetFromName(charSetName);
                    
                if(cs!=XBOX::VTC_UNKNOWN)
                    return cs;
            }
        }
    }

    return XBOX::VTC_US_ASCII;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// VJSXMLHttpRequest
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void VJSXMLHttpRequest::GetDefinition(ClassDefinition& outDefinition)
{
    const XBOX::JS4D::PropertyAttributes attDontWrite=XBOX::JS4D::PropertyAttributeReadOnly;

    const XBOX::JS4D::PropertyAttributes attDontEnumDelete=XBOX::JS4D::PropertyAttributeDontEnum |
        XBOX::JS4D::PropertyAttributeDontDelete;

    const XBOX::JS4D::PropertyAttributes attDontWriteEnumDelete=attDontWrite | attDontEnumDelete;

    static inherited::StaticFunction functions[] =
        {
            { "open",                   js_callStaticFunction<_Open>,                   attDontWriteEnumDelete},
            { "send",                   js_callStaticFunction<_Send>,                   attDontWriteEnumDelete},
            //{ "setProxy",               js_callStaticFunction<_SetProxy>,               attDontWriteEnumDelete},
            { "getResponseHeader",      js_callStaticFunction<_GetResponseHeader>,      attDontWriteEnumDelete},
            { "getAllResponseHeaders",  js_callStaticFunction<_GetAllResponseHeaders>,  attDontWriteEnumDelete},
            { "setRequestHeader",       js_callStaticFunction<_SetRequestHeader>,       attDontWriteEnumDelete},
            { 0, 0, 0}
        };

    static inherited::StaticValue values[] =
        {
            { "responseText",       js_getProperty<_GetResponseText>, nil,      attDontWriteEnumDelete},
            { "readyState",         js_getProperty<_GetReadyState>, nil,        attDontWriteEnumDelete},
            { "status",             js_getProperty<_GetStatus>, nil,            attDontWriteEnumDelete},
            { "statusText",         js_getProperty<_GetStatusText>, nil,        attDontWriteEnumDelete},
            { "onreadystatechange", nil, js_setProperty<_OnReadyStateChange>,   attDontEnumDelete},
            { 0, 0, 0, 0}
        };

    outDefinition.className         = "XMLHttpRequest";
    outDefinition.staticFunctions   = functions;

    outDefinition.initialize        = js_initialize<Initialize>;
    outDefinition.finalize          = js_finalize<Finalize>;

    //outDefinition.callAsFunction    = js_callAsFunction<CallAsFunction>;
    //outDefinition.callAsConstructor = js_callAsConstructor<CallAsConstructor>;

    outDefinition.staticValues      = values;
}


void VJSXMLHttpRequest::Initialize(const XBOX::VJSParms_initialize& inParms, XMLHttpRequest* inXhr)
{
    //Alloc and init is done in Construct();
}


void VJSXMLHttpRequest::Finalize(const XBOX::VJSParms_finalize& inParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
        delete inXhr;
}


//void VJSXMLHttpRequest::CallAsConstructor(XBOX::VJSParms_callAsConstructor& ioParms)
void VJSXMLHttpRequest::Construct(XBOX::VJSParms_construct& ioParms)
{
    XMLHttpRequest* xhr=new XMLHttpRequest();

    if(!xhr)
    {
        XBOX::vThrowError(VE_XHRQ_IMPL_FAIL_ERROR);
        return;
    }
	

    XBOX::VJSObject pData(ioParms.GetContext());
	bool resData;
    resData=ioParms.GetParamObject(1, pData);

	if(resData)
	{
		XBOX::VJSValue hostValue=pData.GetProperty("host", NULL);
		XBOX::VString hostString;
		bool hostRes=hostValue.GetString(hostString, NULL);

		if(hostRes) //we have a proxy
		{
			XBOX::VJSValue portValue=pData.GetProperty("port", NULL);
			sLONG portLong=0;
			bool portRes=portValue.GetLong(&portLong, NULL);

			portLong=(portRes && portLong>0) ? portLong : 80;

			//todo : user and passwd

			XBOX::VError res=xhr->SetProxy(hostString, portLong);

			if(res!=XBOX::VE_OK)
				XBOX::vThrowError(res);
		}
	}

    ioParms.ReturnConstructedObject(VJSXMLHttpRequest::CreateInstance(ioParms.GetContextRef(), xhr));

}


// void VJSXMLHttpRequest::CallAsFunction(XBOX::VJSParms_callAsFunction& ioParms)
// {
//     XBOX::VError err=XBOX::VE_OK;
// }


void VJSXMLHttpRequest::_Open(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    XBOX::VString pMethod;
    bool resMethod;
    resMethod=ioParms.GetStringParam(1, pMethod);

    XBOX::VString pUrl;
    bool resUrl;    //jmo - todo : Verifier les longueurs max d'une chaine et d'une URL
    resUrl=ioParms.GetStringParam(2, pUrl);

    bool pAsync;
    bool resAsync;
    resAsync=ioParms.GetBoolParam(3, &pAsync);

    if(resMethod && resUrl && inXhr)
    {
        XBOX::VError res=inXhr->Open(pMethod, pUrl, pAsync);  //ASync is optional, we don't care if it's not set

        if(res!=XBOX::VE_OK)
            XBOX::vThrowError(res);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


// void VJSXMLHttpRequest::_SetProxy(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
// {
//     XBOX::VJSObject pData(ioParms.GetContext());
//     ioParms.GetParamObject(1, pData);

//     XBOX::VJSValue hostValue=pData.GetProperty("host", NULL);
//     XBOX::VJSValue portValue=pData.GetProperty("port", NULL);

//     if(hostValue.IsString() && portValue.IsNumber() && inXhr)
//     {
//         XBOX::VString hostString;
//         bool hostRes=hostValue.GetString(hostString, NULL);

//         sLONG portLong=0;
//         bool portRes=portValue.GetLong(&portLong, NULL);

//         if(hostRes && portRes && portLong>0 && inXhr)
//         {
//             XBOX::VError res=inXhr->SetProxy(hostString, portLong);

//             if(res!=XBOX::VE_OK)
//                 XBOX::vThrowError(res);
//         }
//     }
//     else
//         XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
// }


void VJSXMLHttpRequest::_SetRequestHeader(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    XBOX::VString pHeader;
    XBOX::VString pValue;

    bool resHeader=ioParms.GetStringParam(1, pHeader);
    bool resValue=ioParms.GetStringParam(2, pValue);

    if(resHeader && resValue && inXhr)
    {
        XBOX::VError res=inXhr->SetRequestHeader(pHeader, pValue);

        if(res!=XBOX::VE_OK)
            XBOX::vThrowError(res);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


bool VJSXMLHttpRequest::_OnReadyStateChange(XBOX::VJSParms_setProperty& ioParms, XMLHttpRequest* inXhr)
{
    XBOX::VJSObject pReceiver=ioParms.GetObject();
    XBOX::VJSObject pHandler(pReceiver);
    ioParms.GetPropertyValue().GetObject(pHandler, NULL);

    if(pHandler.IsFunction() && inXhr)
    {
        XBOX::VError res=inXhr->OnReadyStateChange(pReceiver, pHandler);

        if(res!=XBOX::VE_OK)
            XBOX::vThrowError(res);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);

    return true;    //todo - mouai, voir ca de plus pres...
}


void VJSXMLHttpRequest::_Send(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    XBOX::VString pData;
    bool resData;
    resData=ioParms.GetStringParam(1, pData);

    if(inXhr)   //Data is optional, we don't care if it's not set
    {
        XBOX::VError impl_err=XBOX::VE_OK;
        XBOX::VError res=inXhr->Send(pData, &impl_err);

        if(res!=XBOX::VE_OK)
        {

			uLONG code=ERRCODE_FROM_VERROR(impl_err);
			uLONG comp=COMPONENT_FROM_VERROR(impl_err);

			//XBOX::VErrorBase*		verrBase = new XBOX::VErrorBase ( impl_err, 0 );
			//XBOX::VTask::GetCurrent ( )-> PushRetainedError ( verrBase );
			
			//We may have an implementation error which might be documented
            if(impl_err!=XBOX::VE_OK)
               XBOX::vThrowError(impl_err);

            //Now throw the more generic error
            XBOX::vThrowError(res);
        }
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_Abort(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VError res=inXhr->Abort();

        if(res!=XBOX::VE_OK)
            XBOX::vThrowError(res);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetReadyState(XBOX::VJSParms_getProperty& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VError res;
        XBOX::VLong value;

        res=inXhr->GetReadyState(&value); //Should never fails...
        ioParms.ReturnVValue(value);

        if(res!=XBOX::VE_OK)
            XBOX::vThrowError(res);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetStatus(XBOX::VJSParms_getProperty& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VLong value(0);
        XBOX::VError res=inXhr->GetStatus(&value);
        ioParms.ReturnVValue(value);

        //Don't throw any error here, but returns 0.
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetStatusText(XBOX::VJSParms_getProperty& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VString value;
        XBOX::VError res=inXhr->GetStatusText(&value);
        ioParms.ReturnVValue(value);

        //Don't throw any error here, but returns an empty string.
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetResponseHeader(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    XBOX::VString pHeader;
    bool resHeader;
    resHeader=ioParms.GetStringParam(1, pHeader);

    if(resHeader && inXhr)
    {
        XBOX::VString value;
        XBOX::VError res;

        res=inXhr->GetResponseHeader(pHeader, &value);

        //Don't throw any error here, but returns null

        if(res==XBOX::VE_OK)
            ioParms.ReturnString(value);
        else
            ioParms.ReturnNullValue();
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetAllResponseHeaders(XBOX::VJSParms_callStaticFunction& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VString value;
        XBOX::VError res=inXhr->GetAllResponseHeaders(&value);

        //returns an empty string on error
        ioParms.ReturnString(value);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}


void VJSXMLHttpRequest::_GetResponseText(XBOX::VJSParms_getProperty& ioParms, XMLHttpRequest* inXhr)
{
    if(inXhr)
    {
        XBOX::VString value;
        XBOX::VError res=inXhr->GetResponseText(&value);

        //returns an empty string on error
        ioParms.ReturnString(value);
    }
    else
        XBOX::vThrowError(VE_XHRQ_JS_BINDING_ERROR);
}
