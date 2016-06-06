#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <limits.h>
#include <glob.h>
#include <ctype.h>
#include <gst/gst.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ezxml.h"
#include "odi-config.h"
#include "Flash2AppSM_internal.h"

//#define ODI_KERNEL

/* ODI kernel required delay element to keep sync */
#ifdef ODI_KERNEL
#define ODI_AUDIO_DELAY 100000000
#define MP3_PARSE ""
#else
#define ODI_AUDIO_DELAY 0
#define MP3_PARSE " mp3parse ! "
#endif

// global variables
enum GST_LOG_LEVEL
{
    GST_LOG_NONE, GST_LOG_INFO, GST_LOG_DEBUG, GST_LOG_ERROR
};
enum GST_LOG_LEVEL log_level = GST_LOG_NONE;

enum enum_checksum_mode
{
    CHECKSUM_NONE, CHECKSUM_MKV_ONLY, CHECKSUM_XML_ONLY, CHECKSUM_ALL
};
enum enum_checksum_mode checksum_mode = CHECKSUM_ALL;

enum enum_pre_event_state
{
    PRE_EVENT_SHUTING_DOWN = 0,
    PRE_EVENT_IDLE,
    PRE_EVENT_RUNNING
};
enum enum_pre_event_state pre_event_state = PRE_EVENT_IDLE;

GstElement* g_pipe = NULL;
char szVideoSource[100];
char szAudioSource[100];
char szOutputFile[100];
char szDataPath[100];
char szConfigFile[100];
char szDvrId[100];
char szOfficerName[100];
char szOfficerId[100];
char szUnitName[100];
char szStartTime[100];
char szStopTime[100];
char szDvrId[100];
char szDtsTz[100];
char szDtsDst[100];
int snap_trace_val = 0;
int resolution = 4;
int trace_cnt = 0;
int file_index = 0;
int szVideoSize = 2000;
guint64 szDurationTime = 0;
char file_start_time[100];
gboolean first_filename_set = TRUE;
char szEventTime[100];
gboolean take_snapshot = FALSE;
gchar file_sha_string[FLASH2_SM_MEDIA_HASH_SIZE * 2 + 1];
gint file_offset;
long mkv_file_size;
long target_file_size;
char mkv_filename[256];
GMainLoop* loop;
guint watch_id;
int pre_event_time = 0;


struct tm* today;
GstClockTime duration;

SM_MEDIA_HANDLE HashHandle;
unsigned char RSA_buf[128];

pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

#define TIME_BUFFER_SIZE 2000
#define LOGGER_BUFFER_SIZE 2000
char info_buffer[LOGGER_BUFFER_SIZE];
char debug_buffer[LOGGER_BUFFER_SIZE];
char error_buffer[LOGGER_BUFFER_SIZE];
char pause_time_buffer[TIME_BUFFER_SIZE];


const char* main_bin_description = "mfw_v4lsrc capture-mode=4 queue-size=15 name=video_src ! videorate force-fps=30/1 ! tee name=t ! queue name=image_queue max-size-buffers=0 ! jpegenc ! multifilesink name=image_multifile next-file=0 enable-last-buffer=FALSE async=FALSE sync=FALSE t. ! queue max-size-buffers=0 ! vpuenc codec=6 bitrate=6100000 framerate-nu=30 framerate-de=1 force-framerate=true name=h264_encoder ! copy ! interpipesink name=inter_video_sink_capture node-name=inter_video_sink_capture preroll-queue-len=0 sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true drop=false  alsasrc blocksize=4608 provide-clock=true latency-time=3000 ! audiorate ! queue max-size-buffers=0 ! volume name=volume ! mfw_mp3encoder bitrate=128 optmod=1 sample-rate=44100 ! "MP3_PARSE" delay name=audio_delay delay=0 !  interpipesink name=inter_audio_sink_capture node-name=inter_audio_sink_capture sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true preroll-queue-len=0 drop=false";

const char* video_pretrigger_1 = "interpipesrc name=inter_video_src_0 block=true max-buffers=2 ! pretrigger name=video_trigger_0 buffering=true on-key-frame=true buf-time=0 ! interpipesink name=inter_video_sink_0 node-name=inter_video_sink_0 preroll-queue-len=0 sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true drop=false";

const char* video_pretrigger_2 = "interpipesrc name=inter_video_src_1 block=true max-buffers=2 ! pretrigger name=video_trigger_1 buffering=true on-key-frame=true buf-time=0 ! interpipesink name=inter_video_sink_1 node-name=inter_video_sink_1 preroll-queue-len=0 sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true drop=false";

const char* audio_pretrigger_1 = "interpipesrc name=inter_audio_src_0 block=true max-buffers=2 ! pretrigger name=audio_trigger_0 buffering=true on-key-frame=false buf-time=0 ! interpipesink name=inter_audio_sink_0 node-name=inter_audio_sink_0 sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true preroll-queue-len=0 drop=false";

const char* audio_pretrigger_2 = "interpipesrc name=inter_audio_src_1 block=true max-buffers=2 ! pretrigger name=audio_trigger_1 buffering=true on-key-frame=false buf-time=0 ! interpipesink name=inter_audio_sink_1 node-name=inter_audio_sink_1 sync=false async=false enable-last-buffer=false forward-eos=true forward-tag=true preroll-queue-len=0 drop=false";

const char* sink_bin = "interpipesrc name=inter_video_src_sink block=true max-buffers=2 ! identity name=quit_event_monitor ! video/x-h264 ! matroskamux name=mux target-file-size=2147483648 enable-trace-file=true ! multifilesink name=video_multifile cert=/odi/conf/MobileHD-cert.pem key=/odi/conf/MobileHD-key.pem fact-cert=/odi/conf/Mobile_HD_fact-cert.pem next-file=1 enable-last-buffer=FALSE sync=FALSE async=FALSE interpipesrc name=inter_audio_src_sink block=true max-buffers=2 ! audio/mpeg ! mux.";

const char* video_trigger_bin[] = {"video_pretrigger_bin_0", "video_pretrigger_bin_1"};
const char* audio_trigger_bin[] = {"audio_pretrigger_bin_0", "audio_pretrigger_bin_1"};

unsigned int active_trigger_bin;
unsigned int idle_trigger_bin;
