#include "Flash2AppSM_hash.h"

#ifdef HOST_BUILD //setup internal pem files on HOST
#define KEYF  "../../conf/MobileHD-SU-key.pem"
#define CERTF "../../conf/MobileHD-SU-cert.pem"
#define FACTF "../../conf/Mobile_HD_fact-cert.pem"
#else			// All target pem files on define location
#define KEYF  "/odi/conf/MobileHD-SU-key.pem"
#define CERTF "/odi/conf/MobileHD-SU-cert.pem"
#define FACTF "/odi/conf/Mobile_HD_fact-cert.pem"
#endif

#define FLASH2_RESULT int
#define FLASH2_FAILURE -1
#define FLASH2_SUCCESS 0

static void SMSSLInit(void)
{
	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();
	//ERR_load_crypto_strings();

	/* Init available hardware crypto engines. */
	ENGINE_load_builtin_engines();
	ENGINE_register_all_complete();
}

FLASH2_CHAR* Flash2AppSMGetCertificate(void)
{
	static char cert[MAX_CERT_SIZE];

	FILE* fp;
	int len;
	int init = 0;

	if (init == 0)
	{
		fp = fopen(CERTF, "r");
		if (fp == NULL)
		{
			printf("error opening cert file\n");
			return NULL;
		}

		len = fread(cert, 1, MAX_CERT_SIZE, fp);

		if (len <= 0)
		{
			printf("error reading certificate\n");
			fclose(fp);
			return NULL;
		}

		cert[len] = '\0';

		fclose(fp);

		init = 1;
	}

	return cert;
}

SM_MEDIA_HANDLE Flash2AppSMGenerateHashOpen(void)
{
	SM_MEDIA_HANDLE h;

	h = (SM_MEDIA_HANDLE)malloc(sizeof(*h));
	if (h)
	{
		/* Just load the crypto library error strings,
		* SSL_load_error_strings() loads the crypto AND the SSL ones */
		/* SSL_load_error_strings();*/
		//ERR_load_crypto_strings();

		{
			FILE* fp;

			/* Read private key */
			fp = fopen(KEYF, "r");
			if (fp == NULL)
			{
				printf("error opening keyfile\n");
				free(h);
				return NULL;
			}

			h->pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
			fclose(fp);
		}


		if (h->pkey == NULL)
		{
			printf("error reading key\n");
			free(h);
			return NULL;
		}

		/* Do the signature */

		EVP_SignInit(&h->md_ctx, SM_HASH_FUNCTION);
	}

	return h;
}

FLASH2_RESULT Flash2AppSMGenerateHashClose(SM_MEDIA_HANDLE h, SM_MEDIA_HASH metadata, FLASH2_UINT32* metadata_len)
{
	int err, ret = FLASH2_FAILURE;

	if (h)
	{
		if (metadata == NULL)
		{
			return FLASH2_FAILURE;
		}

		if (metadata_len == NULL || ((*metadata_len) < sizeof(*metadata)))
		{
			printf("error: hash buffer too small\n");
			return FLASH2_FAILURE;
		}

		err = EVP_SignFinal(&h->md_ctx, metadata->data, metadata_len, h->pkey);

		if (err == 1)
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

FLASH2_RESULT Flash2AppSMVerifyHashWrite(SM_MEDIA_HANDLE h, void* buffer, FLASH2_UINT32 buffer_len)
{
	int err;

	if (h == NULL)
	{
		return FLASH2_FAILURE;
	}

	err = EVP_VerifyUpdate(&h->md_ctx, buffer, buffer_len);

	if (err != 1)
	{
		return FLASH2_FAILURE;
	}

	return FLASH2_SUCCESS;
}

FLASH2_RESULT Flash2AppSMVerifyHashClose(SM_MEDIA_HANDLE h)
{
	int err, ret = FLASH2_FAILURE;

	if (h)
	{
		err = EVP_VerifyFinal(&h->md_ctx, h->hash.data, h->hash_len, h->pkey);

		if (err == 1)
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

SM_MEDIA_HANDLE Flash2AppSMVerifyHashOpen(SM_MEDIA_HASH metadata, FLASH2_UINT32 metadata_len, char* certificate)
{
	FILE* fp;
	X509* x509;
	int len;

	SM_MEDIA_HANDLE h;

	if (metadata == NULL)
	{
		return NULL;
	}

	if (metadata_len > sizeof(h->hash))
	{
		printf("error: hash too large\n");
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

		//ERR_load_crypto_strings();

		/* Read public key */

		fp = tmpfile();

		if (fp == NULL)
		{
			printf("error opening temp file\n");
			free(h);
			return NULL;
		}

		len = strlen(certificate);

		if (fwrite(certificate, 1, len, fp) != len)
		{
			printf("error writing to temp file\n");
			fclose(fp);
			free(h);
			return NULL;
		}

		rewind(fp);

		x509 = PEM_read_X509(fp, NULL, NULL, NULL);

		fclose(fp);

		if (x509 == NULL)
		{
			printf("error reading certificate\n");
			free(h);
			return NULL;
		}

		h->pkey = X509_get_pubkey(x509);

		X509_free(x509);

		if (h->pkey == NULL)
		{
			printf("error getting public key\n");
			free(h);
			return NULL;
		}

		/* Do the verification */
		EVP_VerifyInit(&h->md_ctx, SM_HASH_FUNCTION);
	}

	return h;
}

FLASH2_RESULT Flash2AppSMGenerateHashWrite(SM_MEDIA_HANDLE h, void* buffer, FLASH2_UINT32 buffer_len)
{
	int err;

	if (h == NULL)
	{
		return FLASH2_FAILURE;
	}

	err = EVP_SignUpdate(&h->md_ctx, buffer, buffer_len);

	if (err != 1)
	{
		return FLASH2_FAILURE;
	}

	return FLASH2_SUCCESS;
}
