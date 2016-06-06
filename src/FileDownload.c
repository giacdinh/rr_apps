#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include "BVcloud.h"

static int upload_bytes = 0;
static int FileDownload_ret = BV_FAILURE;
extern int endpoint_url;

static int FileDownload_writecallback(void* ptr, size_t size, size_t nmemb, void* stream)
{
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}
static size_t FileDownload_Header_Response(char* buffer, size_t size, size_t nitems, void* userdata)
{
	if (strstr(buffer, "HTTP"))
	{
		if (strstr(buffer, "200 OK") || strstr(buffer, "100"))
		{
			FileDownload_ret = BV_SUCCESS;
		}
		else
		{
			logger_cloud(buffer);
			FileDownload_ret = BV_FAILURE;
		}
	}
	return nitems;
}

int FileDownload(char* signedurl, char* filename)
{
	CURL* curl;
	CURLcode res;
	FILE* download;
	char abs_dl_name[64];
	char* p_abs_dl_name = (char*)&abs_dl_name[0];
	bzero(p_abs_dl_name, 64);
	strcpy(p_abs_dl_name, "/odi/data/upload/BodyVision_");
	strcat(p_abs_dl_name, filename);

	logger_cloud("Firmware to download: %s", p_abs_dl_name);
	download = fopen(p_abs_dl_name, "wb");

	/* get a curl handle */
	curl = curl_easy_init();
	if (curl)
	{

		curl_easy_setopt(curl, CURLOPT_URL, signedurl);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileDownload_writecallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, download);
		//Setup CA path for amazon
		//curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/cacert.pem");
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
			logger_cloud("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	fclose(download);
	curl_global_cleanup();

	return FileDownload_ret;
}
