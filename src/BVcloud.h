#ifndef _BVCLOUD_H
#define _BVCLOUD_H

#ifdef __cplusplus
extern "C" {
#endif


enum {
    BV_OK = 5000,
    HAD_NOFILE,
    HTTP_200,
    HTTP_400,
    HTTP_401,
    GETTOKEN_ERR,
    REFRESH_ERR,
    SIGNEDURL_ERR,
    UPLOAD_ERR,
    UPDATE_ERR,
    UNKNOWN
} BV_RESPONSE_CODE;
    
int HTTP_Status_Handler(char *, const char *);
const char * process_json_data(char *ptr, char * search_key, int * intval);
void logger_cloud(char* str_log, ...);
int RefreshToken(char *main_token, char *refresh_token);
//char *user_name;
//char *password;
void * mqtt_sub_main_task();
void * mqtt_pub_main_task();
void cloud_main_task();
char *p_main_token;
int checkhost();
int logfile_for_cloud();
int check_log_push_time();


#define BV_SUCCESS	1
#define BV_FAILURE	0

//CLOUD vs DES define
#define TO_DES 1
#define TO_CLOUD !TO_DES

#define FILE_LIST_SIZE  200
#define FILE_NAME_SIZE  32
#define SIGNEDURL_SIZE  2048 
#define TOKEN_SIZE      64
#define TOCLOUDATTEMPT	5
extern int cloud_init;

//#define UPLOAD_MAIN_URL "http://ec2-96-127-65-40.us-gov-west-1.compute.amazonaws.com"
#define UPLOAD_MAIN_URL 	"https://qa-app-lb.l3capture.com"
#define PROD_UPLOAD_MAIN_URL 	"https://prod-app-lb.l3capture.com"
#define QA_UPLOAD_MAIN_URL 	"https://qa-app-lb.l3capture.com"

// URL for Log Upload
#define PROD_LOG_UPLOAD_URL	"https://l3bv-devicemiscfiles-prod"
#define QA_LOG_UPLOAD_URL	"https://l3bv-devicemiscfiles-qa"

#define AWS_SIGNED	UPLOAD_MAIN_URL"""/api/media/signedURL"
#define AWS_TIME	UPLOAD_MAIN_URL"""/api/devices/time"
#define AWS_AUTH	UPLOAD_MAIN_URL"""/api/oauth/token"
#define AWS_UPDATE	UPLOAD_MAIN_URL"""/api/media/update"

#define PROD_AWS_SIGNED	PROD_UPLOAD_MAIN_URL"""/api/media/signedURL"
#define PROD_AWS_TIME	PROD_UPLOAD_MAIN_URL"""/api/devices/time"
#define PROD_AWS_AUTH	PROD_UPLOAD_MAIN_URL"""/api/oauth/token"
#define PROD_AWS_UPDATE	PROD_UPLOAD_MAIN_URL"""/api/media/update"
//#define PROD_AWS_CONFIG	PROD_UPLOAD_MAIN_URL"""/api/devices/updateConfigDate"
#define PROD_AWS_CONFIG	PROD_UPLOAD_MAIN_URL"""/api/devices/updateConfig"

//Log request go to different URL
#define PROD_AWS_LOG	PROD_UPLOAD_MAIN_URL"""/api/devices/signedURL"

#define QA_AWS_SIGNED	QA_UPLOAD_MAIN_URL"""/api/media/signedURL"
#define QA_AWS_TIME	QA_UPLOAD_MAIN_URL"""/api/devices/time"
#define QA_AWS_AUTH	QA_UPLOAD_MAIN_URL"""/api/oauth/token"
#define QA_AWS_UPDATE	QA_UPLOAD_MAIN_URL"""/api/media/update"
//#define QA_AWS_CONFIG 	QA_UPLOAD_MAIN_URL"""/api/devices/updateConfigDate"
#define QA_AWS_CONFIG 	QA_UPLOAD_MAIN_URL"""/api/devices/updateConfig"

//Log request go to different URL
#define QA_AWS_LOG 	QA_UPLOAD_MAIN_URL"""/api/devices/signedURL"

#define CLOUD_AUTHORIZE		"Authorization: Basic Qm9keVZpc2lvbmFwcDpteVNlY3JldE9BdXRoU2VjcmV0"
#define CLOUD_AUTHORIZE_TIME	"Authorization: Bearer %s"
#define CLOUD_ACCEPT		"Accept: application/json"
#define CLOUD_DEVICE		"Device: BodyVision"

#endif
	
