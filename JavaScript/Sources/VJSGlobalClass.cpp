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

#include "VJSContext.h"
#include "VJSValue.h"
#include "VJSClass.h"
#include "VJSRuntime_file.h"
#include "VJSRuntime_stream.h"
#include "VJSRuntime_progressIndicator.h"
#include "VJSRuntime_Image.h"
#include "VJSRuntime_Atomic.h"
#include "VJSRuntime_blob.h"
#include "VJSGlobalClass.h"
#include "VXMLHttpRequest.h"
#include "VJSEndPoint.h"
#include "VJSSystemWorker.h"
#include "VJSEvent.h"
#include "VJSWorker.h"
#include "VJSWorkerProxy.h"
#include "VJSWebStorage.h"
#include "VJSProcess.h"

#include "VJSMysqlBuffer.h"
#include "VJSTLS.h"
#include "VJSW3CFileSystem.h"
#include "VJSBuffer.h"
#include "VJSNet.h"

USING_TOOLBOX_NAMESPACE


VJSSpecifics::~VJSSpecifics()
{
	for( MapOfValues::iterator i = fMap.begin() ; i != fMap.end() ; ++i)
	{
		if (i->second.first != NULL && i->second.second != NULL)
			i->second.second( i->second.first);
	}
}


void *VJSSpecifics::Get( key_type inKey) const
{
	MapOfValues::const_iterator i = fMap.find( inKey);
	return (i != fMap.end()) ? i->second.first : NULL;
}


bool VJSSpecifics::Set( key_type inKey, void *inValue, value_destructor_type inDestructor)
{
	bool ok = true;
	MapOfValues::iterator i = fMap.find( inKey);
	if (i != fMap.end())
	{
		if (i->second.first != inValue)
			i->second.second( i->second.first);
		i->second.first = inValue;
		i->second.second = inDestructor;
	}
	else
	{
		MapOfValues::mapped_type value( inValue, inDestructor);
		fMap.insert( MapOfValues::value_type( inKey, value));
	}
	return ok;
}


//======================================================


VJSGlobalObject::VJSGlobalObject( JS4D::GlobalContextRef inContext, VJSContextGroup *inContextGroup, IJSRuntimeDelegate *inRuntimeDelegate)
: fContext( inContext)
, fContextGroup( inContextGroup)
, fRuntimeDelegate( inRuntimeDelegate)
, fIncludedFiles( false)	// path matching without diacritics
, fGarbageCollectionPending( 0)
, fLastGarbageCollection( 0)
, fContextUseTaskID( NULL_TASK_ID)
, fContextUseCount( 0)
{
}


VJSGlobalObject::~VJSGlobalObject()
{
}


VJSGlobalObject::operator VJSObject() const
{
	return VJSObject( fContext, JSContextGetGlobalObject( fContext));
}


bool VJSGlobalObject::RegisterIncludedFile( VFile *inFile)
{
	bool newlyRegistered;
	
	if (inFile != NULL)
	{
		VString path;
		inFile->GetPath( path, FPS_POSIX);

		std::pair<MapOfIncludedFile::const_iterator, bool> result = fIncludedFiles.insert( MapOfIncludedFile::value_type( path, inFile));
		if (result.second)
		{
			newlyRegistered = true;

			VTime modificationTime;
			inFile->GetTimeAttributes( &modificationTime);
			fIncludedFilesModificationTime.push_back( VectorOfFileModificationTime::value_type( inFile, modificationTime));
		}
		else
		{
			newlyRegistered = false;
		}
	}
	else
	{
		newlyRegistered = false;
	}
	return newlyRegistered;
}


bool VJSGlobalObject::IsAnIncludedFile( const VFile *inFile) const
{
	bool registered = false;
	
	if (inFile != NULL)
	{
		VString path;
		inFile->GetPath( path, FPS_POSIX);
	#if 0	// build fix MAC
		registered = (fIncludedFiles.find( path) != fIncludedFiles.end());
	#endif
	}
	return registered;
}


bool VJSGlobalObject::IsIncludedFilesHaveBeenChanged() const
{
	bool haveChanges = false;

	for (VectorOfFileModificationTime::const_iterator iter = fIncludedFilesModificationTime.begin() ; (iter != fIncludedFilesModificationTime.end()) && !haveChanges ; ++iter)
	{
		if (iter->first != NULL)
		{
			VTime modificationTime;
			iter->first->GetTimeAttributes( &modificationTime);
			haveChanges = (modificationTime.CompareTo( iter->second) != CR_EQUAL);
		}
	}
	return haveChanges;
}


void VJSGlobalObject::UseContext()
{
	VTaskLock lock( &fUseMutex);

	if (fContextUseTaskID == NULL_TASK_ID)
	{
		xbox_assert( fContextUseCount == 0);
		fContextUseTaskID = VTask::GetCurrentID();
		fContextUseCount = 1;
	}
	else if (testAssert( fContextUseTaskID == VTask::GetCurrentID()))	// can't be in use by another task
	{
		++fContextUseCount;
	}
}


void VJSGlobalObject::UnuseContext()
{
	GarbageCollectIfNecessary();

	{
		VTaskLock lock( &fUseMutex);

		xbox_assert( fContextUseTaskID == VTask::GetCurrentID());
		xbox_assert( fContextUseCount > 0);

		if (--fContextUseCount == 0)
			fContextUseTaskID = NULL_TASK_ID;
	}
}


void VJSGlobalObject::GarbageCollect()
{
	fUseMutex.Lock();
	if (fContextUseTaskID == NULL_TASK_ID)
	{
		// no one is using the context.
		// one must keep the use mutex until the end of garbage collection.
		JSGarbageCollect( fContext);
		fLastGarbageCollection = fGarbageCollectionPending;
		fUseMutex.Unlock();
	}
	else if (VTask::GetCurrentID() == fContextUseTaskID)
	{
		// the context is being used by current thread.
		// we can safely garbage collect now.
		// release the mutex early so that the cache manager can call us without latency.
		fUseMutex.Unlock();
		fLastGarbageCollection = fGarbageCollectionPending;
		JSGarbageCollect( fContext);
	}
	else
	{
		// deferred garbage collection until unuse or next c++ call from javascript
		AskDeferredGarbageCollection();
		fUseMutex.Unlock();
	}
}


void VJSGlobalObject::GarbageCollectIfNecessary()
{
	xbox_assert( VTask::GetCurrentID() == fContextUseTaskID);

	if (fLastGarbageCollection != fGarbageCollectionPending)
	{
		fLastGarbageCollection = fGarbageCollectionPending;
		JSGarbageCollect( fContext);
	}
}

XBOX::VJSObject VJSGlobalObject::Require (XBOX::VJSContext inContext, const XBOX::VString &inClassName)
{
	XBOX::VJSObject							object(inContext, NULL);
	VJSGlobalObject::SObjectMap::iterator	it;

	if ((it = fRequireMap.find(inClassName)) != fRequireMap.end()) {

		xbox_assert(it->second != NULL);
		object.SetObjectRef(it->second);

	} else {

		if (inClassName.EqualToString("net", true)) {

			VJSNetClass::Class();
			object = VJSNetClass::CreateInstance(inContext, NULL);

		} else if (inClassName.EqualToString("tls", true)) {

			VJSTLSClass::Class();
			object = VJSTLSClass::CreateInstance(inContext, NULL);

		}
 
		if (object.GetObjectRef() != NULL)

			fRequireMap[inClassName] = object.GetObjectRef();

	}

	return object;
}

//======================================================


VJSGlobalClass::VectorOfStaticFunction		VJSGlobalClass::sStaticFunctions;
VJSGlobalClass::VectorOfStaticValue			VJSGlobalClass::sStaticValues;
bool VJSGlobalClass::sGetDefinitionHasBeenCalled = false;

void VJSGlobalClass::Initialize( const VJSParms_initialize& inParms, VJSGlobalObject* inGlobalObject)
{
	xbox_assert( inGlobalObject == NULL);	// cause there's one and only one global class instance, created after the context

	// VJSGlobalClass::CreateGlobalClasses() is not called before by Wakanda Studio.

	// Add Buffer, SystemWorker, Folder, File, and TextStream object constructors.

	inParms.GetObject().SetProperty("Buffer", VJSBufferClass::MakeConstructor(inParms.GetContext()), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly); 
	inParms.GetObject().SetProperty("SystemWorker", VJSSystemWorkerClass::MakeConstructor(inParms.GetContext()), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly); 
	inParms.GetObject().SetProperty("Folder", VJSFolderIterator::MakeConstructor(inParms.GetContext()), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly); 
	inParms.GetObject().SetProperty("File", VJSFileIterator::MakeConstructor(inParms.GetContext()), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly); 
	inParms.GetObject().SetProperty("TextStream", VJSTextStream::MakeConstructor(inParms.GetContext()), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly); 
	inParms.GetObject().SetProperty("process", VJSProcess::CreateInstance( inParms.GetContext(), NULL), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly);
	inParms.GetObject().SetProperty("os", VJSOS::CreateInstance( inParms.GetContext(), NULL), JS4D::PropertyAttributeDontDelete | JS4D::PropertyAttributeReadOnly);

	// Add LocalFileSystem and LocalFileSystemSync interfaces support.

	VJSLocalFileSystem::AddInterfacesSupport(inParms.GetContext());
}

void VJSGlobalClass::Finalize( const VJSParms_finalize& inParms, VJSGlobalObject* inGlobalObject)
{
	inGlobalObject->Release();
}
	
	
void VJSGlobalClass::AddStaticFunction( const char *inName, JSObjectCallAsFunctionCallback inCallBack, JS4D::PropertyAttributes inAttributes)
{
	xbox_assert( !sGetDefinitionHasBeenCalled);	// The global class has aready been created, its too late to add static values, 
	_CheckInitStaticFunctions();
	xbox_assert( !sStaticFunctions.empty());
	_StaticFunction fn = { inName, inCallBack, inAttributes};
	#if WITH_ASSERT
	// check the name is not already registered.
	for( VectorOfStaticFunction::const_iterator i = sStaticFunctions.begin() ; i != sStaticFunctions.end() ; ++i)
		xbox_assert( (i->name == NULL) || (strcmp( i->name, inName) != 0) );
	#endif
	sStaticFunctions.insert( sStaticFunctions.end() - 1, fn);	// leave trailing {0} marker at the end
}


void VJSGlobalClass::AddStaticValue( const char *inName, JSObjectGetPropertyCallback inGetCallback, JSObjectSetPropertyCallback inSetCallback, JS4D::PropertyAttributes inAttributes)
{
	xbox_assert( !sGetDefinitionHasBeenCalled);	// The global class has aready been created, its too late to add static values, 
	_CheckInitStaticValues();
	xbox_assert( !sStaticValues.empty());
	_StaticValue value = { inName, inGetCallback, inSetCallback, inAttributes};
	#if WITH_ASSERT
	// check the name is not already registered.
	for( VectorOfStaticValue::const_iterator i = sStaticValues.begin() ; i != sStaticValues.end() ; ++i)
		xbox_assert( (i->name == NULL) || (strcmp( i->name, inName) != 0) );
	#endif
	sStaticValues.insert( sStaticValues.end() - 1, value);	// leave trailing {0} marker at the end
}


void VJSGlobalClass::AddConstructorObjectStaticValue( const char *inClassName, JSObjectCallAsConstructorCallback inCallback)
{
	if (JS4D::RegisterConstructor( inClassName, inCallback))
	{
		AddStaticValue( inClassName, JS4D::GetConstructorObject, NULL, kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete | kJSPropertyAttributeDontEnum);
	}
}
	
	
void VJSGlobalClass::_PushBackStaticFunction( const char *inName, JSObjectCallAsFunctionCallback inCallBack, JS4D::PropertyAttributes inAttributes)
{
	_StaticFunction fn = { inName, inCallBack, inAttributes};
	sStaticFunctions.push_back( fn);
}
	
	
void VJSGlobalClass::_PushBackStaticValue( const char *inName, JSObjectGetPropertyCallback inGetCallback, JSObjectSetPropertyCallback inSetCallback, JS4D::PropertyAttributes inAttributes)
{
	_StaticValue value = { inName, inGetCallback, inSetCallback, inAttributes};
	sStaticValues.push_back( value);
}

		
void VJSGlobalClass::GetDefinition( ClassDefinition& outDefinition)
{
	_CheckInitStaticFunctions();
	_CheckInitStaticValues();
	outDefinition.className = "Component";
	outDefinition.initialize = js_initialize<Initialize>;
	outDefinition.finalize = js_finalize<Finalize>;
	outDefinition.staticFunctions = (JSStaticFunction*) &sStaticFunctions.at(0);
	outDefinition.staticValues = (JSStaticValue*) &sStaticValues.at(0);
	outDefinition.attributes = JS4D::ClassAttributeNoAutomaticPrototype;	// important because that's the global object class. Else JS will screw up in JSObjectHasProperty in infinite loop.
	sGetDefinitionHasBeenCalled = true;
}


void VJSGlobalClass::_CheckInitStaticFunctions()
{
	if (sStaticFunctions.empty())
	{
		assert_compile( sizeof( _StaticFunction) == sizeof( JSStaticFunction));

		//** TODO: Garder ? Mozilla propose dump() avec même comportement.
		_PushBackStaticFunction( "trace", js_callStaticFunction<do_trace>, JS4D::PropertyAttributeNone);

		//_PushBackStaticFunction( "testMe", js_callStaticFunction<do_testMe>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction( "include", js_callStaticFunction<do_include>, JS4D::PropertyAttributeNone);
//		_PushBackStaticFunction( "Folder", js_callStaticFunction<do_Folder>, JS4D::PropertyAttributeNone); // Folder(string : file URL)
//		_PushBackStaticFunction( "File", js_callStaticFunction<do_File>, JS4D::PropertyAttributeNone); // File(string : file URL | ( Folder : folder , string : filename) )
		_PushBackStaticFunction( "ProgressIndicator", js_callStaticFunction<do_ProgressIndicator>, JS4D::PropertyAttributeNone); // ProgressIndicator( { number : maxvalue, string : sessiontext, bool : caninterrupt = false, string : windowtitle, string : progressInfo })
		_PushBackStaticFunction( "getProgressIndicator", js_callStaticFunction<do_GetProgressIndicator>, JS4D::PropertyAttributeNone); // getProgressIndicator( string : progressInfo })
		_PushBackStaticFunction( "BinaryStream", js_callStaticFunction<do_BinaryStream>, JS4D::PropertyAttributeNone); // BinaryStream : BinaryStream( File | stringURL, bool | "Write" "Read");
//		_PushBackStaticFunction( "TextStream", js_callStaticFunction<do_TextStream>, JS4D::PropertyAttributeNone); // TextStream : TextStream( File | stringURL, bool | "Write" "Read", {number:charset});
		_PushBackStaticFunction( "displayNotification", js_callStaticFunction<do_displayNotification>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction( "XmlToJSON", js_callStaticFunction<do_XmlToJSON>, JS4D::PropertyAttributeNone); // string : XmlToJSON( string : XML text, "simpleJson" "fullJson");
		_PushBackStaticFunction( "JSONToXml", js_callStaticFunction<do_JSONToXml>, JS4D::PropertyAttributeNone); // string : JSONToXml( string : JSON text, "simpleJson" "fullJson");
		_PushBackStaticFunction( "loadImage", js_callStaticFunction<do_loadImage>, JS4D::PropertyAttributeNone); // Picture : LoadImage(File)
		_PushBackStaticFunction( "garbageCollect", js_callStaticFunction<do_garbageCollect>, JS4D::PropertyAttributeNone); 
		_PushBackStaticFunction( "isoToDate", js_callStaticFunction<do_isoToDate>, JS4D::PropertyAttributeNone); 
		_PushBackStaticFunction( "dateToIso", js_callStaticFunction<do_dateToIso>, JS4D::PropertyAttributeNone); 
		_PushBackStaticFunction( "Mutex", js_callStaticFunction<do_AtomicSection>, JS4D::PropertyAttributeNone); 
		_PushBackStaticFunction( "SyncEvent", js_callStaticFunction<do_SyncEvent>, JS4D::PropertyAttributeNone); // object : System ( string : commandLine );
		_PushBackStaticFunction( "loadText", js_callStaticFunction<do_LoadText>, JS4D::PropertyAttributeNone); // string : loadText(File, {number:charset})
		_PushBackStaticFunction( "saveText", js_callStaticFunction<do_SaveText>, JS4D::PropertyAttributeNone); // saveText(text: string, File, {number:charset})
		_PushBackStaticFunction( "getURLPath", js_callStaticFunction<do_GetURLPath>, JS4D::PropertyAttributeNone); // array of string : getURLPath(string:url)
		_PushBackStaticFunction( "getURLQuery", js_callStaticFunction<do_GetURLQuery>, JS4D::PropertyAttributeNone); // object : getURLQuery(string:url)
		
		_PushBackStaticFunction( "generateUUID", js_callStaticFunction<do_GenerateUUID>, JS4D::PropertyAttributeNone); // string : generateUUID();
		_PushBackStaticFunction( "requireNative", js_callStaticFunction<do_Require>, JS4D::PropertyAttributeNone);			// object : require(string:className);

		_PushBackStaticFunction("setTimeout", js_callStaticFunction<VJSTimer::SetTimeout>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction("clearTimeout", js_callStaticFunction<VJSTimer::ClearTimeout>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction("setInterval", js_callStaticFunction<VJSTimer::SetInterval>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction("clearInterval", js_callStaticFunction<VJSTimer::ClearInterval>, JS4D::PropertyAttributeNone);

		_PushBackStaticFunction("close", js_callStaticFunction<VJSWorker::Close>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction("wait", js_callStaticFunction<VJSWorker::Wait>, JS4D::PropertyAttributeNone);
		_PushBackStaticFunction("exitWait", js_callStaticFunction<VJSWorker::ExitWait>, JS4D::PropertyAttributeNone);

		//_PushBackStaticFunction("SetCurrentUser", js_callStaticFunction<do_SetCurrentUser>, JS4D::PropertyAttributeNone);

		_PushBackStaticFunction( 0, 0, 0);
	}
}


void VJSGlobalClass::_CheckInitStaticValues()
{
	if (sStaticValues.empty())
	{
		assert_compile( sizeof( _StaticValue) == sizeof( JSStaticValue));
		_PushBackStaticValue( 0, 0, 0, 0);
	}
}


void VJSGlobalClass::CreateGlobalClasses()
{
	VJSTextStream::Class();
	VJSStream::Class();
	VJSProgressIndicator::Class();
	VJSFolderIterator::Class();
	VJSFileIterator::Class();
	VJSBlob::Class();
	AddConstructorObjectStaticValue("Blob", VJSParms_construct::Callback<VJSBlob::Construct>);
	
	VJSXMLHttpRequest::Class();
	AddConstructorObjectStaticValue("XMLHttpRequest", VJSParms_construct::Callback<VJSXMLHttpRequest::Construct>);

	VJSEndPoint::Class();
	AddConstructorObjectStaticValue("EndPoint", VJSParms_construct::Callback<VJSEndPoint::Construct>);

	VJSDedicatedWorkerClass::Class();
	AddConstructorObjectStaticValue("Worker", VJSParms_construct::Callback<VJSDedicatedWorkerClass::Construct>);

	VJSSharedWorkerClass::Class();
	AddConstructorObjectStaticValue("SharedWorker", VJSParms_construct::Callback<VJSSharedWorkerClass::Construct>);	

	VJSMysqlBufferClass::Class();
	AddConstructorObjectStaticValue("MysqlBuffer", VJSParms_construct::Callback<VJSMysqlBufferClass::Construct>);

	VJSStorageClass::Class();
	VJSBufferClass::Class();

}



void VJSGlobalClass::do_SetCurrentUser( VJSParms_callStaticFunction& inParms, VJSGlobalObject*)
{
	VString username;
	inParms.GetStringParam(1, username);
	if (!username.IsEmpty())
	{
		// a faire ici ou dans VJSSolution ?
	}
}


void VJSGlobalClass::do_trace( VJSParms_callStaticFunction& inParms, VJSGlobalObject*)
{
	size_t count = inParms.CountParams();
	for( size_t i = 1 ; i <= count ; ++i)	
	{
		VString msg;
		bool ok = inParms.GetStringParam( i, msg);
		if (!ok)
			break;
		VDebugMgr::Get()->DebugMsg( msg);
	}
}


void VJSGlobalClass::do_displayNotification( VJSParms_callStaticFunction& inParms, VJSGlobalObject*)
{
	VString message;
	inParms.GetStringParam( 1, message);
	
	VString title( "Notification");
	inParms.GetStringParam( 2, title);
	
	bool critical = false;
	inParms.GetBoolParam( 3, &critical);

	VSystem::DisplayNotification( title, message, critical ? EDN_StyleCritical : 0);
}


void VJSGlobalClass::do_include( VJSParms_callStaticFunction& inParms, VJSGlobalObject *inGlobalObject)
{
	// EvaluateScript() uses NULL (this is default) as inThisObject argument. 
	// This works fine currently, but would it be better (if any problem) to pass global (application) object instead? 
	// See WAK0074064.

	VFile* file = inParms.RetainFileParam(1, false);
	if (file != NULL)
	{
		bool newlyRegistered = inGlobalObject->RegisterIncludedFile( file);		// sc 15/06/2010 the file must be registered
		if (inParms.GetBoolParam(2,"refresh","auto") || newlyRegistered)
		{
			inParms.GetContext().EvaluateScript( file, NULL, inParms.GetExceptionRefPointer());
		}
		file->Release();
	}
	else
	{
		VString pathname;
		if (inParms.IsStringParam(1) && inParms.GetStringParam( 1, pathname))
		{
			VFolder* folder = inGlobalObject->GetRuntimeDelegate()->RetainScriptsFolder();
			if (folder != NULL)
			{
				file = new VFile( *folder, pathname, FPS_POSIX);
				
				bool newlyRegistered = inGlobalObject->RegisterIncludedFile( file);		// sc 15/06/2010 the file must be registered
				if (inParms.GetBoolParam(2,"refresh","auto") || newlyRegistered)
				{
					inParms.GetContext().EvaluateScript( file, NULL, inParms.GetExceptionRefPointer());
				}
				ReleaseRefCountable( &file);
			}
			ReleaseRefCountable( &folder);
		}
		else
			vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_FILE, "1");
	}
}


void VJSGlobalClass::do_testMe( VJSParms_callStaticFunction& inParms, VJSGlobalObject*)
{
	VJSObject globalObject( inParms.GetContext().GetGlobalObject());
	
	VString functionName;
	inParms.GetStringParam( 1, functionName);
	
	std::vector<VJSValue> values;
	size_t count = inParms.CountParams();
	for( size_t i = 2 ; i <= count ; ++i)
		values.push_back( inParms.GetParamValue( i));

	VJSValue result( inParms.GetContextRef());
	bool ok = globalObject.CallMemberFunction( functionName, &values, &result, inParms.GetExceptionRefPointer());
	inParms.ReturnValue( result);
}

void VJSGlobalClass::do_GetProgressIndicator(VJSParms_callStaticFunction& ioParms, VJSGlobalObject *inContext)
{
	if (ioParms.IsStringParam(1))
	{
		VString userinfo;
		ioParms.GetStringParam(1, userinfo);
		VProgressIndicator* progress = VProgressManager::RetainProgressIndicator(userinfo);
		if (progress != nil)
			ioParms.ReturnValue(VJSProgressIndicator::CreateInstance(ioParms.GetContextRef(), progress));
		QuickReleaseRefCountable(progress);
	}
}

	
void VJSGlobalClass::do_ProgressIndicator(VJSParms_callStaticFunction& ioParms, VJSGlobalObject *inContext)
{
	VString sessiontile;
	VString windowtile, userinfo;
	bool caninterrupt = false;
	Real maxvalue = -1;
	VError err = VE_OK;
	
	if (ioParms.IsNumberParam(1))
		ioParms.GetRealParam(1, &maxvalue);
	else
		err = vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_NUMBER, "1");

	if (err == VE_OK )
	{
		if (ioParms.IsStringParam(2))
			ioParms.GetStringParam(2, sessiontile);
		else
			err = vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
	}

	caninterrupt = ioParms.GetBoolParam( 3, L"Can Interrupt", "Cannot Interrupt");

		
	//VProgressIndicator* progress = inContext->GetRuntimeDelegate()->CreateProgressIndicator( windowtile);

	if (err == VE_OK)
	{
		VProgressIndicator* progress = new VProgressIndicator();
		if (progress != NULL)
		{
			if (ioParms.CountParams() >= 4)
			{
				if (ioParms.IsStringParam(4))
				{
					ioParms.GetStringParam(4, windowtile);
					progress->SetTitle(windowtile);
				}
				else
					vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "4");
			}
			if (ioParms.CountParams() >= 5)
			{
				if (ioParms.IsStringParam(5))
				{
					ioParms.GetStringParam(5, userinfo);
					progress->SetUserInfo(userinfo);
				}
				else
					vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "5");
			}
			progress->BeginSession((sLONG8)maxvalue, sessiontile, caninterrupt);
		}
		ioParms.ReturnValue(VJSProgressIndicator::CreateInstance(ioParms.GetContextRef(), progress));
		
		ReleaseRefCountable( &progress);
	}
	else
		ioParms.ReturnNullValue();	
}	


void VJSGlobalClass::do_BinaryStream(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VJSStream::do_BinaryStream( ioParms);
}

void VJSGlobalClass::do_XmlToJSON(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString xml;
	VString json, root;
	bool simpleJSON = false;

	ioParms.GetStringParam( 1, xml);
	if ( ioParms.CountParams() >= 2)
	{
		VString s;
		if (ioParms.IsStringParam(2))
		{
			ioParms.GetStringParam(2, s);
			if(s== "json-bag")
			{
				simpleJSON = true;
				root="bag";
				if(ioParms.CountParams() >= 3)
				{
					if (ioParms.IsStringParam(3))
						ioParms.GetStringParam(3, root);
					else
						vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "3");
				}
			}
		}
		else
			vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
	}

	if (simpleJSON)
	{
		XBOX::VValueBag *bag = new XBOX::VValueBag;

		VError err = LoadBagFromXML( xml, root, *bag);
		if(err == VE_OK)
			bag->GetJSONString(json);
		ReleaseRefCountable( &bag); 			
	}
	else
	{
		VString errorMessage; sLONG lineNumber;
		VError err = VXMLJsonUtility::XMLToJson(xml, json, errorMessage, lineNumber);
	}

	ioParms.ReturnString(json);
}

void VJSGlobalClass::do_JSONToXml(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString xml;
	VString json, root;
	bool simpleJSON = false;

	ioParms.GetStringParam( 1, json);
	if ( ioParms.CountParams() >= 2)
	{
		VString s;
		if (ioParms.IsStringParam(2))
		{
			ioParms.GetStringParam(2, s);
			if(s== "json-bag")
			{
				simpleJSON = true;
				root="bag";
				if(ioParms.CountParams() >= 3)
				{
					if (ioParms.IsStringParam(3))
						ioParms.GetStringParam(3, root);
					else
						vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "3");
				}
			}
		}
		else
			vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
	}

	if (simpleJSON)
	{
		XBOX::VValueBag *bag = new XBOX::VValueBag;
		bag->FromJSONString(json);
		
		xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
		bag->DumpXML( xml, root, false);
		ReleaseRefCountable( &bag); 			
	}
	else
	{
		VError err = VXMLJsonUtility::JsonToXML(json, xml);
	}

	ioParms.ReturnString(xml);
}


void VJSGlobalClass::do_loadImage(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	bool okloaded = false;

#if !VERSION_LINUX   // Postponed Linux Implementation !

	VFile* file = ioParms.RetainFileParam(1);

	if (file != nil)
	{
		if (file->Exists())
		{
			XBOX::VPicture* pict = new XBOX::VPicture(*file, false);
			/*
			VJSPictureContainer* pictContains = new VJSPictureContainer(pict, true, ioParms.GetContextRef());
			ioParms.ReturnValue(VJSImage::CreateInstance(ioParms.GetContextRef(), pictContains));
			pictContains->Release();
			*/
			if (pict != NULL)
			{
				okloaded = true;
				ioParms.ReturnVValue(*pict);
				delete pict;
			}
		}
		file->Release();
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_FILE, "1");

#endif

	if (!okloaded)
		ioParms.ReturnNullValue();

}


void VJSGlobalClass::do_garbageCollect(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	ioParms.GetContext().GarbageCollect();
}


void VJSGlobalClass::do_isoToDate(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VTime dd;
	VString s;
	ioParms.GetStringParam(1, s);
	dd.FromJSONString(s);
	ioParms.ReturnTime(dd);
}

void VJSGlobalClass::do_dateToIso(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	VJSValue jsval(ioParms.GetContextRef());
	if (ioParms.CountParams() > 0)
	{
		VTime dd;
		jsval = ioParms.GetParamValue(1);
		jsval.GetTime(dd);
		dd.GetJSONString(s);
	}
	ioParms.ReturnString(s);
}



void VJSGlobalClass::do_AtomicSection(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	bool okresult = false;
	if (ioParms.IsStringParam(1))
	{
		ioParms.GetStringParam(1, s);
		if (!s.IsEmpty())
		{
			jsAtomicSection* atomsec = jsAtomicSection::RetainAtomicSection(s);
			if (atomsec != nil)
			{
				okresult = true;
				ioParms.ReturnValue(VJSAtomicSection::CreateInstance(ioParms.GetContextRef(), atomsec));
				atomsec->Release();
			}
		}
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
	if (!okresult)
		ioParms.ReturnNullValue();
}


void VJSGlobalClass::do_SyncEvent(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	bool okresult = false;
	if (ioParms.IsStringParam(1))
	{	ioParms.GetStringParam(1, s);
		if (!s.IsEmpty())
		{
			jsSyncEvent* sync = jsSyncEvent::RetainSyncEvent(s);
			if (sync != nil)
			{
				okresult = true;
				ioParms.ReturnValue(VJSSyncEvent::CreateInstance(ioParms.GetContextRef(), sync));
				sync->Release();
			}
		}
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
	if (!okresult)
		ioParms.ReturnNullValue();
}

void VJSGlobalClass::do_LoadText(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	bool okloaded = false;

	VFile* file = ioParms.RetainFileParam(1);

	if (file != nil)
	{
		if (file->Exists())
		{
			VFileStream inp(file);
			VError err = inp.OpenReading();
			if (err == VE_OK)
			{
				CharSet defaultCharSet = VTC_UTF_8;
				sLONG xcharset = 0;
				if (ioParms.GetLongParam(2, &xcharset) && xcharset != 0)
					defaultCharSet = (CharSet)xcharset;
				inp.GuessCharSetFromLeadingBytes(defaultCharSet );
				inp.SetCarriageReturnMode( eCRM_NATIVE );
				VString s;
				err = inp.GetText(s);
				if (err == VE_OK)
				{
					ioParms.ReturnString(s);
					okloaded = true;
				}
				inp.CloseReading();
			}
			
			
		}
		file->Release();
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_FILE, "1");

	if (!okloaded)
		ioParms.ReturnNullValue();

}


void VJSGlobalClass::do_SaveText(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	if (ioParms.IsStringParam(1))
	{
		if (ioParms.GetStringParam(1, s))
		{
			VFile* file = ioParms.RetainFileParam(2);
			if (file != nil)
			{
				VError err = VE_OK;
				if (file->Exists())
					err = file->Delete();
				if (err == VE_OK)
					err = file->Create();
				if (err == VE_OK)
				{
					CharSet defaultCharSet = VTC_UTF_8;
					sLONG xcharset = 0;
					if (ioParms.GetLongParam(3, &xcharset) && xcharset != 0)
						defaultCharSet = (CharSet)xcharset;
					VFileStream inp(file);
					err = inp.OpenWriting();
					if (err == VE_OK)
					{
						inp.SetCharSet(defaultCharSet);
						inp.PutText(s);
						inp.CloseWriting();
					}
				}
			}
			else
				vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_FILE, "2");
		}
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
}


void VJSGlobalClass::do_GetURLPath(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	if (ioParms.IsStringParam(1))
	{
		if (ioParms.GetStringParam(1, s))
		{
			VFullURL url(s);
			VJSArray result(ioParms.GetContextRef());
			const VString* next = url.NextPart();
			while (next != NULL)
			{
				VJSValue elem(ioParms.GetContextRef());
				elem.SetString(*next);
				result.PushValue(elem);
				next = url.NextPart();
			}
			ioParms.ReturnValue(result);
		}
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
}


void VJSGlobalClass::do_GetURLQuery(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VString s;
	if (ioParms.IsStringParam(1) && ioParms.GetStringParam(1, s))
	{
		VFullURL url(s);
		VJSObject result(ioParms.GetContextRef());
		result.MakeEmpty();
		const VValueBag& values = url.GetValues();
		VIndex nbvalues = values.GetAttributesCount();
		for (VIndex i = 1; i <= nbvalues; i++)
		{
			VString attName;
			const VValueSingle* cv = values.GetNthAttribute(i, &attName);
			if (cv != nil)
			{
				result.SetProperty(attName, cv);
			}
		}
		ioParms.ReturnValue(result);
	}
	else
		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
}

void VJSGlobalClass::do_GenerateUUID(VJSParms_callStaticFunction& ioParms, VJSGlobalObject*)
{
	VUUID		theUUID(true);
	VString		uuidStr;
	
	theUUID.GetString(uuidStr);
	ioParms.ReturnString(uuidStr);
}


void VJSGlobalClass::do_Require(VJSParms_callStaticFunction& ioParms, VJSGlobalObject *inGlobalObject)
{
	xbox_assert(inGlobalObject != NULL);

	XBOX::VString	className;

	if (ioParms.CountParams() != 1 || !ioParms.IsStringParam(1) || !ioParms.GetStringParam(1, className))

		vThrowError(VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");

	else

		ioParms.ReturnValue(inGlobalObject->Require(ioParms.GetContext(), className));
}