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
#ifndef __SNET_SSL_STUB__
#define __SNET_SSL_STUB__


#if defined(_WIN32)

	#include "openssl/ssl.h"
	#include "openssl/err.h"
	#include "openssl/rand.h"
	#include "openssl/pem.h"

	#define SSLSTUB			snet_ssl_stub
	#define SNET_STDCALL	__stdcall
	#define SNET_CDECL		__cdecl
#else

	#include "openssl/ssl.h"
	#include "openssl/err.h"
	#include "openssl/rand.h"

	#define SSLSTUB
	#define SNET_STDCALL
	#define SNET_CDECL
	#define SSLCONST

#endif


namespace snet_ssl_stub
{
	BIO*					SNET_STDCALL	BIO_new						(BIO_METHOD* type);
	int						SNET_STDCALL	BIO_free					(BIO* a);
	BIO_METHOD*				SNET_STDCALL	BIO_s_mem					(); 
	int						SNET_STDCALL	BIO_write					(BIO* b, const void* buf, int len);

	int						SNET_STDCALL	CRYPTO_num_locks			();
	void					SNET_STDCALL	CRYPTO_set_id_callback		(unsigned long (SNET_CDECL *id_function)());
	void					SNET_STDCALL	CRYPTO_set_locking_callback	(void (SNET_CDECL *locking_function)(int mode, int n, const char* file, int line));

	void					SNET_STDCALL	ERR_clear_error				();
	void					SNET_STDCALL	ERR_error_string_n			(unsigned long e, char* buf, size_t len);
	void					SNET_STDCALL	ERR_free_strings			();
	unsigned long			SNET_STDCALL	ERR_get_error				();

	typedef int				SNET_CDECL		pem_password_cb				(char* buf, int size, int rwflag, void* userdata);

	RSA*					SNET_STDCALL	PEM_read_bio_RSAPrivateKey	(BIO* bp, RSA** rsa, pem_password_cb* cb, void* u);
	X509*					SNET_STDCALL	PEM_read_bio_X509			(BIO* bp, X509** x, pem_password_cb* cb, void* u);

	void					SNET_STDCALL	RSA_free					(RSA* rsa);
	int						SNET_STDCALL	RSA_private_encrypt			(int flen, unsigned char *from, unsigned char *to, RSA *rsa, int padding);
	int						SNET_STDCALL	RSA_size					(const RSA* rsa);

	void					SNET_STDCALL	SSL_CTX_free				(SSL_CTX* ctx);
	SSL_CTX*				SNET_STDCALL	SSL_CTX_new					(const SSL_METHOD* meth);
	int						SNET_STDCALL	SSL_CTX_use_RSAPrivateKey	(SSL_CTX* ctx, RSA* rsa);
	int						SNET_STDCALL	SSL_CTX_use_certificate		(SSL_CTX* ctx, X509* x);

	void					SNET_STDCALL	SSL_free					(SSL* ssl);
	int						SNET_STDCALL	SSL_get_error				(const SSL* ssl, int ret);
	int						SNET_STDCALL	SSL_get_fd					(const SSL* ssl);
	int						SNET_STDCALL	SSL_library_init			();
	void					SNET_STDCALL	SSL_load_error_strings		();
	SSL*					SNET_STDCALL	SSL_new						(SSL_CTX* ctx);
	int						SNET_STDCALL	SSL_pending					(const SSL *ssl);
	int						SNET_STDCALL	SSL_read					(SSL* ssl, void* buf, int num);
	void					SNET_STDCALL	SSL_set_connect_state		(SSL* ssl);
	void					SNET_STDCALL	SSL_set_accept_state		(SSL* ssl);
	int						SNET_STDCALL	SSL_set_fd					(SSL* ssl, int fd);
	int						SNET_STDCALL	SSL_shutdown				(SSL* ssl);
	int						SNET_STDCALL	SSL_use_certificate			(SSL* ssl, X509* x);
	int						SNET_STDCALL	SSL_use_RSAPrivateKey		(SSL* ssl, RSA* rsa);
	int						SNET_STDCALL	SSL_write					(SSL* ssl, const void* buf, int num);

	const SSL_METHOD*		SNET_STDCALL	SSLv23_method				();

	void					SNET_STDCALL	X509_free					(X509* x);

	// Used by VSslDelegate::HandShake() (SSJS socket implementation).
	int						SNET_STDCALL	SSL_connect					(SSL *ssl);
}


#endif
