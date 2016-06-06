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
#include "ezxml.h"
#define VERSION "1.0"

bool init_process_messages = true;
int init_msg_count = 0;
int sub_init_done = -1;
int endpoint_url = -1;

int get_end_URL()
{
    ezxml_t xmlParent= NULL;
    char szTmp[50];
    xmlParent = ezxml_parse_file("/odi/conf/config.xml");
    if (xmlParent == NULL)
    {
        logger_error("Parsing config.xml failed");
        return -1;
    }
    if (getField(xmlParent, "officer_title", (char *) &szTmp))
    {
        logger_error("officer_name not found");
    }
    else
        logger_cloud("officer title: %s", (char *) &szTmp);

    if(0 == strcmp("qa", (char *) &szTmp))
    {
        endpoint_url = 1;
        logger_cloud("%s: Run QA URL: %d", __FUNCTION__, endpoint_url);
    }
    else
    {
        endpoint_url = 0;
        logger_cloud("%s: Run PRODUCTION URL: %d", __FUNCTION__, endpoint_url);
    }
}

void init_sub_message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    struct mosq_config *cfg;
    int i;
    bool res;
    logger_detailed("%s: SUB Entering ...",__FUNCTION__);

    if(init_process_messages == false) return;

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

//    logger_cloud("SUB receive message: %s", message->payload);
    if(strstr(message->payload, "error"))
    {
        logger_cloud("Request return with error. Stop");
        mosquitto_disconnect(mosq);
    }
    process_json_data(message->payload, UNIT_PUB_FREQ_TAG, &mqtt_publish_freq);
    strcpy((char *) &agency_uuid, process_json_data(message->payload,UNIT_AGENCY_UUID_TAG, 0));
    strcpy((char *) &mqtt_server_url, process_json_data(message->payload,UNIT_MQTT_SERVER_TAG, 0));
    logger_cloud("Turn on SUB config. Got agency_uuid: %s", (char *) &agency_uuid);
    sub_init_done = 1;
    mosquitto_disconnect(mosq);

    if(cfg->msg_count>0)
    {
        init_msg_count++;
        if(cfg->msg_count == init_msg_count)
        {
            init_process_messages = false;
            mosquitto_disconnect(mosq);
        }
    }
    // Get MQTT server URL
    p_mqtt_server_url = (char *) &mqtt_server_url;
    bzero(p_mqtt_server_url, 128);
    p_mqtt_server_url = process_json_data(message->payload, "mqtt_server", 0);
}


void init_sub_connect_callback(struct mosquitto *mosq, void *obj, int result)
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
            logger_cloud("%s\n", mosquitto_connack_string(result));
        }
    }
}

void init_sub_subscribe_callback(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
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

void init_sub_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
    printf("%s\n", str);
}


char *ptest_init[17] = {"mosquitto_sub",
                        "-u", "                                                  	",
                        "-i", "                                                  	",
                        "-h", MQTT_SERVER_URL,
                        "-p", "8080",
                        "-q", "1",
                        "-t", "                                				",
                        "--tls-version", "tlsv1",
                        "--cafile", MQTT_CERT_FILE
                       };

int mqtt_sub_init(char *token, char *topic, char *device)
{
    struct mosq_config cfg;
    struct mosquitto *mosq = NULL;
    int rc;
    int argc = 17;
    char **argv = NULL;
    char *tmp;

    ptest_init[2] = token;
    ptest_init[4] = topic;
    ptest_init[12] = device;
    //logger_cloud("token : %s", ptest_init[2]);
    //logger_cloud("device: %s", ptest_init[4]);
    //logger_cloud("topic : %s", ptest_init[12]);

    //get_end_URL();
    if(endpoint_url == 1) //qa url site
    {
        ptest_init[6] = QA_MQTT_SERVER_URL;
        ptest_init[16] = QA_MQTT_CERT_FILE;
    }
    else
    {
        ptest_init[6] = PROD_MQTT_SERVER_URL;
        ptest_init[16] = PROD_MQTT_CERT_FILE;
    }

    argv = ptest_init;

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
        mosquitto_log_callback_set(mosq, init_sub_log_callback);
        mosquitto_subscribe_callback_set(mosq, init_sub_subscribe_callback);
    }

    mosquitto_connect_callback_set(mosq, init_sub_connect_callback);
    mosquitto_message_callback_set(mosq, init_sub_message_callback);

    rc = client_connect(mosq, &cfg);
    if(rc) return rc;

    rc = mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if(cfg.msg_count>0 && rc == MOSQ_ERR_NO_CONN)
    {
        rc = 0;
    }
    if(rc)
    {
        logger_cloud("Error: %s\n", mosquitto_strerror(rc));
    }
    //logger_cloud("Exit: %s ...", __FUNCTION__);
    return rc;
}

