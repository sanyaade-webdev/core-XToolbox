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

#include "VJSSystemWorker.h"

#include "VJSClass.h"
#include "VJSGlobalClass.h"
#include "VJSEvent.h"
#include "VJSRuntime_stream.h"
#include "VJSBuffer.h"

USING_TOOLBOX_NAMESPACE

sLONG	VJSSystemWorker::sNumberRunning;
	
bool VJSSystemWorker::IsRunning (bool inWaitForStartup) 
{
	bool	isOk;

	for ( ; ; ) {

		sLONG	startupStatus;

		if ((startupStatus = XBOX::VInterlocked::AtomicGet((sLONG *) &fStartupStatus))) {
			
			if (startupStatus > 0) {

				// External process has started properly, but is it still running?

				isOk = !fIsTerminated;

			} else
				
				isOk = false;

			break;

		} else {
			
			// Hasn't been launched yet, wait if needed .

			if (inWaitForStartup) 
	
				XBOX::VTask::Sleep(kPollingInterval);

			else {

				// Hasn't start yet, so can't have failed yet!

				isOk = true;
				break;

			}

		}

	}

	return isOk;
}

void VJSSystemWorker::Kill ()
{
	XBOX::StLocker<XBOX::VCriticalSection>	lock(&fCriticalSection);	

	if (!fIsTerminated && fProcessLauncher.IsRunning()) {
		
		fProcessLauncher.Shutdown(true);
		fForcedTermination = fIsTerminated = true;	// _DoRun() will quit.
		
	}
}

bool VJSSystemWorker::MatchEvent (const IJSEvent *inEvent, sLONG inEventType)
{
	xbox_assert(inEvent != NULL);

	if (inEvent->GetType() != IJSEvent::eTYPE_SYSTEM_WORKER) 

		return false;

	else {
		
		VJSSystemWorkerEvent	*systemWorkerEvent;

		systemWorkerEvent = (VJSSystemWorkerEvent *) inEvent;
		return systemWorkerEvent->GetSystemWorker() && systemWorkerEvent->GetSubType() == inEventType;

	}
}

VJSSystemWorker::VJSSystemWorker (const XBOX::VString &inCommandLine, const XBOX::VString &inFolderPath, VJSWorker *inWorker)
{
	xbox_assert(inWorker != NULL);

	fWorker = inWorker;

	fCommandLine = inCommandLine;
	
	fProcessLauncher.CreateArgumentsFromFullCommandLine(inCommandLine);
	fProcessLauncher.SetWaitClosingChildProcess(false);
	fProcessLauncher.SetRedirectStandardInput(true);
	fProcessLauncher.SetRedirectStandardOutput(true);

	if (inFolderPath.GetLength()) {

		// On Mac platform, convert POSIX path to HFS.

#if VERSIONMAC == 1

		XBOX::VFilePath	path;

		path.FromFullPath(inFolderPath, XBOX::FPS_POSIX);
		fProcessLauncher.SetDefaultDirectory(path.GetPath());
		
#else
		
		fProcessLauncher.SetDefaultDirectory(inFolderPath);
		
#endif
	
	}
	
#if VERSIONWIN
	
	fProcessLauncher.WIN_DontDisplayWindow();
	fProcessLauncher.WIN_CanGetExitStatus();
	
#endif

	fVTask = new XBOX::VTask(NULL, 0, XBOX::eTaskStylePreemptive, VJSSystemWorker::_RunProc);
	fVTask->SetKindData((sLONG_PTR) this);

	fTerminationRequested = fPanicTermination = fForcedTermination = fIsTerminated = fTerminationEventDiscarded = false;
	fStartupStatus = 0;

	fExitStatus = 0;

#if VERSIONMAC || VERSION_LINUX

	fPID = -1;

#endif
}

VJSSystemWorker::~VJSSystemWorker ()
{
	xbox_assert(fVTask == NULL);
}

void VJSSystemWorker::_DoRun ()
{
	// WAK0073145: Object finalization can be called even before external processed launcher had 
	// a chance to run. This can happen if the SystemWorker is created at end of a script and there 
	// is no wait(). It is destroyed immediately. That is why a retain() is called before the vtask
	// run(). Note that VJSSystemWorker may be freed after script's context is freed.

	if (fPanicTermination) {

		// Script already terminated, don't even bother to launch.

		fVTask = NULL;
		Release();
		return;

	}

	sLONG	startingStatus;

	startingStatus = !fProcessLauncher.Start() ? +1 : -1;
	XBOX::VInterlocked::Exchange((sLONG *) &fStartupStatus, startingStatus);
 
	if (startingStatus < 0) 

		fIsTerminated = true;	// Launched failed!

	else {
		
#if VERSIONMAC || VERSION_LINUX
		
		fPID = fProcessLauncher.GetPid();
		
#endif
		
		XBOX::VInterlocked::Increment((sLONG *) &VJSSystemWorker::sNumberRunning);

		uBYTE	*readBuffer;

		readBuffer = (uBYTE *) ::malloc(kBufferSize);
		xbox_assert(readBuffer != NULL);

		while (!fIsTerminated) {

			bool	hasProducedData;
			sLONG	size;
			
			hasProducedData	= false;
				
			fCriticalSection.Lock();		

			// Data on stdout?
			
			if (!fPanicTermination 
			&& (size = fProcessLauncher.ReadFromChild((char *) readBuffer, kBufferSize - 1)) > 0) {

				fCriticalSection.Unlock();

				readBuffer[size] = '\0';
				fWorker->QueueEvent(VJSSystemWorkerEvent::Create(this, VJSSystemWorkerEvent::eTYPE_STDOUT_DATA, fThis, readBuffer, size));
				hasProducedData = true;

				readBuffer = (uBYTE *) ::malloc(kBufferSize);
				xbox_assert(readBuffer != NULL);
				
				fCriticalSection.Lock();

			}
			
			// Data on stderr?

			if (!fPanicTermination 
			&& (size = fProcessLauncher.ReadErrorFromChild((char *) readBuffer, kBufferSize - 1)) > 0) {

				fCriticalSection.Unlock();
				
				readBuffer[size] = '\0';
				fWorker->QueueEvent(VJSSystemWorkerEvent::Create(this, VJSSystemWorkerEvent::eTYPE_STDERR_DATA, fThis, readBuffer, size));
				hasProducedData = true;

				readBuffer = (uBYTE *) ::malloc(kBufferSize);
				xbox_assert(readBuffer != NULL);
				
				fCriticalSection.Lock();
							
			}
			
			// Check for termination condition. 
			
			if (fVTask->GetState() == XBOX::TS_DYING || fTerminationRequested || fPanicTermination) {
							
				fProcessLauncher.Shutdown(true);

				fCriticalSection.Unlock();

				fIsTerminated = fForcedTermination = true;
				break;
					
			} else if (!hasProducedData) {
			
				// Check external process termination only if no data has been produced.
				// This will ensure that data is fully read. IsRunning() will clean-up
				// the terminated process, and any data not yet read with it.
			
				if (!fProcessLauncher.IsRunning()) {
				
					fCriticalSection.Unlock();

					fForcedTermination = false;
					fIsTerminated = true;				

					break;

				} else {
				
					// No data, sleep then poll later.
				
					fCriticalSection.Unlock();
					XBOX::VTask::Sleep(kPollingInterval);
				
				}
			
			} else {
			
				// Data has been produced, just yield. Try to flush read buffers. 
				
				fCriticalSection.Unlock();
				XBOX::VTask::Yield();

			}

		}

		::free(readBuffer);
				
		XBOX::VInterlocked::Decrement((sLONG *) &VJSSystemWorker::sNumberRunning);
		
		if (!fForcedTermination && !fPanicTermination) {

			fCriticalSection.Lock();
			fExitStatus = fProcessLauncher.GetExitStatus();	
			fCriticalSection.Unlock();

		}
		
	}

	// If not a "panic" termination, trigger an event.

	if (!fPanicTermination) {

		VJSSystemWorkerEvent::STerminationData	*data = new VJSSystemWorkerEvent::STerminationData();

		data->fHasStarted = fStartupStatus > 0;
		data->fForcedTermination = fForcedTermination;
		data->fExitStatus = fExitStatus;

#if VERSIONMAC || VERSION_LINUX 

		data->fPID = fPID;

#endif

		fWorker->QueueEvent(VJSSystemWorkerEvent::Create(this, VJSSystemWorkerEvent::eTYPE_TERMINATION, fThis, (uBYTE *) data, sizeof(*data)));
			
	}

	fVTask = NULL;

	Release();
}

sLONG VJSSystemWorker::_RunProc (XBOX::VTask *inVTask)
{
	((VJSSystemWorker *) inVTask->GetKindData())->_DoRun();	
	return 0;
}

void VJSSystemWorkerClass::GetDefinition (ClassDefinition &outDefinition)
{
    static inherited::StaticFunction functions[] =
	{
		{ "postMessage",		js_callStaticFunction<_postMessage>,		JS4D::PropertyAttributeDontDelete	},
		{ "endOfInput",			js_callStaticFunction<_endOfInput>,			JS4D::PropertyAttributeDontDelete	},
		{ "terminate",			js_callStaticFunction<_terminate>,			JS4D::PropertyAttributeDontDelete	},	
		{ "getInfos",			js_callStaticFunction<_getInfos>,			JS4D::PropertyAttributeDontDelete	},
		{ "getNumberRunning",	js_callStaticFunction<_getNumberRunning>,	JS4D::PropertyAttributeDontDelete	},
		{ "setBinary",			js_callStaticFunction<_setBinary>,			JS4D::PropertyAttributeDontDelete	},
		{ "wait",				js_callStaticFunction<_wait>,				JS4D::PropertyAttributeDontDelete	},
		{ 0, 0, 0 },
	};
			
    outDefinition.className			= "SystemWorker";
    outDefinition.staticFunctions	= functions;
    outDefinition.finalize			= js_finalize<_Finalize>;
}

XBOX::VJSObject VJSSystemWorkerClass::MakeConstructor (XBOX::VJSContext inContext)
{
	XBOX::VJSObject	constructor(inContext);

	constructor.MakeConstructor(Class(), js_constructor<_Construct>);

	XBOX::VJSObject	execObject(inContext);

	execObject.MakeCallback(XBOX::js_callback<void, VJSSystemWorkerClass::_Exec>);
	constructor.SetProperty("exec", execObject);

	return constructor;
}

void VJSSystemWorkerClass::_Construct (XBOX::VJSParms_callAsConstructor &ioParms)
{	
	XBOX::VString	commandLine;
	XBOX::VString	folderPath;
	bool			isOk;			
	
	if (!ioParms.CountParams() || !ioParms.GetStringParam(1, commandLine)) {
		
		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
		isOk = false;
				
	} else if (ioParms.CountParams() >= 2 && !ioParms.GetStringParam(2, folderPath)) {
	
		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
		isOk = false;

	} else 

		isOk = true;

	if (isOk) {
 
		VJSSystemWorker	*systemWorker;

		systemWorker = new VJSSystemWorker(commandLine, folderPath, VJSWorker::GetWorker(ioParms.GetContext()));

		XBOX::VJSObject	constructedObject = VJSSystemWorkerClass::CreateInstance(ioParms.GetContext(), systemWorker);

		systemWorker->fThis = constructedObject.GetObjectRef();
		
		systemWorker->Retain();	// WAK0073145: See comment at start of VJSSystemWorker::_DoRun().

		systemWorker->fVTask->Run();

		ioParms.ReturnConstructedObject(constructedObject);	
		
	} else

		ioParms.ReturnUndefined();
}

void VJSSystemWorkerClass::_Finalize (const XBOX::VJSParms_finalize &inParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	// Garbage collection of SystemWorker object. If not terminated already, force quit.

	inSystemWorker->fCriticalSection.Lock();
	inSystemWorker->fPanicTermination = true;	// Prevent generation of a termination event.
	inSystemWorker->fCriticalSection.Unlock();

	inSystemWorker->Kill();		
	inSystemWorker->Release();
}

void VJSSystemWorkerClass::_postMessage (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	ioParms.ReturnNumber(0);

	if (!inSystemWorker->IsRunning(true))

		return;

	if (!ioParms.CountParams())
		
		return;
	
	XBOX::VString	string;

	bool			isBuffer	= false;
	VJSBufferObject	*bufferObject;
		
	if (ioParms.IsStringParam(1)) {
		
		if (!ioParms.GetStringParam(1, string)) 
		
			return;		
				
	} else if (ioParms.IsObjectParam(1)) {
		
		XBOX::VJSObject	object(ioParms.GetContext());
		
		if (!ioParms.GetParamObject(1, object)) {
			
			XBOX::vThrowError(XBOX::VE_INVALID_PARAMETER);
			return;
			
		}

		if (object.IsInstanceOf("BinaryStream")) {
			
			XBOX::VStream	*stream;		
		 
			stream = object.GetPrivateData<VJSStream>();
			stream->GetText(string);

		} else if (object.IsOfClass(VJSBufferClass::Class())) {
	
			isBuffer = true;

			bufferObject = object.GetPrivateData<VJSBufferClass>();
			xbox_assert(bufferObject != NULL);

		}
		
	}

#define SLICE_SIZE	1024
#define BUFFER_SIZE	(2 * SLICE_SIZE + 1)

	VSize	totalWritten = 0;

	if (!isBuffer) {
	
		sLONG	stringIndex, stringLength;

		stringIndex = 1;
		stringLength = string.GetLength();
			
		while (stringLength) {
		
			XBOX::VString	subString;
			char			buffer[BUFFER_SIZE];
			sLONG			size;

			size = stringLength > SLICE_SIZE ? SLICE_SIZE : stringLength;
			string.GetSubString(stringIndex, size, subString);
			stringIndex += size;
			stringLength -= size;
			
			if ((size = (sLONG) subString.ToBlock(buffer, BUFFER_SIZE, XBOX::VTC_StdLib_char, true, false)) > 0) {
		
				inSystemWorker->fCriticalSection.Lock();
				if ((size = inSystemWorker->fProcessLauncher.WriteToChild(buffer, size)) < 0) {
				
					totalWritten = 0;
					break;
				
				} else 
					
					totalWritten += size;

				inSystemWorker->fCriticalSection.Unlock();
				
			}
			
		}

	} else {

		VSize	totalSize, size;
		uBYTE	*p;
		
		totalSize = bufferObject->GetDataSize();
		p = (uBYTE *) bufferObject->GetDataPtr();
		while (totalWritten < totalSize) {

			size = totalSize - totalWritten > SLICE_SIZE ? SLICE_SIZE : totalSize - totalWritten;

			inSystemWorker->fCriticalSection.Lock();
			if (inSystemWorker->fProcessLauncher.WriteToChild(p, (uLONG) size) < 0) {
			
				totalWritten = 0;
				break;
				
			} else {
					
				totalWritten += size;
				p += size;

			}
			inSystemWorker->fCriticalSection.Unlock();

		}

	}
	ioParms.ReturnNumber(totalWritten);
}

void VJSSystemWorkerClass::_endOfInput (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	if (inSystemWorker->IsRunning(true)) {

		XBOX::StLocker<XBOX::VCriticalSection>	lock(&inSystemWorker->fCriticalSection);

		inSystemWorker->fProcessLauncher.CloseStandardInput();

	}
}

void VJSSystemWorkerClass::_terminate (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	if (!inSystemWorker->IsRunning(true))

		return;

	bool	doWait;

	if (!ioParms.CountParams() || !ioParms.IsBooleanParam(1) || !ioParms.GetBoolParam(1, &doWait))
		
		doWait = false;	

	// Force close stdin. This way, if the external process was blocked waiting data on stdin, it will quit.

	inSystemWorker->fProcessLauncher.CloseStandardInput();
	inSystemWorker->fTerminationRequested = true;

	if (doWait && !inSystemWorker->fWorker->IsInsideWaitFor()) {

		XBOX::VJSContext	context(ioParms.GetContext());
		
		inSystemWorker->fWorker->WaitFor(context, -1, inSystemWorker, VJSSystemWorkerEvent::eTYPE_TERMINATION);

	}
}

void VJSSystemWorkerClass::_getInfos (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	XBOX::StLocker<XBOX::VCriticalSection>	lock(&inSystemWorker->fCriticalSection);
	XBOX::VJSObject							object(ioParms.GetContext());
	
	object.MakeEmpty();	
	object.SetProperty("commandLine", inSystemWorker->fCommandLine);	
	object.SetProperty("hasStarted", inSystemWorker->fStartupStatus > 0);
	object.SetProperty("isTerminated", inSystemWorker->fIsTerminated);
	
#if VERSIONMAC || VERSION_LINUX
	
	object.SetProperty("pid", inSystemWorker->fPID);	
	
#endif
	
	ioParms.ReturnValue(object);
}

void VJSSystemWorkerClass::_getNumberRunning (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	ioParms.ReturnNumber(XBOX::VInterlocked::AtomicGet((sLONG *) &VJSSystemWorker::sNumberRunning));
}

void VJSSystemWorkerClass::_setBinary (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	bool	isBinary	= true;

	if (ioParms.CountParams()) 

		ioParms.GetBoolParam(1, &isBinary);

	ioParms.GetThis().SetProperty("isbinary", isBinary);
}

void VJSSystemWorkerClass::_wait (XBOX::VJSParms_callStaticFunction &ioParms, VJSSystemWorker *inSystemWorker)
{
	xbox_assert(inSystemWorker != NULL);

	sLONG	waitDuration;
	bool	isTerminated;

	// Check if the termination process has already been processed or discarded. If so, do not wait trying to match this event.

	waitDuration = VJSWorker::GetWaitDuration(ioParms);
	{
		XBOX::StLocker<XBOX::VCriticalSection>	lock(&inSystemWorker->fCriticalSection);

		isTerminated = inSystemWorker->fTerminationEventDiscarded;

	}
	if (!isTerminated) {

		XBOX::VJSContext	context(ioParms.GetContext());

		isTerminated = (VJSWorker::GetWorker(context)->WaitFor(context, waitDuration, inSystemWorker, VJSSystemWorkerEvent::eTYPE_TERMINATION) == -1);

	}

	ioParms.ReturnBool(isTerminated);
}

void VJSSystemWorkerClass::_Exec (XBOX::VJSParms_callStaticFunction &ioParms, void *)
{	
	XBOX::VString			commandLine, executionPath, inputString;
	XBOX::VJSBufferObject	*inputBuffer;
	
	ioParms.ReturnNullValue();
	inputBuffer = NULL;

	if (!ioParms.GetStringParam(1, commandLine)) {
		
		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "1");
		return;
				
	} 
	
	if (ioParms.CountParams() >= 2) {

		if (ioParms.IsStringParam(2)) {

			if (!ioParms.GetStringParam(2, inputString)) {

				XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_STRING, "2");
				return;

			}

		} else if (ioParms.IsObjectParam(2)) {

			XBOX::VJSObject	bufferObject(ioParms.GetContext());

			if (!ioParms.GetParamObject(2, bufferObject) || !bufferObject.IsOfClass(VJSBufferClass::Class())) {

				XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER_TYPE_BUFFER, "2");
				return;

			}

			inputBuffer = XBOX::RetainRefCountable<VJSBufferObject>(bufferObject.GetPrivateData<VJSBufferClass>());
			xbox_assert(inputBuffer != NULL);

		} else if (ioParms.IsNullParam(2)) {

			// null is acceptable, like an empty string: There is no input.

		} else {
			
			XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER, "2");
			return;

		}

	} 

	if (ioParms.CountParams() >= 3 && !ioParms.GetStringParam(3, executionPath)) {

		XBOX::vThrowError(XBOX::VE_JVSC_WRONG_PARAMETER, "3");
		ReleaseRefCountable<VJSBufferObject>(&inputBuffer);
		return;

	}

#if VERSIONWIN
#	define OPTION	(VProcessLauncher::eVPLOption_HideConsole)
#else
#	define OPTION	(VProcessLauncher::eVPLOption_None)
#endif
	
	sLONG			exitStatus;
	VMemoryBuffer<>	stdinBuffer, stdoutBuffer, stderrBuffer;
	XBOX::VString	*defaultDirectory;
	VMemoryBuffer<>	*stdinBufferPointer;

	defaultDirectory = executionPath.GetLength() ? &executionPath : NULL;
	if (inputString.GetLength()) {

#define BUFFER_SIZE	(1 << 14)

		sLONG	stringIndex, stringLength;

		stringIndex = 1;
		stringLength = inputString.GetLength();
			
		while (stringLength) {
		
			XBOX::VString	subString;
			char			buffer[BUFFER_SIZE];
			sLONG			size;

			size = stringLength > BUFFER_SIZE ? BUFFER_SIZE : stringLength;
			inputString.GetSubString(stringIndex, size, subString);
			stringIndex += size;
			stringLength -= size;
			
			if ((size = (sLONG) subString.ToBlock(buffer, BUFFER_SIZE, XBOX::VTC_StdLib_char, true, false)) > 0)
		
				stdinBuffer.PutData(stringIndex - 1, buffer, size);
			
		}

		stdinBufferPointer = &stdinBuffer;

	} else if (inputBuffer != NULL) {

		stdinBuffer.SetDataPtr(inputBuffer->GetDataPtr(), inputBuffer->GetDataSize(), inputBuffer->GetDataSize());
		stdinBufferPointer = &stdinBuffer;

	} else

		stdinBufferPointer = NULL;

	if (!VProcessLauncher::ExecuteCommandLine(commandLine, OPTION, stdinBufferPointer, &stdoutBuffer, &stderrBuffer, NULL, defaultDirectory, &exitStatus)) {

		// Successful call, set up "result" object.

		XBOX::VJSObject	resultObject(ioParms.GetContext());

		resultObject.MakeEmpty();
		resultObject.SetProperty("exitStatus", (Real) exitStatus);
		resultObject.SetProperty("output", VJSBufferClass::NewInstance(ioParms.GetContext(), stdoutBuffer.GetDataSize(), stdoutBuffer.GetDataPtr()));
		resultObject.SetProperty("error", VJSBufferClass::NewInstance(ioParms.GetContext(), stderrBuffer.GetDataSize(), stderrBuffer.GetDataPtr()));

		// Buffer objects will free memory.

		stdoutBuffer.ForgetData();
		stderrBuffer.ForgetData();

		ioParms.ReturnValue(resultObject);

	}
	stdinBuffer.ForgetData();
	ReleaseRefCountable<VJSBufferObject>(&inputBuffer);
}