/*
Copyright (c) 2009-2014 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mosquitto.h>
#include "mqtt_common.h"
#include "BVcloud.h"
#include "libxml/parser.h"
#include "libxml/valid.h"
#include "libxml/xmlschemas.h"
#include "libxml/xmlreader.h"
#include "version.h"
#define VERSION "1.0"

//Prototype
void cloud_config_xml_update(char *filename);
int mqtt_authentication_refresh();
extern int check_cloud_flag();
extern char hw_version[];
extern int pre_event;

#define ODI_CONFIG_XML "/odi/conf/config.xml"
#define ODI_HOME	"/odi"
#define MY_STRUCT_FILE_INITIALIZER { 0, 0, 0, 0 }
#define FW_SIGNEDURL	2048
struct my_file
{
    int is_directory;
    time_t modification_time;
    int64_t size;
    // set to 1 if the content is gzipped
    // in which case we need a content-encoding: gzip header
    int gzipped;
};

bool process_messages = true;
int msg_count = 0;

//unsigned int sub_conf_keepalive = 1;
extern int sub_init_done;
extern int endpoint_url;
extern int DES_CLOUD_connect;

//use sub_mode to differentiate between INIT and NORMAL SUB
char officer_uuid[128];
char *p_officer_uuid;
char fw_signedURL[FW_SIGNEDURL];
char *p_fw_signedURL;		//Global fw_signed URL pointer
extern char mqtt_server_url[];
char *p_mqtt_server_url;	//Global MQTT server URL pointer

char mqtt_refresh_token[TOKEN_SIZE+1];
char mqtt_main_token[TOKEN_SIZE+1];
char *SU_getVersion()
{
    char *str = malloc(20);
    sprintf(str, "%s", BODYVISIONVERSION);
    int i;
    for(i = 0; i < strlen(str); i++)
    {
        if(str[i] == '.')
            str[i] = '_';
    }
    return str;
}

int check_config_update(char *submessage)
{
    static int conf_version = -1;
    int tem_conf_version;
    if(strstr(submessage, "config_version"))
    {
        if(conf_version == -1) // Get init version hash
        {
            process_json_data(submessage, "config_version", &conf_version);
            return 1;
        }
        else
        {
            //compare to see if update nedded
            process_json_data(submessage, "config_version", &tem_conf_version);
            if(tem_conf_version != conf_version)
            {
                conf_version = tem_conf_version;
                return 1;
            }
            else
                return 0;
        }
    }
    else
        return 0;
}


void sub_conf_message_callback(struct mosquitto *mosq, void *obj,
                               const struct mosquitto_message *message)
{
    struct mosq_config *cfg;
    int i;
    bool res;
    static int fw_dl_status = 1;
    static int config_change_reboot = 0;
    //logger_cloud("%s: SUB Entering ...",__FUNCTION__);
    //Create connection flag to lock to CLOUD
    if(!check_cloud_flag())
        system("echo 1 > /odi/conf/cloud.txt");

    if(process_messages == false) return;

    assert(obj);
    cfg = (struct mosq_config *)obj;

    if(message->retain && cfg->no_retain) return;
    if(cfg->filter_outs)
    {
        for(i=0; i<cfg->filter_out_count; i++)
        {
            mosquitto_topic_matches_sub(cfg->filter_outs[i], message->topic, &res);
            if(res) return;
        }
    }

    ready_pub = 1;
#ifdef UNIT_TEST
    logger_cloud("SUB receive message: %s", message->payload);
#else
    logger_cloud("SUB receive message");
#endif
    if(strstr(message->payload, "error"))
    {
        //logger_cloud("SUBDEBUG Request return with error. Respawn %d", sub_conf_keepalive);
        //reset flag to relaunch sub config
        mosquitto_disconnect(mosq);
    }

    // Get MQTT server URL
    p_mqtt_server_url = (char *) &mqtt_server_url;
    bzero(p_mqtt_server_url, 64);
    strcpy(p_mqtt_server_url, process_json_data(message->payload, "mqtt_server", 0));


    //should check to see if config update needed
    // Run config update by compare hash of config_version
    // First time after boot always try to save officer uuid for record.
    if(check_config_update(message->payload))
    {
        //do the rest of config
        //Start config file header
        FILE *fp = NULL; //open update_config.xml
        fp = fopen("/odi/data/upload/update_config.xml", "w+");
        if(fp == NULL)
        {
            logger_cloud("Process to create update config xml failed");
            return;
        }

        fwrite(UNIT_XML_HEADER, 1, strlen(UNIT_XML_HEADER), fp);
        fwrite(UNIT_XML_METHEAD, 1, strlen(UNIT_XML_METHEAD), fp);
        fwrite(UNIT_XML_CONFHEAD, 1, strlen(UNIT_XML_CONFHEAD), fp);
        char str_holder[128];
        char payload[2048];
        strcpy(payload, message->payload);

        if(strstr(payload, "officer_uuid"))
        {
            p_officer_uuid = (char *) &officer_uuid;
            char str_holder1[128];
            bzero((char *) &str_holder1, 128);
            strcpy((char *) &officer_uuid,process_json_data(payload, "officer_uuid", 0));
            sprintf((char *) &str_holder1, "\t<%s>%s</%s>\n", UNIT_XML_UUID,
                    process_json_data(payload, "officer_uuid", 0), UNIT_XML_UUID);
            fwrite((char *) &str_holder1, 1, strlen((char *) &str_holder1), fp);
            logger_cloud("Officer UUID: %s", (char *) &officer_uuid);
        }
        //Parse through return to search for update field
        //UNIT_XML_DTS_DST
        if(strstr(payload, UNIT_XML_DTS_DST))
        {
            //logger_cloud("DST");
            bzero((char *) &str_holder, 128);
            int dst;
            process_json_data(payload, "dts_dst", &dst);
            if(dst == 1)
                sprintf((char *) &str_holder, "\t<%s>true</%s>\n", UNIT_XML_DTS_DST, UNIT_XML_DTS_DST);
            else
                sprintf((char *) &str_holder, "\t<%s>false</%s>\n", UNIT_XML_DTS_DST, UNIT_XML_DTS_DST);

            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_DTS_TZ
        if(strstr(payload, UNIT_XML_DTS_TZ))
        {
            //logger_cloud("TZ");
            bzero((char *) &str_holder, 128);
            sprintf((char *) &str_holder, "\t<%s>%s</%s>\n", UNIT_XML_DTS_TZ,
                    process_json_data(payload, UNIT_XML_DTS_TZ, 0), UNIT_XML_DTS_TZ);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);

        }

        //UNIT_XML_NAME
        if(strstr(payload, UNIT_XML_NAME))
        {
            logger_cloud("Officer name");
            bzero((char *) &str_holder, 128);
            sprintf((char *) &str_holder,"\t<%s>%s</%s>\n", UNIT_XML_NAME,
                    process_json_data(payload, UNIT_XML_NAME, 0), UNIT_XML_NAME);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);

            // clear out officer uuid if officer name = NO Name
            bzero((char *) &str_holder, 128);
            if(strstr(payload, "NO NAME"))
                sprintf((char *) &str_holder, "\t<%s> </%s>\n", UNIT_XML_UUID, UNIT_XML_UUID);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_MUTECTL
        if(strstr(payload, UNIT_XML_MUTECTL))
        {
            //logger_cloud("mute control");
            bzero((char *) &str_holder, 128);
            int mute_ctrl;
            process_json_data(payload, "mute_ctrl", &mute_ctrl);
            //sprintf((char *) &str_holder, "\t<%s>%d</%s>\n",
            //    UNIT_XML_MUTECTL, mute_ctrl, UNIT_XML_MUTECTL);
            sprintf((char *) &str_holder, "\t<%s>%s</%s>\n",
                    UNIT_XML_MUTECTL, mute_ctrl==1?"true":"false", UNIT_XML_MUTECTL);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_TITLE
        if(strstr(payload,UNIT_XML_CARNUM))
        {
            //logger_cloud("Title");
            bzero((char *) &str_holder, 128);
            sprintf((char *) &str_holder,"\t<%s>%s</%s>\n", UNIT_XML_CARNUM,
                    process_json_data(payload, UNIT_XML_CARNUM, 0), UNIT_XML_CARNUM);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_CARNUM store as "device_name"
        if(strstr(payload, UNIT_XML_DEVICE))
        {
            //logger_cloud("Car number");
            bzero((char *) &str_holder, 128);
            sprintf((char *) &str_holder, "\t<%s>%s</%s>\n", UNIT_XML_CARNUM,
                    process_json_data(payload, UNIT_XML_DEVICE, 0), UNIT_XML_CARNUM);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_RECBUZZ
        if(strstr(payload, UNIT_XML_RECBUZZ))
        {
            //logger_cloud("record buzz");
            bzero((char *) &str_holder, 128);
            int rec_buzz;
            process_json_data(payload, "rec_buzz", &rec_buzz);
            sprintf((char *) &str_holder, "\t<%s>%d</%s>\n",
                    UNIT_XML_RECBUZZ, rec_buzz, UNIT_XML_RECBUZZ);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_RECPRE
        if(strstr(payload, UNIT_XML_RECPRE) && ((int) hw_version[0] >= 0x34) )
        {
            //logger_cloud("record pre");
            bzero((char *) &str_holder, 128);
            int rec_pre;
            process_json_data(payload, "rec_pre", &rec_pre);
            sprintf((char *) &str_holder, "\t<%s>%d</%s>\n",
                    UNIT_XML_RECPRE, rec_pre, UNIT_XML_RECPRE);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
            if(rec_pre != pre_event)
                config_change_reboot = 1;
        }


        //UNIT_XML_RECQUAL
        if(strstr(payload, UNIT_XML_RECQUAL))
        {
            //logger_cloud("record quality");
            bzero((char *) &str_holder, 128);
            int rec_qual;
            process_json_data(payload, "rec_qual", &rec_qual);
            sprintf((char *) &str_holder, "\t<%s>%d</%s>\n",
                    UNIT_XML_RECQUAL, rec_qual, UNIT_XML_RECQUAL);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_TRACE
        if(strstr(payload, UNIT_XML_TRACE))
        {
            //logger_cloud("Trace");
            bzero((char *) &str_holder, 128);
            int trace;
            process_json_data(payload, "snap_trace", &trace);
            sprintf((char *) &str_holder, "\t<%s>%d</%s>\n",
                    UNIT_XML_TRACE, trace, UNIT_XML_TRACE);
            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //UNIT_XML_USB
        if(strstr(payload, UNIT_XML_USB))
        {
            //logger_cloud("USB login");
            bzero((char *) &str_holder, 128);
            int login;
            process_json_data(payload, "usb_login", &login);
            if(login == 1)
                sprintf((char *) &str_holder, "\t<%s>true</%s>\n", UNIT_XML_USB, UNIT_XML_USB);
            else
                sprintf((char *) &str_holder, "\t<%s>false</%s>\n", UNIT_XML_USB, UNIT_XML_USB);

            fwrite((char *) &str_holder, 1, strlen((char *) &str_holder), fp);
        }

        //write xml ending tag
        //logger_cloud("Write end tag");
        fwrite(UNIT_XML_CONFEND, 1, strlen(UNIT_XML_CONFEND), fp);
        fwrite(UNIT_XML_METEND, 1, strlen(UNIT_XML_METEND), fp);

        fclose(fp);
        cloud_config_xml_update("/upload/update_config.xml");

        //Get token for Config update call
        char m_token[64], r_token[64];
        bzero((void *) &m_token, TOKEN_SIZE);
        if(BV_FAILURE != GetToken(&m_token, &r_token))
        {
            logger_cloud("Call updateConfigDate");
            Configupdate(&m_token);
        }
        else
            logger_cloud("Call GetToken failed. Can't update config date");

    }

    if(cfg->msg_count>0)
    {
        msg_count++;
        if(cfg->msg_count == msg_count)
        {
            process_messages = false;
            mosquitto_disconnect(mosq);
        }
    }

    //check to see if update is needed
    if(strstr(message->payload, UNIT_LATEST_FW_TAG))
    {
        char server_fw[64];
        strcpy((char *) &server_fw, process_json_data(message->payload,UNIT_LATEST_FW_TAG, 0));
        if(!strcmp((char*) SU_getVersion(),(char *) &server_fw))
            logger_cloud("Match firmware. NO update. Device fw: %s -- server fw: %s",
                         SU_getVersion(), (char *) &server_fw);
        else
        {
            static int SU_in_progress = -1;
            if(SU_in_progress == -1)
            {
                SU_in_progress = 1;
                logger_cloud("Don't match prepare for update. Device fw: %s -- server fw: %s",
                             SU_getVersion(), (char *) &server_fw);
                // Get firmware signed URL
                p_fw_signedURL = (char *) &fw_signedURL;
                bzero(p_fw_signedURL, FW_SIGNEDURL);
                p_fw_signedURL = (char *) process_json_data(message->payload, "firmware_download_url", 0);
                strcat((char *) &server_fw, ".tar");
                FileDownload(p_fw_signedURL, (char *) &server_fw);
                logger_cloud("Done FW download from cloud. Start upgrade firmware");
                char str_command[256];
                sprintf(str_command, "%s/%s data/upload/BodyVision_%s > %s/firmware_upgrade 2>&1",
                        "/usr/local/bin" , "fw_upgrade.sh", (char *) &server_fw, "/odi/log");
                system(str_command);
            }
        }
    }
    if(config_change_reboot == 1)
    {
        logger_cloud("Device need reboot to apply config change");
        system("sync; reboot");
    }
}

void sub_connect_callback(struct mosquitto *mosq, void *obj, int result)
{
    int i;
    struct mosq_config *cfg;
    //    logger_cloud("%s: SUB Entering ...", __FUNCTION__);
    assert(obj);
    cfg = (struct mosq_config *)obj;

    if(!result)
    {
        for(i=0; i<cfg->topic_count; i++)
        {
            mosquitto_subscribe(mosq, NULL, cfg->topics[i], cfg->qos);
        }
    }
    else
    {
        if(result && !cfg->quiet)
        {
            logger_cloud("%s: MQTT error %s. Try to reconnect",__FUNCTION__, mosquitto_connack_string(result));
            // If getting here most of the time cause by "Authorized failure"
            // Time to refresh token from mqtt server
            sub_init_done = -1; // Reset flag
            mqtt_authentication_refresh();
        }
    }
}

void sub_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    int i;
    struct mosq_config *cfg;

    assert(obj);
    cfg = (struct mosq_config *)obj;
    logger_cloud("%s: Subscribed (mid: %d): %d", __FUNCTION__, mid, granted_qos[0]);
    if(!cfg->quiet) printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
    for(i=1; i<qos_count; i++)
    {
        if(!cfg->quiet) printf(", %d", granted_qos[i]);
    }
    if(!cfg->quiet) printf("\n");
}

void sub_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
    printf("%s\n", str);
}

//New change that would top using token refresh. Any time "Unauthorized" return would trigger another Gettoken
int mqtt_authentication_refresh()
{
    char *unitID;
    char *p_new_dev, new_dev[32];
    char *p_dev_id, dev_id[32];

    unitID = (char *) getSerial();
    // Either no refresh token or token_refresh call failed do get new TOKEN
    int result = mqtt_token(unitID, unitID);
    if(result == BV_FAILURE)
    {
        logger_cloud("Failed to get MQTT token. Stop");
        return;
    }
    logger_cloud("SUB: Get mqtt token: %s", (char *) &mqtt_main_token);
    p_new_dev = (char *) &new_dev;
    strcpy(p_new_dev, "NEWDEV-");
    strcat(p_new_dev, unitID);

    p_dev_id = (char *) &dev_id;
    strcpy(p_dev_id, "/DEVICE/");
    strcat(p_dev_id, unitID);

    // Call SUB init
    mqtt_sub_init((char *) &mqtt_main_token, p_new_dev, p_dev_id);
    int sub_init_try = 0;
    while(sub_init_done != 1)
    {
        logger_cloud("Wait for return from SUB init");
        sleep(1);
        if(sub_init_try++%5 == 0)
        {
            // Retry sub init
            logger_cloud("%s: retrying if failed after 5s ...", __FUNCTION__);
            mqtt_sub_init((char *) &mqtt_main_token, p_new_dev, p_dev_id);
        }
        if(sub_init_try > 120)
        {
            logger_cloud("%s: Call sub init failed. Stop", __FUNCTION__);
            sub_init_try = 0;
            break;
        }
    }
    return MQTT_SUCCESS;
}

void* mqtt_sub_main_task(void* thread_func_param)
{
    char *p_new_dev, new_dev[32];
    char *p_dev_id, dev_id[32];
    logger_detailed("SUB: Entering: %s PID: %lu", __FUNCTION__, (unsigned long) pthread_self());
    int rc;
    //reset mqtt refresh token for init use
    bzero((char *) &mqtt_refresh_token,TOKEN_SIZE);
    while(1)
    {
        if(checkhost() == 1)
            break;
        //else
        //    logger_cloud("SUB:mqtt can't ping out. Unit sleep awhile then try again");
        sleep(20);
    }
    logger_cloud("SUB: start call mqtt_sub chain");
    get_end_URL();
    // SUB init
    char *unitID;
    unitID = (char *) getSerial();
    //logger_cloud("Unit ID: %s", unitID);

    // Setup call with correct unit ID
    int result = mqtt_token(unitID, unitID);
    if(result == BV_FAILURE)
    {
        logger_cloud("Failed to get MQTT token. Stop");
        return;
    }
    logger_cloud("SUB: Received mqtt token: %s", (char *) &mqtt_main_token);
    p_new_dev = (char *) &new_dev;
    strcpy(p_new_dev, "NEWDEV-");
    strcat(p_new_dev, unitID);

    p_dev_id = (char *) &dev_id;
    strcpy(p_dev_id, "/DEVICE/");
    strcat(p_dev_id, unitID);

    // Call SUB init
    int sub_init_try = 0;
    mqtt_sub_init((char *) &mqtt_main_token, p_new_dev, p_dev_id);
    while(sub_init_done != 1)
    {
        logger_cloud("Wait for return from SUB init");
        sleep(1);
        if(sub_init_try++%5 == 0)
        {
            // Retry sub init
            logger_cloud("%s: retrying if failed after 5s ...", __FUNCTION__);
            mqtt_sub_init((char *) &mqtt_main_token, p_new_dev, p_dev_id);
        }
        if(sub_init_try > 120)
        {
            logger_cloud("%s: Call sub init failed. Stop", __FUNCTION__);
            break;
        }
    }

    // Call SUB config
    static unsigned int sub_conf_keepalive = 1;
    logger_cloud("Call SUB config");
    while(1)
    {
        if(sub_conf_keepalive == 1)
        {
            sub_conf_keepalive = 0;
            //logger_cloud("SUBDEBUG start SUB config or respawn after process error");
#ifdef UNIT_TEST
            unsigned int test = mqtt_sub_test(&sub_conf_keepalive);
#else
            unsigned int test = mqtt_sub_conf(&sub_conf_keepalive);
#endif
            logger_detailed("Exit from mqtt_sub. ret: %d flag: %d", test, sub_conf_keepalive);
            if(test == 1)
            {
                sub_conf_keepalive = test;
                logger_detailed("Should force restart: %d flag: %d", test, sub_conf_keepalive);
            }
        }
        else
            logger_detailed("SUBDEBUG SUB CONFIG running fine: %d", sub_conf_keepalive);
        sleep(30);
    }
}

char *ptest_conf[17] = {"mosquitto_sub",
                        "-u", "                                                          	",
                        "-t", "                                                          	",
                        "-h", MQTT_SERVER_URL,
                        "-p", "8080",
                        "-q", "1",
                        "-i", "                              					",
                        "--tls-version", "tlsv1",
                        "--cafile", MQTT_CERT_FILE
                       };
int mqtt_sub_conf(int *sub_conf_keepalive)
{
    struct mosq_config cfg;
    struct mosquitto *mosq = NULL;
    int rc;
    int argc = 17;
    char **argv = NULL;
    char buf[128];
    char *tmp = (char *) &buf;
    char dev_id[64];
    char *p_dev_id = (char *) &dev_id;

    char *unitID;
    unitID = (char *) getSerial();

    ptest_conf[2] = (char *) &mqtt_main_token;
    bzero(tmp, 128);
    strcpy(tmp, "/");
    strcat(tmp, (char *) &agency_uuid);
    strcat(tmp, "/");
    strcat(tmp, unitID);
    ptest_conf[4] = tmp;

    bzero(p_dev_id, 64);
    strcpy(p_dev_id, "DEVICE-");
    strcat(p_dev_id, unitID);
    ptest_conf[12] = p_dev_id;

    logger_cloud("mqtt main token: %s", ptest_conf[2]);
    logger_cloud("uuid/serial: %s", ptest_conf[4]);
    logger_cloud("device: %s", ptest_conf[12]);

    if(endpoint_url == 1) //qa url site
    {
        logger_cloud("%s: User QA URL", __FUNCTION__);
        ptest_conf[6] = QA_MQTT_SERVER_URL;
        ptest_conf[16] = QA_MQTT_CERT_FILE;
    }
    else
    {
        logger_cloud("%s: User PRODUCTION URL", __FUNCTION__);
        ptest_conf[6] = PROD_MQTT_SERVER_URL;
        ptest_conf[16] = PROD_MQTT_CERT_FILE;
    }


    argv = ptest_conf;

    logger_detailed("SUB: Entering: %s ...", __FUNCTION__);

    rc = client_config_load(&cfg, CLIENT_SUB, argc, argv);
    if(rc)
    {
        client_config_cleanup(&cfg);
        logger_cloud("Use 'mosquitto_sub --help' to see usage.");
        return 1;
    }

    mosquitto_lib_init();

    if(client_id_generate(&cfg, "mosqsub"))
    {
        return 1;
    }

    mosq = mosquitto_new(cfg.id, cfg.clean_session, &cfg);
    if(!mosq)
    {
        switch(errno)
        {
            case ENOMEM:
                logger_cloud("Error: Out of memory.");
                break;
            case EINVAL:
                logger_cloud("Error: Invalid id and/or clean_session.");
                break;
        }
        mosquitto_lib_cleanup();
        return 1;
    }

    if(client_opts_set(mosq, &cfg))
    {
        logger_cloud("Option set failed");
        return 1;
    }
    if(cfg.debug)
    {
        mosquitto_log_callback_set(mosq, sub_log_callback);
        mosquitto_subscribe_callback_set(mosq, sub_subscribe_callback);
    }

    mosquitto_connect_callback_set(mosq, sub_connect_callback);
    mosquitto_message_callback_set(mosq, sub_conf_message_callback);

    rc = client_connect(mosq, &cfg);
    if(rc)
    {
        *sub_conf_keepalive = 1;
        logger_cloud("Connection attempt error. Respawn: %d", *sub_conf_keepalive);
        return 1;
    }
    rc = mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if(cfg.msg_count>0 && rc == MOSQ_ERR_NO_CONN)
    {
        rc = 0;
    }
    if(rc)
    {
        *sub_conf_keepalive = 1;
        logger_cloud("%s: %s\n",__FUNCTION__, mosquitto_strerror(rc));
        logger_cloud("SUBDEBUG Request return with error. Respawn: %d", *sub_conf_keepalive);
        mqtt_authentication_refresh();
        //reset flag to relaunch sub config
    }
    *sub_conf_keepalive = 1;
    //logger_cloud("Exit: %s ...", __FUNCTION__);
    return 1;
}

static int modifytree(xmlNodePtr *ParentPtr, xmlNodePtr sourceNode)
{
    int notfound = -1; //not found
    xmlNodePtr child = (*ParentPtr)->xmlChildrenNode, txt = NULL;
    while (child != NULL)
    {
        if ((xmlStrcmp(child->name, (const xmlChar *)"text")))
        {
            if ((!xmlStrcmp(child->name, sourceNode->name)))
            {
                xmlNodePtr destNode = xmlCopyNode(sourceNode, 1);
                xmlNodePtr old = xmlReplaceNode(child, destNode);
                xmlFreeNode(old);
                notfound = 0;
                break;
            }
        }
        else
            txt = child;

        child = child->next;
    }

    if (notfound == -1)
    {
        xmlAddChild(*ParentPtr, xmlCopyNode(sourceNode, 1));
        xmlAddChild(*ParentPtr, xmlCopyNode(txt, 1));
        logger_remotem("LOAD_CONFIG_FILE: Node not found, adding new node: %s",
                       sourceNode->name);
    }

    return notfound;
}


void cloud_config_xml_update(char *filename)
{
    char configXmlPath[200];
    char inputXmlPath[200];
    char str_file_path[200];
    char *address = 0, *netmask = 0, *gateway = 0;
    int dhcp = 0;

    int found = 0;
    int isNetUpdate = 0;

    //    get_qsvar(request_info, "path", str_file_path, sizeof(str_file_path));
    //    if (strlen(str_file_path) == 0) {
    //	logger_cloud("Update config xml path not found");
    //        return;
    //    }

    sprintf(inputXmlPath, "%s%s", ODI_HOME, filename);
    sprintf(configXmlPath, "%s", ODI_CONFIG_XML);

    logger_cloud("load_config_file: merge XML Config File: from %s to %s",
                 inputXmlPath, ODI_CONFIG_XML);

    struct my_file xmlfile = MY_STRUCT_FILE_INITIALIZER;
    if (!my_file_stat(inputXmlPath, &xmlfile))
    {
        logger_cloud("No Input Config file to Load  %s", inputXmlPath);
        //goto leave;
        return;
    }
    if (!my_file_stat(ODI_CONFIG_XML, &xmlfile))
    {
        logger_cloud("No destination Config file to Merge  %s",
                     ODI_CONFIG_XML);
        //goto leave;
        return;
    }    // read network file
    logger_cloud("Start merge XML Config File: %s", inputXmlPath);

    xmlDocPtr inputXmlDocumentPointer = xmlParseFile(inputXmlPath);
    if (inputXmlDocumentPointer == 0)
    {
        logger_cloud("LOAD_CONFIG_FILE: input XML not well formed %s",
                     inputXmlPath);
        return;
    }
    xmlDocPtr configXmlDocumentPointer = xmlParseFile(ODI_CONFIG_XML);
    if (configXmlDocumentPointer == 0)
    {
        logger_cloud("LOAD_CONFIG_FILE: config XML not well formed %s",
                     ODI_CONFIG_XML);
        return;
    }

    // doc check
    xmlNodePtr cur = xmlDocGetRootElement(inputXmlDocumentPointer);

    if (cur == NULL)
    {
        logger_cloud("LOAD_CONFIG_FILE: input XML is empty");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }
    // config-meta check
    if (xmlStrcmp(cur->name, (const xmlChar *) "config-metadata"))
    {
        logger_cloud("LOAD_CONFIG_FILE: config-metadata tag not found");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    xmlNodePtr destParent = xmlDocGetRootElement(configXmlDocumentPointer);

    //node not found
    if (destParent == NULL)
    {
        logger_cloud("LOAD_CONFIG_FILE: empty doc");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    if (xmlStrcmp(destParent->name, (const xmlChar *) "config-metadata"))
    {
        logger_cloud("LOAD_CONFIG_FILE:  root node != config-metadata");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    destParent = destParent->xmlChildrenNode;
    while (destParent)
    {
        if ((!xmlStrcmp(destParent->name, (const xmlChar *)"config")))
        {
            found = 1;
            break;
        }
        destParent = destParent->next;
    }
    if (!found)
    {
        logger_cloud("LOAD_CONFIG_FILE: config tag not found in %s",
                     configXmlPath);
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    cur = cur->xmlChildrenNode;
    xmlNodePtr child= NULL;
    char *new_date = 0, *new_time = 0, *new_tz = 0;
    char str_comm[200];
    while (cur != NULL)
    {
        if ((!xmlStrcmp(cur->name, (const xmlChar *)"config")))
        {
            child = cur->xmlChildrenNode;
            while (child != NULL)
            {
                if (!(xmlStrcmp(child->name, (const xmlChar *)"text")))
                {
                    ;
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"dvr_id")))
                {
                    // cannot change DVR ID
                    ;
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"dts_time")))
                {
                    new_time = (char *)xmlNodeGetContent(child) ;
                    if (strlen(new_time) == 8)   // Check for valid time
                    {
                        logger_cloud("LOAD_CONFIG_FILE: setting time %s", new_time);
                        modifytree(&destParent, child);
                        memset(str_comm, 0, 200);
                        set_sys_clock(NULL, new_time); //Passed string and time set flag
                    }
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"dts_tz")))
                {
                    new_tz = (char *)xmlNodeGetContent(child) ;
                    modifytree(&destParent, child);

                    char ptr[strlen(new_tz)+1];
                    int i, j=0;

                    for (i=0; new_tz[i]!=' '; i++)
                    {
                        // skip till space
                    }
                    i++; // move up from the space
                    for (i; new_tz[i]!='\0'; i++)
                    {
                        ptr[j++]=new_tz[i];
                    }
                    ptr[j]='\0';

                    memset(str_comm, 0, 200);
                    sprintf(str_comm, "export TZ=%s", ptr);
                    system(str_comm);

                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_dhcp")))
                {
                    isNetUpdate = 1;
                    if (!(xmlStrcmp((const xmlChar *)xmlNodeGetContent(child),
                                    (const xmlChar *)"true")))
                    {
                        dhcp = 1;
                        logger_cloud("LOAD_CONFIG_FILE: change to DHCP");
                    }
                    else
                    {
                        dhcp = 0;
                        logger_cloud("LOAD_CONFIG_FILE: DHCP is static");
                    }
                    modifytree(&destParent, child);
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_addr")))
                {
                    address = (char *)xmlNodeGetContent(child) ;
                    if (address != NULL && strlen(address) > 7)
                    {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_mask")))
                {
                    netmask = (char *)xmlNodeGetContent(child);
                    if (netmask != NULL && strlen(netmask) > 7)
                    {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                }
                else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_gate")))
                {
                    gateway = (char *)xmlNodeGetContent(child);
                    if (gateway != NULL && strlen(gateway) > 7)
                    {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                }
                else
                {
                    if (modifytree(&destParent, child) == -1)
                    {
                        logger_cloud("LOAD_CONFIG_FILE: %s doesn't contain %s tag",
                                     configXmlPath, (char *)child->name);
                    }
                }
                child = child->next;

            }
        }
        cur = cur->next;
    } // end while loop until NULL all elements processed

    if (isNetUpdate)
        update_network(dhcp, address, netmask, gateway);

    logger_cloud("LOAD_CONFIG_FILE: saving new config, size=%d",
                 xmlSaveFileEnc(configXmlPath, configXmlDocumentPointer, "UTF-8"));

leave:
    if (inputXmlDocumentPointer)
        xmlFreeDoc(inputXmlDocumentPointer);

    if (configXmlDocumentPointer)
        xmlFreeDoc(configXmlDocumentPointer);
}

#ifdef UNIT_TEST
char *ptest_mvi[9] = {"mosquitto_sub",
                      "-t", "mvi",
                      "-h", "192.168.250.119",
                      "-p", "1883",
                      "-q", "1"
                     };
int mqtt_sub_test(int *sub_conf_keepalive)
{
    struct mosq_config cfg;
    struct mosquitto *mosq = NULL;
    int rc;
    int argc = 9;
    char **argv = NULL;
    char buf[128];
    char *tmp = (char *) &buf;
    char dev_id[64];
    char *p_dev_id = (char *) &dev_id;

    argv = ptest_mvi;

    logger_detailed("SUB: Entering: %s ...", __FUNCTION__);

    rc = client_config_load(&cfg, CLIENT_SUB, argc, argv);
    if(rc)
    {
        client_config_cleanup(&cfg);
        logger_cloud("Use 'mosquitto_sub --help' to see usage.");
        return 1;
    }

    mosquitto_lib_init();

    if(client_id_generate(&cfg, "mosqsub"))
    {
        return 1;
    }

    mosq = mosquitto_new(cfg.id, cfg.clean_session, &cfg);
    if(!mosq)
    {
        switch(errno)
        {
            case ENOMEM:
                logger_cloud("Error: Out of memory.");
                break;
            case EINVAL:
                logger_cloud("Error: Invalid id and/or clean_session.");
                break;
        }
        mosquitto_lib_cleanup();
        return 1;
    }

    if(client_opts_set(mosq, &cfg))
    {
        logger_cloud("Option set failed");
        return 1;
    }
    if(cfg.debug)
    {
        mosquitto_log_callback_set(mosq, sub_log_callback);
        mosquitto_subscribe_callback_set(mosq, sub_subscribe_callback);
    }

    mosquitto_connect_callback_set(mosq, sub_connect_callback);
    mosquitto_message_callback_set(mosq, sub_conf_message_callback);

    rc = client_connect(mosq, &cfg);
    if(rc)
    {
        *sub_conf_keepalive = 1;
        logger_cloud("Connection attempt error. Respawn: %d", *sub_conf_keepalive);
        return 1;
    }
    rc = mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if(cfg.msg_count>0 && rc == MOSQ_ERR_NO_CONN)
    {
        rc = 0;
    }
    if(rc)
    {
        *sub_conf_keepalive = 1;
        logger_cloud("%s: %s\n",__FUNCTION__, mosquitto_strerror(rc));
        logger_cloud("SUBDEBUG Request return with error. Respawn: %d", *sub_conf_keepalive);
        mqtt_authentication_refresh();
        //reset flag to relaunch sub config
    }
    *sub_conf_keepalive = 1;
    //logger_cloud("Exit: %s ...", __FUNCTION__);
    return 1;
}
#endif
