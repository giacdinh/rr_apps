#ifndef _FLASH2SM_INTERNAL
#define _FLASH2SM_INTERNAL

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include <openssl/rsa.h>       /* SSLeay stuff */
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/engine.h>

#define DEVICE_ID_STRLEN 11

#define MAX_CERT_SIZE	2048

#define FLASH2_SM_MEDIA_HASH_SIZE 128

#define FLASH2_FAILURE -1
#define FLASH2_SUCCESS 0
#define FLASH2_UINT32 int
#define FLASH2_CHAR char
#define FLASH2_RESULT int

#if 0
typedef enum enum_flash2_result { FLASH2_FAILURE=0, FLASH2_SUCCESS=1 } FLASH2_RESULT;
typedef void FLASH2_VOID;
typedef unsigned long FLASH2_UINT32;
#endif

// secure communication

typedef struct
{
  SSL_CTX* ctx;
  SSL*     ssl;
  X509*    peer_cert;
  int	   sd;
} _SM_HANDLE;

typedef _SM_HANDLE * SM_HANDLE;

// media hashing

typedef struct
{
	unsigned char data[FLASH2_SM_MEDIA_HASH_SIZE];
} _SM_MEDIA_HASH;

typedef _SM_MEDIA_HASH * SM_MEDIA_HASH;

typedef struct
{
	EVP_MD_CTX     md_ctx;
	EVP_PKEY *     pkey;
	_SM_MEDIA_HASH  hash;
	int				hash_len;
} _SM_MEDIA_HANDLE;

typedef _SM_MEDIA_HANDLE * SM_MEDIA_HANDLE;

int ssl_verify_cert_chain(SSL *s,STACK_OF(X509) *sk);

int Flash2AppSMInit(char *certf, char *keyf, char *factf);
SM_MEDIA_HANDLE Flash2AppSMGenerateHashOpen(char *keyf);
FLASH2_RESULT Flash2AppSMGenerateHashWrite(SM_MEDIA_HANDLE h, void *buffer, FLASH2_UINT32 buffer_len);
FLASH2_RESULT Flash2AppSMGenerateHashClose(SM_MEDIA_HANDLE h, SM_MEDIA_HASH metadata, FLASH2_UINT32 *metadata_len);
FLASH2_CHAR *Flash2AppSMGetCertificate(char *certf);
SM_MEDIA_HANDLE Flash2AppSMGenerateHashOpen(char *keyf);
#endif //_FLASH2SM_INTERNAL
