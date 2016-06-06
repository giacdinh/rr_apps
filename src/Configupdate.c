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
extern int endpoint_url;

static int GetTime_ret = BV_FAILURE;

/* Set test Global value. At final release this */
static size_t Configupdate_Header_Response(char* buffer, size_t size, size_t nitems, void* userdata)
{
    if(strstr(buffer, "HTTP"))
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

unsigned int get_utc()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

int Configupdate(char* aws_token)
{
    CURL* curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char author[64];
    char payload[64], *p_payload;
    p_payload = (char*)&payload[0];

    //logger_cloud("%s: Entering ...", __FUNCTION__);

    bzero((char*)&author[0], 64);
    sprintf((char*)&author[0], CLOUD_AUTHORIZE_TIME, aws_token);
    headers = curl_slist_append(headers, CLOUD_ACCEPT);
    headers = curl_slist_append(headers, (char*)&author[0]);
    headers = curl_slist_append(headers, CLOUD_DEVICE);
    headers = curl_slist_append(headers, (char*)&Content[0]);

    sprintf(p_payload, "{\"conf_time\":\"%i\",\"revision\":\"%s\",\"fw_version\":\"%s\"}",
            get_utc(), get_hw_version(), getVersion());

    /* In windows, this will init the Winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, Configupdate_Header_Response);

        if(endpoint_url == 1)
        {
            curl_easy_setopt(curl, CURLOPT_URL, QA_AWS_CONFIG);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_URL, PROD_AWS_CONFIG);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
        }

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, p_payload);

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
