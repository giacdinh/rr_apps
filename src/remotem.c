#include <time.h>
#include "odi-config.h"
#include "remotem.h"
#include "mongoose.h"
#include "BVcloud.h"

extern int write_command_to_serial_port(char *comm);
extern void logger_remotem(char* str_log, ...);
extern void logger_error(char* str_log, ...);
extern int get_battery_level();
extern char *get_assignable();
extern char *get_hw_version();
extern char *get_command(char *cmd);
extern char *get_dvr_name();
extern void set_dvr_name(char *s);
extern void set_planb_size(int sz);
extern void set_assignable(char *s);
int set_sys_clock(char *datestring, char *timestring);
char fw_version[20];

// Remove trailing spaces from var
void trimString(char * s) {
    char * p = s;
    int l = strlen(p);

    while (isspace(p[l - 1]))
        p[--l] = 0;
    while (*p && isspace(*p))
        ++p, --l;

    memmove(s, p, l + 1);
}

static void get_qsvar(const struct mg_request_info *request_info,
                      const char *name, char *dst, size_t dst_len) {
    const char *qs = request_info->query_string;
    char tmp_arg[dst_len];

    mg_get_var(qs, strlen(qs == NULL ? "" : qs), name, tmp_arg, dst_len);
    int j = 0;
    unsigned int i = 0;
    for (i = 0; i < dst_len; i ++) {
        if (tmp_arg[i] == '"')
            continue;
        dst[j] = tmp_arg[i];
        j ++;
    }
}

// check file path that contains .. directory descriptor
static int is_valid_file_path(char* str_path) {
    int i = 0;
    int int_ret_val = 1;
    for (i = 0; i < ((int)strlen(str_path) - 1); i ++) {
        if (str_path[i] == '.' && str_path[i+1] == '.') {
            int_ret_val = 0;
            break;
        }
    }

    return int_ret_val;
}

int my_file_stat(const char *path, struct my_file *filep) {
    struct stat st;

    filep->modification_time = (time_t) 0;
    if (stat(path, &st) == 0) {
        filep->size = st.st_size;
        filep->modification_time = st.st_mtime;
        filep->is_directory = S_ISDIR(st.st_mode);

        if (filep->modification_time == (time_t) 0) {
            filep->modification_time = (time_t) 1;
        }
    }

    return filep->modification_time != (time_t) 0;
}

static int my_str_cmp(char* str_full_src, char* str_cmp) {
    int i = 0;
    if (strlen(str_cmp) > strlen(str_full_src))
        return -1;

    int int_same = 0;
    for (i = 0; i < (int)strlen(str_cmp); i ++) {
        if (str_full_src[i] != str_cmp[i]) {
            int_same = 1;
            break;
        }
    }
    if (int_same == 0) {
        if (str_full_src[strlen(str_cmp)] != '/')
            int_same = 1;
    }
    return int_same;
}

// below added for xml merge
// removes spaces between # and the text
static int modifytree(xmlNodePtr *ParentPtr, xmlNodePtr sourceNode) {
    int notfound = -1; //not found
    xmlNodePtr child = (*ParentPtr)->xmlChildrenNode, txt = NULL;
    while (child != NULL) {
        if ((xmlStrcmp(child->name, (const xmlChar *)"text"))) {
            if ((!xmlStrcmp(child->name, sourceNode->name))) {
                xmlNodePtr destNode = xmlCopyNode(sourceNode, 1);
                xmlNodePtr old = xmlReplaceNode(child, destNode);
                xmlFreeNode(old);
                notfound = 0;
                break;
            }
        } else {
            txt = child;
        }
        child = child->next;
    }

    if (notfound == -1) {
        xmlAddChild(*ParentPtr, xmlCopyNode(sourceNode, 1));
        xmlAddChild(*ParentPtr, xmlCopyNode(txt, 1));
        logger_remotem("LOAD_CONFIG_FILE: Node not found, adding new node: %s",
                       sourceNode->name);
    }
    return notfound;
}

char *getVersion() {
    sprintf((char *) &fw_version, "%s", BODYVISIONVERSION);
    return (char *) &fw_version;
}

char *getSerial() {
    char *id = malloc(20);
    char *str,*ptr;
    FILE *fp;
    X509 *x509;

    char deviceid[11];
    ERR_load_crypto_strings();

    fp = fopen(ODI_CERT_FILE, "r");
    if (NULL == fp) {
        printf("Error opening file: %s\n", ODI_CERT_FILE);
    }
    x509 = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose (fp);
    if (x509 == NULL) {
        printf("error reading certificate\n");
        return 0;
    }
    str = X509_NAME_oneline (X509_get_subject_name (x509), 0, 0);
    if (str) {
        ptr=strstr(str,"/CN=");
        if (ptr) {
            if (strlen(ptr)==14) {
                strcpy(deviceid,ptr+4);
                sprintf(id, "%s", deviceid);
            } else {
                printf("error parsing unit certificate - invalid device id\n");
            }
        } else {
            printf("error parsing unit certificate - can't find CN\n");
        }
    }
    OPENSSL_free (str);
    X509_free (x509);
    return id;
}

char *getMAC(int fmt) {
    int sd;
    uint8_t src_mac[14];
    struct ifreq ifr;

    //int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    // Submit request for a socket descriptor to look up interface.
    if ((sd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
        perror ("socket() failed to get socket descriptor for using ioctl() ");
        return;
    }

    // Use ioctl() to look up interface name and get its MAC address.
    memset (&ifr, 0, sizeof (ifr));
    snprintf (ifr.ifr_name, sizeof (ifr.ifr_name), "eth0");
    if (ioctl (sd, SIOCGIFHWADDR, &ifr) < 0) {
        perror ("ioctl() failed to get source MAC address ");
        return;
    }
    close(sd);
    // Copy source MAC address.
    memcpy (src_mac, ifr.ifr_hwaddr.sa_data, 6 * sizeof (uint8_t));
    char *str = malloc(20);
    if (fmt) {
        sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
                src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);
    } else {
        sprintf(str, "%02x%02x%02x%02x%02x%02x",
                src_mac[0],src_mac[1],src_mac[2],src_mac[3],src_mac[4],src_mac[5]);
    }

    return str;
}

void get_config_info() {
    char line[512] = "";
    char *token, *ptr;
    FILE *fp = fopen(WEB_CONFIG_CONF, "r");

    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            token = strtok_r(line, "=", &ptr);
            trimString(ptr);
            if (strcmp(token, "filepath") == 0) {
                strcpy(DataPath, ptr);

            }
        }
        fclose(fp);

        logger_remotem("DataPath: %s", DataPath);
    } else {
        logger_remotem("GET_INFO: cannot open  config file - %s", WEB_CONFIG_CONF);
    }
}

// Build connmanctl command from network settings
void update_network(int dhcp, char *address, char *netmask, char *gateway) {
    DIR *dir;
    struct dirent *dp;

    char sbuff[360], mac_address[30];
    char name[12] = "ethernet_";

    sprintf(mac_address, "ethernet_%s_cable", getMAC(0));

    name[9] = 0;
    memset(sbuff, 0, 360);
    dir = opendir("/var/lib/connman/");
    while ((dp = readdir(dir)) != NULL) {
        if (strstr(dp->d_name, name) != NULL && strcmp(dp->d_name, mac_address) == 0) {
            // formulate command
            if (dhcp) {
                sprintf(sbuff, "connmanctl config %s --ipv4 dhcp", dp->d_name);
            } else {
                sprintf(sbuff, "connmanctl config %s --ipv4 manual %s %s %s",
                        dp->d_name, address, netmask, gateway);
            }
            closedir(dir);
            logger_remotem("update_network: %s", sbuff);
            system(sbuff);
            system("sync; sync") ;
            int i;
            for (i=0;i<5;i++) sleep(1);
            return;
        }
    }
}

// Update network settings from /odi/conf/config.xml file
void configure_network() {
    struct my_file xmlfile = MY_STRUCT_FILE_INITIALIZER;
    if (!my_file_stat(ODI_CONFIG_XML, &xmlfile)) {
        logger_remotem("Config file not found: %s", ODI_CONFIG_XML);
        return;
    }

    xmlDocPtr configXmlDocumentPointer = xmlParseFile(ODI_CONFIG_XML);
    if (configXmlDocumentPointer == 0) {
        logger_error("Configure network: config XML not well formed %s",
                     ODI_CONFIG_XML);
        return;
    }

    // doc check
    xmlNodePtr cur = xmlDocGetRootElement(configXmlDocumentPointer);

    if (cur == NULL) {
        logger_error("Configure network: input XML is empty");
        xmlFreeDoc(configXmlDocumentPointer);
        return;
    }
    // config-meta check
    if (xmlStrcmp(cur->name, (const xmlChar *) "config-metadata")) {
        logger_remotem("Configure network: config-metadata tag not found");
        xmlFreeDoc(configXmlDocumentPointer);
        return;
    }
    cur = cur->xmlChildrenNode;
    xmlNodePtr child= NULL;
    char *address = 0, *netmask = 0, *gateway = 0;
    int dhcp = 0;
    while (cur != NULL) {
        if ((!xmlStrcmp(cur->name, (const xmlChar *)"config"))) {
            child = cur->xmlChildrenNode;
            while (child != NULL) {
                if (!(xmlStrcmp(child->name, (const xmlChar *)"text"))) {
                    ;
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_dhcp"))) {
                    if (!(xmlStrcmp((const xmlChar *)xmlNodeGetContent(child),
                                    (const xmlChar *)"true"))) {
                        dhcp = 1;
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_addr"))) {
                    address = (char *)xmlNodeGetContent(child) ;
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_mask"))) {
                    netmask = (char *)xmlNodeGetContent(child);
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_gate"))) {
                    gateway = (char *)xmlNodeGetContent(child);
                }
                child = child->next;
            }
        }
        cur = cur->next;
    } // end while loop until NULL all elements processed

    if (dhcp == 0 && (address == NULL || strlen(address) == 0 ||
                      netmask == NULL || strlen(netmask) == 0 ||
                      gateway  == NULL || strlen(gateway) == 0) ) {
        logger_remotem("Configure network error: address=%s, netmask=%s, gateway=%s",
                       address, netmask, gateway);
        logger_remotem("Configure network: static requires address, net mask & gateway");
        return;
    }
    update_network(dhcp, address, netmask, gateway);
}

// end of addition

// Convert numeric ip to string
char* ip2s(long ip) {
    int i;
    int ips[4];
    for (i=0; i<4; i++) {
        ips[i] = ip&255;
        ip = ip >> 8;
    }
    char *str = malloc(20);
    sprintf(str, "%d.%d.%d.%d", ips[3], ips[2], ips[1], ips[0]);
    return str;
}

// Return total size and free blocks of fs
unsigned long *getDriveInfo(char * fs) {
    int int_status;
    struct statvfs stat_buf;
    static unsigned long s[2];

    int_status = statvfs(fs, &stat_buf);
    if (int_status == 0) {
        s[1] = (stat_buf.f_bavail * 4) / 1000;
        s[0] = (stat_buf.f_blocks * 4) / 1000;
    }
    return s;
}

// DENCZEK - Recoded
// Return number of videos in data dir
int getVideoCount(char* fs)
{
    if(!fs)
    {
        logger_error("opendir() FAILED, path is NULL");
        return -1;
    }

    DIR* ptr_dir;
    struct dirent* stu_dirent;
    int int_items = 0;

    ptr_dir = opendir(fs);
    if(ptr_dir)
    {
        while((stu_dirent = readdir(ptr_dir)) != 0)
        {
            if(stu_dirent->d_type == DT_REG)
            {
                if (strstr(stu_dirent->d_name, ".mkv"))
                {
                    int_items++;
                }
            }
        }
        closedir(ptr_dir);
    }
    else
    {
        logger_error("opendir() FAILED path - %s", fs);
        return -1;
    }

    return int_items;
}

// 1. LIST_DIRECTORY
static void proc_cmd_list_directory(struct mg_connection *conn,
                                    const struct mg_request_info *request_info) {
    char str_path[20];
    char str_date[20];
    int  date_filtered = 0;
    int log_dir = -1;
    get_qsvar(request_info, "path", str_path, sizeof(str_path));

    //Get date range in number to filter later
    get_qsvar(request_info, "date", str_date, sizeof(str_date));
    date_filtered = strtol(str_date, NULL, 10);


    //check if list log directory, only return *.log files
    if(NULL != strstr(str_path, (char *) LOG_DIRECTORY))
        log_dir = 1;

    if (strcmp(str_path, UPLOAD_DIRECTORY) != 0 &&
            strcmp(str_path, CONF_DIRECTORY) != 0 &&
            strcmp(str_path, LOG_DIRECTORY) != 0 &&
            strcmp(str_path, DATA_DIRECTORY) != 0) {
        // error : : Input parameter not specified.
        logger_remotem("list_directory FAILED path - %s", str_path);
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n"
                  "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                  "errno=%d\r\n",
                  INPUT_PARAM_NOT_SPECIFIED);
        return;
    }
    // Get file list
    char str_dir[50];
    sprintf(str_dir, "%s%s", ODI_HOME, str_path);

    DIR *ptr_dir;
    struct dirent *stu_dirent;
    int int_items;
    char str_file_path[100];
    char buff[20];

    int_items = 0;
    ptr_dir = opendir(str_dir);
    if (ptr_dir == NULL) {
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n"
                  "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                  "items=0\r\n"
                  "errno=0\r\n");
        return;
    }
    pthread_rwlock_rdlock(&rwlock);
    //  Get count for malloc
    int vc = 2;
    while ( (stu_dirent = readdir(ptr_dir) )) {
        if (strcmp(stu_dirent->d_name, ".") == 0 ||
                strcmp(stu_dirent->d_name, "..") == 0 || stu_dirent->d_type == DT_DIR) {
            continue;
        } else {
            vc++;
        }
    }
    closedir(ptr_dir);

    struct stat statbuf;
    char *str_ret_cont = malloc(vc*100);
    memset(str_ret_cont, vc*100, 0);
    struct tm tmi;

    ptr_dir = opendir(str_dir);
    int int_pos = 0;
    while ( (stu_dirent = readdir(ptr_dir) )) {
        if (strcmp(stu_dirent->d_name, ".") == 0 || strcmp(stu_dirent->d_name,
                          "..") == 0 || stu_dirent->d_type == DT_DIR) {
            continue;
        } else {
            if(log_dir == -1) //other then /log directory return everything
            {
                sprintf(str_ret_cont + int_pos, "file.%d=%s/%s\r\n", int_items,
                        str_path, stu_dirent->d_name);
                int_pos = strlen(str_ret_cont);
                sprintf(str_file_path, "%s/%s", str_dir, stu_dirent->d_name);
                if (stat(str_file_path, &statbuf) == -1) {
                    sprintf(str_ret_cont + int_pos, "size.%d=0\r\n", int_items);
                    int_pos = strlen(str_ret_cont);

                    time_t now = time(NULL);
                    strftime(buff, 20, "%Y%m%d-%H%M%S", localtime_r(&now, &tmi));
                    sprintf(str_ret_cont + int_pos, "date.%d=%s\r\n", int_items, buff);
                    int_pos = strlen(str_ret_cont);
                } else {
                    sprintf(str_ret_cont + int_pos, "size.%d=%u\r\n", int_items, (unsigned int)statbuf.st_size);
                    int_pos = strlen(str_ret_cont);
                    strftime(buff, 20, "%Y%m%d-%H%M%S",
                             localtime_r(&(statbuf.st_mtime), &tmi));
                    sprintf(str_ret_cont + int_pos, "date.%d=%s\r\n", int_items, buff);
                    int_pos = strlen(str_ret_cont);
                }
                int_items ++;
            }
            else //only return log files
            {
                if(NULL != strstr(stu_dirent->d_name, "status")) //log file header name match
                {
                    //get file status
                    sprintf(str_file_path, "%s/%s", str_dir, stu_dirent->d_name);
                    stat(str_file_path, &statbuf);
                    int current_date, file_date;
                    char str_file_date[8];
                    //get file date from file name
                    strncpy((char *) &str_file_date, (char *) (stu_dirent->d_name+7), 8); //strlen of "status_"
                    file_date = strtol(str_file_date, NULL, 10);
                    //Get current mon/date value to compare for current log file print
                    time_t t = time(NULL);
                    struct tm local = *localtime_r(&t, &tmi);
                    current_date = (local.tm_year + 1900) *10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
                    //get file status
                    stat(str_file_path, &statbuf);

                    if(date_filtered > 0 && file_date >= date_filtered) //print filtered date
                    {
                        //file name
                        sprintf(str_ret_cont + int_pos, "file.%d=%s/%s\r\n", int_items, str_path, stu_dirent->d_name);
                        int_pos = strlen(str_ret_cont);
                        sprintf(str_file_path, "%s/%s", str_dir, stu_dirent->d_name);
                        //file size
                        sprintf(str_ret_cont + int_pos, "size.%d=%u\r\n", int_items, (unsigned int)statbuf.st_size);
                        int_pos = strlen(str_ret_cont);
                        //file date
                        strftime(buff, 20, "%Y%m%d-%H%M%S", localtime_r(&(statbuf.st_mtime), &tmi));
                        sprintf(str_ret_cont + int_pos, "date.%d=%s\r\n", int_items, buff);
                        int_pos = strlen(str_ret_cont);
                        int_items ++;
                    }
                    else if (date_filtered <= 0 && current_date == file_date)
                    {
                        //file name
                        sprintf(str_ret_cont + int_pos, "file.%d=%s/%s\r\n", int_items, str_path, stu_dirent->d_name);
                        int_pos = strlen(str_ret_cont);
                        sprintf(str_file_path, "%s/%s", str_dir, stu_dirent->d_name);
                        //file size
                        sprintf(str_ret_cont + int_pos, "size.%d=%u\r\n", int_items, statbuf.st_size);
                        int_pos = strlen(str_ret_cont);
                        //file date
                        strftime(buff, 20, "%Y%m%d-%H%M%S", localtime_r(&(statbuf.st_mtime), &tmi));
                        sprintf(str_ret_cont + int_pos, "date.%d=%s\r\n", int_items, buff);
                        int_pos = strlen(str_ret_cont);
                        int_items ++;
                    }
                }
            }
        }
    }
    closedir(ptr_dir);
    pthread_rwlock_unlock(&rwlock);

    sprintf(str_ret_cont + int_pos, "items=%d\r\nerrno=0\r\n", int_items);
    mg_printf(conn,
              "HTTP/1.0 200 OK\r\n"
              "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
              "%s\r\n",
              str_ret_cont);

    free(str_ret_cont);
    logger_remotem("List_directory completed path: %s, count=%d", str_path, int_items);
}

// 2. PUT_FILE
static void proc_cmd_put_file(struct mg_connection *conn,
                              const struct mg_request_info *request_info) {

    char str_path[200];
    char str_target_dir[200];

    get_qsvar(request_info, "path", str_path, sizeof(str_path));

    if (strcmp(str_path, CONF_DIRECTORY) != 0 &&
            strcmp(str_path, DATA_DIRECTORY) != 0) {
        logger_remotem(" put_file path not valid = %s; switch to %s", str_path,
                       UPLOAD_DIRECTORY);
        sprintf(str_path, "%s", UPLOAD_DIRECTORY);
    }

    sprintf(str_target_dir, "%s%s", ODI_HOME, str_path);
    memset(str_path, 0, 200);
    logger_remotem("PUT_FILE: path - %s", str_target_dir);
    FILE *fp = mg_upload(conn, str_target_dir, str_path, sizeof(str_path));
    if (fp != NULL) {
        fclose(fp);
        logger_remotem("Upload success: put_file path - %s", str_path);
    } else {
        logger_remotem("PUT_FILE : Failed to send.");
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n\r\nPut file failed\r\nerrno=%d\r\n",
                  PUT_FILE_FAILED);
        return;
    }

    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");
}

// 3. PUT_FILE_CLEANUP
static void proc_cmd_put_file_cleanup(struct mg_connection *conn,
                                      const struct mg_request_info *request_info) {
    char str_command[100];
    logger_remotem("PUT_FILE with CLEANUP: delete all files in %s",
                   UPLOAD_DIRECTORY);
    // Clean up upload directory

    sprintf(str_command, "exec rm -rf %s/*", ODI_UPLOAD);
    pthread_rwlock_rdlock(&rwlock);
    system(str_command);
    pthread_rwlock_unlock(&rwlock);
    // Process upload file
    proc_cmd_put_file(conn, request_info);
    logger_remotem("PUT_FILE with CLEANUP: SUCCESS");
}

// 4. GET_FILE
static void proc_cmd_get_file(struct mg_connection *conn,
                              const struct mg_request_info *request_info) {
    char str_file_path[200];
    char str_full_path[200];
    memset(str_file_path, 0, 200);
    memset(str_full_path, 0, 200);
    get_qsvar(request_info, "path", str_file_path, sizeof(str_file_path));

    if (str_file_path == NULL || strlen(str_file_path) == 0) {
        logger_remotem("GET_FILE FAILED: no path entered");
        mg_printf(
                    conn,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                    "errno=%d\r\n",
                    INPUT_PARAM_NOT_SPECIFIED);
        return;
    }

    char* str_file_name = basename(str_file_path);

    if ((my_str_cmp(str_file_path, UPLOAD_DIRECTORY) && my_str_cmp(
             str_file_path, CONF_DIRECTORY) && my_str_cmp(str_file_path,
                                                          LOG_DIRECTORY) && my_str_cmp(str_file_path, DATA_DIRECTORY) )
            || (strlen(str_file_name) == 0)) {
        logger_remotem("GET_FILE FAILED: wrong path %s", str_file_path);
        mg_printf(
                    conn,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                    "errno=%d\r\n",
                    INPUT_PARAM_NOT_SPECIFIED);
        return;
    }

    sprintf(str_full_path, "%s%s", ODI_HOME, str_file_path);

    logger_remotem("GET_FILE  STARTED: %s, client=%s", str_full_path,
                   ip2s(request_info->remote_ip));
    mg_send_file(conn, str_full_path);
    logger_remotem("GET_FILE COMPLETE, clearing caches: %s", str_full_path);
    system("echo 3 > /proc/sys/vm/drop_caches");
}

// 5. DELETE_FILE
static void proc_cmd_delete_file(struct mg_connection *conn,
                                 const struct mg_request_info *request_info) {
    char str_file_path[200];
    char str_full_path[200];
    // Get file name from the request
    get_qsvar(request_info, "path", str_file_path, sizeof(str_file_path));
    sprintf(str_full_path, "%s%s", ODI_HOME, str_file_path);

    char* str_file_name = basename(str_file_path);
    if (str_file_path == NULL || strlen(str_file_path) == 0
            || (strlen(str_file_name) == 0)) {
        logger_remotem("DELETE_FILE: path variable is null.");
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n"
                  "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                  "errno=%d\r\n",
                  INPUT_PARAM_NOT_SPECIFIED);
        return;
    }

    if (my_str_cmp(str_file_path, CONF_DIRECTORY) == 0 || my_str_cmp(
                str_file_path, LOG_DIRECTORY) == 0
            || is_valid_file_path(str_full_path) == 0) {
        // error : : Input parameter not specified.
        logger_remotem("DELETE_FILE: invalid file path %s.", str_file_path);
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n"
                  "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
                  "errno=%d\r\n",
                  FILE_NOT_ALLOWED_DELETE);
        return;
    }

    struct my_file file = MY_STRUCT_FILE_INITIALIZER;

    if (!my_file_stat(str_full_path, &file)) {
        logger_remotem("DELETE_FILE: File not found %s.", str_full_path);
        send_http_error(conn, 404, "Not Found", "%s", "File not found");
        return;
    } else if (!file.modification_time) {
        logger_remotem("DELETE_FILE: IO error %s, errno %s", str_full_path,
                       strerror(errno));
        send_http_error(conn, 500, http_500_error, "remove(%s): %s",
                        str_file_path, strerror(errno));
        return;
    } else if (remove(str_full_path) != 0) {
        logger_remotem("DELETE_FILE: File Locked  %s, errno %s", str_full_path,
                       strerror(errno));
        send_http_error(conn, 423, "Locked", "remove(%s): %s", str_file_path,
                        strerror(errno));
        return;
    }

    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");

    logger_remotem("DELETE_FILE: SUCCESS path %s", str_file_path);
    system("sync;sync");
}

// 6. GET_DRIVE_INFO
static void proc_cmd_get_drive_info(struct mg_connection* conn,
                                    const struct mg_request_info* request_info) {
    char str_path[200];
    char str_drive[200];
    get_qsvar(request_info, "path", str_path, sizeof(str_path));

    sprintf(str_drive, "/odi%s", str_path);
    logger_remotem("GET_DRIVE_INFO: %s", str_drive);

    unsigned long *drive_info = getDriveInfo(str_drive);
    mg_printf(conn,
              "HTTP/1.0 200 OK\r\n\r\n"
              "filesystem_size=%lu\r\n"
              "free_blocks=%lu\r\n"
              "errno=0\r\n",
              drive_info[0], drive_info[1]);
}

// 7. FORMAT_DEVICE
static void proc_cmd_format_device(struct mg_connection* conn,
                                   const struct mg_request_info* request_info) {
    DIR *ptr_dir;
    struct dirent *stu_dirent;
    char str_path[200];
    char str_full_path[200];
    char str_dir[200];
    //
    // True formatting of the file system not implemeted at this time
    // We will simply delete all files from the path to simulate this action
    //
    get_qsvar(request_info, "path", str_path, sizeof(str_path));
    logger_remotem("FORMAT Device: %s",str_path);
    sprintf(str_dir, "%s%s", ODI_HOME, str_path);

    ptr_dir = opendir(str_dir);
    while ( (stu_dirent = readdir(ptr_dir) )) {
        if (strcmp(stu_dirent->d_name, ".") == 0 || strcmp(stu_dirent->d_name,
                                                           "..") == 0 || stu_dirent->d_type == DT_DIR) continue;
        sprintf(str_full_path, "%s/%s", str_dir, stu_dirent->d_name);
        logger_remotem("Deleting file: %s %d", str_full_path, remove(str_full_path));
    }

    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");
    logger_remotem("FORMAT Device: complete");
}

// 8. LOAD_CONFIG_FILE
// test: HTTP://192.168.1.155/cgi-bin/Flash2AppREMOTEM_load_config_file.cgi?path=/upload/input.xml
static void proc_cmd_load_config_file(struct mg_connection* conn,
                                      const struct mg_request_info* request_info) {
    char configXmlPath[200];
    char inputXmlPath[200];
    char str_file_path[200];
    char *address = 0, *netmask = 0, *gateway = 0;
    int dhcp = 0;
    char *broadcast_xml_field;
    char *rec_pre_xml_field;

    int found = 0;
    int isNetUpdate = 0;

    get_qsvar(request_info, "path", str_file_path, sizeof(str_file_path));
    if (strlen(str_file_path) == 0) {
        mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=%d\r\n",
                  INPUT_PARAM_NOT_SPECIFIED);
        return;
    }

    sprintf(inputXmlPath, "%s%s", ODI_HOME, str_file_path);
    sprintf(configXmlPath, "%s", ODI_CONFIG_XML);

    logger_remotem("load_config_file: merge XML Config File: from %s to %s",
                   inputXmlPath, ODI_CONFIG_XML);

    struct my_file xmlfile = MY_STRUCT_FILE_INITIALIZER;
    if (!my_file_stat(inputXmlPath, &xmlfile)) {
        logger_remotem("No Input Config file to Load  %s", inputXmlPath);
        send_http_error(conn, 404, "No Input Config file to Load ",
                        "%s not found", inputXmlPath);
        goto leave;
    }
    if (!my_file_stat(ODI_CONFIG_XML, &xmlfile)) {
        logger_remotem("No destination Config file to Merge  %s",
                       ODI_CONFIG_XML);
        send_http_error(conn, 404,
                        "No original config file to compare for merge ",
                        "%s not found", ODI_CONFIG_XML);
        goto leave;
    }
    // read network file
    logger_remotem("Start merge XML Config File: %s", inputXmlPath);

    xmlDocPtr inputXmlDocumentPointer = xmlParseFile(inputXmlPath);
    if (inputXmlDocumentPointer == 0) {
        logger_remotem("LOAD_CONFIG_FILE: input XML not well formed %s",
                       inputXmlPath);
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n\r\nInputXML file is not well-formed.\r\n");
        return;
    }
    xmlDocPtr configXmlDocumentPointer = xmlParseFile(ODI_CONFIG_XML);
    if (configXmlDocumentPointer == 0) {
        logger_remotem("LOAD_CONFIG_FILE: config XML not well formed %s",
                       ODI_CONFIG_XML);
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n\r\nConfiguration XML file is not well-formed.\r\n");
        return;
    }

    // doc check
    xmlNodePtr cur = xmlDocGetRootElement(inputXmlDocumentPointer);

    if (cur == NULL) {
        logger_remotem("LOAD_CONFIG_FILE: input XML is empty");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }
    // config-meta check
    if (xmlStrcmp(cur->name, (const xmlChar *) "config-metadata")) {
        logger_remotem("LOAD_CONFIG_FILE: config-metadata tag not found");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    xmlNodePtr destParent = xmlDocGetRootElement(configXmlDocumentPointer);

    destParent = xmlDocGetRootElement(configXmlDocumentPointer);
    //node not found
    if (destParent == NULL) {
        logger_remotem("LOAD_CONFIG_FILE: empty doc");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    if (xmlStrcmp(destParent->name, (const xmlChar *) "config-metadata")) {
        logger_remotem("LOAD_CONFIG_FILE:  root node != config-metadata");
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    destParent = destParent->xmlChildrenNode;
    while (destParent) {
        if ((!xmlStrcmp(destParent->name, (const xmlChar *)"config"))) {
            found = 1;
            //Parse child content for field that need reboot
            xmlNodePtr info = destParent->xmlChildrenNode;
            while(info != NULL) {
                if (!xmlStrcmp(info->name, (const xmlChar *) "remotem_broadcast_ip")) {
                    //    logger_info("Tag 1: %s", info->name);
                    //    logger_info("Content: %s", (char *)xmlNodeGetContent(info));
                    broadcast_xml_field = (char *) xmlNodeGetContent(info);
                } else if (!xmlStrcmp(info->name, (const xmlChar *) "rec_pre")) {
                    //    logger_info("Tag 2: %s", info->name);
                    //    logger_info("Content: %s", (char *)xmlNodeGetContent(info));
                    rec_pre_xml_field = (char *) xmlNodeGetContent(info);

                }
                info = info->next;
            }
            break;
        }
        destParent = destParent->next;
    }
    if (!found) {
        logger_remotem("LOAD_CONFIG_FILE: config tag not found in %s",
                       configXmlPath);
        xmlFreeDoc(configXmlDocumentPointer);
        xmlFreeDoc(inputXmlDocumentPointer);
        goto leave;
    }

    cur = cur->xmlChildrenNode;
    xmlNodePtr child= NULL;
    char *new_date = 0, *new_time = 0, *new_tz = 0;
    char str_comm[200];
    unsigned int config_change_reboot = 0;
    while (cur != NULL) {
        if ((!xmlStrcmp(cur->name, (const xmlChar *)"config"))) {
            child = cur->xmlChildrenNode;
            while (child != NULL) {
                if (!(xmlStrcmp(child->name, (const xmlChar *)"text"))) {
                    ;
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"dvr_id"))) {
                    // cannot change DVR ID
                    ;
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"dts_date"))) {
                    new_date = (char *)xmlNodeGetContent(child) ;
                    if (strlen(new_date) == 10) { // Check for valid date
                        logger_remotem("LOAD_CONFIG_FILE: setting date %s", new_date);
                        modifytree(&destParent, child);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"dts_time"))) {
                    new_time = (char *)xmlNodeGetContent(child) ;
                    if (strlen(new_time) == 8) { // Check for valid time
                        logger_remotem("LOAD_CONFIG_FILE: setting time %s", new_time);
                        modifytree(&destParent, child);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"dvr_name"))) {
                    char *str = (char *)xmlNodeGetContent(child) ;
                    if (strlen(str) > 1) {
                        logger_remotem("LOAD_CONFIG_FILE: setting dvr_name %s", str);
                        modifytree(&destParent, child);
                        set_dvr_name(str);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"ops_carnum"))) {
                    char *str = (char *)xmlNodeGetContent(child) ;
                    if (strlen(str) > 1) { // Check for valid time
                        logger_remotem("LOAD_CONFIG_FILE: setting ops_carnum %s", str);
                        modifytree(&destParent, child);
                        set_dvr_name(str);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"usb_login"))) {
                    char *str = (char *)xmlNodeGetContent(child) ;
                    if (strlen(str) > 1) { // Check for valid time
                        logger_remotem("LOAD_CONFIG_FILE: setting usb_login %s", str);
                        modifytree(&destParent, child);
                        set_assignable(str);
		    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"dts_tz"))) {
                    new_tz = (char *)xmlNodeGetContent(child) ;
                    modifytree(&destParent, child);

                    char ptr[strlen(new_tz)+1];
                    int i, j=0;

                    for (i=0; new_tz[i]!=' '; i++) {
                        // skip till space
                    }
                    i++; // move up from the space
                    for (i; new_tz[i]!='\0'; i++) {
                        ptr[j++]=new_tz[i];
                    }
                    ptr[j]='\0';

                    memset(str_comm, 0, 200);
                    sprintf(str_comm, "export TZ=%s", ptr);
                    system(str_comm);

                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_dhcp"))) {
                    isNetUpdate = 1;
                    if (!(xmlStrcmp((const xmlChar *)xmlNodeGetContent(child),
                                    (const xmlChar *)"true"))) {
                        dhcp = 1;
                        logger_remotem("LOAD_CONFIG_FILE: change to DHCP");
                    } else {
                        dhcp = 0;
                        logger_remotem("LOAD_CONFIG_FILE: DHCP is static");
                    }
                    modifytree(&destParent, child);
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_addr"))) {
                    address = (char *)xmlNodeGetContent(child) ;
                    if (address != NULL && strlen(address) > 7) {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_mask"))) {
                    netmask = (char *)xmlNodeGetContent(child);
                    if (netmask != NULL && strlen(netmask) > 7) {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"eth_gate"))) {
                    gateway = (char *)xmlNodeGetContent(child);
                    if (gateway != NULL && strlen(gateway) > 7) {
                        isNetUpdate = 1;
                        modifytree(&destParent, child);
                    }
                    // Set flag for config update item need to reboot to apply change
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"remotem_broadcast_ip"))) {
                    logger_info("New: %s Old: %s", xmlNodeGetContent(child), broadcast_xml_field);
                    if(strcmp((char *) xmlNodeGetContent(child), broadcast_xml_field))
						config_change_reboot = 1;
					modifytree(&destParent, child);
                } else if (!(xmlStrcmp(child->name, (const xmlChar *)"rec_pre"))) {
                    if(rec_pre_xml_field == NULL)
                        config_change_reboot = 1;
                    else if(strcmp((char *) xmlNodeGetContent(child), rec_pre_xml_field))
						if(strncmp(get_hw_version(), "3", 1)) // Only update when >= v4.0
                        	config_change_reboot = 1;
                    modifytree(&destParent, child);
                } else {
                    if (modifytree(&destParent, child) == -1) {
                        logger_remotem("LOAD_CONFIG_FILE: %s doesn't contain %s tag",
                                       configXmlPath, (char *)child->name);
                    }
                }
                child = child->next;
            }
        }
        cur = cur->next;
    } // end while loop until NULL all elements processed

    if (new_date == NULL && new_time == NULL) {
        logger_remotem("%s: date and time are null", __FUNCTION__);
    } else {
        set_sys_clock(new_date, new_time);
    }

    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");
    if (isNetUpdate)
        update_network(dhcp, address, netmask, gateway);

    logger_remotem("LOAD_CONFIG_FILE: saving new config, size=%d",
                   xmlSaveFileEnc(configXmlPath, configXmlDocumentPointer, "UTF-8"));

leave:
    if (inputXmlDocumentPointer)
        xmlFreeDoc(inputXmlDocumentPointer);

    if (configXmlDocumentPointer)
        xmlFreeDoc(configXmlDocumentPointer);

    if(config_change_reboot == 1)
    {
        logger_info("Config item change need system reboot");
//        write_command_to_serial_port("RST\r\n");
//        sleep(1);
        system("reboot");
    }
}

// 9. LOAD_SOFTWARE_UPGRADE
static void proc_cmd_load_sw_upgrade(struct mg_connection* conn,
                                     const struct mg_request_info *request_info) {
    char str_command[200];
    char str_path[200];

    get_qsvar(request_info, "path", str_path, sizeof(str_path));
    sprintf(str_command, "%s/%s %s > %s/firmware_upgrade 2>&1",
            ODI_BIN, SW_UPGRADE_COMMAND, str_path, ODI_LOG);

    // send uC Mess RFS
    logger_remotem("FIRMWARE UPGRADE send RFS to uC");
    write_command_to_serial_port("RFS\r\n");

    strcpy(downloading_status,"Upgrading");
    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");

    system(str_command);
    logger_remotem("Load sw upgrade: SUCCESS %s", str_command);
    write_command_to_serial_port("RFD\r\n");

    logger_remotem("FIRMWARE UPGRADE: completed, execute reboot");
    system(REBOOT_COMMAND);
}

// 10. GET_STATUS
static void proc_cmd_get_status(struct mg_connection* conn,
                                const struct mg_request_info *request_info) {
    logger_remotem("GET_STATUS from client: %s", ip2s(request_info->remote_ip));
    mg_printf(conn, "HTTP/1.0 200 OK\r\n\ndvr_status=%s\r\nerrno=0\r\n",
              downloading_status);
}

// 11. REBOOT
static void proc_cmd_reboot(struct mg_connection* conn) {
    logger_remotem("REBOOT: system kill all processes");
    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");
    system(REBOOT_COMMAND);
    logger_remotem("REBOOT: SUCCESS %s", REBOOT_COMMAND);
}

// 12. GET_INFO
static void proc_cmd_get_info(struct mg_connection* conn,
                              const struct mg_request_info* request_info) {
    logger_remotem("GET_INFO from client: %s, version %s, ID: %s",
                   ip2s(request_info->remote_ip), getVersion(), DVR_ID);
    unsigned long *drive_info = getDriveInfo(DataPath);

    if(DVR_ID == NULL)
    {
        logger_info("No valid ID found wait for next call: %s", DVR_ID);
        mg_printf(conn,"errno=51010\r\n");
        return;
    }

    char tb[40];
    time_t now = time(NULL);
    struct tm tmi;
    strftime(tb, 20, "%Y%m%d_%H%M%S", localtime_r(&now, &tmi));
    mg_printf(conn,
              "HTTP/1.0 200 OK\r\n\n"
              "serial=%s\r\n"
              "version=%s\r\n"
              "dvr_status=%s\r\n"
              "battery_status=%s\r\n"
              "product_code=FBBW1\r\n"
              "filesystem_size=%lu\r\n"
              "free_blocks=%lu\r\n"
              "fg_video=%d\r\n"
              "mac_address=%s\r\n"
              "mainboard=%s\r\n"
              "date_time=%s\r\n"
              "assignable=%s\r\n"
              "dvr_name=%s\r\n"
              "errno=0\r\n",
              DVR_ID, getVersion(), downloading_status, get_battery_level_string(),
              drive_info[0], drive_info[1], getVideoCount(DataPath),
              getMAC(1), get_hw_version(), tb, get_assignable(), get_dvr_name());
}

// 13. DOWNLOADING
static void proc_cmd_downloading(struct mg_connection* conn,
                                 const struct mg_request_info* request_info) {
    char str_downloading_mode[50];
    get_qsvar(request_info, "mode", str_downloading_mode,
              sizeof(str_downloading_mode));
    logger_remotem("DOWNLOADING: received status %s", str_downloading_mode);
    if ( (strstr(str_downloading_mode, "started") == 0) && (strstr(
                                                                str_downloading_mode, "stopped") == 0)) {
        logger_remotem("DOWNLOADING: error unknown mode requested  %s",
                       str_downloading_mode);
        mg_printf(conn,
                  "HTTP/1.0 200 OK\r\n\nerrno=1\r\nmode=%s\r\nno such mode\r\n",
                  str_downloading_mode);
    } else {
        if (strstr(str_downloading_mode, "started") != NULL) {
            strcpy(downloading_status,"Downloading");
            logger_remotem("Upload started send UPS to uC");
            write_command_to_serial_port("UPS\r\n");
            sleep(1);
        }
        mg_printf(conn, "HTTP/1.0 200 OK\r\n\nerrno=0\r\nmode=%s\r\n",
                  str_downloading_mode);
        if (strstr(str_downloading_mode, "stopped") != NULL) {
            logger_remotem("Upload stopped send UPD to uC");
            strcpy(downloading_status, "Idle");
            write_command_to_serial_port("UPD\r\n");
            sleep(1);
        }
    }
}

// 14. IDENTIFY
static void proc_cmd_identify(struct mg_connection* conn) {
    mg_printf(conn, "HTTP/1.0 200 OK\r\n\r\nerrno=0\r\n");

    // send uC Mess VID
    logger_remotem("VVLP IDENTIFY send VID to uC");
    write_command_to_serial_port("VID\r\n");
    sleep(1);

}

// 15. LOAD_UCODE_UPGRADE
static void proc_cmd_load_uc_upgrade(struct mg_connection* conn,
                                     const struct mg_request_info *request_info) {
}

// 16. Remote command
static void proc_command(struct mg_connection *conn,
                                    const struct mg_request_info *request_info) {
    char cmd[20];
    char ret[100];

    get_qsvar(request_info, "cmd", cmd, sizeof(cmd));
    logger_remotem("proc_command=%s", cmd);
    if (strstr(cmd, "PLANB")) {
        char size[20];
        get_qsvar(request_info, "size", size, sizeof(size));
        int sz = atoi(size);
        set_planb_size(sz);
        strcpy(ret, size);
    } else if (strstr(cmd, "TIO")) {
        struct statvfs stat_buf;
        unsigned long long_free_blk;
        unsigned long long_data_size;
        char message[14];

        statvfs(ODI_DATA, &stat_buf);
        long_free_blk = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
        long_data_size = stat_buf.f_blocks * stat_buf.f_bsize / 1024000;
        // for free size get time 35MB per minute
        logger_remotem("proc_recording_time: size=%u free=%u", long_data_size, long_free_blk);

        float fminutes = (float)long_free_blk / (float)35;
        int hour = fminutes / 60;
        int minutes = (int)fminutes - (hour*60);
        int seconds = (fminutes - (minutes+hour*60)) * 60.;
        sprintf(message, "TM%.2d:%.2d:%.2d\r\n", hour, minutes, seconds);
        strcpy(ret, message);
        get_command(message);
    } else {
        strcat(cmd, "\r\n");
        strcpy(ret, get_command(cmd));
    }

    mg_printf(conn, "HTTP/1.0 200 OK\r\n"
              "Content-Type: text/plain;charset=iso-8859-1\r\n\r\n"
              "%s\r\n", ret);
}

static int event_handler(struct mg_event *event) {
    struct mg_request_info *request_info = event->request_info;
    struct mg_connection *conn = event->conn;
    int result = 1;
    /*
    logger_debug("=====>request_info->uri: %s%s?%s,%d",
                   ip2s(request_info->remote_ip), request_info->uri,
                   request_info->query_string,event->type);
    */
    //If lock to CLOUD, don't allow DES call until config.xml reset
    if(check_cloud_flag())
    {
        return;
    }

    if (event->type != MG_REQUEST_BEGIN) {
        return 0;
    }

    // Using HTTPS protocol
    /* if (!request_info->is_ssl)
     //{
     //    redirect_to_ssl(conn, request_info);
     // }*/
    // 1. LIST_DIRECTORY
    else if (strcmp(request_info->uri, str_cmd_list_directory) == 0) {
        proc_cmd_list_directory(conn, request_info);
    }
    // 2. PUT_FILE
    else if (strcmp(request_info->uri, str_cmd_put_file) == 0) {
        proc_cmd_put_file(conn, request_info);
    }
    // 3. PUT_FILE_CLEANUP
    else if (strcmp(request_info->uri, str_cmd_put_file_cleanup) == 0) {
        proc_cmd_put_file_cleanup(conn, request_info);
    }
    // 4. GET_FILE
    else if (strcmp(request_info->uri, str_cmd_get_file) == 0) {
        proc_cmd_get_file(conn, request_info);
    }
    // 5. DELETE_FILE
    else if (strcmp(request_info->uri, str_cmd_delete_file) == 0) {
        proc_cmd_delete_file(conn, request_info);
    }
    // 6. GET_DRIVE_INFO
    else if (strcmp(request_info->uri, str_cmd_get_drive_info) == 0) {
        proc_cmd_get_drive_info(conn, request_info);
    }
    // 7. FORMAT_DEVICE
    else if (strcmp(request_info->uri, str_cmd_format_device) == 0) {
        proc_cmd_format_device(conn, request_info);
    }
    // 8. LOAD_CONFIG_FILE
    else if (strcmp(request_info->uri, str_cmd_load_config_file) == 0) {
        proc_cmd_load_config_file(conn, request_info);
    }
    // 9. LOAD_SOFTWARE_UPGRADE
    else if (strcmp(request_info->uri, str_cmd_load_sw_upgrade) == 0) {
        proc_cmd_load_sw_upgrade(conn, request_info);
    }
    // 10. GET_STATUS
    else if (strcmp(request_info->uri, str_cmd_get_status) == 0) {
        proc_cmd_get_status(conn, request_info);
    }
    // 12. GET_INFO
    else if (strcmp(request_info->uri, str_cmd_get_info) == 0) {
        proc_cmd_get_info(conn, request_info);
    }
    // 13. DOWNLOADING
    else if (strcmp(request_info->uri, str_cmd_downloading) == 0) {
        proc_cmd_downloading(conn, request_info);
    }
    // 14. IDENTIFY
    else if (strcmp(request_info->uri, str_cmd_identify) == 0) {
        proc_cmd_identify(conn);
    }
    // 15. LOAD_UCODE_UPGRADE
    else if (strcmp(request_info->uri, str_cmd_load_uc_upgrade) == 0) {
        proc_cmd_load_uc_upgrade(conn, request_info);
    }
    // 11. REBOOT
    else if (strcmp(request_info->uri, str_cmd_reboot) == 0) {
        proc_cmd_reboot(conn);
    }
    // 16. Remote commad
    else if (strcmp(request_info->uri, str_command) == 0) {
        proc_command(conn, request_info);
    }
    else {
        // No suitable handler found, mark as not processed. Mongoose will
        // try to serve the request.
        result = 0;
    }

    return result;
}

int start_mongoose(int ether) {
    struct mg_context *ctx;

    // Initialize random number generator. It will be used later on for
    // the session identifier creation.
    srand((unsigned) time(0));

    // Setup and start Mongoose
    const char *options[] = {
        "document_root", ODI_HOME,
        "listening_ports", WEB_SERVER_PORT,
        //"access_log_file", "/odi/log/mongoose_access",
        "error_log_file", "/odi/log/mongoose_error",
        "enable_directory_listing", "no",
        "ssl_certificate", SSL_KEY_FILE,
        "request_timeout_ms", REQUESTED_TIME_OUT,
        NULL
    };

    if ((ctx = mg_start(options, event_handler, NULL)) == NULL) {
        logger_error("%s\n", "Cannot start REMOTEM, fatal exit");
        exit(EXIT_FAILURE);
    }
    logger_remotem("Web server root directory is %s, started on ports %s",
                   mg_get_option(ctx, "document_root"), mg_get_option(ctx, "listening_ports"));

    strcpy(downloading_status, "Idle");
    get_config_info();
    if (ether) configure_network();
    DVR_ID = getSerial();

    return EXIT_SUCCESS;
}

//Move time string format from rtc_read.c to head since the system hw clock change
// INPUT 
//	* datestring: Passed in by config.xml
//	* timestring: Passed in by config.xml
int set_sys_clock(char *datestring, char *timestring)
{
    struct tm timeinfo, tmi;
    struct timeval tv;

    gettimeofday(&tv, 0);
    timeinfo = *localtime_r(&tv.tv_sec, &tmi);

    int year  = timeinfo.tm_year;
    int month = timeinfo.tm_mon;
    int day   = timeinfo.tm_mday;
    int hour  = timeinfo.tm_hour;
    int min   = timeinfo.tm_min;
    int sec   = timeinfo.tm_sec;

    logger_remotem("%s: current datetime Y%04d: m%02d:d%02d::%02d:%02d:%02d", __FUNCTION__,
                   year + 1900, month, day, hour, min, sec);

    if (datestring != NULL) {
        char *token = strtok(datestring, "/");
        month = (atoi(token) - 1);
        logger_detailed("%s: month %s - %d \n", __FUNCTION__, token, month );

        token = strtok(NULL, "/");
        if (token != NULL) {
            day = atoi(token) ;
            logger_detailed("%s: day %s - %d \n", __FUNCTION__, token, day );
        }

        token = strtok(NULL, "/");
        if (token != NULL) {
            year = (atoi(token) - 1900);
            logger_detailed("%s: year %s - %d \n", __FUNCTION__, token, year );
        }
    }

    if (timestring != NULL) {
        char *token = strtok(timestring, ":");
        hour = atoi(token);
        logger_detailed("%s: hour %s - %d", __FUNCTION__, token, hour );

        token = strtok(NULL, ":");
        if (token != NULL) {
            min = atoi(token);
            logger_detailed("%s: min %s - %d", __FUNCTION__, token, min );
        }

        token = strtok(NULL, ":");
        if (token != NULL) {
            sec = atoi(token);
            logger_detailed("%s: sec %s - %d", __FUNCTION__, token, sec );
        }
    }

    logger_remotem("%s: new datetime Y%04d: m%02d:d%02d::%02d:%02d:%02d", __FUNCTION__,
                   year + 1900, month, day, hour, min, sec);

    timeinfo.tm_year = year;
    timeinfo.tm_mon = month;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = min;
    timeinfo.tm_sec = sec;

    tv.tv_sec = mktime(&timeinfo);

    if(settimeofday(&tv, NULL) == -1) {
        logger_error("%s: Failed to set time",__FUNCTION__);
        return;
    } else {
        logger_remotem("%s: Write change time to hw clock: %d", __FUNCTION__,
                       system("/sbin/hwclock -w"));
    }
}


