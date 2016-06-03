#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Flash2AppSM_hash.h"

FLASH2_RESULT read_hash(unsigned char *name, void *sig_buf, FLASH2_UINT32 *sig_len)
{
	int f;

	f=open(name,O_RDONLY);

	if (f<=0)
	{
		printf("error opening file\n");
		return FLASH2_FAILURE;
	}

	*sig_len=read(f,sig_buf,*sig_len);

	close(f);

	return FLASH2_SUCCESS;
}

FLASH2_RESULT write_hash(unsigned char *name, void *sig_buf, FLASH2_UINT32 sig_len)
{
	int f;

	f=creat(name,S_IRWXU|S_IRWXG|S_IRWXO);

	if (f<=0)
	{
		printf("error creating file\n");
		return FLASH2_FAILURE;
	}

	write(f,sig_buf,sig_len);

	close(f);

	return FLASH2_SUCCESS;
}


FLASH2_RESULT generate_hash(unsigned char *name, void *sig_buf, FLASH2_UINT32 *sig_len)
{
	FLASH2_RESULT err;
	SM_MEDIA_HANDLE h;
	int f, len;
	static char buf[1024];

	f=open(name,O_RDONLY);

	if (f<=0)
	{
		printf("error in fopen()\n");
		return FLASH2_FAILURE;
	}

	h=(SM_MEDIA_HANDLE) Flash2AppSMGenerateHashOpen();

	if (h==NULL)
	{
		printf("error in Flash2AppSMMediaOpen()\n");
		close(f);
		return FLASH2_FAILURE;
	}

	while (1)
	{
		len=read(f,buf,1024);

		if (len<0)
		{
			printf("error in read()\n");
			close(f);
			return FLASH2_FAILURE;
		}

		if (len==0)
			break;

		err=Flash2AppSMGenerateHashWrite(h, buf, len);

		if (err!=FLASH2_SUCCESS)
		{
			printf("error in Flash2AppSMMediaHash()\n");
			close(f);
			return FLASH2_FAILURE;
		}
	}

	close(f);

	return Flash2AppSMGenerateHashClose(h, sig_buf, sig_len);

}

FLASH2_RESULT verify_hash(unsigned char *name, void *sig_buf, FLASH2_UINT32 sig_len)
{
	FLASH2_RESULT err;
	SM_MEDIA_HANDLE h;
	int f, len;
	static char buf[1024];

	f=open(name,O_RDONLY);

	if (f<=0)
	{
		printf("error in fopen()\n");
		return FLASH2_FAILURE;
	}

	h=(SM_MEDIA_HANDLE) Flash2AppSMVerifyHashOpen(sig_buf, sig_len, Flash2AppSMGetCertificate());

	if (h==NULL)
	{
		printf("error in Flash2AppSMMediaOpen()\n");
		close(f);
		return FLASH2_FAILURE;
	}

	while (1)
	{
		len=read(f,buf,1024);

		if (len<0)
		{
			printf("error in read()\n");
			close(f);
			return FLASH2_FAILURE;
		}

		if (len==0)
			break;

		err=Flash2AppSMVerifyHashWrite(h, buf, len);

		if (err!=FLASH2_SUCCESS)
		{
			printf("error in Flash2AppSMMediaHash()\n");
			close(f);
			return FLASH2_FAILURE;
		}
	}

	close(f);

	return Flash2AppSMVerifyHashClose(h);

}

#undef FLASH2_SM_MEDIA_HASH_SIZE
#define FLASH2_SM_MEDIA_HASH_SIZE 2048

int main (int argc, char *argv[])
{
	FLASH2_UINT32 sig_len = FLASH2_SM_MEDIA_HASH_SIZE;
	FLASH2_RESULT err = FLASH2_FAILURE;

	static unsigned char sig_buf [FLASH2_SM_MEDIA_HASH_SIZE];

	if ((argc<4) || (argv[1][0]!='c' && argv[1][0]!='v'))
	{
		printf("usage: hash_test [c | v] input-file hash-file\n");
		exit(0);
	}

	switch(argv[1][0])
	{
		case 'c':
			printf("creating hash...");

			err=generate_hash(argv[2], sig_buf, &sig_len);

			if (err!=FLASH2_SUCCESS)
				break;

			err=write_hash(argv[3], sig_buf, sig_len);
			break;

		case 'v':
			printf("verifying hash...");

			err=read_hash(argv[3], sig_buf, &sig_len);

			if (err!=FLASH2_SUCCESS)
				break;

			err=verify_hash(argv[2], sig_buf, sig_len);
			break;
	}

	sig_len=sizeof(sig_buf);


	if (err!=FLASH2_SUCCESS)
	{
		printf("failed.\n\n");
		exit(10);
	}
	 else
	{
		printf("success.\n");
		exit(0);
	}
}

FLASH2_RESULT Flash2AppCMGetData(FLASH2_UINT32 ModuleID,
         FLASH2_UINT32  CMItemID,
          FLASH2_UINT32        ItemSizeInBytes,
           FLASH2_VOID          *pData)
{
        return FLASH2_FAILURE;
}

FLASH2_RESULT Flash2AppCMSetData(FLASH2_UINT32  ModuleID,
         FLASH2_UINT32 CMItemID,
          FLASH2_UINT32       ItemSizeInBytes,
           FLASH2_VOID         *pData)
{
        return FLASH2_FAILURE;
}

