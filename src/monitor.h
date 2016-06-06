#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <linux/input.h>

#define ETH0_CARRIER_FILE "/sys/class/net/eth0/carrier"
#define GPIO90_STATE_FILE "/sys/class/gpio/gpio90/value"
#define GPIO101_STATE_FILE "/sys/class/gpio/gpio101/value"

#ifdef __DEBUG__
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netdb.h>
#include <ifaddrs.h>
#endif

#define SYSTEM_DEFAULT_TIME (1441929611)

struct termios serial_port;
int eth0_saved_state = 0;

// list of commands from imx6 -> uC
// requires ACK
int ack_bop = 0;
int ack_sto = 0;
int ack_stf = 0;
int ack_eto = 0;
int ack_etf = 0;
int ack_sso = 0;
int ack_tio = 0;
int ack_tif = 0;
int ack_vid = 0;
int ack_ups = 0;
int ack_upd = 0;
int ack_rfd = 0;
int ack_rfs = 0;
int ack_bat = 0;
int ack_api = 0;
int stealth_disable = 0;
int current_found = 0;
int rec_time_cnt = 0;
int low_disk_space = 0;
int battery_level;
char battery_level_str[16];
int nstate90 = 0, nstate101 = 1 , tstate90 = 1, stealth_counter = 0;
int stealth_on = 0;
int resync_read = 0;
int mute_on = 0;

int RecSizeChkState = 0;	// 01.06.2015
char current_file[100];
char* ip_address = 0;
int network_counter = 0;
int recording_counter = 0;
struct sigaction saio; /* definition of signal action */
void signal_handler_IO(int status); /* definition of signal handler */
volatile int is_processing_done = 0;
volatile int recording = 0; // start off as OFF
volatile int camera_docked = 0; // start off as OFF
volatile int chargerON = 0; // start off as OFF
volatile int httpON = 0; // start off as OFF
int stealth_time_state_save = 0;
int planb_size = 400000;

// global variables
enum MON_LOG_LEVEL {
    MON_LOG_INFO, MON_LOG_DEBUG, MON_LOG_ERROR, MON_LOG_NONE = 0xF
};
enum MON_LOG_LEVEL log_level = MON_LOG_NONE;

#define MINIMUM_SPACE 200   // minimum space to start new recording in MB
#define MINIMUM_BATTERY 15  // minimum battery level

#define COMM_MESSAGE_SIZE 7
#define COMM_BUFFER_SIZE 7*1024
char serial_read_buf[COMM_MESSAGE_SIZE + 1];
char serial_raw_read_buf[COMM_BUFFER_SIZE + 1];

int serial_raw_read_cur = 0;
int serial_raw_read_last = 0; // amt avail == cur - last read
int serial_port_fd = -1;
pthread_t tty_read_tid;
pthread_mutex_t tty_lock;
int thread_run = 1;
int snap_trace = 0; // default to not do anything
char broadcast_ip[200]; //change to fit ip list 10x20
char* version_str = NULL;
char* dvr_id = NULL;
char api_version[8];
char hw_version[8];
char assignable[8];
char dvr_name[32];

// DENCZEK - removed dynamic memory allocation used in logging
#define TIME_BUFFER_SIZE 2000
#define LOGGER_BUFFER_SIZE 2000

char info_buffer[LOGGER_BUFFER_SIZE];
char debug_buffer[LOGGER_BUFFER_SIZE];
char error_buffer[LOGGER_BUFFER_SIZE];
char pause_time_buffer[TIME_BUFFER_SIZE];

