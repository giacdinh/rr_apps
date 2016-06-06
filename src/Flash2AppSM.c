#include "Flash2AppSM_internal.h"
#define SM_HASH_FUNCTION EVP_sha1()
SSL_CTX* sm_global_ctx = NULL;

#define SW_CERTIFICATE		1
#define PRIVATE_KEY			2
#define UNIT_CERTIFICATE	3
#define FACT_CERTIFICATE	4
#define MAX_SECURITY_DATA	4

/* Default HW cryptodevice for the Flash project is the OCF cryptodev */
#define FLASH2_SM_HW_ENGINE	"cryptodev"
#define SM_CIPHER_STRING	"DES-CBC-SHA"
//#define SM_CIPHER_STRING	"RC4-MD5:DES-CBC3-SHA"
#define DEVICE_ID_STRLEN 11
#define MAX_CERT_SIZE   2048

static int Flash2AppSMWriteFile(char* filename, char* buf, int buflen)
{
	int f;
	int len;
	f = creat(filename, S_IRWXU);
	if (f <= 0)
	{
		printf("error in open(%s)\n", filename);
		return -1;
	}
	len = write(f, buf, buflen);
	close(f);
	if (len != buflen)
	{
		printf("error in write()\n");
		return -1;
	}
	return len;
}

static void SMSSLInit(void)
{
	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	/* Init available hardware crypto engines. */
	ENGINE_load_builtin_engines();
	ENGINE_register_all_complete();
}

int Flash2AppSMInit(char* certf, char* keyf, char* factf)
{
	static int init = 0;
	if (0 == init)
	{
		/* SSL preliminaries. We keep the certificate and
		* key with the context. */
		SMSSLInit();
		if ((!certf) || (!keyf) || (!factf))
		{
			fprintf(stderr, "[Flash2AppSMInit] One or more arguments passed to function was NULL\n");
			return FLASH2_FAILURE;
		}
		sm_global_ctx = SSL_CTX_new(SSLv3_method());
		if (!sm_global_ctx)
		{
			ERR_print_errors_fp(stderr);
			return FLASH2_FAILURE;
		}
		if (0 == SSL_CTX_set_cipher_list(sm_global_ctx, SM_CIPHER_STRING))
		{
			ERR_print_errors_fp(stderr);
			return FLASH2_FAILURE;
		}
		if (SSL_CTX_use_certificate_file(sm_global_ctx, certf, SSL_FILETYPE_PEM) <= 0)
		{
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(sm_global_ctx);
			sm_global_ctx = NULL;
			return FLASH2_FAILURE;
		}
		if (SSL_CTX_use_PrivateKey_file(sm_global_ctx, keyf, SSL_FILETYPE_PEM) <= 0)
		{
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(sm_global_ctx);
			sm_global_ctx = NULL;
			return FLASH2_FAILURE;
		}
		if (!SSL_CTX_load_verify_locations(sm_global_ctx, factf, NULL))
		{
			fprintf(stderr, "[Flash2AppSMInit] Error loading Factory Certificate.\n");
			SSL_CTX_free(sm_global_ctx);
			sm_global_ctx = NULL;
			return FLASH2_FAILURE;
		}
		if (!SSL_CTX_check_private_key(sm_global_ctx))
		{
			fprintf(stderr, "[Flash2AppSMInit] Private key does not match the certificate public key\n");
			SSL_CTX_free(sm_global_ctx);
			sm_global_ctx = NULL;
			return FLASH2_FAILURE;
		}
		SSL_CTX_set_verify(sm_global_ctx, SSL_VERIFY_PEER, NULL);
		SSL_CTX_set_verify_depth(sm_global_ctx, 2);
		init = 1;
	}
	return FLASH2_SUCCESS;
}
///////////////////
FLASH2_CHAR* Flash2AppSMGetCertificate(char* certf)
{
	static char cert[MAX_CERT_SIZE];
	FILE *fp;
	int len;
	int init = 0;
	if (0 == init)
	{
		if (!certf)
		{
			fprintf(stderr, "[Flash2AppSMGetCertificate] Argument passed to function was NULL\n");
			return NULL;
		}
		fp = fopen(certf, "r");
		if (NULL == fp)
		{
			perror("[Flash2AppSMGetCertificate] Error opening cert file");
			return NULL;
		}
		len = fread(cert, 1, MAX_CERT_SIZE, fp);
		if (len <= 0)
		{
			fprintf(stderr, "[Flash2AppSMGetCertificate] Error reading certificate\n");
			fclose(fp);
			return NULL;
		}
		cert[len] = '\0';
		fclose(fp);
		init = 1;
	}
	return cert;
}
///////////////////
SM_MEDIA_HANDLE Flash2AppSMGenerateHashOpen(char* keyf)
{
	SM_MEDIA_HANDLE h;
	if (!keyf)
	{
		fprintf(stderr, "[Flash2AppSMGenerateHashOpen] Argument passed to function was NULL\n");
		return NULL;
	}
	h = (SM_MEDIA_HANDLE)malloc(sizeof(*h));
	if (h)
	{
		/* Just load the crypto library error strings,
		* SSL_load_error_strings() loads the crypto AND the SSL ones */
		/* SSL_load_error_strings();*/
		ERR_load_crypto_strings();
		{
			FILE* fp;
			/* Read private key */
			fp = fopen(keyf, "r");
			if (NULL == fp)
			{
				perror("[Flash2AppSMGenerateHashOpen] Error opening keyfile");
				free(h);
				return NULL;
			}
			h->pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}
		if (NULL == h->pkey)
		{
			fprintf(stderr, "[Flash2AppSMGenerateHashOpen] Error reading key %s \n", keyf);
			free(h);
			return NULL;
		}
		/* Do the signature */
		EVP_SignInit(&h->md_ctx, SM_HASH_FUNCTION);
	}
	return h;
}

FLASH2_RESULT Flash2AppSMGenerateHashWrite(SM_MEDIA_HANDLE h, void* buffer, FLASH2_UINT32 buffer_len)
{
	int is_no_err;
	if (NULL == h)
	{
		return FLASH2_FAILURE;
	}
	is_no_err = EVP_SignUpdate(&h->md_ctx, buffer, buffer_len);
	if (is_no_err != 1)
	{
		ERR_print_errors_fp(stderr);
		return FLASH2_FAILURE;
	}
	return FLASH2_SUCCESS;
}

FLASH2_RESULT Flash2AppSMGenerateHashClose(SM_MEDIA_HANDLE h, SM_MEDIA_HASH metadata, FLASH2_UINT32* metadata_len)
{
	int is_no_err, ret = FLASH2_FAILURE;
	if (h)
	{
		if (NULL == metadata)
		{
			return FLASH2_FAILURE;
		}
		if ((NULL == metadata_len) || ((*metadata_len) < sizeof(*metadata)))
		{
			fprintf(stderr, "[Flash2AppSMGenerateHashClose] error because hash buffer is too small\n");
			return FLASH2_FAILURE;
		}
		is_no_err = EVP_SignFinal(&h->md_ctx, metadata->data, metadata_len, h->pkey);
		if (1 == is_no_err)
		{
			ret = FLASH2_SUCCESS;
		}
		if (h->pkey)
		{
			EVP_PKEY_free(h->pkey);
		}
		free(h);
	}
	return ret;
}
///////////////////
SM_MEDIA_HANDLE Flash2AppSMVerifyHashOpen(SM_MEDIA_HASH metadata, FLASH2_UINT32 metadata_len, char* certificate)
{
	FILE* fp;
	X509* x509;
	int len;
	SM_MEDIA_HANDLE h;
	if (NULL == metadata)
	{
		return NULL;
	}
	if (metadata_len > sizeof(h->hash))
	{
		fprintf(stderr, "[Flash2AppSMVerifyHashOpen] Error because hash is too large\n");
		return NULL;
	}
	h = (SM_MEDIA_HANDLE)malloc(sizeof(*h));
	if (h)
	{
		/* save metadata */
		memcpy(&h->hash, metadata, metadata_len);
		h->hash_len = metadata_len;
		/* Just load the crypto library error strings,
		* SSL_load_error_strings() loads the crypto AND the SSL ones */
		/* SSL_load_error_strings();*/
		ERR_load_crypto_strings();
		/* Read public key */
		fp = tmpfile();
		if (NULL == fp)
		{
			fprintf(stderr, "[Flash2AppSMVerifyHashOpen] Error opening temp file\n");
			free(h);
			return NULL;
		}
		len = strlen(certificate);
		if (fwrite(certificate, 1, len, fp) != len)
		{
			fprintf(stderr, "[Flash2AppSMVerifyHashOpen] Error writing to temp file\n");
			fclose(fp);
			free(h);
			return NULL;
		}
		rewind(fp);
		x509 = PEM_read_X509(fp, NULL, NULL, NULL);
		fclose(fp);
		if (x509 == NULL)
		{
			fprintf(stderr, "[Flash2AppSMVerifyHashOpen] Error reading certificate\n");
			free(h);
			return NULL;
		}
		h->pkey = X509_get_pubkey(x509);
		X509_free(x509);
		if (h->pkey == NULL)
		{
			fprintf(stderr, "[Flash2AppSMVerifyHashOpen] Error getting public key\n");
			free(h);
			return NULL;
		}
		/* Do the verification */
		EVP_VerifyInit(&h->md_ctx, SM_HASH_FUNCTION);
	}
	return h;
}

FLASH2_RESULT Flash2AppSMVerifyHashWrite(SM_MEDIA_HANDLE h, void* buffer, FLASH2_UINT32 buffer_len)
{
	int is_no_err;
	if (NULL == h)
	{
		return FLASH2_FAILURE;
	}
	is_no_err = EVP_VerifyUpdate(&h->md_ctx, buffer, buffer_len);
	if (is_no_err != 1)
	{
		return FLASH2_FAILURE;
	}
	return FLASH2_SUCCESS;
}

FLASH2_RESULT Flash2AppSMVerifyHashClose(SM_MEDIA_HANDLE h)
{
	int is_no_err, ret = FLASH2_FAILURE;
	if (h)
	{
		is_no_err = EVP_VerifyFinal(&h->md_ctx, h->hash.data, h->hash_len, h->pkey);
		if (is_no_err == 1)
		{
			ret = FLASH2_SUCCESS;
		}
		if (h->pkey)
		{
			EVP_PKEY_free(h->pkey);
		}
		free(h);
	}
	return ret;
}

#ifdef NOTUSE
SM_MEDIA_HANDLE Flash2AppSMVerifySUOpen(SM_MEDIA_HASH metadata, FLASH2_UINT32 metadata_len)
{
	static char cert[MAX_CERT_SIZE];
	if (Flash2AppSMReadSecurityData(SW_CERTIFICATE, cert, MAX_CERT_SIZE)>0)
	{
		return Flash2AppSMVerifyHashOpen(metadata, metadata_len, cert);
	}
	return NULL;
}
#endif

FLASH2_RESULT Flash2AppSMVerifySUWrite(SM_MEDIA_HANDLE h, void* buffer, FLASH2_UINT32 buffer_len)
{
	return Flash2AppSMVerifyHashWrite(h, buffer, buffer_len);
}

FLASH2_RESULT Flash2AppSMVerifySUClose(SM_MEDIA_HANDLE h)
{
	return Flash2AppSMVerifyHashClose(h);
}

//////// Commented out because of https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=653235
//FLASH2_RESULT Flash2AppSMVerifyCertificate(char *certificate)
//{
//	X509 *x509=NULL;
//	STACK_OF(X509) *sk=NULL;
//	SSL *s=NULL;
//	FILE *fp=NULL;
//	int len, i;
//	/* Read public key */
//	fp = tmpfile();
//	if (fp == NULL)
//	{
//	  printf("error opening temp file\n");
//	  return FLASH2_FAILURE;
//	}
//	len=strlen(certificate);
//	if (fwrite(certificate, 1, len, fp)!=len)
//	{
//	  printf("error writing to temp file\n");
//	  fclose(fp);
//	  return FLASH2_FAILURE;
//	}
//	rewind(fp);
//	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
//	fclose (fp);
//	if (x509 == NULL)
//	{
//	  printf("error reading certificate\n");
//	  return FLASH2_FAILURE;
//	}
//	if (sm_global_ctx == NULL)
//	{
//		printf("error Flash2AppSM not initialized\n");
//		return FLASH2_FAILURE;
//	}
//	s=SSL_new (sm_global_ctx);
//	if (s==NULL)
//	{
//		ERR_print_errors_fp(stderr);
//		return FLASH2_FAILURE;
//	}
//	sk=sk_X509_new_null();
//	if (sk==NULL)
//	{
//		ERR_print_errors_fp(stderr);
//		return FLASH2_FAILURE;
//	}
//	sk_X509_push(sk,x509);
//	i=ssl_verify_cert_chain(s, sk);
//	sk_X509_pop_free(sk,X509_free);
//	SSL_free(s);
//	if (i>0)
//	{
//		return FLASH2_SUCCESS;
//	}
//	 else
//	{
//		return FLASH2_FAILURE;
//	}
//}
#ifdef NOTUSE
FLASH2_UINT32 Flash2AppSMGetSerialNumber(void)
{
	char *str, *ptr;
	unsigned char buf[MAX_CERT_SIZE];
	FILE* fp;
	X509* x509;
	int len;
	FLASH2_UINT32 id = 0xFFFFFFFF;
	FLASH2_CHAR deviceid[DEVICE_ID_STRLEN + 1];
	ERR_load_crypto_strings();
	len = Flash2AppSMReadSecurityData(UNIT_CERTIFICATE, buf, MAX_CERT_SIZE);
	if (len <= 0)
	{
		fprintf(stderr, "[Flash2AppSMGetSerialNumber] Error in Flash2AppSMReadSecurityData() \n");
		return 0;
	}
	fp = tmpfile();
	if (NULL == fp)
	{
		perror("[Flash2AppSMGetSerialNumber] Error opening temp file\n");
		return 0;
	}
	if (fwrite(buf, 1, len, fp) != len)
	{
		printf("error writing to temp file\n");
		fclose(fp);
		return 0;
	}
	rewind(fp);
	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
	fclose(fp);
	if (x509 == NULL)
	{
		printf("error reading certificate\n");
		return 0;
	}
	str = X509_NAME_oneline(X509_get_subject_name(x509), 0, 0);
	if (str)
	{
		ptr = strstr(str, "/CN=");
		if (ptr)
		{
			if (strlen(ptr) == 4 + DEVICE_ID_STRLEN)
			{
				strcpy(deviceid, ptr + 4);
				//CR3825 Remove print line to clean up for REMOTEM API
				//printf("\t unit deviceid: %s\n",deviceid);
				if (memcmp(deviceid, "FB_", 3) == 0)
				{
					sscanf(deviceid + 3, "%x", &id);
				}
				else printf("error parsing unit certificate - incorrect certificate type\n");
			}
			else printf("error parsing unit certificate - invalid device id\n");
		}
		else printf("error parsing unit certificate - can't find CN\n");
		OPENSSL_free(str);
	}
	else printf("error parsing unit certificate - can't find subject\n");
	X509_free(x509);
	return id;
}
#endif

//////////////////////
#define MAX_PATH 1024
static FLASH2_RESULT Flash2AppSMHashFile(char* keyf, unsigned char* name, void* sig_buf, FLASH2_UINT32* sig_len)
{
	FLASH2_RESULT ret_code;
	SM_MEDIA_HANDLE h;
	int f, len;
	static char buf[1024];
	if (!keyf)
	{
		fprintf(stderr, "[Flash2AppSMHashFile] Argument keyf, passed to function was NULL\n");
		return FLASH2_FAILURE;
	}
	if (!name)
	{
		fprintf(stderr, "[Flash2AppSMHashFile] Argument name, passed to function was NULL\n");
		return FLASH2_FAILURE;
	}
	f = open(name, O_RDONLY);
	if (f <= 0)
	{
		perror("[Flash2AppSMHashFile] Error in open()\n");
		return FLASH2_FAILURE;
	}
	h = Flash2AppSMGenerateHashOpen(keyf);
	if (NULL == h)
	{
		fprintf(stderr, "[Flash2AppSMHashFile] Error in Flash2AppSMMediaOpen()\n");
		close(f);
		return FLASH2_FAILURE;
	}
	while (1)
	{
		len = read(f, buf, 1024);
		if (len < 0)
		{
			fprintf(stderr, "[Flash2AppSMHashFile] Error in read()\n");
			close(f);
			return FLASH2_FAILURE;
		}
		if (0 == len)
		{
			break;
		}
		ret_code = Flash2AppSMGenerateHashWrite(h, buf, len);
		if (ret_code != FLASH2_SUCCESS)
		{
			fprintf(stderr, "[Flash2AppSMHashFile] Error in Flash2AppSMMediaHash()\n");
			close(f);
			return FLASH2_FAILURE;
		}
	}
	close(f);
	return Flash2AppSMGenerateHashClose(h, sig_buf, sig_len);
}
/*
* vim:ft=c:fdm=marker:ff=unix:expandtab: tabstop=4: shiftwidth=4: autoindent: smartindent:
*/
