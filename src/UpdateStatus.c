#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <json/json.h>
#include <json/json_tokener.h>
#include <string.h>
#include <stdlib.h>
#include "BVcloud.h"

extern int endpoint_url;
extern char main_token;
extern char refresh_token;
/* Parametters setup */
static char Content[] = "Content-Type: application/json; charset=UTF-8";
static char Filename[];
static int UpdateStatus_ret = BV_FAILURE;

static int UpdateStatus_Response(void *ptr, size_t size, size_t nmemb, void *stream)
{
    return nmemb;
}

static size_t UpdateStatus_Header_Response(char *buffer, size_t size, size_t nitems, void *userdata)
{
    if(strstr(buffer, "HTTP"))
    {
        if(strstr(buffer, "200 OK"))
	{
            UpdateStatus_ret = BV_SUCCESS;
	}
	else
	{
	    logger_cloud("%s: Failed call %s", __FUNCTION__, buffer);
	    if(strstr(buffer, "401"))
		UpdateStatus_ret = HTTP_401;
	    else
	        UpdateStatus_ret = BV_FAILURE;
	}
    }
    return nitems;
}
 
int UpdateStatus(char *Auth_token , char * upfile, char *cksum)
{
    CURL *curl;
    CURLcode res;
    FILE *temfile;
    struct curl_slist *headers = NULL;
    char *Auth_signedurl = NULL;
    char auth_header[]= "Authorization: Bearer ";
    char Filename[256];
    bzero((char *) &Filename, 256);
 
    logger_detailed("%s: Entering ...", __FUNCTION__);
    Auth_signedurl = (char *) malloc(128);
    memset(Auth_signedurl, '\0', 128);

    // Setup file name for Update status call
    if(cksum != NULL)
	sprintf((char *) &Filename,"{\"filename\":\"%s\",\"status\":\"UPLOADED\",\"MD5-checksum\":\"%s\"}", upfile, cksum);
    else
        sprintf((char *) &Filename,"{\"filename\":\"%s\",\"status\":\"UPLOADED\"}", upfile);

    headers = curl_slist_append(headers, CLOUD_ACCEPT);
    headers = curl_slist_append(headers, CLOUD_DEVICE);
    headers = curl_slist_append(headers, Content);
    //Authorization for GetSignedURL must get from GetToken
    strcat(Auth_signedurl, (char *) &auth_header);
    strcat(Auth_signedurl, Auth_token);
    headers = curl_slist_append(headers, Auth_signedurl);


    /* In windows, this will init the winsock stuff */ 
    curl_global_init(CURL_GLOBAL_ALL);
 
    /* get a curl handle */ 
    curl = curl_easy_init();
    if(curl) {

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, UpdateStatus_Response);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, UpdateStatus_Header_Response);

        if(endpoint_url == 1)
        {
            curl_easy_setopt(curl, CURLOPT_URL, QA_AWS_UPDATE);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_qa.crt");
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_URL, PROD_AWS_UPDATE);
            curl_easy_setopt(curl, CURLOPT_CAPATH, "/odi/conf/m2mqtt_ca_prod.crt");
        }

        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, FALSE);
//        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
 
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, Filename);


        /* Now run off and do what you've been told! */ 
        res = curl_easy_perform(curl);
        /* Check for errors */ 
        if(res != CURLE_OK)
	{
            logger_cloud("%s:curl_easy_perform() failed: %s\n",
              		__FUNCTION__, curl_easy_strerror(res));
	    UpdateStatus_ret = UPDATE_ERR;
	}
        /* always cleanup */ 
        curl_easy_cleanup(curl);
    }
    free(Auth_signedurl);
    curl_global_cleanup();
    return UpdateStatus_ret;
}
