/*
Copyright (c) 2014 Roger Light <roger@atchoo.org>

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

#ifndef _CLIENT_CONFIG_H
#define _CLIENT_CONFIG_H

#include <stdio.h>
#include "BVcloud.h"

/* pub_client.c modes */
#define MSGMODE_NONE 0
#define MSGMODE_CMD 1
#define MSGMODE_STDIN_LINE 2
#define MSGMODE_STDIN_FILE 3
#define MSGMODE_FILE 4
#define MSGMODE_NULL 5

#define CLIENT_PUB 1
#define CLIENT_SUB 2

#define MQTT_SERVER_URL "prod-mqtt-lb.l3capture.com"
#define MQTT_CERT_FILE "/odi/conf/m2mqtt_ca_prod.crt"

#define PROD_MQTT_SERVER_URL "prod-mqtt-lb.l3capture.com"
#define PROD_MQTT_CERT_FILE "/odi/conf/m2mqtt_ca_prod.crt"

#define QA_MQTT_SERVER_URL "qa-mqtt-lb.l3capture.com"
#define QA_MQTT_CERT_FILE "/odi/conf/m2mqtt_ca_qa.crt"

#define MQTT_SUCCESS 1
#define MQTT_FAILURE 0


struct mosq_config
{
    char *id;
    char *id_prefix;
    int protocol_version;
    int keepalive;
    char *host;
    int port;
    int qos;
    bool retain;
    int pub_mode; /* pub */
    char *file_input; /* pub */
    char *message; /* pub */
    long msglen; /* pub */
    char *topic; /* pub */
    char *bind_address;
#ifdef WITH_SRV
    bool use_srv;
#endif
    bool debug;
    bool quiet;
    unsigned int max_inflight;
    char *username;
    char *password;
    char *will_topic;
    char *will_payload;
    long will_payloadlen;
    int will_qos;
    bool will_retain;
#ifdef WITH_TLS
    char *cafile;
    char *capath;
    char *certfile;
    char *keyfile;
    char *ciphers;
    bool insecure;
    char *tls_version;
#  ifdef WITH_TLS_PSK
    char *psk;
    char *psk_identity;
#  endif
#endif
    bool clean_session; /* sub */
    char **topics; /* sub */
    int topic_count; /* sub */
    bool no_retain; /* sub */
    char **filter_outs; /* sub */
    int filter_out_count; /* sub */
    bool verbose; /* sub */
    bool eol; /* sub */
    int msg_count; /* sub */
#ifdef WITH_SOCKS
    char *socks5_host;
    int socks5_port;
    char *socks5_username;
    char *socks5_password;
#endif
};

//Main stored and shared data for MQTT
//char mqtt_main_token[TOKEN_SIZE+1];
//char mqtt_refresh_token[TOKEN_SIZE+1];
char agency_uuid[128];
char mqtt_server_url[128];
extern int  mqtt_publish_freq;
extern int ready_pub;
//extern char officer_uuid;
//extern char fw_signedURL[256];
extern char *p_fw_signedURL;           //Global fw_signed URL pointer
//extern char mqtt_server_url[64];
extern char *p_mqtt_server_url;        //Global MQTT server URL pointer
extern int sub_init_done;


int client_config_load(struct mosq_config *config, int pub_or_sub, int argc, char *argv[]);
void client_config_cleanup(struct mosq_config *cfg);
int client_opts_set(struct mosquitto *mosq, struct mosq_config *cfg);
int client_id_generate(struct mosq_config *cfg, const char *id_base);
int client_connect(struct mosquitto *mosq, struct mosq_config *cfg);


#define UNIT_XML_HEADER 	"<?xml version=\"1.0\" encoding=\"UTF-8\"?> \n"
#define UNIT_XML_METHEAD 	"    <config-metadata>\n"
#define UNIT_XML_CONFHEAD 	"\t<config>\n"
#define UNIT_XML_DTS_DST	"dts_dst"
#define UNIT_XML_DTS_TZ		"dts_tz"
#define UNIT_XML_NAME		"officer_name"
#define UNIT_XML_UUID		"officer_id"
#define UNIT_XML_DEVICE		"device_name"
#define UNIT_XML_MUTECTL	"mute_ctrl"
#define UNIT_XML_CARNUM		"ops_carnum"
#define UNIT_XML_RECBUZZ	"rec_buzz"
#define UNIT_XML_RECPRE		"rec_pre"
#define UNIT_XML_RECQUAL	"rec_qual"
#define UNIT_XML_TRACE		"snap_trace"
#define UNIT_XML_USB		"usb_login"
#define UNIT_XML_CONFEND    	"    </config>\n"
#define UNIT_XML_METEND		"</config-metadata>"

//#define UNIT_XML_ETH		"\t<eth_addr>%s</eth_addr>\n"
//#define UNIT_XML_DHCP		"\t<eth_dhcp>%s</eth_dhcp>\n"
//#define UNIT_XML_GATEWAY	"\t<eth_gate>%s</eth_gate>\n"
//#define UNIT_XML_NETMASK	"\t<eth_mask>%s</eth_mask>\n"
//#define UNIT_XML_ID		"\t<officer_id>%s</officer_id>\n"
//#define UNIT_XML_BCAST		"\t<remotem_broadcast_ip>%s</remotem_broadcast_ip>\n"

#define UNIT_NAME_TAG		"officer_name"
#define UNIT_UUID_TAG		"officer_uuid"
#define UNIT_AGENCY_UUID_TAG	"agency_uuid"
#define UNIT_LATEST_FW_TAG	"latest_firmware"
#define UNIT_FW_URL_TAG		"firmware_download_url"
#define UNIT_PUB_FREQ_TAG	"publish_frequency"
#define UNIT_MQTT_SERVER_TAG	"mqtt_server"
#define UNIT_DEV_NAME_TAG	"device_name"
#define UNIT_REC_QUAL_TAG	"rec_qual"
#define UNIT_SNAP_TRACE_TAG	"snap_trace"
#define UNIT_DST_DTS_TAG	"dts_dst"
#define UNIT_DST_TZ_TAG		"dts_tz"
#define UNIT_USB_LOGIN_TAG	"usb_login"
#define ACCESS_TOKEN_TAG	"access_token"
#define REFRESH_TOKEN_TAG	"refresh_token"
#define SIGNED_URL_TAG		"signedURL"




#endif
//end mqtt_common.h
