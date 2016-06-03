#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include <stdlib.h>
#include "BVcloud.h"

/* Parametters setup */
static char Content[] = "Content-Type: application/json; charset=UTF-8";
static char Filename[64]; 
static int LogGetSignedURL_ret = BV_FAILURE;
static char local_signedURL[1024];
extern int endpoint_url;
 
static int LogGetSignedURL_Response(void *ptr, size_t size, size_t nmemb, void *stream)
{
    const char *tem;
    if(strstr(ptr, "error"))
    {
        logger_cloud("Request return with error: %s", ptr);
        LogGetSignedURL_ret = BV_FAILURE;
        return 0;
    }
    bzero((char *) &local_signedURL, 1024);
    tem = process_json_data(ptr, "signedURL", 0);
    //logger_cloud("%s: %s", __FUNCTION__, tem);
    memcpy((char *) &local_signedURL, tem, SIGNEDURL_SIZE);

    return nmemb;
}

static size_t LogGetSignedURL_Header_Response(char *buffer, size_t size, size_t nitems, void *userdata)
{
    if(strstr(buffer, "HTTP"))
    {
        if(strstr(buffer, "200 OK"))
            LogGetSignedURL_ret = BV_SUCCESS;
	else
	{
	    logger_cloud(buffer);
	    LogGetSignedURL_ret = BV_FAILURE;
	}
    }
    return nitems;
}
 
int LogGetSignedURL(char *Auth_token, char *signedURL , char * upfile)
{
    CURL *curl;
    CURLcode res;
    FILE *temfile;
    struct curl_slist *headers = NULL;
    char Auth_signedurl[128];
    char auth_header[]= "Authorization: Bearer ";
 
    //logger_cloud("%s: Entering ...", __FUNCTION__);
    memset((char *) &Auth_signedurl, '\0', 128);
    //Setup file name json format
    sprintf((char *) &Filename,"{\"filename\":\"%s\",\"folder\":\"LOG\"}", upfile);

    headers = curl_slist_append(headers, CLOUD_ACCEPT);
    headers = curl_slist_append(headers, Content);

    //Authorization for LogGetSignedURL must get from GetToken
    strcat((char *) &Auth_signedurl, (char *) &auth_header);
    strcat((char *) &Auth_signedurl, Auth_token);
    headers = curl_slist_append(headers, (char *) &Auth_signedurl);

    /* In windows, this will init the winsock stuff */ 
    curl_global_init(CURL_GLOBAL_ALL);
 
    /* get a curl handle */ 
    curl = curl_easy_init();
    if(curl) {

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, LogGetSignedURL_Response);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, LogGetSignedURL_Header_Response);
 
	if(endpoint_url == 1)
	{
            curl_easy_setopt(curl, CURLOPT_URL, QA_AWS_LOG);
	    curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
	}
	else
	{
            curl_easy_setopt(curl, CURLOPT_URL, PROD_AWS_LOG);
	    curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
	}

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Filename);

        /* Now run off and do what you've been told! */ 
        res = curl_easy_perform(curl);
        /* Check for errors */ 
        if(res != CURLE_OK)
	{
            logger_cloud("%s:curl_easy_perform() failed: %s\n",
			__FUNCTION__, curl_easy_strerror(res));
	    LogGetSignedURL_ret = SIGNEDURL_ERR;
	}
        /* always cleanup */ 
        curl_easy_cleanup(curl);
    }
    memcpy(signedURL,(char *) &local_signedURL, SIGNEDURL_SIZE);
    curl_global_cleanup();

    return LogGetSignedURL_ret;
}
