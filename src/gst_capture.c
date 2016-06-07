#include "gst_capture.h"
#include <math.h>
#include <inttypes.h>
#include "version.h"

#define CERT_FILE "/odi/conf/MobileHD-cert.pem"
#define KEY_FILE "/odi/conf/MobileHD-key.pem"
#define FACT_CERT_FILE "/odi/conf/Mobile_HD_fact-cert.pem"
#define IDF_ERROR "Internal data flow error"

gboolean gst_cap_bus_call(GstBus* bus, GstMessage* msg, void* data);
void generate_last_xml();
void gst_attach_sink();
void gst_detach_sink();
void gst_detach_trigger(unsigned int nbin);
void gst_attach_trigger(unsigned int nbin);
int number_of_process_instances(char* process_name);

int number_of_process_instances(char* process_name)
{
	logger_debug("Entering: %s", __FUNCTION__);
	FILE* proc_fp;
	char* command_string = NULL;
	char* name = NULL;
	name = malloc(strlen(process_name) + 1);
	if (!name)
	{
		logger_error("Memory allocation error");
		return -1;
	}
	memset(name, 0, strlen(process_name) + 1);
	strcpy(name, process_name);

	command_string = realloc(command_string, strlen("/bin/pidof ") + strlen(name) + 1);
	memset(command_string, 0, strlen("/bin/pidof ") + strlen(name) + 1);
	strcat(command_string, "/bin/pidof ");
	strcat(command_string, process_name);

	proc_fp = popen(command_string, "r");
	if (proc_fp == NULL)
	{
		logger_error("Failed to run command: %s", command_string);
		free(command_string);
		free(name);
		return -1;
	}
	/* Read the output a line at a time - output it. */
	while (fgets(command_string, 10, proc_fp) != NULL)
	{
		size_t newbuflen = strlen(command_string);
		int pid_founds = 0;
		if (command_string[newbuflen - 1] == '\n')
		{
			command_string[newbuflen - 1] = '\0';
		}

		char* pid_str;
		pid_str = strtok(command_string, " ");
		while (pid_str != NULL)
		{
			pid_founds++;
			logger_info("Found %s instance with pid %s\n", process_name, pid_str);
			pid_str = strtok(NULL, " ");
		}

		logger_info("Found %d instances of %s", pid_founds, process_name, command_string);
		pclose(proc_fp);
		free(command_string);
		free(name);

		return pid_founds;
	}
	return -1;
}

// davis Start
static void file_write_log(char* level, char* str_log)
{
	time_t now = time(NULL);
	struct tm time_t;
	char time_buf[20];

	localtime_r(&now, &time_t);
	strftime(time_buf, 20, "%Y%m%d", &time_t);
	char fileName[500];

	sprintf(fileName, "%s/%s_%s.log", ODI_LOG, ODI_STATUS_FILE, time_buf);
	FILE* fp = fopen(fileName, "a");
	localtime_r(&now, &time_t);
	strftime(time_buf, 17, "(%Y%m%d%H%M%S)", &time_t);
	sprintf(&pause_time_buffer[0], "%s GST: %s \n", time_buf, str_log);
	fwrite(&pause_time_buffer[0], strlen(&pause_time_buffer[0]), 1, fp);
	fclose(fp);
	if (strstr(str_log, IDF_ERROR))
	{
		sync();
		sleep(1);
		//system("reboot");
	}
}

void logger_info(char* str_log, ...)
{
	if (log_level <  GST_LOG_INFO)
	{
		return;
	}
	memset(&error_buffer[0], 0, LOGGER_BUFFER_SIZE);

	va_list vl;
	va_start(vl, str_log);
	vsprintf(&info_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("INFO", &info_buffer[0]);
}

void logger_debug(char* str_log, ...)
{
	if (log_level <  GST_LOG_DEBUG)
	{
		return;
	}
	memset(&error_buffer[0], 0, LOGGER_BUFFER_SIZE);

	va_list vl;
	va_start(vl, str_log);
	vsprintf(&error_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("DEBUG", &error_buffer[0]);
}

void logger_error(char* str_log, ...)
{
	if (log_level == GST_LOG_NONE)
	{
		return;
	}
	memset(&error_buffer[0], 0, LOGGER_BUFFER_SIZE);

	va_list vl;
	va_start(vl, str_log);
	vsprintf(&error_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("ERROR", &error_buffer[0]);
}
// davis End

int generateManifest(const char* xml_filename, int index)
{
	char filename[256];
	FILE* fp = NULL;
	char stopreason = '0';
	guint nBytes;
	gchar* buffer = NULL;
	gint maxReadBytes = 4096;
	FILE* fp_cert = NULL;

	//    logger_info("%s: Entering ...", __FUNCTION__);

	fp = fopen("/odi/log/stopreason", "r");
	if (fp == NULL)
	{
		logger_error("fopen for stopreason failed. It could be continuation recording");
	}
	else
	{
		fread(&stopreason, 1, 1, fp);
		fclose(fp);
		remove("/odi/log/stopreason");
	}

	sprintf(filename, "%s.xml", xml_filename);
	logger_info("Generating XML file: %s", filename);
	fp = fopen(filename, "w");
	if (fp == NULL)
	{
		logger_error("fopen for manifest failed");
		return -1;
	}
	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(fp, "<header-metadata>\n");
	fprintf(fp, "\t<header>\n");
	fprintf(fp, "\t\t<dts_tz>%s</dts_tz>\n", szDtsTz);
	fprintf(fp, "\t\t<dts_dst>%s</dts_dst>\n", szDtsDst);
	fprintf(fp, "\t\t<officer_name>%s</officer_name>\n", szOfficerName);
	fprintf(fp, "\t\t<unit_name>%s</unit_name>\n", szUnitName);
	fprintf(fp, "\t\t<officer_id>%s</officer_id>\n", szOfficerId);
	fprintf(fp, "\t\t<dvr_id>%s</dvr_id>\n", szDvrId);
	fprintf(fp, "\t\t<start>%s</start>\n", szStartTime);
	fprintf(fp, "\t\t<stop>%s</stop>\n", szStopTime);
	fprintf(fp, "\t\t<stop_event_reason>%c</stop_event_reason>\n", stopreason);
	fprintf(fp, "\t\t<trace>%d</trace>\n", trace_cnt);
	fprintf(fp, "\t\t<sequence>%d</sequence>\n", index);
	fprintf(fp, "\t\t<continuation>%s</continuation>\n", first_filename_set ? "false" : "true");
	fprintf(fp, "\t\t<record_event_time>%s</record_event_time>\n", szEventTime);
	if (checksum_mode == CHECKSUM_MKV_ONLY || checksum_mode == CHECKSUM_ALL)
	{
		fprintf(fp, "\t\t<secure_media>\n");
		fprintf(fp, "\t\t\t<offset>%d</offset>\n", file_offset);
		fprintf(fp, "\t\t\t<length>%d</length>\n", mkv_file_size - file_offset);
		fprintf(fp, "\t\t\t<hash>%s</hash>\n", file_sha_string);
		fprintf(fp, "\t\t</secure_media>\n");
	}
	fprintf(fp, "\t\t<ch1-video_props>\n");
	fprintf(fp, "\t\t\t<fps>3000</fps>\n");
	fprintf(fp, "\t\t\t<bitrate>%d</bitrate>\n", (resolution == 2 ? 2300000 : 6100000));
	fprintf(fp, "\t\t\t<geometry>\n");
	fprintf(fp, "\t\t\t\t<width>%d</width>\n", (resolution == 2 ? 720 : 1280));
	fprintf(fp, "\t\t\t\t<height>%d</height>\n", (resolution == 2 ? 480 : 720));
	fprintf(fp, "\t\t\t</geometry>\n");
	fprintf(fp, "\t\t</ch1-video_props>\n");
	fprintf(fp, "\t</header>\n");

	fflush(fp);
	fclose(fp);

	if (checksum_mode == CHECKSUM_XML_ONLY || checksum_mode == CHECKSUM_ALL)
	{
		/* calculate hash for XML header */

		/* hashing setup */
		if (FLASH2_SUCCESS != Flash2AppSMInit(CERT_FILE, KEY_FILE, FACT_CERT_FILE))
		{
			logger_debug("Error opening files required for RSA calculations");
			return -1;
		}
		HashHandle = Flash2AppSMGenerateHashOpen(KEY_FILE);
		if (NULL == HashHandle)
		{
			logger_debug("Error Hash Failed");
			return -1;
		}

		logger_debug("Calculating XML header hash");
		buffer = (guint8*)malloc(sizeof(char) * maxReadBytes);
		fp = fopen(filename, "r");
		if (fp == NULL)
		{
			logger_error("fopen for hash calculation failed");
			return -1;
		}

		nBytes = fread(buffer, 1, maxReadBytes, fp);
		while (nBytes > 0)
		{
			logger_debug("Generating Hash");
			if (FLASH2_SUCCESS != Flash2AppSMGenerateHashWrite(HashHandle, buffer, nBytes))
			{
				logger_error("failed to calculate XML hashing");
				return -1;
			}
			nBytes = fread(buffer, 1, maxReadBytes, fp);
		}
		fclose(fp);
		logger_debug("Close hash calculation");
		int hash_size = FLASH2_SM_MEDIA_HASH_SIZE;
		Flash2AppSMGenerateHashClose(HashHandle, RSA_buf, &hash_size);

		/* Now open the XML to append the hashing */
		logger_debug("Writing XML header hash");
		fp = fopen(filename, "a");
		if (fp == NULL)
		{
			logger_error("fopen for XML  hash write failed");
			return -1;
		}

		fprintf(fp, "\t<header-hash>");
		int i = 0;
		for (i = 0; i < FLASH2_SM_MEDIA_HASH_SIZE; i++)
		{
			fprintf(fp, "%02X", RSA_buf[i]);
		}
		fprintf(fp, "</header-hash>\n");
		fflush(fp);
		fclose(fp);
	}

	/* If we saved any kind of hashing we need to store the cert information */
	if (checksum_mode)
	{
		/* Read the cert file and append it to the XML*/
		fp = fopen(filename, "a");
		if (fp == NULL)
		{
			logger_error("fopen for cert write failed");
			return -1;
		}

		logger_debug("Reading CERT file");
		fp_cert = fopen(CERT_FILE, "r");
		if (fp_cert == NULL)
		{
			logger_error("fopen for cert read failed");
			return -1;
		}

		if (buffer == NULL)
		{
			buffer = (guint8*)malloc(sizeof(char) * maxReadBytes);
		}

		fprintf(fp, "\t<header-cert>");
		nBytes = fread(buffer, 1, maxReadBytes, fp_cert);
		while (nBytes > 0)
		{
			fwrite(buffer, 1, nBytes, fp);
			nBytes = fread(buffer, 1, maxReadBytes, fp_cert);
		}
		fprintf(fp, "\t</header-cert>\n");
		fflush(fp);
		fclose(fp);
		fclose(fp_cert);
	}

	fp = fopen(filename, "a");
	if (fp == NULL)
	{
		logger_error("fopen for last tag in XML failed");
		return -1;
	}
	fprintf(fp, "</header-metadata>\n");
	fflush(fp);
	fclose(fp);

	if (buffer != NULL)
	{
		free(buffer);
	}

	return 0;
}

int getField(ezxml_t xmlParent, char* fieldname, char* dest)
{
	ezxml_t xmlChild;

	xmlChild = ezxml_get(xmlParent, "config", 0, fieldname, -1);
	if (xmlChild == NULL)
	{
		return 1;
	}
	else
	{
		char* szValue = xmlChild->txt;
		strcpy(dest, szValue);
		dest[strlen(szValue) + 1] = 0;
	}
	return 0;
}

int gst_get_hw_version()
{
	int fp;
	char version;
	fp = open("/odi/log/hw_version.txt", O_RDONLY);
	if (fp < 0)
		return 0;
	else
	{
		read(fp, &version, 1);
		close(fp);
		return ((int)(version - 0x30));
	}
	return 0;
}

int parseXML()
{
	// logger_info("%s: Entering ...", __FUNCTION__);
	ezxml_t xmlParent = NULL;
	char szTmp[50];
	xmlParent = ezxml_parse_file(szConfigFile);
	if (xmlParent == NULL)
	{
		logger_error("Parsing config.xml failed");
		return -1;
	}

	snap_trace_val = 0;
	resolution = 4;
	if (getField(xmlParent, "dts_tz", szDtsTz))
	{
		logger_error("dts_tz not found");
	}
	if (getField(xmlParent, "dts_dst", szDtsDst))
	{
		logger_error("dts_dst not found");
	}
	if (getField(xmlParent, "officer_name", szOfficerName))
	{
		logger_error("officer_name not found");
	}
	if (getField(xmlParent, "officer_id", szOfficerId))
	{
		logger_error("officer_id not found");
	}
	if (getField(xmlParent, "ops_carnum", szUnitName))
	{
		logger_error("ops_carnum not found");
	}
	if (getField(xmlParent, "rec_qual", szTmp))
	{
		logger_error("rec_qual not found");
	}
	else
	{
		resolution = atoi(szTmp);
		if (resolution != 2 && resolution != 4)
		{
			logger_error("Resolution value out of range %d", resolution);
			resolution = 4;
		}
	}
	if (getField(xmlParent, "snap_trace", szTmp))
	{
		logger_error("snap_trace not found");
	}
	else
	{
		snap_trace_val = atoi(szTmp);
		if ((snap_trace_val < 0) || (snap_trace_val > 3))
		{
			logger_error("Snap Trace value out of range %d", snap_trace_val);
		}
	}

	if (getField(xmlParent, "rec_pre", szTmp))
	{
		logger_error("no pre-event time found");
		pre_event_time = 0;
	}
	else
	{
		pre_event_time = atoi(szTmp);
		logger_info("Pre-Event time read: %d", pre_event_time);
	}

	int hw_version = gst_get_hw_version();
	if (hw_version < 4)
	{
		logger_info("Hardware version less than 4.00. Force to turn off pre_event");
		pre_event_time = 0;
	}

	/* logger_info("XML: %s,%s,%s,%s,%s,%s,%d,%d", */
	/*                szDtsTz,szDtsDst,szDvrId,szOfficerId,szOfficerName,szUnitName,snap_trace_val, resolution); */
	ezxml_free(xmlParent);
	return 0;
}

void gst_cap_stop_capture()
{
	logger_debug("Stopped gst_capture program");
}

static gboolean jpeg_probe(GstObject* pad, GstBuffer* buffer, gpointer* pointer)
{
	if (take_snapshot)
	{
		take_snapshot = FALSE;
		return TRUE;
	}

	return FALSE;
}

GstElement* gst_setting_up_pipeline()
{
	GstElement* main_pipe, *videoSrc, *video_multifile, *x264enc, *main_bin;
	GstElement* video_pretrigger_bin_1, *video_pretrigger_bin_2;
	GstElement* audio_pretrigger_bin_1, *audio_pretrigger_bin_2;
	GstElement* video_interpipesrc_1, *audio_interpipesrc_1;
	GstElement* audioSrc;
	GstElement* image_queue, *image_multifile, *video_trigger, *audio_trigger;
	GstElement* volCtrl_elem, *audio_delay;
	GstPad* sinkpadimage;

	logger_info("%s: enter", __FUNCTION__);

	active_trigger_bin = 0;
	idle_trigger_bin = 1;

	main_bin = gst_parse_bin_from_description(main_bin_description, FALSE, NULL);
	video_pretrigger_bin_1 = gst_parse_bin_from_description(video_pretrigger_1, FALSE, NULL);
	video_pretrigger_bin_2 = gst_parse_bin_from_description(video_pretrigger_2, FALSE, NULL);
	audio_pretrigger_bin_1 = gst_parse_bin_from_description(audio_pretrigger_1, FALSE, NULL);
	audio_pretrigger_bin_2 = gst_parse_bin_from_description(audio_pretrigger_2, FALSE, NULL);

	if (main_bin == NULL || video_pretrigger_bin_1 == NULL || video_pretrigger_bin_2 == NULL
		|| audio_pretrigger_bin_1 == NULL || audio_pretrigger_bin_2 == NULL)
	{
		logger_error("could not create bins from main section");
	}

	gst_element_set_name(GST_ELEMENT(main_bin), "main_bin");
	gst_element_set_name(GST_ELEMENT(video_pretrigger_bin_1), "video_pretrigger_bin_0");
	gst_element_set_name(GST_ELEMENT(video_pretrigger_bin_2), "video_pretrigger_bin_1");
	gst_element_set_name(GST_ELEMENT(audio_pretrigger_bin_1), "audio_pretrigger_bin_0");
	gst_element_set_name(GST_ELEMENT(audio_pretrigger_bin_2), "audio_pretrigger_bin_1");

	main_pipe = gst_pipeline_new(NULL);
	g_assert(main_pipe);

	gst_bin_add(GST_BIN(main_pipe), main_bin);
	gst_bin_add(GST_BIN(main_pipe), video_pretrigger_bin_1);
	gst_bin_add(GST_BIN(main_pipe), video_pretrigger_bin_2);
	gst_bin_add(GST_BIN(main_pipe), audio_pretrigger_bin_1);
	gst_bin_add(GST_BIN(main_pipe), audio_pretrigger_bin_2);

	gboolean use_pre_event = FALSE;
	if (pre_event_time > 0)
	{
		logger_info("Using pre-event recording");
		use_pre_event = TRUE;
	}

	video_trigger = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin_1), "video_trigger_0");
	audio_trigger = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin_1), "audio_trigger_0");
	g_object_set(G_OBJECT(video_trigger), "buffering", use_pre_event, NULL);
	g_object_set(G_OBJECT(audio_trigger), "buffering", use_pre_event, NULL);

	if (pre_event_time > 0)
	{
		g_object_set(G_OBJECT(video_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
		g_object_set(G_OBJECT(audio_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
	}
	gst_object_unref(video_trigger);
	gst_object_unref(audio_trigger);

	video_trigger = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin_2), "video_trigger_1");
	audio_trigger = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin_2), "audio_trigger_1");
	g_object_set(G_OBJECT(video_trigger), "buffering", use_pre_event, NULL);
	g_object_set(G_OBJECT(audio_trigger), "buffering", use_pre_event, NULL);

	if (pre_event_time > 0)
	{
		g_object_set(G_OBJECT(video_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
		g_object_set(G_OBJECT(audio_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
	}

	video_interpipesrc_1 = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin_1), "inter_video_src_0");
	audio_interpipesrc_1 = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin_1), "inter_audio_src_0");
	g_object_set(G_OBJECT(video_interpipesrc_1), "listen-to", "inter_video_sink_capture", NULL);
	g_object_set(G_OBJECT(audio_interpipesrc_1), "listen-to", "inter_audio_sink_capture", NULL);

	/* Set the volume on mute. TODO: This should be done only when using pre-recording */
	volCtrl_elem = gst_bin_get_by_name(GST_BIN(main_bin), "volume");
	g_object_set(G_OBJECT(volCtrl_elem), "mute", use_pre_event, NULL);

	/* Set the delay for audio branch if we are on ODI build */
	audio_delay = gst_bin_get_by_name(GST_BIN(main_bin), "audio_delay");
	g_object_set(G_OBJECT(audio_delay), "delay", (gint64)ODI_AUDIO_DELAY, NULL);

	videoSrc = gst_bin_get_by_name(GST_BIN(main_bin), "video_src");
	// capture-mode 2=720x480, 4=1280x720
	g_object_set(G_OBJECT(videoSrc), "capture-mode", resolution, NULL);

	/* Set the correct bitrate to the H264 encoder */
	x264enc = gst_bin_get_by_name(GST_BIN(main_bin), "h264_encoder");
	if (resolution == 2)
	{
		g_object_set(G_OBJECT(x264enc), "bitrate", (gint64)2300000, NULL);
	}
	else
	{
		g_object_set(G_OBJECT(x264enc), "bitrate", (gint64)6100000, NULL);
	}

	/* Add buffer probe to image queue */
	image_queue = gst_bin_get_by_name(GST_BIN(main_bin), "image_queue");
	sinkpadimage = gst_element_get_static_pad(image_queue, "sink");
	gst_pad_add_buffer_probe(sinkpadimage, G_CALLBACK(jpeg_probe), NULL);

	/* Set image file name */
	image_multifile = gst_bin_get_by_name(GST_BIN(main_bin), "image_multifile");
	g_object_set(G_OBJECT(image_multifile), "location", "/dev/NULL", NULL);

	gst_object_unref(audio_delay);
	gst_object_unref(video_trigger);
	gst_object_unref(audio_trigger);
	gst_object_unref(volCtrl_elem);
	gst_object_unref(videoSrc);
	gst_object_unref(x264enc);
	gst_object_unref(image_queue);
	gst_object_unref(sinkpadimage);
	gst_object_unref(image_multifile);
	gst_object_unref(video_interpipesrc_1);
	gst_object_unref(audio_interpipesrc_1);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(main_pipe), GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "gst_setting_up_pipeline");

	logger_info("%s: exit", __FUNCTION__);
	return main_pipe;
}

gboolean gst_cap_bus_call(GstBus* bus, GstMessage* msg, void* data)
{
	gchar* debug;
	GError* err = NULL;
	gchar* element_name;
	gchar s_duration[100];
	const gchar* struct_name;
	const gchar* xml_filename;
	const gchar* start_filename;
	const gchar trace_point_filename[100];
	const gchar* file_sha;
	gchar jpeg_filename[100];
	GstElement* jpeg_multifile, *matroska;
	int retval = 0, iter = 0;
	gchar* lastdot;
	gchar* lastslash;
	const GstStructure* st;
	GMainLoop* loop = (GMainLoop*)data;
	enum GST_LOG_LEVEL level = GST_LOG_NONE;
	FILE* current;
	switch (GST_MESSAGE_TYPE(msg))
	{
		case GST_MESSAGE_ELEMENT:
			element_name = GST_MESSAGE_SRC_NAME(msg);
			st = gst_message_get_structure(msg);
			struct_name = gst_structure_get_name(st);
			logger_debug("GST_MESSAGE_ELEMENT message received from %s with struct name = %s", element_name, struct_name);

			if (strcmp(struct_name, "INT_MATROSKA") == 0)
			{
				logger_debug("Received interrupt from matroska element");
				/* Lets get the duration and parse it to get the stop time */
				gst_structure_get_clock_time(st, "Duration", &duration);
				logger_debug("Duration: %"GST_TIME_FORMAT" duration in ms: %llu", GST_TIME_ARGS(duration), duration / 1000000); //duration is in nano
			}
			else if (strcmp(element_name, "video_multifile") == 0)
			{
				if (strcmp(struct_name, "INT_CLOSE_FILE") == 0)
				{
					logger_debug("Received CLOSE_FILE interrupt");
					/* We get the information of the file just closed */
					xml_filename = gst_structure_get_string(st, "Filename");
					gst_structure_get_int(st, "FileIndex", &file_index);

					/* Get file offset, hash and length */
					file_sha = (gchar*)g_value_get_pointer(gst_structure_get_value(st, "SHA"));
					gst_structure_get_int(st, "Offset", &file_offset);
					sprintf(file_sha_string, "%02X", file_sha[0]);
					for (iter = 1; iter < FLASH2_SM_MEDIA_HASH_SIZE; iter++)
					{
						sprintf(file_sha_string, "%s%02X", file_sha_string, file_sha[iter]);
					}

					FILE* fd = NULL;
					/* NOTE: Here xml_filename has the mkv file name */
					fd = fopen(xml_filename, "rb");
					if (fd == NULL)
					{
						logger_error("Could not open mkv file to get lenght");
					}
					else
					{
						fseek(fd, 0, SEEK_END);
						mkv_file_size = ftell(fd);
						fclose(fd);
					}

					logger_debug("File SHA: %s", file_sha_string);

					// Set the XML Stop value
					//logger_debug("szStartTime: %s", szStartTime);

					char buf[100];
					memset(buf, 0, sizeof(buf));

					struct tm tmStopTime;
					tmStopTime.tm_year = atoi(strncpy(&buf[0], &szStartTime[0], 4)) - 1900;
					memset(buf, 0, sizeof(buf));
					tmStopTime.tm_mon = atoi(strncpy(&buf[0], &szStartTime[4], 2)) - 1;
					memset(buf, 0, sizeof(buf));
					tmStopTime.tm_mday = atoi(strncpy(&buf[0], &szStartTime[6], 2));
					memset(buf, 0, sizeof(buf));
					tmStopTime.tm_hour = atoi(strncpy(&buf[0], &szStartTime[8], 2));
					memset(buf, 0, sizeof(buf));
					tmStopTime.tm_min = atoi(strncpy(&buf[0], &szStartTime[10], 2));
					memset(buf, 0, sizeof(buf));
					tmStopTime.tm_sec = atoi(strncpy(&buf[0], &szStartTime[12], 2));

					//logger_debug("tm_year: %d, tm_mon: %d, tm_mday: %d, tm_hour: %d, tm_min: %d, tm_sec: %d", tmStopTime.tm_year, tmStopTime.tm_mon, tmStopTime.tm_mday, tmStopTime.tm_hour, tmStopTime.tm_min, tmStopTime.tm_sec);

					time_t tmResult = mktime(&tmStopTime);
					if (tmResult != (time_t)-1)
					{
						memset(buf, 0, sizeof(buf));
						sprintf(buf, "%.3f", ((double)duration / 1000000000.0));
						int nDurationSec = (int)(atof(buf) + 0.5);

						//logger_debug("Duration: %d sec", nDurationSec);

						tmStopTime.tm_sec += nDurationSec;
						mktime(&tmStopTime);

						sprintf(&szStopTime[0], "%d%02d%02d%02d%02d%02d", tmStopTime.tm_year + 1900, tmStopTime.tm_mon + 1,
							tmStopTime.tm_mday, tmStopTime.tm_hour, tmStopTime.tm_min, tmStopTime.tm_sec);

						logger_debug("XML Stop: %s", szStopTime);
					}
					else
					{
						logger_error("XML Stop calculation failed");
					}

					logger_debug("Index for the file that made the close file interruption: %d", file_index);
					lastdot = strrchr(xml_filename, '.'); // to remove mkv in the file ext.
					if (lastdot != NULL)
					{
						*lastdot = '\0';
					}
					retval = generateManifest(xml_filename, file_index);
					if (retval < 0)
					{
						logger_error("manifest generation failed");
					}
					if (first_filename_set)
					{
						first_filename_set = FALSE;
					}
					strcpy(szStartTime, szStopTime);
				}
				else if (strcmp(struct_name, "INT_EOS_HASH") == 0)
				{
					logger_debug("Received EOS_HASH interrupt");
					/* We get the information of the file just closed */
					xml_filename = gst_structure_get_string(st, "Filename");
					gst_structure_get_int(st, "FileIndex", &file_index);

					/* Get file offset, hash and length */
					file_sha = (gchar*)g_value_get_pointer(gst_structure_get_value(st, "SHA"));
					gst_structure_get_int(st, "Offset", &file_offset);
					sprintf(file_sha_string, "%02X", file_sha[0]);
					for (iter = 1; iter < FLASH2_SM_MEDIA_HASH_SIZE; iter++)
					{
						sprintf(file_sha_string, "%s%02X", file_sha_string, file_sha[iter]);
					}

					FILE* fd = NULL;
					/* NOTE: Here xml_filename has the mkv file name */
					fd = fopen(xml_filename, "rb");
					if (fd == NULL)
					{
						logger_error("Could not open mkv file to get lenght");
					}
					else
					{
						fseek(fd, 0, SEEK_END);
						mkv_file_size = ftell(fd);
						fclose(fd);
					}

					logger_debug("File SHA: %s", file_sha_string);

					generate_last_xml();

					if (pre_event_time == 0 || pre_event_state == PRE_EVENT_RUNNING)
					{
						logger_debug("exiting main loop");
						g_main_loop_quit(loop);
					}
					else
					{
						unsigned int tmp = active_trigger_bin;
						active_trigger_bin = idle_trigger_bin;
						idle_trigger_bin = tmp;
						logger_info("Active trigger bin: %d", active_trigger_bin);

						gst_detach_sink();
						gst_attach_sink();
						gst_detach_trigger(idle_trigger_bin);
						gst_attach_trigger(idle_trigger_bin);
						pre_event_state = PRE_EVENT_IDLE;
					}
				}
				else if (strcmp(struct_name, "INT_OPEN_FILE") == 0)
				{
					logger_debug("Received OPEN_FILE interrupt");

					/* This logic is for getting the first file name and save it to a .txt file */
					start_filename = (gchar*)gst_structure_get_string(st, "Filename");
					logger_info("Starting new video: %s", start_filename);
					strcpy(jpeg_filename, start_filename); // Saving the complete filename before it gets modified

														   /* We get the start time of the filename to generate the XML file with the same name,
														   the format for the Start Time on the XML and on the name of the file are different */
					lastslash = strrchr(start_filename, '/');
					int nStartOffset = lastslash - start_filename + 1;
					memset(&file_start_time[0], 0, sizeof(file_start_time));
					strncpy(file_start_time, &start_filename[nStartOffset + 11], 13);

					strcpy(szStartTime, "20");
					strncat(szStartTime, file_start_time, 6);
					strncat(szStartTime, &(file_start_time[7]), 6);
					logger_debug("szStartTime: %s", szStartTime);

					/* Generating trace point file name and assigning it to matroska muxer */
					strcpy(trace_point_filename, start_filename);
					lastdot = strrchr(trace_point_filename, '.');
					*(lastdot + 1) = 'v';
					*(lastdot + 2) = 't';
					*(lastdot + 3) = 't';
					matroska = gst_bin_get_by_name(GST_BIN(g_pipe), "mux");
					g_object_set(G_OBJECT(matroska), "trace-file", trace_point_filename, NULL);
					gst_object_unref(GST_OBJECT(matroska));

					lastslash = strrchr(start_filename, '/');
					start_filename = lastslash + 1;
					if (first_filename_set)
					{
						/* Only on the first file we need to update the szStartTime */
						strcpy(szEventTime, szStartTime);
						logger_debug("Creating current file: %s", start_filename);

						current = fopen(CURRENT_FILE, "w");
						if (NULL == current)
						{
							logger_error("Error opening file: %s", CURRENT_FILE);
							exit(0);
						}
						fputs(start_filename, current);
						fclose(current);
						system("sync;sync");
					}
					/* Updating the base filename for the JPEG files */
					lastdot = strrchr(jpeg_filename, '.'); // to remove mkv in the file ext.
					if (lastdot != NULL)
					{
						*lastdot = '\0';
					}
					strcat(jpeg_filename, "_%d.jpg");
					jpeg_multifile = gst_bin_get_by_name(GST_BIN(g_pipe), "image_multifile");
					g_object_set(G_OBJECT(jpeg_multifile), "location", jpeg_filename, NULL);
					g_object_set(G_OBJECT(jpeg_multifile), "index", 0, NULL);
					g_object_unref(jpeg_multifile);
					trace_cnt = 0;
				}
			}
			break;
		case GST_MESSAGE_APPLICATION:
			//g_print("APP received on OBJ NAME %s\n",GST_OBJECT_NAME(msg->src));
			logger_info("Application message received");
			break;
		case GST_MESSAGE_EOS:
			logger_debug("EOS message received");
			sleep(1);
			if (pre_event_state != PRE_EVENT_SHUTING_DOWN)
			{
				g_main_loop_quit(loop);
			}
			break;
		case GST_MESSAGE_INFO:
			level = GST_LOG_INFO;
			gst_message_parse_info(msg, &err, &debug);
			logger_info(err->message);
			break;
		case GST_MESSAGE_WARNING:
			level = GST_LOG_DEBUG;
			gst_message_parse_warning(msg, &err, &debug);
			logger_debug(err->message);
			break;
		case GST_MESSAGE_ERROR:
			level = GST_LOG_ERROR;
			gst_message_parse_error(msg, &err, &debug);
			logger_error(err->message);
			break;
		default:
			break;
	}
	if (level != GST_LOG_NONE)
	{
		g_free(debug);
		g_error_free(err);
		if (level == GST_LOG_ERROR)
		{
			g_main_loop_quit(loop);
		}
	}

	return TRUE;
}

void gst_switch_pretrigger_element()
{
	GstElement *inter_audio_src, *inter_video_src;
	GstElement* video_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), video_trigger_bin[active_trigger_bin]);
	GstElement* audio_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), audio_trigger_bin[active_trigger_bin]);

	logger_info("%s: enter", __FUNCTION__);

	char id[80];
	sprintf(id, "inter_video_src_%d", active_trigger_bin);
	inter_video_src = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin), id);
	sprintf(id, "inter_audio_src_%d", active_trigger_bin);
	inter_audio_src = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin), id);

	g_object_set(G_OBJECT(inter_audio_src), "listen-to", NULL, NULL);
	g_object_set(G_OBJECT(inter_video_src), "listen-to", NULL, NULL);

	GstEvent* video_eos_event = gst_event_new_eos();
	GstEvent* audio_eos_event = gst_event_new_eos();

	/* gst_object_ref(eos_event); */
	gst_element_send_event(inter_video_src, video_eos_event);
	gst_element_send_event(inter_audio_src, audio_eos_event);

	gst_object_unref(inter_audio_src);
	gst_object_unref(inter_video_src);
	gst_object_unref(video_pretrigger_bin);
	gst_object_unref(audio_pretrigger_bin);

	video_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), video_trigger_bin[idle_trigger_bin]);
	audio_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), audio_trigger_bin[idle_trigger_bin]);

	sprintf(id, "inter_video_src_%d", idle_trigger_bin);
	inter_video_src = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin), id);
	sprintf(id, "inter_audio_src_%d", idle_trigger_bin);
	inter_audio_src = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin), id);

	g_object_set(G_OBJECT(inter_audio_src), "listen-to", "inter_audio_sink_capture", NULL);
	g_object_set(G_OBJECT(inter_video_src), "listen-to", "inter_video_sink_capture", NULL);

	gst_object_unref(inter_audio_src);
	gst_object_unref(inter_video_src);
	gst_object_unref(video_pretrigger_bin);
	gst_object_unref(audio_pretrigger_bin);

	logger_info("%s: exit", __FUNCTION__);
}

void gst_cap_signal_handler(int signo)
{
	logger_info("%s: enter with signo: %d", __FUNCTION__, signo);
	if (signo == SIGINT)
	{
		if (g_pipe != NULL)
		{
			logger_debug("Stop capture signal received");
			if (pre_event_time == 0 || pre_event_state == PRE_EVENT_RUNNING)
			{
				logger_debug("Sending SIGINT event to pipeline");

				gst_switch_pretrigger_element();
			}
			else
			{
				logger_debug("Not running - exiting loop");
				GstBin* bin;

				bin = (GstBin*)gst_bin_get_by_name(GST_BIN(g_pipe), "sink_bin");
				gst_detach_trigger(0);
				gst_detach_trigger(1);
				gst_bin_remove(GST_BIN(g_pipe), GST_ELEMENT(bin));

				g_main_loop_quit(loop);
			}
		}
		else
		{
			logger_info("Signal received - pipeline not started exit\n");
			exit(0);
		}
	}
	else if (signo == SIGUSR1)
	{
		if (GST_STATE(g_pipe) == GST_STATE_PLAYING)
		{
			logger_info("It was playing now pause.");
			gst_element_set_state(g_pipe, GST_STATE_PAUSED);
			GstEvent* eos_event1 = gst_event_new_eos();
			gst_element_send_event(g_pipe, eos_event1);
		}
		else
		{
			logger_info("It is not playing. so start record");
			gst_element_set_state(g_pipe, GST_STATE_PLAYING);
		}
	}
	// add SIGUSR2 signal for taking snapshots
	else if (signo == SIGUSR2)
	{
		if (GST_STATE(g_pipe) == GST_STATE_PLAYING)
		{
			logger_info("Received snapshot signal - SSO");
			GstTagList* taglist;

			GstElement* bin = gst_bin_get_by_name(GST_BIN(g_pipe), "main_bin");

			GstElement* camera_src = gst_bin_get_by_name(GST_BIN(bin), "video_src");

			taglist = gst_tag_list_new();
			char to_send_tag_str[100];
			if ((2 == snap_trace_val) || (3 == snap_trace_val))
			{
				trace_cnt++;
			}
			if ((1 == snap_trace_val) || (3 == snap_trace_val))
			{
				take_snapshot = TRUE;
			}

			sprintf(to_send_tag_str, "image clicked-%d", snap_trace_val);

			gst_tag_list_add(taglist, GST_TAG_MERGE_APPEND, GST_TAG_DESCRIPTION, to_send_tag_str, NULL);
			GstEvent* tag_event = gst_event_new_tag(taglist);

			GstPad* camera_pad = gst_element_get_static_pad(camera_src, "src");
			gst_pad_push_event(camera_pad, tag_event);
			gst_object_unref(camera_pad);
			gst_object_unref(bin);
			gst_object_unref(camera_src);
		}
	}
	else if (signo == SIGRTMIN)
	{
		logger_info("SIGRTMIN signal received: Muting audio stream");
		gboolean is_mute;
		GstElement* volCtrl_elem = gst_bin_get_by_name(GST_BIN(g_pipe), "volume");
		g_object_get(G_OBJECT(volCtrl_elem), "mute", &is_mute, NULL);
		if (is_mute == FALSE)
		{
			is_mute = TRUE;
			g_object_set(G_OBJECT(volCtrl_elem), "mute", is_mute, NULL);
		}
		gst_object_unref(volCtrl_elem);
	}
	else if (signo == SIGRTMIN + 1)
	{
		logger_info("SIGRTMIN+1 signal received: Unmuting audio stream");
		gboolean is_mute;
		GstElement* volCtrl_elem = gst_bin_get_by_name(GST_BIN(g_pipe), "volume");
		g_object_get(G_OBJECT(volCtrl_elem), "mute", &is_mute, NULL);
		if (is_mute == TRUE)
		{
			is_mute = FALSE;
			g_object_set(G_OBJECT(volCtrl_elem), "mute", is_mute, NULL);
		}
		gst_object_unref(volCtrl_elem);
	}
	else if (signo == SIGCONT)
	{
		logger_info("SIGCONT signal received");

		if (pre_event_state == PRE_EVENT_IDLE && pre_event_time > 0)
		{
			logger_info("Triggering pre-event recording");
			char id[80];
			GstElement* bin = gst_bin_get_by_name(GST_BIN(g_pipe), "main_bin");
			GstElement* video_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), video_trigger_bin[active_trigger_bin]);
			GstElement* audio_pretrigger_bin = gst_bin_get_by_name(GST_BIN(g_pipe), audio_trigger_bin[active_trigger_bin]);

			sprintf(id, "video_trigger_%d", active_trigger_bin);
			GstElement* video_trigger = gst_bin_get_by_name(GST_BIN(video_pretrigger_bin), id);

			sprintf(id, "audio_trigger_%d", active_trigger_bin);
			GstElement* audio_trigger = gst_bin_get_by_name(GST_BIN(audio_pretrigger_bin), id);

			GstElement* volCtrl_elem = gst_bin_get_by_name(GST_BIN(bin), "volume");

			GstElement* encoder = gst_bin_get_by_name(GST_BIN(bin), "h264_encoder");

			logger_info("Triggering pre-event");
			g_object_set(G_OBJECT(volCtrl_elem), "mute", FALSE, NULL);
			g_object_set(G_OBJECT(video_trigger), "buffering", FALSE, NULL);
			g_object_set(G_OBJECT(audio_trigger), "buffering", FALSE, NULL);

			/* Sending tag to mark pre-event end */
			GstTagList* taglist;
			taglist = gst_tag_list_new();
			char to_send_tag_str[100];// = "pre-event recording end";

			sprintf(to_send_tag_str, "pre-record-tag");

			gst_tag_list_add(taglist, GST_TAG_MERGE_APPEND, GST_TAG_DESCRIPTION, to_send_tag_str, NULL);
			GstEvent* tag_event = gst_event_new_tag(taglist);

			GstPad* encoder_pad = gst_element_get_static_pad(encoder, "src");
			gst_pad_push_event(encoder_pad, tag_event);

			pre_event_state = PRE_EVENT_RUNNING;

			gst_object_unref(bin);
			gst_object_unref(video_pretrigger_bin);
			gst_object_unref(audio_pretrigger_bin);
			gst_object_unref(encoder_pad);
			gst_object_unref(video_trigger);
			gst_object_unref(audio_trigger);
			gst_object_unref(encoder);
			gst_object_unref(volCtrl_elem);
		}
		else
		{
			logger_info("Pre-event recording already running");
		}
	}
	else if (signo == SIGQUIT)
	{
		logger_info("SIGQUIT signal received");

		if (pre_event_state == PRE_EVENT_RUNNING && pre_event_time > 0)
		{
			logger_info("Stopping pre-event recording");
			pre_event_state = PRE_EVENT_SHUTING_DOWN;

			GstElement* bin = gst_bin_get_by_name(GST_BIN(g_pipe), "main_bin");

			/* gst_element_send_event(bin, quit_event); */
			gst_switch_pretrigger_element();

			GstElement* volCtrl_elem = gst_bin_get_by_name(GST_BIN(bin), "volume");

			g_object_set(G_OBJECT(volCtrl_elem), "mute", TRUE, NULL);

			gst_object_unref(bin);
			gst_object_unref(volCtrl_elem);
		}
		else
		{
			logger_info("Pre-event recording already stopped");
		}
	}
	logger_info("%s: exit", __FUNCTION__);
}

char* getSerial()
{
	//    logger_info("%s: Entering ...", __FUNCTION__);
	char* id = malloc(20);
	char* str, *ptr;
	FILE* fp;
	X509* x509;

	char deviceid[11];
	ERR_load_crypto_strings();

	fp = fopen(ODI_CERT_FILE, "r");
	if (NULL == fp)
	{
		logger_error("Error opening file: %s", ODI_CERT_FILE);
		exit(0);
	}
	x509 = PEM_read_X509(fp, NULL, NULL, NULL);
	fclose(fp);
	if (x509 == NULL)
	{
		logger_error("error reading certificate");
		return 0;
	}
	str = X509_NAME_oneline(X509_get_subject_name(x509), 0, 0);
	if (str)
	{
		ptr = strstr(str, "/CN=");
		if (ptr)
		{
			if (strlen(ptr) == 14)
			{
				strcpy(deviceid, ptr + 4);
				sprintf(id, "%s", deviceid);
			}
			else
			{
				logger_error("error parsing unit certificate - invalid device id");
			}
		}
		else
		{
			printf("error parsing unit certificate - can't find CN\n");
		}
	}
	OPENSSL_free(str);
	X509_free(x509);
	return id;
}

void gst_cap_loadConfiguration()
{
	//    logger_info("%s: Entering ...", __FUNCTION__);
	strcpy(szDvrId, getSerial());

	FILE* fp = fopen(WEB_CONFIG_CONF, "r");
	if (fp)
	{
		char szBuff[100];
		char* szValue = NULL;
		while (!feof(fp))
		{
			fgets(szBuff, 100, fp);
			if (szBuff[0] == '#')
			{
				continue;    // skip comment
			}
			if (strlen(szBuff) == 0)
			{
				continue;    // skip emtpy line
			}
			if (strstr(szBuff, "videosrc") != NULL)
			{
				szValue = strstr(szBuff, "=");
				strcpy(szVideoSource, szValue + 1);
				szVideoSource[strlen(szVideoSource) - 1] = '\0';
			}
			else if (strstr(szBuff, "videosize") != NULL)
			{
				szValue = strstr(szBuff, "=");
				szVideoSize = atoi(szValue + 1);
			}
			else if (strstr(szBuff, "audiosrc") != NULL)
			{
				char* szValue = strstr(szBuff, "=");
				strcpy(szAudioSource, szValue + 1);
				szAudioSource[strlen(szAudioSource) - 1] = '\0';
			}
			else if (strstr(szBuff, "loglevel") != NULL)
			{
				char* szValue = strstr(szBuff, "=");
				log_level = strtol(szValue + 1, NULL, 10);
			}
			else if (strstr(szBuff, "configfile") != NULL)
			{
				char* szValue = strstr(szBuff, "=");
				strcpy(szConfigFile, szValue + 1);
				szConfigFile[strlen(szConfigFile) - 1] = '\0';
				if (parseXML() < 0)
				{
					logger_error("ParseXML failed.!!!");
					fclose(fp);
					exit(1);
				}
			}
			else if (strstr(szBuff, "filepath") != NULL)
			{
				char* szValue = strstr(szBuff, "=");
				strcpy(szDataPath, szValue + 1);
				szDataPath[strlen(szDataPath) - 1] = '\0';
			}
		}

		fclose(fp);
		sprintf(szOutputFile, "%s/%s", szDataPath, szDvrId);
	}
}

void generate_last_xml()
{
	// Set the XML Stop value
	char msg[1024];
	char last_xml_filename[250];

	memset(msg, 0, sizeof(msg));
	sprintf(msg, "szStartTime: %s", szStartTime);
	logger_debug(msg);

	char buf[100];
	memset(buf, 0, sizeof(buf));

	struct tm tmStopTime;
	tmStopTime.tm_year = atoi(strncpy(&buf[0], &szStartTime[0], 4)) - 1900;
	memset(buf, 0, sizeof(buf));
	tmStopTime.tm_mon = atoi(strncpy(&buf[0], &szStartTime[4], 2)) - 1;
	memset(buf, 0, sizeof(buf));
	tmStopTime.tm_mday = atoi(strncpy(&buf[0], &szStartTime[6], 2));
	memset(buf, 0, sizeof(buf));
	tmStopTime.tm_hour = atoi(strncpy(&buf[0], &szStartTime[8], 2));
	memset(buf, 0, sizeof(buf));
	tmStopTime.tm_min = atoi(strncpy(&buf[0], &szStartTime[10], 2));
	memset(buf, 0, sizeof(buf));
	tmStopTime.tm_sec = atoi(strncpy(&buf[0], &szStartTime[12], 2));

	memset(msg, 0, sizeof(msg));
	sprintf(msg, "tm_year: %d, tm_mon: %d, tm_mday: %d, tm_hour: %d, tm_min: %d, tm_sec: %d", tmStopTime.tm_year, tmStopTime.tm_mon, tmStopTime.tm_mday, tmStopTime.tm_hour, tmStopTime.tm_min, tmStopTime.tm_sec);
	logger_debug(msg);

	time_t tmResult = mktime(&tmStopTime);

	if (tmResult != (time_t)-1)
	{
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "%.3f", ((double)duration / 1000000000.0));
		int nDurationSec = (int)(atof(buf) + 0.5);

		memset(msg, 0, sizeof(msg));
		sprintf(msg, "Duration: %d sec", nDurationSec);
		logger_debug(msg);

		tmStopTime.tm_sec += nDurationSec;
		mktime(&tmStopTime);

		sprintf(&szStopTime[0], "%d%02d%02d%02d%02d%02d", tmStopTime.tm_year + 1900, tmStopTime.tm_mon + 1,
			tmStopTime.tm_mday, tmStopTime.tm_hour, tmStopTime.tm_min, tmStopTime.tm_sec);

		memset(msg, 0, sizeof(msg));
		sprintf(msg, "XML Stop: %s", szStopTime);
		logger_debug(msg);
	}
	else
	{
		logger_error("XML Stop calculation failed");
	}

	sprintf(last_xml_filename, "%s_%s", szOutputFile, file_start_time);

	/* Generate XML file */
	if (generateManifest(last_xml_filename, file_index + 1) < 0)
	{
		logger_error("manifest generation failed");
	}
}

void gst_detach_trigger(unsigned int nbin)
{
	GstBin* video_bin, *audio_bin;

	logger_info("%s: %s - %s", __FUNCTION__, video_trigger_bin[nbin], audio_trigger_bin[nbin]);
	video_bin = (GstBin*)gst_bin_get_by_name(GST_BIN(g_pipe), video_trigger_bin[nbin]);
	audio_bin = (GstBin*)gst_bin_get_by_name(GST_BIN(g_pipe), audio_trigger_bin[nbin]);

	gst_bin_remove(GST_BIN(g_pipe), GST_ELEMENT(video_bin));
	gst_bin_remove(GST_BIN(g_pipe), GST_ELEMENT(audio_bin));

	logger_info("Setting video_trigger_bin to NULL");
	gst_element_set_state(GST_ELEMENT(video_bin), GST_STATE_NULL);
	{
		GstState astate, apending;
		GstClockTime timeout = 5 * GST_SECOND;
		GstStateChangeReturn statechangeReturn;
		statechangeReturn = gst_element_get_state(GST_ELEMENT(video_bin), &astate, &apending, timeout);
		if (astate != GST_STATE_NULL)
		{
			logger_error("Could not set video_trigger_bin state to NULL");
		}
		else
		{
			logger_info("video_trigger_bin state set to NULL");
		}
	}

	logger_info("Setting audio_trigger_bin to NULL");
	gst_element_set_state(GST_ELEMENT(audio_bin), GST_STATE_NULL);
	{
		GstState astate, apending;
		GstClockTime timeout = 5 * GST_SECOND;
		gst_element_get_state(GST_ELEMENT(audio_bin), &astate, &apending, timeout);
		if (astate != GST_STATE_NULL)
		{
			logger_error("Could not set audio_trigger_bin state to NULL");
		}
		else
		{
			logger_info("audio_trigger_bin state set to NULL");
		}

	}

	g_object_unref(video_bin);
	g_object_unref(audio_bin);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(g_pipe), GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "gst_pipeline_trigger_detached");

	logger_info("%s: exit", __FUNCTION__);
}

void gst_attach_trigger(unsigned int nbin)
{
	GstBin *video_bin, *audio_bin;

	logger_info("%s: %s - %s", __FUNCTION__, video_trigger_bin[nbin], audio_trigger_bin[nbin]);

	if (!nbin)
	{
		video_bin = (GstBin*)gst_parse_bin_from_description(video_pretrigger_1, FALSE, NULL);
		audio_bin = (GstBin*)gst_parse_bin_from_description(audio_pretrigger_1, FALSE, NULL);
	}
	else
	{
		video_bin = (GstBin*)gst_parse_bin_from_description(video_pretrigger_2, FALSE, NULL);
		audio_bin = (GstBin*)gst_parse_bin_from_description(audio_pretrigger_2, FALSE, NULL);
	}

	gst_element_set_name(GST_ELEMENT(video_bin), video_trigger_bin[nbin]);
	gst_element_set_name(GST_ELEMENT(audio_bin), audio_trigger_bin[nbin]);

	gst_bin_add(GST_BIN(g_pipe), GST_ELEMENT(video_bin));
	gst_bin_add(GST_BIN(g_pipe), GST_ELEMENT(audio_bin));

	GstElement *audio_sink, *video_sink;
	char id[80];
	sprintf(id, "inter_video_sink_%d", nbin);
	video_sink = gst_bin_get_by_name(GST_BIN(video_bin), id);
	g_object_set(G_OBJECT(video_sink), "node-name", id, NULL);
	sprintf(id, "inter_audio_sink_%d", nbin);
	audio_sink = gst_bin_get_by_name(GST_BIN(audio_bin), id);
	g_object_set(G_OBJECT(audio_sink), "node-name", id, NULL);

	if (pre_event_time > 0)
	{
		GstElement* video_trigger, *audio_trigger;
		sprintf(id, "video_trigger_%d", nbin);
		video_trigger = gst_bin_get_by_name(GST_BIN(video_bin), id);
		g_object_set(G_OBJECT(video_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
		sprintf(id, "audio_trigger_%d", nbin);
		audio_trigger = gst_bin_get_by_name(GST_BIN(audio_bin), id);
		g_object_set(G_OBJECT(audio_trigger), "buf-time", (guint64)(pre_event_time * 1000), NULL);
		gst_object_unref(video_trigger);
		gst_object_unref(audio_trigger);
	}
	gst_element_set_state(GST_ELEMENT(video_bin), GST_STATE_PLAYING);
	gst_element_set_state(GST_ELEMENT(audio_bin), GST_STATE_PLAYING);

	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(g_pipe), GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "gst_pipeline_trigger_attached");
	logger_debug("exit: attaching trigger");

	gst_object_unref(video_sink);
	gst_object_unref(audio_sink);

	//GHECU Add on readiness for uC side
	logger_debug("Set ready record flag again");
	system("touch /odi/log/rdylog"); // 11.23.2014 davis
	logger_info("%s: exit", __FUNCTION__);
}

void gst_detach_sink()
{
	GstBin* bin;

	logger_info("%s: enter", __FUNCTION__);
	bin = (GstBin*)gst_bin_get_by_name(GST_BIN(g_pipe), "sink_bin");

	gst_bin_remove(GST_BIN(g_pipe), GST_ELEMENT(bin));

	logger_info("Setting sink_bin state set to NULL");
	gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);
	{
		GstState astate, apending;
		GstClockTime timeout = 5 * GST_SECOND;
		gst_element_get_state(GST_ELEMENT(bin), &astate, &apending, timeout);
		if (astate != GST_STATE_NULL)
		{
			logger_error("Could not set sink_bin state to NULL");
		}
		else
		{
			logger_info("sink_bin state set to NULL");
		}
	}

	g_object_unref(bin);

	logger_info("%s: exit", __FUNCTION__);
}

void gst_attach_sink()
{
	GstBin* bin;
	GstElement *matroska_muxer, *video_multifile, *inter_audio_src, *inter_video_src;

	logger_info("%s: enter", __FUNCTION__);

	bin = (GstBin*)gst_parse_bin_from_description(sink_bin, FALSE, NULL);
	gst_element_set_name(GST_ELEMENT(bin), "sink_bin");
	gst_bin_add(GST_BIN(g_pipe), GST_ELEMENT(bin));

	matroska_muxer = gst_bin_get_by_name(GST_BIN(bin), "mux");
	video_multifile = gst_bin_get_by_name(GST_BIN(bin), "video_multifile");
	inter_audio_src = gst_bin_get_by_name(GST_BIN(bin), "inter_audio_src_sink");
	inter_video_src = gst_bin_get_by_name(GST_BIN(bin), "inter_video_src_sink");
	if (snap_trace_val < 2)
	{
		g_object_set(G_OBJECT(matroska_muxer), "enable-trace-file", FALSE, NULL); //disable VTT file generation
	}
	g_object_set(G_OBJECT(matroska_muxer), "target-file-size", (gint64)target_file_size, NULL); //2gb
	g_object_set(G_OBJECT(matroska_muxer), "capture-mode", resolution, NULL);

	sprintf(mkv_filename, "%s_%%s.mkv", szOutputFile);
	g_object_set(G_OBJECT(video_multifile), "location", mkv_filename, NULL);

	/* Setting the certificate video file properties */
	if (checksum_mode == CHECKSUM_MKV_ONLY || checksum_mode == CHECKSUM_ALL)
	{
		g_object_set(G_OBJECT(video_multifile), "rsa-enable", TRUE, NULL);
	}
	else
	{
		g_object_set(G_OBJECT(video_multifile), "rsa-enable", FALSE, NULL);
	}

	/* Setting bin to PLAYING state */
	gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

	char id[80];
	sprintf(id, "inter_audio_sink_%d", active_trigger_bin);
	logger_debug("attaching audio sink to %s", id);
	g_object_set(G_OBJECT(inter_audio_src), "listen-to", id, NULL);
	sprintf(id, "inter_video_sink_%d", active_trigger_bin);
	logger_debug("attaching video sink to %s", id);
	g_object_set(G_OBJECT(inter_video_src), "listen-to", id, NULL);
	// Generate video subsystem ready for recording flag
	system("touch /tmp/record_ready");

	/* If enabled, print the pipeline graph */
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(g_pipe), GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "gst_pipeline_sink_attach");

	gst_object_unref(matroska_muxer);
	gst_object_unref(video_multifile);
	gst_object_unref(inter_audio_src);
	gst_object_unref(inter_video_src);

	logger_info("%s: exit", __FUNCTION__);
}

int main(int argc, char* argv[])
{
	log_level = GST_LOG_INFO;
	int log_level_arg = -1;
	int target_size_arg = -1;
	int res_arg = -1;
	int c;
	GstBus* bus;
	pid_t pid;
	int num_instances = 0;

	/* Check there is not another instance already running */
	num_instances = number_of_process_instances("gst_capture");
	if (num_instances < 0)
	{
		logger_error("Could not check for another gst_capture instance, can not guarantee uniqueness!");
	}
	else if (num_instances > 1)
	{
		logger_error("Another gst_capture instance is already running, exiting.");
		return -1;
	}
	else
	{
		logger_info("Unique gst_capture process, starting up.");
	}

	/* Reload ov5640 camera driver */
	logger_info("Removing ov5640 and capture driver");
	system("rmmod ov5640_camera_mipi");
	system("rmmod mxc_v4l2_capture");
	sleep(1);
	logger_info("Loading ov5640 and capture driver");
	system("modprobe ov5640_camera_mipi");
	system("modprobe mxc_v4l2_capture");
	sleep(1);

	/* By default MKV and XML header hashing is enabled */
	checksum_mode = CHECKSUM_ALL;
	//    logger_info("%s: Entering ...", __FUNCTION__);
	opterr = 0;
	while ((c = getopt(argc, argv, "d:f:hr:s:c:?")) != -1)
	{
		switch (c)
		{
			case 'd':
				log_level_arg = atoi(optarg);
				break;
			case 'f':
				strcpy(szOutputFile, optarg);
				break;
			case 'h':
			case '?':
				printf("usage: gst_capture [-d <log-level> 0=none, 1=info, 2=debug, 3=error]  [-r <resolution 2=1280x720, 4=720x480>] [-s <file-size-in-MB>] [-c <checksum_mode>]\n\n <checksum_mode>: \n\t[0] none\n\t[1] MKV File\n\t[2] XML header\n\t[3] MKV and XML\n");
				exit(0);
			case 'r':
				res_arg = atol(optarg);
				break;
			case 's':
				target_size_arg = atol(optarg);
				break;
			case 'c':
				checksum_mode = atoi(optarg);
				break;
			default:
				logger_error("Illegal argument!");
				abort();
		}
	}
	//clean up stop reason flag, may be the remnant from previous process
	remove("/odi/log/stopreason");

	gst_cap_loadConfiguration();

	if (log_level_arg != -1)
	{
		log_level = log_level_arg;
	}
	if (res_arg != -1)
	{
		resolution = res_arg;
	}
	if (target_size_arg != -1)
	{
		target_file_size = target_size_arg * 1000000;
	}
	else
	{
		target_file_size = szVideoSize * 1000000;
	}

	pid = getpid();
	logger_debug("gst_capture is up with pid: %d", pid);

	system("echo 3 > /proc/sys/vm/drop_caches");
	logger_info("RidgeRun version: "BODYVISIONVERSION);
	logger_info("Config Parameters: DVR Id: %s, Log level: %d, Video size(MB): %d, Resolution: %d",
		szDvrId, log_level, target_file_size / 1000000, resolution);

	__int64_t maxTime = 9000000000;
	struct sigaction sig_handler;
	sig_handler.sa_flags = 0;
	sigemptyset(&sig_handler.sa_mask);
	sig_handler.sa_handler = gst_cap_signal_handler;

	if (sigaction(SIGINT, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGINT signal handler failed");
		return -1;
	}

	if (sigaction(SIGUSR1, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGUSR1 signal handler failed");
		return -1;
	}

	/* Added to take a snapshot of the recording */
	if (sigaction(SIGUSR2, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGUSR2 signal handler failed.!!!");
		return -1;
	}

	/* audio muting of the pipeline */
	if (sigaction(SIGRTMIN, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGRTMIN signal handler failed.!!!");
		return -1;
	}

	/* audio unmuting of the pipeline */
	if (sigaction(SIGRTMIN + 1, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGRTMIN+1 signal handler failed.!!!");
		return -1;
	}

	/* pre-trigger signal */
	if (sigaction(SIGCONT, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGCONT signal handler failed.!!!");
		return -1;
	}

	if (sigaction(SIGQUIT, &sig_handler, 0) == -1)
	{
		logger_error("Registering SIGQUIT signal handler failed.!!!");
		return -1;
	}

	//    logger_info("%s: Done signal handler setup",__FUNCTION__);
	loop = g_main_loop_new(NULL, FALSE);
	gst_init(&argc, &argv);

	g_pipe = gst_setting_up_pipeline();

	bus = gst_pipeline_get_bus(GST_PIPELINE(g_pipe));
	watch_id = gst_bus_add_watch(bus, gst_cap_bus_call, loop);
	gst_object_unref(bus);

	logger_debug("Started gstreamer main loop");

	/* Setting pipeline to PLAYING state */
	gst_element_set_state(g_pipe, GST_STATE_PLAYING);

	/* Attach sink branch */
	gst_attach_sink();

	/* If enabled, print the pipeline graph */
	GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(g_pipe), GST_DEBUG_GRAPH_SHOW_NON_DEFAULT_PARAMS, "gst_pipeline");

	//    logger_info("%s: start gstreamer main loop",__FUNCTION__);
	// changes above to work around PM bug
	g_main_loop_run(loop);

	//    logger_info("%s: Stop gstreamer main loop",__FUNCTION__);
	gst_cap_stop_capture();
	logger_debug("cleaning up program & exit");

	GstEvent* eos_event = gst_event_new_eos();
	gst_element_send_event(g_pipe, eos_event);

	gst_element_set_state(g_pipe, GST_STATE_NULL);
	logger_debug("waiting for pipeline to change state");
	// wait till pipeline state is changed
	{
		GstState astate, apending;
		GstClockTime timeout = 5 * GST_SECOND;
		GstStateChangeReturn statechangeReturn;
		statechangeReturn = gst_element_get_state(g_pipe, &astate, &apending, timeout);
		logger_debug("statechange return = %d astate=%d apending=%d", statechangeReturn, astate, apending);
		if (statechangeReturn == 0)
		{
			logger_error("state change failed exit immediately");
			apending = (GstState)NULL;
		}
		if (astate != GST_STATE_NULL)
		{
			logger_error("Could not set pipeline to NULL state");
		}
		else
		{
			logger_info("Pipeline state set to NULL");
		}
	}
	logger_debug("pipeline state changed to NULL");

	// Free Pipeline
	gst_object_unref(GST_OBJECT(g_pipe));
	g_source_remove(watch_id);
	g_main_loop_unref(loop);

	system("sync && echo 3 > /proc/sys/vm/drop_caches");
	logger_debug("sync & drop_cache is done . Create rdy file for <monitor>");
	system("touch /odi/log/rdylog"); // 11.23.2014 davis
	logger_info("Recording program exit");
	logger_debug("gst_capture is down with pid: %d", pid);

	logger_info("Removing ov5640 and capture driver driver");
	system("rmmod ov5640_camera_mipi");
	system("rmmod mxc_v4l2_capture");
	remove("/tmp/record_ready");

	return 0;
}
