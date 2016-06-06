#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include <stdlib.h>
#include "BVcloud.h"

/* Parameters setup */
#define UPLOAD_DATA_STR  "username=%s&password=%s&grant_type=password&scope=read write&client_secret=mySecretOAuthSecret&client_id=BodyVisionapp"
extern char* getSerial();
extern int endpoint_url;

static char l_refresh_token[64];
static char local_token[64];

static int GetToken_ret = BV_FAILURE;

/* Set test Global value. At final release this */
static int GetToken_Response(void* ptr, size_t size, size_t nmemb, void* stream)
{
	const char* tem;
	if (strstr(ptr, "error"))
	{
		logger_cloud("Request return with error: %s", ptr);
		GetToken_ret = BV_FAILURE;
		return NULL;
	}
	tem = process_json_data((char *)ptr, "access_token", 0);
	memcpy((char*)&local_token[0], tem, strlen(tem));
	logger_cloud("BV: access token: %s", (char*)&local_token[0]);

	tem = process_json_data((char*)ptr, "refresh_token", 0);
	memcpy((char*)&l_refresh_token[0], tem, strlen(tem));
	//    logger_cloud("refresh token: %s", (char *) &l_refresh_token);

	return nmemb;
}

static size_t GetToken_Header_Response(char* buffer, size_t size, size_t nitems, void* userdata)
{
	if (strstr(buffer, "HTTP"))
	{
		if (strstr(buffer, "200 OK"))
		{
			GetToken_ret = BV_SUCCESS;
		}
		else
		{
			logger_cloud(buffer);
			GetToken_ret = BV_FAILURE;
		}
	}
	return nitems;
}

int GetToken(char* token, char* refresh)
{
	CURL* curl;
	CURLcode res;
	struct curl_slist* headers = NULL;

	char* unitID;
	char data_field[256];
	char* p_data_field = (char*)&data_field[0];

	unitID = (char*)getSerial();
	//logger_cloud("%s: Entering ... ", __FUNCTION__);

	memset((char*)&local_token[0], '\0', 64);
	//headers = curl_slist_append(headers, (char *) &Accept);
	headers = curl_slist_append(headers, CLOUD_ACCEPT);
	headers = curl_slist_append(headers, CLOUD_AUTHORIZE);

	sprintf(p_data_field, UPLOAD_DATA_STR, unitID, unitID);
	//logger_cloud("DEBUG: %s", p_data_field);

	/* In windows, this will init the winsock stuff */
	curl_global_init(CURL_GLOBAL_ALL);

	/* get a curl handle */
	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GetToken_Response);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, GetToken_Header_Response);

		if (endpoint_url == 1)
		{
			curl_easy_setopt(curl, CURLOPT_URL, QA_AWS_AUTH);
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
		}
		else
		{
			curl_easy_setopt(curl, CURLOPT_URL, PROD_AWS_AUTH);
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
		}

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		//	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, p_data_field);

		/* Now run off and do what you've been told! */
		res = curl_easy_perform(curl);

		/* Check for errors */
		if (res != CURLE_OK)
		{
			logger_cloud("%s:curl_easy_perform() failed: %s\n", __FUNCTION__, curl_easy_strerror(res));
			GetToken_ret = GETTOKEN_ERR;
		}

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	curl_global_cleanup();
	memcpy(token, (char *)&local_token, 64);
	memcpy(refresh, (char *)&l_refresh_token, 64);

	return GetToken_ret;
}
