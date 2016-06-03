/*
 * File:   udp_util.c
 * Author: normanc*
 * Created on January 17, 2014, 7:52 PM
 */
#include "odi-config.h"
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <errno.h>
#include <syslog.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <ifaddrs.h>

#define PORT 54771
#define NET_RECOVERY_STOP "net_recovery_stop"

struct sockaddr_in serv_addr;

extern void logger_info(char* str_log, ...);
extern void logger_debug(char* str_log, ...);
extern void logger_error(char* str_log, ...);
extern int check_cloud_flag();
extern int net_up;
extern int chargerON;

struct REMOTEM_NOTIF_STRUCT {
    char DVR_ID[20]; // This is a left justified ID padded with blanks
    char MAC[16]; // This is the MAC address
    int Timestamp; // NOT USED (LEGACY) 4 blanks
    char api[4]; // Two most significant bytes store
    //Position 2 and 3 should be blanks.
    char version[8]; // Firmware version which is a single digit product code
    // followed by an up to 7 digit version number padded with
    // blanks.
    // The version number is in the format: X.X (example 3.1.3).
};
typedef struct REMOTEM_NOTIF_STRUCT localMessDef;

localMessDef msgBody = { " " };

char local_ip[160];
char *p_local_ip = (char *) &local_ip;
char broadcast_ip_list[10][20];

int get_ip_list(char *ip_list)
{
    char *tem = ip_list;
    char ip_holder[20], p_ip_holder;
    int i = 0, j = 0, k=0, l=0;
//    logger_info("%s: Entering ...", __FUNCTION__);	
    j = strlen(ip_list);
    while(i++ < j)
    {
        broadcast_ip_list[k][l++] = *tem;
	if(*tem++ == ' ')
        {
	    l=0;
	    k++;
	}
    }
  
    return k;
}

int sendUDP() {

    int i;
    int sockfd, slen=sizeof(serv_addr);
    //logger_info("Local IP list: %s", p_local_ip); 
    if(access("/odi/conf/cloud.txt", 0) == 0)
	return 0;

    static int list_cnt = 0;
    if(list_cnt == 0)
	list_cnt = get_ip_list(p_local_ip);
    for(i = 0; i <= list_cnt; i++)
    {
//	logger_info("Send UDP to this IP: %s", broadcast_ip_list[i]);
        serv_addr.sin_addr.s_addr=inet_addr(broadcast_ip_list[i]);

        // set udp
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
            logger_error("socket open error");

        int broadcastEnable=1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable,
                   sizeof(broadcastEnable)) == -1)
            logger_error("setsockopt error %d", errno);

        //     sending UDP
        if (sendto(sockfd, &msgBody, sizeof(msgBody), 0,
            (struct sockaddr*)&serv_addr, slen) == -1) {
            logger_error("UDP sendto error: %d", errno);
	    return -1;
        }
        close(sockfd);
    }
    return 0;
}

char * getIP(int *ip_abort) {
    int sd;
    struct ifreq ifr;
    *ip_abort = 0;
     // Submit request for a socket descriptor to look up interface.
    if ((sd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
      logger_error("socket() failed to get socket descriptor for using ioctl()");
      return NULL;
    }
    memset (&ifr, 0, sizeof (ifr));
    snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "eth0");
    int err;
    int i;
    for (i=0;i<12;i++) {
	if(!chargerON || 0 == get_eth0_state())
	{
	    *ip_abort = -1;
	    return NULL ;
	}
        logger_info(">>>ip request...%d",i);
        err = ioctl (sd, SIOCGIFADDR, &ifr);
        if (err != -1) break;
	  
        sleep(4);
	if(i%2 == 0) 
	    system("echo ETH > /dev/ttymxc2");
        sleep(1);
    }

    close (sd);
    if (err < 0) {
      logger_error ("ioctl() failed to get source IP address: %d",errno);
      return NULL;
    }

    char *ip = malloc(20);
    struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    sprintf(ip, "%s", inet_ntoa(ipaddr->sin_addr));

    *ip_abort = 1;
    return ip;
}

void setMACAddress() {
    int sd;
    uint8_t src_mac[14];
    struct ifreq ifr;

     // Submit request for a socket descriptor to look up interface.
    if ((sd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
      logger_error("socket() failed to get socket descriptor for using ioctl()");
      return;
    }

    // Use ioctl() to look up interface name and get its MAC address.
    memset (&ifr, 0, sizeof (ifr));
    snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "eth0");
    if (ioctl (sd, SIOCGIFHWADDR, &ifr) < 0) {
      logger_error("ioctl() failed to get source MAC address");
      return;
    }

    // Copy source MAC address.
    memcpy (src_mac, ifr.ifr_hwaddr.sa_data, 6 * sizeof (uint8_t));

    // Report source MAC address to stdout.
    logger_info("MAC address for eth0: %02x:%02x:%02x:%02x:%02x:%02x",
            src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);
    sprintf(msgBody.MAC,"%02x%02x%02x%02x%02x%02x",
            src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);
}

void initUDP(char *version_str, char *dvr_id, char *api, char *broadcast_ip) {
    setMACAddress();
    strncpy(msgBody.DVR_ID, dvr_id, 10);
    strncpy(msgBody.api, api, 4);
    strncpy(msgBody.version, version_str, 8);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr=inet_addr("255.255.255.255");
    strcpy(p_local_ip, broadcast_ip);    
}
