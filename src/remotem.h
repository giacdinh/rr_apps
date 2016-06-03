#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>

#include <fcntl.h>
#include <termios.h>

#include <errno.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/crypto.h>

#include "libxml/parser.h"
#include "libxml/valid.h"
#include "libxml/xmlschemas.h"
#include "libxml/xmlreader.h"
#include "version.h"

char *DVR_ID;
char DataPath[32];
char downloading_status[20];

// 1. LIST_DIRECTORY
static const char *str_cmd_list_directory =
        "/cgi-bin/Flash2AppREMOTEM_list_directory.cgi";
// 2. PUT_FILE
static const char *str_cmd_put_file = "/cgi-bin/Flash2AppREMOTEM_put_file.cgi";
// 3. PUT_FILE_CLEANUP
static const char *str_cmd_put_file_cleanup =
        "/cgi-bin/Flash2AppREMOTEM_put_file_cleanup.cgi";
// 4. GET_FILE
static const char *str_cmd_get_file = "/cgi-bin/Flash2AppREMOTEM_get_file.cgi";
// 5. DELETE_FILE
static const char *str_cmd_delete_file =
        "/cgi-bin/Flash2AppREMOTEM_delete_file.cgi";
// 6. GET_DRIVE_INFO
static const char *str_cmd_get_drive_info =
        "/cgi-bin/Flash2AppREMOTEM_get_drive_info.cgi";
// 7. FORMAT_DEVICE
static const char *str_cmd_format_device =
        "/cgi-bin/Flash2AppREMOTEM_format_device.cgi";
// 8. LOAD_CONFIG_FILE
static const char *str_cmd_load_config_file =
        "/cgi-bin/Flash2AppREMOTEM_load_config_file.cgi";
// 9. LOAD_SOFTWARE_UPGRADE
static const char *str_cmd_load_sw_upgrade =
        "/cgi-bin/Flash2AppREMOTEM_load_software_upgrade.cgi";
// 10. GET_STATUS
static const char *str_cmd_get_status =
        "/cgi-bin/Flash2AppREMOTEM_get_status.cgi";
// 11. REBOOT
static const char *str_cmd_reboot = "/cgi-bin/Flash2AppREMOTEM_reboot.cgi";
// 12. GET_INFO
static const char *str_cmd_get_info = "/cgi-bin/Flash2AppREMOTEM_get_info.cgi";
// 13. GET_INFO
static const char *str_cmd_downloading =
        "/cgi-bin/Flash2AppREMOTEM_downloading.cgi";
// 14. IDENTIFY
static const char *str_cmd_identify = "/cgi-bin/Flash2AppREMOTEM_identify.cgi";
// 15. LOAD_UCODE_UPGRADE
static const char *str_cmd_load_uc_upgrade =
        "/cgi-bin/Flash2AppREMOTEM_load_ucode_upgrade.cgi";
// 16. Remote Command
static const char *str_command =
        "/cgi-bin/Flash2AppREMOTEM_command.cgi";

static const char *http_500_error = "Internal Server Error";

// Protects from delete operation
static pthread_rwlock_t rwlock= PTHREAD_RWLOCK_INITIALIZER;

#define INPUT_PARAM_NOT_SPECIFIED   50000	   			// 50000  =  Input parameter not specified.
#define FILE_NOT_ALLOWED_DELETE		50005	   			// 50005  =  File not allowed to be deleted.
#define PARTITION_FORMAT_FAILURE	50008	   			// 50008  =  Partition format failure.
#define PUT_FILE_FAILED				50011	   			// 50011  =  Put file failed.
#define SHUTDOWN_REQ_FAILED			50012	   			// 50012  =  Shutdown request failed.
#define SW_UPGRADE_INVALID_SIGN		51001	   			// 51001  =  Software Upgrade invalid signature.
#define SW_UPGRADE_WRITE_FAILED		51002	   			// 51002  =  Software Upgrade write failed.
#define SW_UPGRADE_READ_FAILED		51003	   			// 51003  =  Software Upgrade read failed.
#define SW_UPGRADE_SAME_VERSION		51004	   			// 51004  =  Software Upgrade same version.
#define SW_UPGRADE_TIMEOUT			51005	   			// 51005  =  Software Upgrade timeout.
#define SW_UPGRADE_INVALID_VERSION  51006	   			// 51006  =  Software Upgrade  invalid version.
#define SW_UPGRADE_NO_SUCH_FILE		51007	   			// 51007  =  Software Upgrade no such file.
#define SW_UPGRSDE_INTERNAL_ERROR   51008	   			// 51008  =  Software Upgrade internal error
#define PARTITION_NAME_INVALID		51009	   			// 51009  =  Partition Name Invalid
#define SW_INTERNAL_ERROR			51010	   			// 51010  =  Software Internal Error
#define WEB_SERVER_STATE_INVALID	51011	   			// 51011  =  WEB Server State Invalid

#define UPLOAD_DIRECTORY			"/upload"  			// Upload partition
#define LOG_DIRECTORY				"/log"	   			// Logging partition
#define CONF_DIRECTORY				"/conf"	   			// Configure partition
#define DATA_DIRECTORY				"/data"	   			// File storage

#define REBOOT_COMMAND				"sync;shutdown -r now"   // System reboot command file
#define SW_UPGRADE_COMMAND			"fw_upgrade.sh"		     // Software upgrade command file
#define SSL_KEY_FILE				"/usr/local/bin/ssl_cert.pem"
#define WEB_SERVER_PORT             "80,8080,443s"
#define RET_BUFFER_SIZE             32000
#define REQUESTED_TIME_OUT          "5000"

#define MY_STRUCT_FILE_INITIALIZER { 0, 0, 0, 0 }

struct my_file {
  int is_directory;
  time_t modification_time;
  int64_t size;
  // set to 1 if the content is gzipped
  // in which case we need a content-encoding: gzip header
  int gzipped;
};

int my_file_stat(const char *path, struct my_file *filep);
