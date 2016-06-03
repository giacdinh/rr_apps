#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include "BVcloud.h"

/* Parametters setup */
static char data[]   = "grant_type=refresh_token&refresh_token=";
static int RefreshToken_ret = BV_FAILURE;
static char l_refresh_token[64];
static char local_token[64];
extern int endpoint_url;

/* Set test Gobal value. At final release this s */
static int RefreshToken_Response(void *ptr, size_t size, size_t nmemb, void *stream)
{
    const char *tem;
    if(strstr(ptr, "error"))
    {
        logger_cloud("Request return with error: %s", ptr);
        RefreshToken_ret = BV_FAILURE; 
        return 0;
    }
    tem = process_json_data((char *) ptr, "access_token", 0);
    memcpy((char *) &local_token, tem, strlen(tem));
    logger_cloud("access token: %s", (char *) &local_token);

    tem = process_json_data((char *) ptr, "refresh_token", 0);
    memcpy((char *) &l_refresh_token, tem, strlen(tem));
    logger_cloud("refresh token: %s", (char *) &l_refresh_token);

    return nmemb;
}

static size_t RefreshToken_Header_Response(char *buffer, size_t size, size_t nitems, void *userdata)
{
    if(strstr(buffer, "HTTP"))
    {
        if(strstr(buffer, "200 OK"))
            RefreshToken_ret = BV_SUCCESS;
        else
            logger_cloud(buffer);
    }

    return nitems;
}

int RefreshToken(char *main_token, char *refresh_token)
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char *refresh_data;
 
    logger_detailed("%s: Entering ...", __FUNCTION__);
    memset((char *) &local_token, '\0', 64);
    headers = curl_slist_append(headers, CLOUD_ACCEPT);
    headers = curl_slist_append(headers, CLOUD_AUTHORIZE);

    //Append refresh token with main token
    sprintf(refresh_data,"%s%s", (char *) &data, refresh_token);
    printf("%s\n", refresh_data);

    /* In windows, this will init the winsock stuff */ 
    curl_global_init(CURL_GLOBAL_ALL);
 
    /* get a curl handle */ 
    curl = curl_easy_init();
    if(curl) {

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RefreshToken_Response);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, RefreshToken_Header_Response);
	
        if(endpoint_url == 1)
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
//        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, refresh_data);

        /* Now run off and do what you've been told! */ 
        res = curl_easy_perform(curl);
        /* Check for errors */ 
        if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    /* always cleanup */ 
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    strcpy(main_token, (char *) &local_token); 
    strcpy(refresh_token, (char *) &l_refresh_token); 
    return RefreshToken_ret;
}
