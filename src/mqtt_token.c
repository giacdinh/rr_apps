#ifdef NOTUSE
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include <stdlib.h>
#include "mqtt_common.h"
#include "BVcloud.h"
#include "libxml/parser.h"
#include "libxml/valid.h"
#include "libxml/xmlschemas.h"
#include "libxml/xmlreader.h"
#else
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>

#include <mosquitto.h>
#include "mqtt_common.h"
#include "BVcloud.h"
#include "libxml/parser.h"
#include "libxml/valid.h"
#include "libxml/xmlschemas.h"
#include "libxml/xmlreader.h"
#endif

/* Parametters setup */
static char Accept[] = "Accept: application/json";
static char Author[] = "Authorization: Basic Qm9keVZpc2lvbmFwcDpteVNlY3JldE9BdXRoU2VjcmV0";

#define MQTT_TOKEN_URL_PROD	"https://prod-app-lb.l3capture.com/api/oauth/token"
#define MQTT_TOKEN_URL_QA	"https://qa-app-lb.l3capture.com/api/oauth/token"

#define DATA_STR "username=%s&password=%s&grant_type=password&scope=read write"
#define REFRESH_STR "grant_type=refresh_token&refresh_token=%s"

extern int endpoint_url;

static char l_refresh_token[64];
static char local_token[64];

static int mqtt_token_ret = BV_FAILURE;

/* Set test Gobal value. At final release this s */
static int GetToken_Response(void *ptr, size_t size, size_t nmemb, void *stream)
{
    const char *tem;
    //Check for error to prevent segmentation
    if(strstr(ptr, "error"))
    {
        logger_cloud("Request return with error: %s", ptr);
        mqtt_token_ret = BV_FAILURE;
        return NULL;
    }

    tem = process_json_data((char *) ptr, "access_token", NULL);
    memcpy((char *) &local_token, tem, strlen(tem));
    logger_cloud("mqtt main token: %s", (char *) &local_token);

    tem = process_json_data((char *) ptr, "refresh_token", NULL);
    memcpy((char *) &l_refresh_token, tem, strlen(tem));
    logger_cloud("mqtt refresh token: %s", (char *) &l_refresh_token);

    return nmemb;
}

static size_t GetToken_Header_Response(char *buffer, size_t size, size_t nitems, void *userdata)
{
    if(strstr(buffer, "HTTP"))
    {
        if(strstr(buffer, "200 OK"))
            mqtt_token_ret = BV_SUCCESS;
        else
        {
            mqtt_token_ret = BV_FAILURE;
        }
    }
    return nitems;
}

extern char mqtt_main_token;
extern char mqtt_refresh_token;

int mqtt_token(char *user, char *pass)
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char *token = (char *) &mqtt_main_token;
    char *refresh = (char *) &mqtt_refresh_token;
    logger_detailed("%s: Entering ...", __FUNCTION__);

    memset((char *) &local_token, '\0', 64);
    headers = curl_slist_append(headers, (char *) &Accept);
    headers = curl_slist_append(headers, CLOUD_DEVICE);
    headers = curl_slist_append(headers, (char *) &Author);

    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GetToken_Response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, GetToken_Header_Response);

        if(endpoint_url == 1)
        {
            curl_easy_setopt(curl, CURLOPT_URL, MQTT_TOKEN_URL_QA);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_URL, MQTT_TOKEN_URL_PROD);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
        }

        //Turn on RSA security
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
//        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        char tem[128];
        bzero((char *) &tem, 128);
        sprintf((char *) &tem, DATA_STR, user, pass);
//	logger_cloud("Data: %s", (char *) &tem);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, tem);

        /* Now run off and do what you've been told! */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK)
            logger_cloud("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    memcpy(token, (char *) &local_token, 64);
    memcpy(refresh, (char *) &l_refresh_token, 64);

    return mqtt_token_ret;
}


int mqtt_token_refresh( )
{
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;
    char *token = (char *) &mqtt_main_token;
    char *refresh = (char *) &mqtt_refresh_token;
    logger_detailed("%s: Entering ...", __FUNCTION__);

    memset((char *) &local_token, '\0', 64);
    headers = curl_slist_append(headers, CLOUD_ACCEPT);
    headers = curl_slist_append(headers, CLOUD_AUTHORIZE);

    /* In windows, this will init the winsock stuff */
    curl_global_init(CURL_GLOBAL_ALL);

    /* get a curl handle */
    curl = curl_easy_init();
    if(curl)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, GetToken_Response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, GetToken_Header_Response);

        if(endpoint_url == 1)
            curl_easy_setopt(curl, CURLOPT_URL, MQTT_TOKEN_URL_QA);
        else
            curl_easy_setopt(curl, CURLOPT_URL, MQTT_TOKEN_URL_PROD);

//        curl_easy_setopt(curl, CURLOPT_URL, MQTT_TOKEN_URL);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        char tem[128];
        bzero((char *) &tem, 128);
        sprintf((char *) &tem, REFRESH_STR, refresh);
        logger_cloud("Refresh: %s", (char *) &tem);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, tem);

        /* Now run off and do what you've been told! */
        res = curl_easy_perform(curl);
        /* Check for errors */
        if(res != CURLE_OK)
            logger_cloud("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        /* always cleanup */
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    memcpy(token, (char *) &local_token, 64);
    memcpy(refresh, (char *) &l_refresh_token, 64);

    return mqtt_token_ret;
}
