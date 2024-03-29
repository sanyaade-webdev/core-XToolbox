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
#include "VServerNetPrecompiled.h"

#include "XWinSocket.h"

#include "Tools.h"
#include "VNetAddr.h"
#include "VSslDelegate.h"

#include <Ws2tcpip.h>


BEGIN_TOOLBOX_NAMESPACE


using namespace ServerNetTools;


#define WITH_SNET_SOCKET_LOG 0


//This global object ensure winsock is properlely loaded, initialized and unloaded...
//http://msdn.microsoft.com/en-us/library/ms742213%28v=VS.85%29.aspx

class WinSockInit
{
public :

	WinSockInit()
	{
		fOk=false;

	    WORD wVersionRequested;
		WSADATA wsaData;

	    wVersionRequested=MAKEWORD(2, 2);

		//The WSAStartup function initiates use of the Winsock DLL by a process.
		int err=WSAStartup(wVersionRequested, &wsaData);
		xbox_assert(err==0);

		if(LOBYTE(wsaData.wVersion)==2 && HIBYTE(wsaData.wVersion)==2)
			fOk=true;

		xbox_assert(fOk);
	}

	~WinSockInit()
	{
		//The WSACleanup function terminates use of the Winsock 2 DLL (Ws2_32.dll).
		int err=WSACleanup();
		xbox_assert(err==0);
	}

	bool IsOk()
	{
		return fOk;
	}

private :

	bool fOk;

} gWinSockInit;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XWinTCPSocket
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//virtual
XWinTCPSocket::~XWinTCPSocket()
{
	//Close();

#if VERSIONDEBUG
	if(fSock!=kBAD_SOCKET)
		DebugMsg ("[%d] XWinTCPSocket::~XWinTCPSocket() : socket %d not closed yet !\n", VTask::GetCurrentID(), fSock);
#endif
	
	if(fSslDelegate!=NULL)
		delete fSslDelegate;
}

#if WITH_DEPRECATED_IPV4_API
IP4 XWinTCPSocket::GetIPv4HostOrder()
{
	XWinNetAddr addr;
	
	if(fProfile==ClientSock)
		addr.FromPeerAddr(fSock);
	else
		addr.FromLocalAddr(fSock);
	
	return addr.GetIPv4HostOrder();
}
#endif

VString XWinTCPSocket::GetIP() const
{
	XWinNetAddr addr;
	
	if(fProfile==ClientSock)
		addr.FromPeerAddr(fSock);
	else
		addr.FromLocalAddr(fSock);
	
	return addr.GetIP();
}


PortNumber XWinTCPSocket::GetPort() const
{
	return GetServicePort();
}


PortNumber XWinTCPSocket::GetServicePort() const
{
	return fServicePort;
}


VError XWinTCPSocket::SetServicePort(PortNumber inServicePort)
{
	fServicePort=inServicePort;

	return VE_OK;
}


Socket XWinTCPSocket::GetRawSocket() const
{
	return fSock;
}


VError XWinTCPSocket::Close(bool inWithRecvLoop)
{
	StSilentErrorContext errCtx;

	Socket invalidSocket=kBAD_SOCKET;

#if ARCH_32
	Socket realSocket=(Socket)XBOX::VInterlocked::Exchange(reinterpret_cast<sLONG*>(&fSock), static_cast<sLONG>(invalidSocket));
#else
	Socket realSocket=(Socket)XBOX::VInterlocked::Exchange(reinterpret_cast<sLONG8*>(&fSock), static_cast<sLONG8>(invalidSocket));
#endif

	if(fSslDelegate!=NULL)
		fSslDelegate->Shutdown();

	int err=shutdown(realSocket, SD_SEND);
	
	if(err!=SOCKET_ERROR && inWithRecvLoop)
	{
		StDropErrorContext errCtx;
		
		char garbage[512];
		uLONG len=sizeof(garbage);

		ReadWithTimeout(garbage, &len, 30); 
	}

	err=closesocket(realSocket);
			
	return err==0 ? VE_OK : vThrowNativeError(WSAGetLastError());
}


VError XWinTCPSocket::SetBlocking(bool inBlocking)
{
	u_long nonBlocking=!inBlocking;

	int err=ioctlsocket(fSock, FIONBIO, &nonBlocking);
		
	if(err==SOCKET_ERROR)
		return vThrowNativeError(WSAGetLastError());

	fIsBlocking=inBlocking;

	return VE_OK;
}


bool XWinTCPSocket::IsBlocking()
{
	return fIsBlocking;
}


VError XWinTCPSocket::WaitFor(Socket inFd, FdSet inSet, sLONG inMsTimeout, sLONG* outMsSpent)
{
	if(inMsTimeout<0)
		return VE_SOCK_TIMED_OUT;
		
	sLONG start=VSystem::GetCurrentTime();
	sLONG stop=start+inMsTimeout;
	sLONG now=start;
	
	for(;;)
	{
		sLONG msTimeout=stop-now;

		timeval timeout={0};
		
		timeout.tv_sec=msTimeout/1000;
		timeout.tv_usec=1000*(msTimeout%1000);
		
        fd_set set;
        FD_ZERO(&set);
        FD_SET(inFd, &set);

		fd_set* rs=(inSet==kREAD_SET) ? &set : NULL;
		fd_set* ws=(inSet==kWRITE_SET) ? &set : NULL;
		fd_set* es=(inSet==kERROR_SET) ? &set : NULL;

        int res=select(inFd+1, rs, ws, es, &timeout);

		if(res==-1 && errno==EINTR)
		{
			now=VSystem::GetCurrentTime();
			continue;
		}
		
		//Prepare to return and update spent time if caller asked for it
		
		if(outMsSpent!=NULL)
		{
			now=VSystem::GetCurrentTime();
			*outMsSpent=now-start;
		}
		
		if(res==0)
			return VE_SOCK_TIMED_OUT;
		
		if(res==1)
			return VE_OK;
		
		return vThrowNativeError(errno);
	}
}


VError XWinTCPSocket::WaitForConnect(sLONG inMsTimeout, sLONG* outMsSpent)
{
	//connect : When the connection has been established asynchronously, select() shall indicate that 
	//          the file descriptor for the socket is ready for writing.
	
	return WaitFor(GetRawSocket(), kWRITE_SET, inMsTimeout, outMsSpent);
}


VError XWinTCPSocket::WaitForAccept(sLONG inMsTimeout, sLONG* outMsSpent)
{
	//accept : When a connection is available, select() indicates that
	//         the file descriptor for the socket is ready for reading.
	return WaitFor(GetRawSocket(), kREAD_SET, inMsTimeout, outMsSpent);
}


VError XWinTCPSocket::WaitForRead(sLONG inMsTimeout, sLONG* outMsSpent)
{
	return WaitFor(GetRawSocket(), kREAD_SET, inMsTimeout, outMsSpent);
}


VError XWinTCPSocket::WaitForWrite(sLONG inMsTimeout, sLONG* outMsSpent)
{
	return WaitFor(GetRawSocket(), kWRITE_SET, inMsTimeout, outMsSpent);
}


VError XWinTCPSocket::Connect(const VNetAddress& inAddr, sLONG inMsTimeout)
{
	xbox_assert(fProfile==NewSock);
	
	VError verr=SetBlocking(false);
	
	if(verr!=VE_OK)
		return verr;
	
	int err=0;

	err=connect(fSock, inAddr.GetAddr(), inAddr.GetAddrLen());
	
	//jmo - TODO : double check those codes
	if(err!=0 && WSAGetLastError()!=WSAEINPROGRESS && WSAGetLastError()!=WSAEALREADY && WSAGetLastError()!=WSAEWOULDBLOCK)
		return vThrowNativeError(WSAGetLastError());

	//If for any reason the socket is already connected, we take a shortcut to save a select syscall.
	if(err==0)
		return verr;
	
	verr=WaitForConnect(inMsTimeout);

	if(verr!=VE_OK)
		return verr;
	
	//Wait returns, but it doesn't mean the connection succed ! Retry connect to retrieve the status
	//(This test is mandatory on Mac... Although windows impl worked without it, it was conceptually broken)
	err=connect(fSock, inAddr.GetAddr(), inAddr.GetAddrLen());

	//OUR SOCKET SEEMS TO HAVE WINSOCK 1.x BEHAVIOR : AS I UNDERSTAND THE DOC, WE SHOULDN'T HAVE TO TEST FOR
	//WSAEINVAL NOR WSAEWOULDBLOCK WITH WINSOCK 2.x
	//http://msdn.microsoft.com/en-us/library/windows/desktop/ms737625(v=vs.85).aspx

	if(err!=0 && WSAGetLastError()!=WSAEISCONN && WSAGetLastError()!=WSAEINVAL && WSAGetLastError()!=WSAEWOULDBLOCK)
		return vThrowNativeError(WSAGetLastError());
	
	fProfile=ClientSock;
	
	verr=SetBlocking(true);

	return verr;
}


VError XWinTCPSocket::Listen(const VNetAddress& inAddr, bool inAlreadyBound)
{
	xbox_assert(fProfile==NewSock);

	int err=0;

	if(!inAlreadyBound)
	{
		int opt=true;
		err=setsockopt(fSock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));
		
		if(err!=0)
			return vThrowNativeError(WSAGetLastError());
		
		err=bind(fSock, inAddr.GetAddr(), inAddr.GetAddrLen());

		if(err!=0)
			return vThrowNativeError(WSAGetLastError());
	}

	err=listen(fSock, SOMAXCONN);

	//DebugMsg ("[%d] XWinTCPSocket::Listen() : socket=%d port=%d err=%d\n",
	//			VTask::GetCurrentID(), fSock, GetServicePort(), err);


	if(err==0)
	{
		fProfile=ServiceSock;
		
		return VE_OK;
	}
	
	return vThrowNativeError(WSAGetLastError());
}


XWinTCPSocket* XWinTCPSocket::Accept(uLONG inMsTimeout)
{
	xbox_assert(fProfile==ServiceSock);

	VError verr=VE_OK;

	if(inMsTimeout>0)
	{
		verr=WaitForAccept(inMsTimeout);
		
		if(verr!=VE_OK)
			return NULL;
	}
	
	verr=SetBlocking(false);

	if(verr!=VE_OK)
		return NULL;

	sockaddr_storage sa_storage;
	int len=sizeof(sa_storage);
	memset(&sa_storage, 0, len);
	
	sockaddr* sa=reinterpret_cast<sockaddr*>(&sa_storage);
	
	Socket sock=kBAD_SOCKET;
	
	sock=accept(GetRawSocket(), sa, &len);
	
	if(sock==kBAD_SOCKET)
	{
		vThrowNativeError(WSAGetLastError());
		return NULL;
	}
		
	bool ok=true;

	if(ok && sa->sa_family!=AF_INET && sa->sa_family!=AF_INET6)
		ok=false;
	
	int opt=true;
	int err=0;

	if(ok)
	{
		err=setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));
		
		if(err!=0)
			{ ok=false ; vThrowNativeError(WSAGetLastError()); }
	}

	if(ok)
	{
		err=setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&opt), sizeof(opt));

		if(err!=0)
			{ ok=false ; vThrowNativeError(WSAGetLastError()); }
	}

	XWinTCPSocket* xsock=NULL;
	
	if(ok)
	{
		xsock=new XWinTCPSocket(sock);

		if(xsock==NULL)
			{ ok=false ; vThrowError(VE_MEMORY_FULL); }
	}

	if(ok)
		xsock->fProfile=ConnectedSock;
		
	if(ok)
	{
		verr=xsock->SetBlocking(true);
		
		if(verr!=VE_OK)
			ok=false;
	}

	if(ok)
	{
		verr=xsock->SetServicePort(GetPort());

		if(verr!=VE_OK)
			ok=false;
	}

	if(!ok)
	{
		if(xsock!=NULL)
		{
			xsock->Close(false);
		
			delete xsock;
		
			xsock=NULL;	
		}	
		else
		{
			do
				err=close(sock);
			while(err==-1 && errno==EINTR);
		}
	}	
	
	return xsock;
}


VError XWinTCPSocket::Read(void* outBuff, uLONG* ioLen)
{
	if(outBuff==NULL || ioLen==NULL)
		return vThrowError(VE_INVALID_PARAMETER);
	
	if(fSslDelegate!=NULL)
		return fSslDelegate->Read(outBuff, ioLen);

	int n=recv(fSock, reinterpret_cast<char*>(outBuff), *ioLen, 0 /*flags*/);
	
	if(n>0)
	{
		*ioLen=static_cast<uLONG>(n);

		return VE_OK;
	}
	
	//We have an error...
	*ioLen=0;

	if(n==0)
	{
		//jmo - TODO : Double check windows behavior
		//If no messages are available to be received and the peer has performed an orderly shutdown,
		//recv() shall return 0.
		
		return VE_SOCK_PEER_OVER;
	}
	
	if(WSAGetLastError()==WSAEWOULDBLOCK)
		return VE_SOCK_WOULD_BLOCK;
	
	if(WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAENOTSOCK || WSAGetLastError()==WSAEBADF)
		return vThrowNativeCombo(VE_SOCK_CONNECTION_BROKEN, WSAGetLastError());

	return vThrowNativeCombo(VE_SOCK_READ_FAILED, WSAGetLastError());
}


VError XWinTCPSocket::Write(const void* inBuff, uLONG* ioLen, bool inWithEmptyTail)
{
	if(inBuff==NULL || ioLen==NULL)
		return vThrowError(VE_INVALID_PARAMETER);
	
	if(fSslDelegate!=NULL)
		return fSslDelegate->Write(inBuff, ioLen);

	int n=send(fSock, reinterpret_cast<const char*>(inBuff), *ioLen, 0 /*flags*/);

	if(n>=0)
	{
		*ioLen=n;
	
		//bWithEmptyTail is used to determine broken socket connection (should be done in the networking code itself)
		if(inWithEmptyTail)
		{
			char emptyString[]={0};
			uLONG len=0;
			
			return Write(emptyString, &len, false);
		}

		return VE_OK;
	}
	
	//We have an error...
	*ioLen=0;
		
	if(WSAGetLastError()==WSAEWOULDBLOCK)
		return VE_SOCK_WOULD_BLOCK;
	
	if(WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAENOTSOCK || WSAGetLastError()==WSAEBADF)
		return vThrowNativeCombo(VE_SOCK_CONNECTION_BROKEN, WSAGetLastError());
	
	return vThrowNativeCombo(VE_SOCK_WRITE_FAILED, WSAGetLastError());
}


VError XWinTCPSocket::ReadWithTimeout(void* outBuff, uLONG* ioLen, sLONG inMsTimeout, sLONG* outMsSpent)
{
	if(outBuff==NULL || ioLen==NULL)
		return vThrowError(VE_INVALID_PARAMETER);
	
	
	VError verr=VE_OK;

	uLONG len=*ioLen;
	
	if(fSslDelegate!=NULL)
	{		
		sLONG spentTotal=0;
		
		sLONG available=fSslDelegate->GetBufferedDataLen();
		
		if(available>=len)
		{
			//First case : OpenSSL has enough buffered data ; No socket IO should occur.
			
			verr=fSslDelegate->Read(outBuff, &len);
			
			spentTotal=0;
		}
		else
		{	
			//Second case : We have to handle SSL protocol IO that might occur before having applicative data
		
			bool wasBlocking=false;
		
			if(IsBlocking())
			{
				SetBlocking(false);
				wasBlocking=true;
			}
				
			sLONG timeout=inMsTimeout;
		
			do
			{
				len=*ioLen;
				
				verr=fSslDelegate->Read(outBuff, &len);

				if(len>0)	//We have some data ; that's enough for now.
					break;
		
				//We have no data ; perhaps we need waiting for something...
			
				sLONG spentOnStep=0;
				
				if(verr==VE_SOCK_WOULD_BLOCK && fSslDelegate->WantRead())
					verr=WaitForRead(timeout, &spentOnStep);
				else if(verr==VE_SOCK_WOULD_BLOCK && fSslDelegate->WantWrite())
					verr=WaitForWrite(timeout, &spentOnStep);
		
				timeout-=spentOnStep;
				spentTotal+=spentOnStep;
			}
			while(verr==VE_OK);

			if(wasBlocking)
				SetBlocking(true);
		}	
		
		if(outMsSpent!=NULL)
			*outMsSpent=spentTotal;
	}
	else
	{
		verr=WaitForRead(inMsTimeout, outMsSpent);

		if(verr==VE_OK)
			verr=Read(outBuff, &len);
		else
			len=0;
	}
	
	if(verr==VE_OK && len==0)
	{
		//Having nothing to read should result in VE_SOCK_TIMED_OUT. If read is OK but len is 0, it really means the peer
		//did a shutdown (or ssl equivalent). We return a special error so the caller dont try to call us in a mad loop.
		
		verr=VE_SOCK_PEER_OVER;
	}
	
	//Check len and err code are consistent
	xbox_assert((len==0 && verr!=VE_OK) || (len>0 && verr==VE_OK));

	//Check this error never escape !
	xbox_assert(verr!=VE_SOCK_WOULD_BLOCK);
	
	*ioLen=len;
	

	//timeout and peer over are not thrown (these are not /real/ errors)
	if(verr==VE_OK || verr==VE_SOCK_TIMED_OUT || verr==VE_SOCK_PEER_OVER)
		return verr;

	return vThrowError(verr);
}


VError XWinTCPSocket::WriteWithTimeout(const void* inBuff, uLONG* ioLen, sLONG inMsTimeout, sLONG* outMsSpent, bool unusedWithEmptyTail)
{	
	if(inBuff==NULL || ioLen==NULL)
		return vThrowError(VE_INVALID_PARAMETER);
	
	
	VError verr=VE_OK;
	
	uLONG len=*ioLen;
	
	if(fSslDelegate!=NULL)
	{
		sLONG spentTotal=0;
		
		bool wasBlocking=false;
		
		if(IsBlocking())
		{
			SetBlocking(false);
			wasBlocking=true;
		}
		
		sLONG timeout=inMsTimeout;
		
		do
		{
			len=*ioLen;
			
			verr=fSslDelegate->Write(inBuff, &len);
			
			if(len>0)	//We sent some data ; that's enough for now.
				break;
			
			//No data were sent ; perhaps we need waiting for something...
			
			sLONG spentOnStep=0;
			
			if(verr==VE_SOCK_WOULD_BLOCK && fSslDelegate->WantRead())
				verr=WaitForRead(timeout, &spentOnStep);
			else if(verr==VE_SOCK_WOULD_BLOCK && fSslDelegate->WantWrite())
				verr=WaitForWrite(timeout, &spentOnStep);
			
			timeout-=spentOnStep;
			spentTotal+=spentOnStep;
		}
		while(verr==VE_OK);
		
		if(wasBlocking)
			SetBlocking(true);
		
	}
	else
	{
		verr=WaitForWrite(inMsTimeout, outMsSpent);
	
		if(verr==VE_OK)
			verr=Write(inBuff, &len, unusedWithEmptyTail);
	}

	//Check len and err code are consistent
	xbox_assert((len==0 && verr!=VE_OK) || (len>0 && verr==VE_OK));
	
	//Check those errors never escape !
	xbox_assert(verr!=VE_SOCK_WOULD_BLOCK);
	xbox_assert(verr!=VE_SOCK_PEER_OVER);
	
	*ioLen=len;
	
	return vThrowError(verr);	//might be VE_OK, which throws nothing.
}



//static
XWinTCPSocket* XWinTCPSocket::NewClientConnectedSock(const VString& inDnsName, PortNumber inPort, sLONG inMsTimeout)
{
	XWinTCPSocket* xsock=NULL;
	
	VNetAddrList addrList;
	
	VError verr=addrList.FromDnsQuery(inDnsName, inPort);
	
	VNetAddrList::const_iterator cit;
	
	for(cit=addrList.begin() ; cit!=addrList.end() ; cit++)
	{
		//We have a list of address structures ; we choose the first one that connects successfully.
	
		//int sock=socket(cit->GetFamily(), cit->GetSockType(), cit->GetProtocol());
		int sock=socket(cit->GetPfFamily(), SOCK_STREAM, IPPROTO_TCP);		
		if(sock==kBAD_SOCKET)
		{
			vThrowNativeError(WSAGetLastError());
		}
		else
		{
			int opt=true;

			int err=setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));
			
			if(err!=0)
			{
				vThrowNativeError(WSAGetLastError());

				err=closesocket(sock);

				return NULL;
			}
			
			err=setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&opt), sizeof(opt));
			
			if(err!=0)
			{
				vThrowNativeError(WSAGetLastError());

				err=closesocket(sock);

				return NULL;
			}
				
			xsock=new XWinTCPSocket(sock);
			
			if(xsock==NULL)
			{
				err=closesocket(sock);

				vThrowError(VE_MEMORY_FULL);

				break;
			}
			
			VError verr=xsock->Connect(*cit, inMsTimeout);
			
			if(verr!=VE_OK)
			{
				xsock->Close();
				
				delete xsock;

				xsock=NULL;
			}
			else
			{
				//DEPRECATED ? - We have to set the service port (duplicated stuff on client sock).
				PortNumber resolvedPort=cit->GetPort();

				xbox_assert(resolvedPort==inPort);
				
				xsock->SetServicePort(resolvedPort);
			}
		}
	}

	return xsock;
}


#if WITH_DEPRECATED_IPV4_API

//static
XWinTCPSocket* XWinTCPSocket::NewServerListeningSock(uLONG inIPv4, PortNumber inPort, Socket inBoundSock)
{
	sockaddr_in v4={0};
	v4.sin_family=AF_INET;
	v4.sin_addr.s_addr=htonl(inIPv4);
	v4.sin_port=htons(inPort);
	
	bool alreadyBound=(inBoundSock!=kBAD_SOCKET) ? true : false;

	Socket sock=(alreadyBound) ? inBoundSock : socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	if(sock==kBAD_SOCKET)
	{
		vThrowNativeError(WSAGetLastError());
		return NULL;
	}

	XWinTCPSocket* xsock=new XWinTCPSocket(sock);
	
	if(xsock==NULL)
	{
		vThrowError(VE_MEMORY_FULL);
		return NULL;
	}
	
	xsock->SetServicePort(inPort);

	VNetAddress addr(v4);

	VError verr=xsock->Listen(addr, alreadyBound);
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}
	
	verr=xsock->SetBlocking(false);
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}

	return xsock;
}

#else

//static
XWinTCPSocket* XWinTCPSocket::NewServerListeningSock(const VNetAddress& inAddr, Socket inBoundSock)
{
	bool alreadyBound=(inBoundSock!=kBAD_SOCKET) ? true : false;
	
	int sock=(alreadyBound) ? inBoundSock : socket(inAddr.GetPfFamily(), SOCK_STREAM, IPPROTO_TCP);
	
	if(sock==kBAD_SOCKET)
	{
		vThrowNativeError(errno);
		return NULL;
	}
	
	XWinTCPSocket* xsock=new XWinTCPSocket(sock);
	
	if(xsock==NULL)
	{
		vThrowError(VE_MEMORY_FULL);
		return NULL;
	}
	
	xsock->SetServicePort(inAddr.GetPort());
		
	VError verr=xsock->Listen(inAddr, alreadyBound);
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}
	
	verr=xsock->SetBlocking(false);
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}
	
	return xsock;
}

#endif


//static
XWinTCPSocket* XWinTCPSocket::NewServerListeningSock(PortNumber inPort, Socket inBoundSock)
{
#if WITH_DEPRECATED_IPV4_API
	return NewServerListeningSock(INADDR_ANY, inPort, inBoundSock);
#else
	VNetAddress anyAddr;
	VError verr=anyAddr.FromAnyIpAndPort(inPort);
	
	return NewServerListeningSock(anyAddr, inBoundSock);
#endif
}

XBOX::VError XWinTCPSocket::SetNoDelay (bool inYesNo)
{
	int	opt	= inYesNo;
	int err	= 0;


	err = setsockopt(fSock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));

	return !err ? XBOX::VE_OK : vThrowNativeError(WSAGetLastError());
}


XBOX::VError XWinTCPSocket::PromoteToSSL(VKeyCertPair* inKeyCertPair)
{
	VSslDelegate* delegate=NULL;
	
	switch(fProfile)
	{
	case ConnectedSock :
			
			delegate=VSslDelegate::NewServerDelegate(GetRawSocket(), inKeyCertPair);
			break;
			
	case ClientSock :
			
			delegate=VSslDelegate::NewClientDelegate(GetRawSocket()/*, inKeyCertPair*/);
			break;
			
	default :

			xbox_assert(fProfile!=ConnectedSock && fProfile!=ClientSock);
	}
	
	if(delegate==NULL)
		return vThrowError(VE_SOCK_FAIL_TO_ADD_SSL);

	fSslDelegate=delegate;
	
	return VE_OK;
}


bool XWinTCPSocket::IsSSL()
{
	return fSslDelegate!=NULL;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XWinAcceptIterator
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VError XWinAcceptIterator::AddServiceSocket(XWinTCPSocket* inSock)
{
	if(inSock==NULL)
		return VE_INVALID_PARAMETER;
	
	fSocks.push_back(inSock);

	//Push back may invalidate the collection iterator...
	fSockIt=fSocks.end();
	
	return VE_OK;
}


VError XWinAcceptIterator::ClearServiceSockets()
{
	fSocks.clear();
	
	//clear will invalidate the collection iterator...
	fSockIt=fSocks.end();
	
	return VE_OK;
}


VError XWinAcceptIterator::GetNewConnectedSocket(XWinTCPSocket** outSock, sLONG inMsTimeout)
{
	if(outSock==NULL)
		return vThrowError(VE_INVALID_PARAMETER);

	bool shouldRetry=false;
	
	VError verr=GetNewConnectedSocket(outSock, inMsTimeout, &shouldRetry);
	
	if(verr==VE_OK && *outSock==NULL && shouldRetry)
		verr=GetNewConnectedSocket(outSock, inMsTimeout, &shouldRetry /*ignored*/);
		
	return verr;
}


VError XWinAcceptIterator::GetNewConnectedSocket(XWinTCPSocket** outSock, sLONG inMsTimeout, bool* outShouldRetry)
{
	if(outSock==NULL || outShouldRetry==NULL)
		return vThrowError(VE_INVALID_PARAMETER);
	
	*outSock=NULL;
	*outShouldRetry=true;	//Indicate wether the select call is done (we should not retry) or not (we should retry).
	
	if(inMsTimeout<0)
		return VE_SOCK_TIMED_OUT;
	
	
	if(fSockIt==fSocks.end())
	{
		//We need to issue a new select call, with timeout
		*outShouldRetry=false;

        FD_ZERO(&fReadSet);
		
		sLONG maxFd=-1;	//Ignored on Windows
		
		SockPtrColl::const_iterator cit;
		
		for(cit=fSocks.begin() ; cit!=fSocks.end() ; ++cit)
		{
			Socket fd=(*cit)->GetRawSocket();
			
			FD_SET(fd, &fReadSet);
		}
		
		sLONG now=VSystem::GetCurrentTime();
		sLONG stop=now+inMsTimeout;
		
		for(;;)
		{
			sLONG msTimeout=stop-now;
			
			timeval timeout={0};
			
			timeout.tv_sec=msTimeout/1000;
			timeout.tv_usec=1000*(msTimeout%1000);
		
			int res=select(maxFd+1, &fReadSet, NULL, NULL, &timeout);
	
			if(res==0)
				return VE_SOCK_TIMED_OUT;
			
			if(res>=1)
				break;

			return vThrowNativeError(WSAGetLastError());
		}
		
		fSockIt=fSocks.begin();
		
		//DebugMsg ("[%d] XWinAcceptIterator::GetNewConnectedSocket() : Reset iterator ; next socket is %d on port %d\n",
		//VTask::GetCurrentID(), (*fSockIt)->GetRawSocket(), (*fSockIt)->GetServicePort());
	}
	
	//We have to handle remaining service sockets ; find next ready one
		
	while(fSockIt!=fSocks.end())
	{
		Socket fd=(*fSockIt)->GetRawSocket();
		
		if(FD_ISSET(fd, &fReadSet))
		{
			//DebugMsg ("[%d] XWinAcceptIterator::GetNewConnectedSocket() : Connection pending on socket %d on port %d\n",
			//VTask::GetCurrentID(), (*fSockIt)->GetRawSocket(), (*fSockIt)->GetServicePort());
			
			*outSock=(*fSockIt)->Accept(0 /*No timeout*/);
			
			//DebugMsg ("[%d] XWinAcceptIterator::GetNewConnectedSocket() : New socket %d accepted from socket %d\n",
			//VTask::GetCurrentID(), (*outSock!=NULL) ? (*outSock)->GetRawSocket() : -1, (*fSockIt)->GetRawSocket(), (*fSockIt)->GetServicePort());

			
			++fSockIt;	//move to next socket ; prefer equity over perf !
			
			return VE_OK;
		}
		else
		{
			//DebugMsg ("[%d] XWinAcceptIterator::GetNewConnectedSocket() : No activity on socket %d on port %d\n",
			//VTask::GetCurrentID(), (*fSockIt)->GetRawSocket(), (*fSockIt)->GetServicePort());
			
			++fSockIt;	//move to next socket ; prefer equity over perf !
		}
	}

	//No luck ; None of the remaining sockets were ready to accept... The caller 'shouldRetry' to call us !
	return VE_OK;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XWinUDPSocket
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


XWinUDPSocket::XWinUDPSocket(sLONG inSockFD, const sockaddr_storage& inSockAddr) :
	fSock(inSockFD), fSockAddr(inSockAddr)
{
	//empty
}


XWinUDPSocket::XWinUDPSocket(sLONG inSockFD, const sockaddr_in& inSockAddr) :
	fSock(inSockFD)
{	
	memset(&fSockAddr, 0, sizeof(fSockAddr));
	memcpy(&fSockAddr, &inSockAddr, sizeof(inSockAddr));
}


XWinUDPSocket::~XWinUDPSocket()
{
	//Close();

#if VERSIONDEBUG
	if(fSock!=kBAD_SOCKET)
		DebugMsg ("[%d] XWinUDPSocket::~XWinUDPSocket() : socket %d not closed yet !\n", VTask::GetCurrentID(), fSock);
#endif
}


VError XWinUDPSocket::SetBlocking(uBOOL inBlocking)
{
	u_long nonBlocking=!inBlocking;
	
	int err=ioctlsocket(fSock, FIONBIO, &nonBlocking);
	
	if(err==SOCKET_ERROR)
		return vThrowNativeError(WSAGetLastError());
	
	return VE_OK;
}


VError XWinUDPSocket::Close()
{
	//Pas necessaire vu qu'il n'y a pas de forceclose...
	Socket invalidSocket=kBAD_SOCKET;

#if ARCH_32
	Socket realSocket=XBOX::VInterlocked::Exchange(reinterpret_cast<sLONG*>(&fSock), static_cast<sLONG>(invalidSocket));
#else
	Socket realSocket=XBOX::VInterlocked::Exchange(reinterpret_cast<sLONG8*>(&fSock), static_cast<sLONG8>(invalidSocket));
#endif
	
	int err=closesocket(realSocket);
			
	return err==0 ? VE_OK : vThrowNativeError(WSAGetLastError());
}


VError XWinUDPSocket::Read(void* outBuff, uLONG* ioLen, XWinNetAddr* outSenderInfo)
{
	if(outBuff==NULL || ioLen==NULL)
		return vThrowError(VE_INVALID_PARAMETER);

	sockaddr_storage addr;
	socklen_t addrLen=sizeof(addr);

	int n=recvfrom(fSock, reinterpret_cast<char*>(outBuff), *ioLen, 0 /*flags*/, reinterpret_cast<sockaddr*>(&addr), &addrLen);
	
	if(n>=0)
	{
		*ioLen=n;

		if(outSenderInfo!=NULL)
			outSenderInfo->SetAddr(addr);

		return VE_OK;
	}
	
	//We have an error...
	*ioLen=0;
	
	//En principe on ne gere pas le mode non bloquant avec UDP
	
	if(WSAGetLastError()==WSAEWOULDBLOCK)
		return VE_SOCK_WOULD_BLOCK;

	// If datagram cannot fit in read buffer, Windows API returns an error.
	// (Not applicable for UNIX (Mac/Linux.)

	if (WSAGetLastError()==WSAEMSGSIZE) 
		return VE_SOCK_UDP_DATAGRAM_TOO_LONG;
	
	//jmo - TODO : Find a better error !

	if(WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAENOTSOCK || WSAGetLastError()==WSAEBADF)
		return vThrowNativeCombo(VE_SOCK_CONNECTION_BROKEN, WSAGetLastError());

	return vThrowNativeCombo(VE_SOCK_READ_FAILED, WSAGetLastError());
}


VError XWinUDPSocket::Write(const void* inBuff, uLONG inLen, const XWinNetAddr& inReceiverInfo)
{
	if(inBuff==NULL)
		return vThrowError(VE_INVALID_PARAMETER);

	int n=sendto(fSock, reinterpret_cast<const char*>(inBuff), inLen, 0 /*flags*/, inReceiverInfo.GetAddr(), inReceiverInfo.GetAddrLen());
   	
	if(n>=0)
		return VE_OK;
		
	//En principe on ne gere pas le mode non bloquant avec UDP

	if(WSAGetLastError()==WSAEWOULDBLOCK)
		return VE_SOCK_WOULD_BLOCK;
	
	//jmo - TODO : Find a better error !

	if(WSAGetLastError()==WSAECONNRESET || WSAGetLastError()==WSAENOTSOCK || WSAGetLastError()==WSAEBADF)
		return vThrowNativeCombo(VE_SOCK_CONNECTION_BROKEN, WSAGetLastError());
	
	return vThrowNativeCombo(VE_SOCK_WRITE_FAILED, WSAGetLastError());
}

//static
XWinUDPSocket* XWinUDPSocket::NewMulticastSock(uLONG inLocalIpv4, uLONG inMulticastIPv4, PortNumber inPort)
{
	sockaddr_in v4={0};
	v4.sin_family=AF_INET;
	v4.sin_addr.s_addr=htonl(inLocalIpv4);
	v4.sin_port=htons(inPort);
	
	int sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	
	if(sock==kBAD_SOCKET)
	{
		vThrowNativeError(errno);
		return NULL;
	}

	XWinUDPSocket* xsock=new XWinUDPSocket(sock, v4);
	
	if(xsock==NULL)
	{
		vThrowError(VE_MEMORY_FULL);
		return NULL;
	}

	VError verr=xsock->SubscribeMulticast(inLocalIpv4, inMulticastIPv4);
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}
	
	verr=xsock->Bind();
	
	if(verr!=VE_OK)
	{
		delete xsock;
		return NULL;
	}
	
	return xsock;
}


//jmo - TODO : get rid of local ip and add IPv6 support (group_req not supported on OS X ?)
VError XWinUDPSocket::SubscribeMulticast(uLONG inLocalIpv4, uLONG inMulticastIPv4)
{
	//Membership is dropped on close or process termination. The OS take care of it.
	
	ip_mreq	mreq;	
	memset(&mreq, 0, sizeof(mreq));

	mreq.imr_multiaddr.s_addr=htonl(inMulticastIPv4);
	mreq.imr_interface.s_addr=htonl(inLocalIpv4);
	
	int err=setsockopt(fSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&mreq), sizeof(mreq));
			
	return err==0 ? VE_OK : vThrowNativeCombo(VE_SRVR_CANT_SUBSCRIBE_MULTICAST, WSAGetLastError());
}


VError XWinUDPSocket::Bind()
{
	int opt=true;
	
	int err=setsockopt(fSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

	if(err!=0)
		return vThrowNativeError(WSAGetLastError());
		
	err=bind(fSock, reinterpret_cast<sockaddr*>(&fSockAddr), fSockAddr.GetAddrLen());

	return err==0 ? VE_OK : vThrowNativeError(WSAGetLastError());
}

END_TOOLBOX_NAMESPACE
