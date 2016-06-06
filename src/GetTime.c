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
static char Content[] = "Content-Type: application/json; charset=UTF-8";
static char ConfigXML_time[32];
static char FW_version[32];
extern int endpoint_url;

static int GetTime_ret = BV_FAILURE;

/* Set test Global value. At final release this */
static int GetTime_Response(void* ptr, size_t size, size_t nmemb, void* stream)
{
	const char* tem;
	if (strstr(ptr, "error"))
	{
		logger_cloud("Request return with error: %s", ptr);
		GetTime_ret = BV_FAILURE;
		return 0;
	}

	tem = process_json_data((char*)ptr, "time", 0);
	logger_cloud("Time: %s", tem);
	char cmd[64];
	sprintf((char*)&cmd[0], "/bin/date %s", tem);
	//    logger_cloud("Set date command: %s", (char *) &cmd);
	system((char*)&cmd[0]);
	system("/sbin/hwclock -w");
	return nmemb;
}

static size_t GetTime_Header_Response(char* buffer, size_t size, size_t nitems, void* userdata)
{
	if (strstr(buffer, "HTTP"))
	{
		if (strstr(buffer, "200 OK"))
		{
			GetTime_ret = BV_SUCCESS;
		}
		else
		{
			logger_cloud("%s: %s", __FUNCTION__, buffer);
			GetTime_ret = BV_FAILURE;
		}
	}
	return nitems;
}

char* GetConfigXMLTime()
{
	struct stat st;
	bzero((char*)&ConfigXML_time[0], 32);
	if (stat("/odi/conf/config.xml", &st))
	{
		logger_cloud("Stat config.xml error");
	}
	else
	{
		sprintf((char*)&ConfigXML_time[0], "Conf_time: %lu", st.st_mtim.tv_sec);
	}

	logger_cloud("Time: %s", (char*)&ConfigXML_time[0]);
	return (char*)&ConfigXML_time[0];
}

char* GetFWVersion()
{
	bzero((char*)&FW_version[0], 32);
	sprintf((char*)&FW_version[0], "FW_version: %s", getVersion());
	logger_cloud("Version: %s", (char*)&FW_version[0]);
	return (char*)&FW_version[0];
}

int GetTime()
{
	CURL* curl;
	CURLcode res;
	struct curl_slist* headers = NULL;
	char author[64];

	//logger_cloud("%s: Entering ...", __FUNCTION__);

	bzero((char*)&author[0], 64);
	sprintf((char*)&author[0], CLOUD_AUTHORIZE_TIME, p_main_token);
	//headers = curl_slist_append(headers, (char *) &Accept);
	headers = curl_slist_append(headers, CLOUD_ACCEPT);
	headers = curl_slist_append(headers, (char*)&author[0]);
	headers = curl_slist_append(headers, CLOUD_DEVICE);
	headers = curl_slist_append(headers, (char*)&Content[0]);
	headers = curl_slist_append(headers, GetFWVersion());
	headers = curl_slist_append(headers, GetConfigXMLTime());

	/* In windows, this will init the Winsock stuff */
	curl_global_init(CURL_GLOBAL_ALL);

	/* get a curl handle */
	curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GetTime_Response);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, GetTime_Header_Response);

		if (endpoint_url == 1)
		{
			curl_easy_setopt(curl, CURLOPT_URL, QA_AWS_TIME);
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
		}
		else
		{
			curl_easy_setopt(curl, CURLOPT_URL, PROD_AWS_TIME);
			curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
		}

		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
		//        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

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
	curl_global_cleanup();

	return GetTime_ret;
}
