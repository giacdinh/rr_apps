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


#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>
#include "mqtt_common.h"
#include "ezxml.h"
#include <sys/statvfs.h>
#include <dirent.h>

#define STATUS_CONNECTING 0
#define STATUS_CONNACK_RECVD 1
#define STATUS_WAITING 2
#define PUB_MSG_SIZE 1024
char l_msg[PUB_MSG_SIZE];

/* Global variables for use in callbacks. See sub_client.c for an example of
 * using a struct to hold variables for use in callbacks. */
static char *topic = NULL;
static char *message = NULL;
static long msglen = 0;
static int qos = 0;
static int retain = 0;
static int mode = MSGMODE_NONE;
static int status = STATUS_CONNECTING;
static int mid_sent = 0;
static int last_mid = -1;
static int last_mid_sent = -1;
static bool connected = true;
static char *username = NULL;
static char *password = NULL;
static bool disconnect_sent = false;
static bool quiet = false;

extern char mqtt_main_token;
extern char mqtt_refresh_token;
extern char *p_officer_uuid;
extern int endpoint_url;
extern LOG_enable;
extern int file_exist;
int had_nofile = 0;

int ready_pub = -1;
int  mqtt_publish_freq = 30; //set INFO PUB default requency in case PUB thread run before SUB return

#define PUB_MQTT_INFO "{\"serialNumber\":\"%s\", \"name\":\"%s\", \
\"firmware\":\"%s\", \"officer_uuid\":\"%s\", \
\"officer_name\":\"%s\", \"upload\":%.02f, \"revision\":\"%s\", \
\"charge\":%.02f, \"disk\":%.02f, \"ip\":\"%s\", \"usb_login\": %s}"

#define PUB_MQTT_NOUUID "{\"serialNumber\":\"%s\", \"name\":\"%s\", \
\"firmware\":\"%s\", \"officer_name\":\"%s\", \"upload\":%.02f, \"revision\":\"%s\", \
\"charge\":%.02f, \"disk\":%.02f, \"ip\":\"%s\", \"usb_login\": %s}"

void reset()
{
    free(topic);
    //free(message);
    msglen = 0;
    qos = 0;
    retain = 0;
    mode = MSGMODE_NONE;
    status = STATUS_CONNECTING;
    mid_sent = 0;
    last_mid = -1;
    last_mid_sent = -1;
    connected = true;
    free(username);
    free(password);
    disconnect_sent = false;
    quiet = false;

}

int get_officer_name(char *off_name)
{
    ezxml_t xmlParent= NULL;
    xmlParent = ezxml_parse_file("/odi/conf/config.xml");
    if (xmlParent == NULL)
    {
        logger_error("Parsing config.xml failed");
        return -1;
    }
    if(getField(xmlParent, "officer_name", off_name))
    {
        logger_error("officer_name not found");
    }
    //logger_cloud("%s: officer name: %s", __FUNCTION__, off_name);
}

int get_unit_name(char *unit_name)
{
    ezxml_t xmlParent= NULL;
    xmlParent = ezxml_parse_file("/odi/conf/config.xml");
    if (xmlParent == NULL)
    {
        logger_error("Parsing config.xml failed");
        return -1;
    }
    if(getField(xmlParent, "ops_carnum", unit_name))
    {
        logger_error("officer_name not found");
    }
    //logger_cloud("%s: officer name: %s", __FUNCTION__, unit_name);
}

int get_config_field(char *field, char *value)
{
    ezxml_t xmlParent= NULL;
    xmlParent = ezxml_parse_file("/odi/conf/config.xml");
    if (xmlParent == NULL)
    {
        logger_error("Parsing config.xml failed");
        return -1;
    }
    if(getField(xmlParent, field , value))
    {
        logger_error("FIELD: %s not found", field);
    }
    //logger_cloud("%s: get %s: %s", __FUNCTION__,field, value);
}


int get_unit_ip(char *ret_ip)
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    ifr.ifr_addr.sa_family = AF_INET;

    snprintf(ifr.ifr_name, IFNAMSIZ, "eth0");

    ioctl(fd, SIOCGIFADDR, &ifr);

    /* and more importantly */
    strcpy(ret_ip,inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    close(fd);
    return;
}

void pub_connect_callback(struct mosquitto *mosq, void *obj, int result)
{
//    printf("Result: %d mode: %d\n", result, mode);
    int rc = MOSQ_ERR_SUCCESS;
    rc = mosquitto_publish(mosq, &mid_sent, topic, msglen, message, qos, retain);
    if(!result)
    {
        switch(mode)
        {
            case MSGMODE_CMD:
            case MSGMODE_FILE:
            case MSGMODE_STDIN_FILE:
                rc = mosquitto_publish(mosq, &mid_sent, topic, msglen, message, qos, retain);
                break;
            case MSGMODE_NULL:
                rc = mosquitto_publish(mosq, &mid_sent, topic, 0, NULL, qos, retain);
                break;
            case MSGMODE_STDIN_LINE:
                status = STATUS_CONNACK_RECVD;
                break;
            default:
                break;
        }
        if(rc)
        {
            switch(rc)
            {
                case MOSQ_ERR_INVAL:
                    logger_cloud("Error: Invalid input. Does your topic contain '+' or '#'?\n");
                    break;
                case MOSQ_ERR_NOMEM:
                    logger_cloud("Error: Out of memory when trying to publish message.\n");
                    break;
                case MOSQ_ERR_NO_CONN:
                    logger_cloud("Error: Client not connected when trying to publish.\n");
                    break;
                case MOSQ_ERR_PROTOCOL:
                    logger_cloud("Error: Protocol error when communicating with brokera\n.");
                    break;
                case MOSQ_ERR_PAYLOAD_SIZE:
                    logger_cloud("Error: Message payload is too large.\n");
                    break;
            }
        }
        mosquitto_disconnect(mosq);
    }
    else
    {
        if(result)
        {
            logger_cloud("%s: %s\n",__FUNCTION__, mosquitto_connack_string(result));
            // If getting here most of the time cause by "Authorized failure"
            // Time to refresh token from mqtt server
            sub_init_done = -1; // Reset flag
            mqtt_authentication_refresh();

        }
    }
}

void pub_disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
    connected = false;
}

void pub_publish_callback(struct mosquitto *mosq, void *obj, int mid)
{
    last_mid_sent = mid;
    if(mode == MSGMODE_STDIN_LINE)
    {
        if(mid == last_mid)
        {
            mosquitto_disconnect(mosq);
            disconnect_sent = true;
        }
    }
    else if(disconnect_sent == false)
    {
        mosquitto_disconnect(mosq);
        disconnect_sent = true;
    }
}

void pub_log_callback(struct mosquitto *mosq, void *obj, int level, const char *str)
{
    printf("%s\n", str);
}

// Return total size and free blocks of fs
static unsigned long *getDriveInfo(char * fs)
{
    int int_status;
    struct statvfs stat_buf;
    static unsigned long s[2];

    int_status = statvfs(fs, &stat_buf);
    if (int_status == 0)
    {
        s[1] = (stat_buf.f_bavail * 4) / 1000;
        s[0] = (stat_buf.f_blocks * 4) / 1000;
    }
    return s;
}

int mqtt_get_file_list()
{
    struct dirent *entries;
    DIR    *directory;
    int i = 0;
    char base_name[25];
    int file_found = -1;
    logger_detailed("%s: Entering ...", __FUNCTION__);
    directory = opendir("/odi/data");
    if (directory != NULL)
    {
        while (1)
        {
            entries = readdir(directory);
            //if ((errno == EBADF) && (entries == NULL))
            if (entries == NULL)
                break;
            else
            {
                if (entries == NULL)
                    break;
                else
                {
                    /* Files only. We don't need directories in the response. */
                    if (DT_REG == entries->d_type)
                    {
                        if(strstr(entries->d_name, "mkv"))
                        {
                            return 1;
                        }
                    }
                }
            }
        }
    }
    return 0;
}

void* mqtt_pub_main_task(void* thread_func_param)
{
    logger_detailed("PUB: Entering: %s PID: %lu", __FUNCTION__, (unsigned long) pthread_self());
    int rc;

    char officer_name[64];
    char unit_name[64];
    char usb_login[16];

//    get_config_field("officer_name", (char *) &officer_name);
//    get_config_field("officer_id", (char *) &unit_name);
//    get_config_field("usb_login", (char *) &usb_login);
    while(1)
    {
        if(checkhost() == 1 && ready_pub == 1)
        {
            get_config_field("officer_name", (char *) &officer_name);
            get_config_field("ops_carnum", (char *) &unit_name);
            get_config_field("usb_login", (char *) &usb_login);
            char *serial = (char *) getSerial(); //"1214100080";
            char *firmware = (char *)getVersion();
            char ip[16];
            get_unit_ip((char *) &ip);
            unsigned long *drive_info = getDriveInfo("/odi/data");
            float upload = (float) (drive_info[0] - drive_info[1]) / drive_info[0];
            static int check_file = 0;
            if(check_file == 0 || check_file > 15*mqtt_publish_freq) //check
            {
                had_nofile = mqtt_get_file_list();
                check_file = 1;
            }
            check_file += mqtt_publish_freq;
            if(had_nofile == 0) // Set for PUB information display
                upload = 0;
            float disk = (float) (1 - upload);
            bzero((char *) &l_msg, PUB_MSG_SIZE);
            if(p_officer_uuid == NULL)
            {
                sprintf((char *) &l_msg, PUB_MQTT_NOUUID,
                        serial, (char *) &unit_name, firmware,
                        (char *) &officer_name, (float) upload, get_hw_version(),
                        (float) get_battery_level()/100, (float) disk,
                        (char *) &ip, (char *) &usb_login);
            }
            else
            {
                sprintf((char *) &l_msg, PUB_MQTT_INFO,
                        serial, (char *) &unit_name, firmware, p_officer_uuid,
                        (char *) &officer_name, (float) upload, get_hw_version(),
                        (float) get_battery_level()/100, (float) disk,
                        (char *) &ip, (char *) &usb_login);
            }
            if(LOG_enable == 1)
            {
                logger_info("PUB MESSAGE: %s", (char *) &l_msg);
                LOG_enable = 0;
            }
            else
                logger_detailed("PUB MESSAGE: %s", (char *) &l_msg);

            mqtt_pub((char *) &l_msg, strlen((char *) &l_msg));
            reset(); // reset all variables before next run
        }
        //logger_cloud("PUB sleep in: %d seconds", mqtt_publish_freq);
        sleep(mqtt_publish_freq);
    }
}

int mqtt_pub(char *pub_msg, int pub_msg_len)
{
    char *pub_test[] = {"mosquitto_pub",
                        "-t", "                                                                                                ",
                        "-u", "                                                                                                ",
                        "-i", "                         	",
                        "-h", MQTT_SERVER_URL, //
                        "-q", "0",
                        "-p", "8080",
                        "--tls-version", "tlsv1",
                        "--cafile", MQTT_CERT_FILE
                       };
    int argc= 17;
    char **argv = pub_test;

    struct mosq_config pub_cfg;
    struct mosquitto *mosq = NULL;
    int rc;
    int rc2;
    char buf[128];
    char *p_uuid = (char *) &buf;
    bzero((char *) &buf, 128);
    strcpy(p_uuid, "/");
    strcat(p_uuid, (char *) &agency_uuid);
    strcat(p_uuid, "/dashboard");
    logger_detailed("PUB: agency_uuid/dashboard: %s", p_uuid);
    pub_test[2] = p_uuid;
    pub_test[4] = (char *) &mqtt_main_token;

    char dev_id[64];
    char *p_dev_id = (char *) &dev_id;
    strcpy(p_dev_id, "device-p-");
    strcat(p_dev_id, (char *) getSerial());
    pub_test[6] = p_dev_id;

    if(endpoint_url == 1) //qa url site
    {
        pub_test[8] = QA_MQTT_SERVER_URL;
        pub_test[16] = QA_MQTT_CERT_FILE;
    }
    else
    {
        pub_test[8] = PROD_MQTT_SERVER_URL;
        pub_test[16] = PROD_MQTT_CERT_FILE;
    }

//    logger_cloud("topic: %s",pub_test[2]);
//    logger_cloud("token: %s",pub_test[4]);
//    logger_cloud("device: %s",pub_test[6]);


    rc = client_config_load(&pub_cfg, CLIENT_PUB, argc, argv);
    if(rc)
    {
        client_config_cleanup(&pub_cfg);
        if(rc == 2)
        {
            printf("Use 'mosquitto_pub --help' to see usage.\n");
            return 1;
        }
    }
    topic = pub_cfg.topic;
    message = pub_msg;
    msglen = pub_msg_len;
    qos = pub_cfg.qos;
    retain = pub_cfg.retain;
    mode = pub_cfg.pub_mode;
    username = pub_cfg.username;
    password = pub_cfg.password;

    mosquitto_lib_init();

    if(client_id_generate(&pub_cfg, "mosqpub"))
    {
        return 1;
    }

    mosq = mosquitto_new(pub_cfg.id, true, NULL);
    if(!mosq)
    {
        switch(errno)
        {
            case ENOMEM:
                printf("Error: Out of memory.\n");
                break;
            case EINVAL:
                printf("Error: Invalid id.\n");
                break;
        }
        mosquitto_lib_cleanup();
        return 1;
    }
    mosquitto_log_callback_set(mosq, pub_log_callback);
    mosquitto_connect_callback_set(mosq, pub_connect_callback);
    mosquitto_disconnect_callback_set(mosq, pub_disconnect_callback);
    mosquitto_publish_callback_set(mosq, pub_publish_callback);

    if(client_opts_set(mosq, &pub_cfg))
    {
        return 1;
    }

    rc = client_connect(mosq, &pub_cfg);
    if(rc) return rc;

    if(mode == MSGMODE_STDIN_LINE)
    {
        mosquitto_loop_start(mosq);
    }

    do
    {
        rc = mosquitto_loop(mosq, -1, 1);
    }
    while(rc == MOSQ_ERR_SUCCESS && connected);

    if(mode == MSGMODE_STDIN_LINE)
    {
        mosquitto_loop_stop(mosq, false);
    }

    if(message && mode == MSGMODE_FILE)
    {
        free(message);
    }
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if(rc)
    {
        printf("Error: %s\n", mosquitto_strerror(rc));
    }

    return rc;
}
