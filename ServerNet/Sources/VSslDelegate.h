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


#ifndef __SNET_SSL_DELEGATE__
#define __SNET_SSL_DELEGATE__


BEGIN_TOOLBOX_NAMESPACE


class VKeyCertPair;

namespace SslFramework
{
	//Inner type to hide implementation global context
	class XContext;
	
	//Init, DeInit and cie are not thread safe !
	VError Init();
	VError DeInit();
	
	VError SetDefaultCertificate(const VMemoryBuffer<>& inCertBuffer);
	VError SetDefaultPrivateKey(const VMemoryBuffer<>& inKeyBuffer);
	
	VKeyCertPair* RetainKeyCertificatePair(const VMemoryBuffer<>& inKeyBuffer, const VMemoryBuffer<>& inCertBuffer);
	void ReleaseKeyCertificatePair(VKeyCertPair* inKeyCertPair);

	//TODO : Legacy Code ; Need rewrite.
	VError Encrypt(uCHAR* inPrivateKeyPEM, uLONG inPrivateKeyPEMSize, uCHAR* inData, uLONG inDataSize, uCHAR* ioEncryptedData, uLONG* ioEncryptedDataSize);
	uLONG GetEncryptedPKCS1DataSize( uLONG inKeySize /* 128 for 1024 RSA; X/8 for X RSA*/, uLONG inDataSize );
	
	XContext* GetContext();
};


class XTOOLBOX_API VSslDelegate : public XBOX::VObject
{
public :
	
	static VSslDelegate* NewClientDelegate(Socket inRawSocket /*, VKeyCertPair* inKeyCertPair*/);
	static VSslDelegate* NewServerDelegate(Socket inRawSocket, VKeyCertPair* inKeyCertPair);
	
	virtual ~VSslDelegate();	

	sLONG GetBufferedDataLen(); //data buffered for reading !
	
	VError Read(void* outBuff, uLONG* ioLen);
	VError Write(const void* inBuff, uLONG* ioLen);
	VError Shutdown();
		
	bool WantRead()		{return fIOState==kWantRead;}
	bool WantWrite()	{return fIOState==kWantWrite;}
	bool IsOver()		{return fIOState==kOver;}
	void Clear()		{fIOState=kBlank;}

	VError HandShake ();
		
private :
	
	typedef enum {kBlank=0, kWantRead, kWantWrite, kOver} IOState;
	IOState fIOState;
	
	VSslDelegate();
	
	VSslDelegate(const VSslDelegate& inUnused); 
	VSslDelegate& operator=(const VSslDelegate& inUnused);
	
	static VSslDelegate* NewDelegate(Socket inRawSocket);
	
	//Inner type to hide implementation connection context
	class XConnection;
	
	XConnection* fConnection;
	
	VKeyCertPair* fKeyCertPair;
};


END_TOOLBOX_NAMESPACE


#endif
