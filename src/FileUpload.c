#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include "BVcloud.h"

static int upload_bytes = 0;
static int FileUpload_ret = BV_FAILURE;
extern int endpoint_url;
static char Encryption[] = "x-amz-server-side-encryption: AES256";
//static int FileUpload_Response(void *ptr, size_t size, size_t nmemb, void *stream)
//{
//    return nmemb;
//}

static int FileUpload_readcallback(void* ptr, size_t size, size_t nmemb, void* stream)
{
	size_t retcode;
	curl_off_t nread;
	retcode = fread(ptr, size, nmemb, stream);
	nread = (curl_off_t)retcode;
	static int count = 0;
	upload_bytes += retcode;
	//    if(count > 300)
	//    {
	//	logger_cloud("%s: Upload %d bytes", __FUNCTION__, upload_bytes);
	//	count = 0;
	//    }
	count++;
	return retcode;
}
static size_t FileUpload_Header_Response(char* buffer, size_t size, size_t nitems, void* userdata)
{
	if (strstr(buffer, "HTTP"))
	{
		if (strstr(buffer, "200 OK") || strstr(buffer, "100"))
		{
			FileUpload_ret = BV_SUCCESS;
		}
		else
		{
			logger_cloud(buffer);
			FileUpload_ret = BV_FAILURE;
		}
	}
	return nitems;
}

int FileUpload(char* upload_auth, char* upfile, char* filetype)
{
	CURL* curl;
	CURLcode res;
	FILE* upload;
	struct stat file_info;
	struct curl_slist* headers = NULL;
	char* abs_filename;
	char file_name[64];
	double speed = 0, duration = 0;
	logger_cloud("%s: Entering ...", __FUNCTION__);

	//Reset upload bytes every time
	upload_bytes = 0;

	/* In windows, this will init the Winsock stuff */
	curl_global_init(CURL_GLOBAL_ALL);
	sprintf((char*)&file_name[0], "/odi/data/%s", upfile);
	abs_filename = (char*)&file_name[0];

	stat(abs_filename, &file_info);
	logger_cloud("%s: file: %s, size: %d , type: %s", __FUNCTION__, upfile, file_info.st_size, filetype);
	upload = fopen(abs_filename, "r");
	if (!upload)
	{
		logger_cloud("Upload file open file: %s", abs_filename);
		return BV_FAILURE;
	}
	headers = curl_slist_append(headers, filetype);
	headers = curl_slist_append(headers, (char*)&Encryption[0]);

	/* get a curl handle */
	curl = curl_easy_init();
	if (curl)
	{

		//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileUpload_Response);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, FileUpload_readcallback);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, FileUpload_Header_Response);

		//Set up connection lost handler
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 1L);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_PUT, 1L);
		curl_easy_setopt(curl, CURLOPT_URL, upload_auth);
		curl_easy_setopt(curl, CURLOPT_READDATA, upload);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_info.st_size);

		//Setup CA path for amazon
		if (endpoint_url == 1)
		{
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
		}
		else
		{
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
		}

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);

		/* Now run off and do what you've been told! */
		res = curl_easy_perform(curl);

		/* Check for errors */
		if (res != CURLE_OK)
		{
			logger_cloud("%s:curl_easy_perform() failed: %s\n", __FUNCTION__, curl_easy_strerror(res));
			FileUpload_ret = UPLOAD_ERR;
		}

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	fclose(upload);
	curl_global_cleanup();
	if (upload_bytes != file_info.st_size)
	{
		logger_cloud("%s: ERROR!!! Size missmatch file: %d  -- upload: %d", __FUNCTION__, file_info.st_size, upload_bytes);
	}

	return FileUpload_ret;
}
