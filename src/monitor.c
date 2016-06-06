#include "odi-config.h"
#include "monitor.h"
#include "ezxml.h"
#include "BVcloud.h"
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#define LOG_INFO_USE 	1
#define LOG_DEBUG_USE 	1
//#define LOG_DETAILED	1

enum
{
	CAPTURE_BUTTON = 0,
	CAPTURE_FILE_MON,
	CAPTURE_REBOOT,
	CAPTURE_INT,
	CAPTURE_PRE_EVENT
} CAPTURE_ENUM;

int get_time = 0;
//int current_path = TO_DES;	// Set Pre-reference search path to DES
//int default_path = TO_DES;
int DES_CLOUD_connect = 0;
int net_up = -1;
int RCO_buzz_enable = 0;
int mute_ctrl = 0;
char filelist[200][32];
int pre_event = 0;
int pre_event_recording = 0;
int chargerIN = 0;	//Create extra flag because chargerON mis-initiating
char command_str[16];
static int chkXmlRdy = 0;
static pid_t capture_pid = -1;

extern char main_token;
extern char refresh_token;
#define DEFAUTL_AWS_SLEEP	(60)
#define PATH_SWITCH_TIMER	(120) // in seconds trying to connect to path DES/CLOUD
#define RCO_BUZZ_TIMER		(5*60) // in seconds recording buzz duration
#define CLOUD_LOG_TIMER		(60)	// in seconds log mqtt action

static void file_write_log(char* level, char* str_log)
{
	time_t now = time(NULL);
	struct tm ts;
	char time_buf[20];
	char fileName[500];

	strftime(time_buf, 20, "%Y%m%d", localtime_r(&now, &ts));

	bzero((char*)&pause_time_buffer[0], TIME_BUFFER_SIZE);

	sprintf(fileName, "%s/%s_%s.log", ODI_LOG, ODI_STATUS_FILE, time_buf);
	FILE* fp = fopen(fileName, "a");
	strftime(time_buf, 17, "(%Y%m%d%H%M%S)", localtime_r(&now, &ts));
	if (0 == strcmp(level, "RMT"))
	{
		sprintf((char*)&pause_time_buffer[0], "%s RMT: %s \n", time_buf, str_log);
	}
	else if (0 == strcmp(level, "CLD"))
	{
		sprintf((char*)&pause_time_buffer[0], "%s CLD: %s \n", time_buf, str_log);
	}
	else
	{
		sprintf((char*)&pause_time_buffer[0], "%s MON: %s \n", time_buf, str_log);
	}

	fwrite((char*)&pause_time_buffer[0], strlen((char*)&pause_time_buffer[0]), 1, fp);
	fclose(fp);
}

void logger_info(char* str_log, ...)
{
	memset(&debug_buffer[0], 0, LOGGER_BUFFER_SIZE);
	va_list vl;
	va_start(vl, str_log);
	vsprintf(&debug_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("INFO", &debug_buffer[0]);
}

void logger_debug(char* str_log, ...)
{
	if (check_debug_flag())
	{
		memset(&debug_buffer[0], 0, LOGGER_BUFFER_SIZE);
		va_list vl;
		va_start(vl, str_log);
		vsprintf(&debug_buffer[0], str_log, vl);
		va_end(vl);

		file_write_log("DEBUG", &debug_buffer[0]);
	}
}

void logger_remotem(char* str_log, ...)
{
	memset(&debug_buffer[0], 0, LOGGER_BUFFER_SIZE);
	va_list vl;
	va_start(vl, str_log);
	vsprintf(&debug_buffer[0], str_log, vl);
	va_end(vl);
	file_write_log("RMT", &debug_buffer[0]);
}

void logger_cloud(char* str_log, ...)
{
	memset(&debug_buffer[0], 0, LOGGER_BUFFER_SIZE);
	va_list vl;
	va_start(vl, str_log);
	vsprintf(&debug_buffer[0], str_log, vl);
	va_end(vl);
	file_write_log("CLD", &debug_buffer[0]);
}

void logger_error(char* str_log, ...)
{
	memset(&error_buffer[0], 0, LOGGER_BUFFER_SIZE);

	va_list vl;
	va_start(vl, str_log);
	vsprintf(&error_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("ERROR", &error_buffer[0]);
}

void logger_detailed(char* str_log, ...)
{
#ifdef LOG_DETAILED
	if (log_level <  MON_LOG_DEBUG) return;
	memset(&debug_buffer[0], 0, LOGGER_BUFFER_SIZE);

	va_list vl;
	va_start(vl, str_log);
	vsprintf(&debug_buffer[0], str_log, vl);
	va_end(vl);

	file_write_log("DETAILED", &debug_buffer[0]);
#endif
}

// Check debug flag
int check_debug_flag()
{
	if (access("/odi/log/debug.txt", 0) == 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

// Check cloud flag
int check_cloud_flag()
{
	if (access("/odi/conf/cloud.txt", 0) == 0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

unsigned long RCO_buzz_timer = 0;
int RCO_buzzing = 0;
int LOG_enable = 0;

void* timer_task()
{
	static unsigned long time_maker = 0, log_timer = 0;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_maker = tv.tv_sec;
	log_timer = tv.tv_sec;
	while (1)
	{
		// Set buzz timer when record start
		if (recording == 1 && RCO_buzz_timer == 0)
		{
			RCO_buzz_timer = tv.tv_sec;
		}
		else if (recording == 0)
		{
			RCO_buzz_timer = 0;
		}

		// Timer for record buzz
		if (RCO_buzz_enable > 0)
		{
			if ((tv.tv_sec - RCO_buzz_timer) > (RCO_buzz_enable * 60) &&
				recording == 1 && RCO_buzzing != 1 && RCO_buzz_timer != 0)
			{
				RCO_buzzing = 1;
				RCO_buzz_timer = 0;
			}
		}

		gettimeofday(&tv, NULL);
		// Timer for MQTT log
		if ((tv.tv_sec - log_timer) > CLOUD_LOG_TIMER)
		{
			LOG_enable = 1;
			log_timer = tv.tv_sec; // Set next timer block
		}
		sleep(3); // Thread delay
	}
}

char* trimStr(char* s)
{
	int i;
	char f[1000];
	int k = 0;
	for (i = 0; i<strlen(s); i++)
	{
		if (isspace(s[i]))
		{
			continue;
		}
		f[k++] = s[i];
	}
	f[k] = '\0';
	char* ff = f;
	return ff;
}

/************************************************************************************************************
* signal handler. sets wait_flag to FALSE, to indicate above loop to FALSE, to indicate above loop that     *
* characters have been received.                                                                            *
*************************************************************************************************************/
void signal_handler_IO(int status)
{
#ifdef DEBUG
	//printf("got SIGIO\n");
#endif
	return;
}

#ifdef __DEBUG__
void signal_handler_QUIT(int status)
{
	is_processing_done = 1;
}
#endif

int open_serial_port()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	int fd = open("/dev/ttymxc2", O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd == -1)
	{
		perror("Could not open UART");
		exit(0);
	}
	struct termios serial;
	bzero(&serial, sizeof(serial));

	if (tcgetattr(fd, &serial) < 0)
	{
		perror("Getting configuration");
		return -1;
	}
	// Set up Serial Configuration
	speed_t spd = B19200;
	cfsetospeed(&serial, (speed_t)spd);
	cfsetispeed(&serial, (speed_t)spd);

	cfmakeraw(&serial);

	serial.c_cc[VMIN] = 7;
	serial.c_cc[VTIME] = 0;

	serial.c_cflag &= ~CSTOPB;
	serial.c_cflag &= ~CRTSCTS; // no HW flow control
	serial.c_cflag |= CLOCAL | CREAD;
	if (tcsetattr(fd, TCSANOW, &serial) < 0)   // Apply configuration
	{
		perror("Setting configuration failed");
		exit(0);
	}
	return fd;
}

void add_signal_handler()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	//  set async read signal handler
	saio.sa_handler = signal_handler_IO;
	sigemptyset(&saio.sa_mask);
	saio.sa_flags = 0;
	saio.sa_restorer = NULL;
	sigaction(SIGIO, &saio, NULL);
	/* allow the process to receive SIGIO */
	fcntl(serial_port_fd, F_SETOWN, getpid());
	//Make the file descriptor asynchronous (the manual page says  only
	//O_APPEND and O_NONBLOCK, will work with F_SETFL...)
	fcntl(serial_port_fd, F_SETFL, FASYNC);
	logger_debug("Initialization of UART done");
}

void* serial_raw_read()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	char* read_ptr = serial_raw_read_buf;
	int curr_read_loc_ptr = 0;		// end of last read in buffer
	int msg_start_ptr = 0;			// start of message in buffer
	logger_debug("serial read thread started");
	while (thread_run)
	{
		// for now just a test to setup long 5 seconds timeout
		if (serial_raw_read_last = serial_raw_read_cur)
		{
			serial_raw_read_last = serial_raw_read_cur = 0;
		}
		if ((serial_port_fd > 0))
		{
			int cnt = read(serial_port_fd, (read_ptr + serial_raw_read_cur), (COMM_BUFFER_SIZE - serial_raw_read_cur));

			curr_read_loc_ptr += cnt;  // position of last character in buffer after read

			// if buffer chars are not enough for a single message, then read some more
			if (curr_read_loc_ptr - msg_start_ptr<COMM_MESSAGE_SIZE)
			{
				continue;
			}

			// if previous message has not been processed, go back and sleep until processed
			// External thread can only handle one message passed to it at a time
			if (serial_raw_read_cur != serial_raw_read_last)
			{
				continue;
			}

			// Make sure there is a message on the starting position
			// keep moving message starting position while message does not have correct prefix and suffix
			while (read_ptr[msg_start_ptr] != 0xff || read_ptr[msg_start_ptr + 1] != 0xff
				|| read_ptr[msg_start_ptr + 5] != 0x0d || read_ptr[msg_start_ptr + 6] != 0x0a)
			{
				msg_start_ptr++; // look at next char
				if (msg_start_ptr >= COMM_BUFFER_SIZE) // reset to prevent buffer over run
				{
					msg_start_ptr = 0;
					break;
				}

				if (curr_read_loc_ptr - msg_start_ptr<COMM_MESSAGE_SIZE)
				{
					continue;	// not enough chars for message, so go read again
				}
			}
			// by the time this loop is exited, then the message buffer looks like 0xff,0xff, XXX, CR, LF
			// so reset global pointers to message starting position
			cnt = COMM_MESSAGE_SIZE;                                   // cnt is message size
			serial_raw_read_last = serial_raw_read_cur = msg_start_ptr;  // set message start location (other thread won't process message yet)

			// Print message to log file
			int i;
			char tst[50], hex[5];
			sprintf(tst, "RAW cnt: %d, ", cnt);
			for (i = 0; i<cnt; i++)
			{
				int c = read_ptr[serial_raw_read_cur + i];
				if (c > 47 && c < 127)
				{
					sprintf(hex, "%c ", c);
				}
				else
				{
					sprintf(hex, "0x%02x ", c);
				}
				strcat(tst, hex);
			}
			logger_info(tst);

			char bat[4];
			sprintf(bat, "%c%c%c",
				read_ptr[serial_raw_read_cur + 2],
				read_ptr[serial_raw_read_cur + 3],
				read_ptr[serial_raw_read_cur + 4]);
			if (strcasecmp(bat, "B0C") == 0)
			{
				logger_info("Battery level < 25%%");
				battery_level = 5;
			}
			else if (strcasecmp(bat, "B1C") == 0)
			{
				logger_info("Battery level > 25%% and < 75%%");
				battery_level = 50;
			}
			else if (strcasecmp(bat, "B2C") == 0)
			{
				logger_info("Battery level > 75%% and < 100%%");
				battery_level = 90;
			}
			else if (strcasecmp(bat, "B0N") == 0)
			{
				logger_info("Battery level < 25%% and not charging");
				battery_level = 5;
			}
			else if (strcasecmp(bat, "B1N") == 0)
			{
				logger_info("Battery level > 25%% and < 75%% and not charging");
				battery_level = 50;
			}
			else if (strcasecmp(bat, "B2N") == 0)
			{
				logger_info("Battery level > 75%% and < 100%% and not charging");
				battery_level = 90;
			}
			else if (strcasecmp(bat, "B2F") == 0)
			{
				logger_info("Battery level fully charged");
				battery_level = 100;
			}
			else if (strcasecmp(bat, "B0F") == 0)
			{
				logger_debug("Battery disconnected");
				battery_level = 100;
			}
			strcpy((char *)&battery_level_str[0], bat);

			if (cnt > 0)
			{
				if ((serial_raw_read_cur + cnt) > (COMM_BUFFER_SIZE - COMM_MESSAGE_SIZE)) // > size of one command wrap
				{
					serial_raw_read_cur = 0;
				}
				else
				{
					serial_raw_read_cur += cnt;
				}
			}
		}
		else
		{
			logger_error("No serial FD %d errno %d", serial_port_fd, errno);
			return;
		}
		usleep(90000);
	} // while loop
}

int write_command_to_serial_port(char* comm)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	if (check_debug_flag())
	{
		char cmd[8];
		memcpy(cmd, comm, 5);
		logger_debug("Sending: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
			cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
	}
	int wcount = write(serial_port_fd, comm, strlen(comm));
	//logger_debug("Sent [%d] characters strlen %d [%s]", wcount, strlen(comm), trimStr(comm));
	if (wcount < 0)
	{
		logger_debug("Write to UART failed, trying again.");
		return -1;
	}
	else
	{
		if (wcount == strlen(comm))
		{
			return 0;
		}
		logger_error("Write to UART verification error");
		return -1;
	}
}

/**** Function: soft_blocking_read_command_from_serial_port ****/
/**** Purpose: Try really hard to get command/delimiter, without being too stubborn about blocking ****/
/****          (tries to read expected length within 100 while loop iterations)                    ****/
/**** Returns:  0 in case of success, -1 in case of error, -2 in case of timeout, -3 in case there ****/
/****           is nothing to read                                                                  ****/
int soft_blocking_read_data_from_serial_port()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	if (serial_raw_read_last != serial_raw_read_cur)
	{
		//logger_debug("Entering soft read...");
		if (((serial_raw_read_cur - serial_raw_read_last) >= COMM_MESSAGE_SIZE)
			|| (((serial_raw_read_cur - serial_raw_read_last) < 0)
				&& ((COMM_BUFFER_SIZE - serial_raw_read_last) >= COMM_MESSAGE_SIZE)))
		{

			if ((serial_raw_read_buf[serial_raw_read_last] != 0xFF)
				|| (serial_raw_read_buf[serial_raw_read_last + 1] != 0xFF))
			{
				logger_debug("preamble is not FF %X - %X continue", serial_read_buf[0], serial_read_buf[1]);
			}

			//logger_debug("Reading from serial buffer");
			memset(serial_read_buf, 0, COMM_MESSAGE_SIZE + 1);
			int i = 0, j = 0;
			for (i = 2; i < 7; i++)
			{
				serial_read_buf[j++] = serial_raw_read_buf[serial_raw_read_last + i];
			}

			if ((COMM_BUFFER_SIZE - serial_raw_read_last) <= COMM_MESSAGE_SIZE)
			{
				serial_raw_read_last = 0;
			}
			else
			{
				serial_raw_read_last += COMM_MESSAGE_SIZE;
			}

			logger_detailed("soft_read got %c%c%c cur=%d las=%d", serial_read_buf[0],
				serial_read_buf[1], serial_read_buf[2], serial_raw_read_cur, serial_raw_read_last);
			
			return 5;
		}
		else
		{
			logger_error("not enough bytes to form a command");
		}
	}
	// For some unforeseen condition
	return -1;
}

int get_operstate()
{
	int fd = -1, readbyte = 0, ret_val = -1;
	char flag_buf[16];
	fd = open("/sys/class/net/eth0/operstate", O_RDONLY);
	if (fd <= 0)
	{
		logger_error("Failed to open operstate flag file");
		return -1;
	}
	readbyte = read(fd, (char*)&flag_buf[0], 15);
	if (readbyte > 0)
	{
		if (strstr((char*)&flag_buf[0], "up"))
		{
			logger_detailed("Ethernet PHY found. Start networking attemp");
			close(fd);
			ret_val = 1;
		}
		else
		{
			logger_detailed("No PHY found");
			net_up = 0;
			ret_val = 0;
		}
	}
	close(fd);
	return ret_val;
}

int get_link_state()
{
	char buffer;
	size_t bytes_read = 0;
	int file_ptr = open("/odi/log/net_state", O_RDONLY);
	if (file_ptr == -1)
	{
		logger_error("%s: Open file: net_state failed", __FUNCTION__);
		return -1;
	}
	bytes_read = read(file_ptr, &buffer, 1);
	close(file_ptr);
	if (bytes_read != 1)
	{
		logger_error("Error reading bytes from net_state");
		return -1;
	}

	return (int)(0x01 & atoi(&buffer));
}

/* Get network state
* Look at all three factor
* - carrier flag file                  /sys/class/net/eth0
* - operstart flag file                /sys/class/net/eth0
* - Link is ?                          /var/log/message
*
* Return 1 if any of those flag is 1. Else return 0
*/
int get_network_phy_state()
{
	static int log_cnt = 0, reboot_cnt = 0;
	int eth, oper, link;
	static int network_restart_timer = 0;
	struct tm timeinfo, ts;
	time_t clock;
	time(&clock);
	timeinfo = *localtime_r(&clock, &ts);

	system("/usr/local/bin/net_test.sh");

	eth = get_eth0_state();
	if (eth < 0)
	{
		eth = 0;
	}
	oper = get_operstate();
	link = get_link_state();

	//    if(link == 1 && chargerIN == 1 && recording == 0 && net_up == 1)
	//    {
	//	if(reboot_cnt > 0)
	//	{
	//When dock after boot with ethernet detect system reboot
	//	    logger_info("System reboot when detect ethernet and in dock");
	//	    sleep(1);
	//	    system("sync; init 6");
	//	    sleep(1);
	//	}
	//	reboot_cnt++;
	//    }

	int total_states = eth + oper + link;
	if (total_states > 0 && total_states < 3 && recording == 0 && chargerON == 1)
	{
		//If unit Ethernet state report in this case. Unit should reboot after 5 minute
		if (network_restart_timer == 0)
		{
			logger_info("Ethernet flags mismatch. If not change system will reboot in 2 minute");
			network_restart_timer = timeinfo.tm_hour * 100 + timeinfo.tm_min;
		}
		if ((timeinfo.tm_hour * 100 + timeinfo.tm_min) - network_restart_timer > 2)
		{
			logger_info("System reboot because of Ethernet flags mismatch");
			//	    write_command_to_serial_port("RST\r\n");
			//	    sleep(1);
			system("reboot");
		}
	}
	else
	{
		network_restart_timer = 0;
	}

	if (log_cnt++ % 18 == 0) //log every 30s
	{
		logger_info("NIC status eth0: %d, operstate: %s, link: %s camera: %s",
			eth, oper == 1 ? "Up" : "Down", link == 1 ? "Up" : "Down", camera_docked == 1 ? "docked" : "undocked");
	}

	if (log_cnt > 100)
	{
		log_cnt = 0;
	}

	if (eth || oper || link)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int process_eth0_carrier_state_change(int sent_udp)
{
	int ip_abort;
	static int eth_jack = 0, send_ETO = -1;
	int net_phy_state = get_network_phy_state();

	// Init ETO send flag
	if (send_ETO == -1 && net_phy_state == 1)
	{
		send_ETO = 1;
	}

	// DOCKED Handle case for pre_event send ETO to ignore REC switch
	if (net_phy_state == 1 && pre_event > 0)
	{
		//logger_info("===================> Camera docked");
		camera_docked = 1;
		if (send_ETO == 1)
		{
			write_command_to_serial_port("ETO\r\n");
			send_ETO = 0;
		}
	}
	else if (net_phy_state == 0 && pre_event > 0 && chargerON == 0)
	{
		//logger_info("===================> Camera Undocked");
		camera_docked = 0;
		if (send_ETO == 0)
		{
			send_ETO = 1; // Reset to send again
		}
	}

	if (net_phy_state == 1 && net_up != 1)
	{
		if (chargerON)
		{
			if (-1 != (int)pid_find(ODI_CAPTURE))
			{
				logger_info("Stopping recording because eth0 came up: %d", kill(capture_pid, SIGINT));
				logger_info("EVENT RECORD STOP [ETH]");
				system("echo 2 > /odi/log/stopreason");
				chkXmlRdy = 1;
				recording = 0;
				capture_pid = -1;
				remove("/odi/log/recording");
			}
			write_command_to_serial_port("ETO\r\n");
			sleep(1);
			ack_eto = 1;
			ack_etf = 0;
			configure_network();
			sent_udp = 0;

			logger_info("Getting Ethernet address when eth0 state change ...");
			ip_address = (char *)getIP(&ip_abort);
			if (ip_abort == -1)
			{
				logger_info("Stop Get IP loop to improve device readiness");
				return 0;
			}

			if (ip_address != NULL)
			{
				logger_info("eth0 up, IP: %s, sending UDP packets...", ip_address);
				network_counter = 0;
				net_up = 1;
				camera_docked = 1;
			}
			else
			{
				sleep(1);
				if (network_counter % 2 == 0)
				{
					logger_info("Address not found, restarting network...");
					system("/etc/init.d/connman restart");
				}
				else
				{
					logger_info("Address not found, starting network...");
					system("/etc/init.d/connman start");
				}
				network_counter++;
			}
			eth_jack = 1;
		}
		else
		{
			// Charger is OFF, we will not start httpServer -- power drain
			logger_detailed("Charger OFF and Ethernet ON.");
			return 0;
		}
		//} else if(net_phy_state == 0 && camera_docked == 1) {
	}
	else if (net_phy_state == 0 && recording == 0 && eth_jack == 1)
	{
		logger_info("eth0 down, stopping UDP and enabling capture... camera: %d", camera_docked);
		camera_docked = 0;
		write_command_to_serial_port("ETF\r\n");
		sleep(1);
		//recording = 0;
		net_up = 0;
		eth_jack = 0;
		return 0;
	}
}

/**** Returns:  0 in case of success, -1 in case of error,
-2 in case there is no data to be read
****/
int consume_and_intrepret_UART_data()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	if (serial_raw_read_last != serial_raw_read_cur)
	{
		//logger_debug("consume_and_intrepret_UART_data");
		if ((serial_raw_read_cur - serial_raw_read_last) >= 7)
		{
			resync_read = 0;
			soft_blocking_read_data_from_serial_port();
		}
		else
		{
			// TBD salvage command
			if (resync_read > 1)
			{
				resync_read = 0;
				// figure out what can be salvage and resync;
				while (serial_raw_read_buf[serial_raw_read_last] != '\n')
				{
					serial_raw_read_last++;
					logger_debug("Sync Buffer cur %d last %d", serial_raw_read_cur, serial_raw_read_last);
					if (serial_raw_read_last == serial_raw_read_cur)
					{
						break;
					}
				}
				logger_error("[INFO] UART link data not aligned- resync ");
			}
			else
			{
				resync_read++;
				logger_debug("Not enough data from uC--wait one cycle");
				return -2;
			}
		}

		if (strlen(serial_read_buf) >= 5)
		{
			char command[4];
			memset(command, 0, 4);
			int i;
			for (i = 0; i < 3; i++)
			{
				command[i] = serial_read_buf[i];
			}
			command[3] = '\0';
			ttymx_action(command);
		}
		else
		{
			char malform[7];
			memcpy((char*)&malform[0], serial_read_buf, 7);
			logger_error("Malformed command from UART: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x",
				malform[0], malform[1], malform[2], malform[3], malform[4], malform[5], malform[6]);
		}
		return 1;
	}
	// For some unforeseen condition
	return 0;
}

pid_t pid_find(char* process_name)
{
	logger_detailed("Entering: %s", __FUNCTION__);
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
		int pid;
		if (command_string[newbuflen - 1] == '\n')
		{
			command_string[newbuflen - 1] = '\0';
		}
		pid = atoi(command_string);
		pclose(proc_fp);
		free(command_string);
		free(name);
		//logger_debug("pid_find: name=%s, pid=%d",process_name, pid);
		return (pid_t)pid;
	}
	return -1;
}

int get_eth0_state()
{
	char buffer;

	size_t bytes_read = 0;
	int file_ptr = open(ETH0_CARRIER_FILE, O_RDONLY);
	if (file_ptr == -1)
	{
		logger_error("%s: Open file: %s failed", __FUNCTION__, ETH0_CARRIER_FILE);
		return -1;
	}

	bytes_read = read(file_ptr, &buffer, 1);
	close(file_ptr);
	if (bytes_read != 1)
	{
		logger_error("Error reading bytes from eth0 file");
		return -1;
	}
	return (int)(0x01 & atoi(&buffer));
}

char* get_battery_level_string()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	write_command_to_serial_port("BAT\r\n");
	battery_level = -1;

	// Clear out level send string
	bzero((char*)&battery_level_str[0], 16);
	int i;
	for (i = 0; i<10; i++)
	{
		//logger_debug("Waiting for battery level...");
		usleep(500000);
		if (battery_level != -1)
		{
			break;
		}
	}
	return (char*)&battery_level_str[0];
}

int get_battery_level()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	battery_level = -1;
	write_command_to_serial_port("BAT\r\n");
	//battery_level = -1;

	//Clear out level send string
	int i;
	for (i = 0; i<10; i++)
	{
		//logger_debug("Waiting for battery level...");
		usleep(500000);
		if (battery_level != -1)
		{
			break;
		}
	}
	return battery_level;
}

char* get_command(char* cmd)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	bzero((char*)&command_str[0], 16);
	write_command_to_serial_port(cmd);
	sleep(1);
	return (char*)&command_str[0];
}

char* get_hw_version()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	return (char*)&hw_version[0];
}

char* get_assignable()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	return (char*)&assignable[0];
}

char* get_dvr_name()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	return (char*)&dvr_name[0];
}

void set_dvr_name(char* s)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	strcpy(dvr_name, s);
}

void set_assignable(char* s)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	strcpy(assignable, s);
}

int available_minutes()
{
	struct statvfs stat_buf;
	unsigned long long_free_blk;
	logger_detailed("Entering: %s", __FUNCTION__);

	statvfs(ODI_DATA, &stat_buf);
	long_free_blk = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
	int minutes = (int)((float)long_free_blk / 35.0);
	//logger_debug("Disk space: %u, Minutes left: %d", long_free_blk, minutes);
}

int available_space()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	struct statvfs stat_buf;
	long long free_blk = 0;

	if (statvfs(ODI_DATA, &stat_buf) == 0)
	{
		free_blk = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
		logger_detailed("Available space: %d", (int)(free_blk));
	}
	else
	{
		logger_error("check available space statvfs failed!");
	}
	return (int)(free_blk);
}

int check_current_file()
{
	int i;
	for (i = 0; i<8; i++)
	{
		sleep(1);
		logger_info("Current file check..%d", i);
		FILE* current_fp = fopen(CURRENT_FILE, "r");
		if (current_fp != NULL)
		{
			fgets(current_file, 100, current_fp);
			fclose(current_fp);
			remove(CURRENT_FILE);
			logger_debug("Current file found: %s", current_file);
			capture_pid = pid_find(ODI_CAPTURE);
			return 1;
		}
	}
	return 0;
}

void start_capture(int cause)
{
	logger_info("%s: Start video capture reason [%d]", __FUNCTION__, cause);
	strcpy(current_file, "");
	time_t now = time(NULL);
	struct tm ts;
	char ymd[20];
	strftime(ymd, 20, "%Y%m%d", localtime_r(&now, &ts));

	char cmd[256];
	sprintf(cmd, "GST_DEBUG_NO_COLOR=1 GST_DEBUG_FILE=/odi/log/gst_capture_%s.log GST_DEBUG=2 %s/%s >> %s/%s_%s 2>&1 &",
		ymd, ODI_BIN, ODI_CAPTURE, ODI_LOG, ODI_CAPTURE, ymd);

	system(cmd);
	logger_info("Capture command: %s", cmd);

	capture_pid = -1;
	current_found = check_current_file();
	if (!current_found)
	{
		logger_error("%s:%d Current file not found, restarting gst_capture...", __FUNCTION__, __LINE__);
		logger_info("%s:%d Sending SIGINT to gst_capture", __FUNCTION__, __LINE__);
		system("killall -SIGINT gst_capture > /dev/null 2>&1");
		system(cmd);
		current_found = check_current_file();
		if (-1 == (int)pid_find(ODI_CAPTURE) || !current_found)
		{
			logger_error("Restart failed, rebooting...");
			//write_command_to_serial_port("VID\r\n");
			//sleep(1);
			//write_command_to_serial_port("RCB\r\n");
			logger_error("System Rebooting");
			system("cp /var/log/messages /odi/log");
			//write_command_to_serial_port("RST\r\n");
			//sleep(1);
			system("reboot");
		}
	}
	logger_info("%s: exit", __FUNCTION__);
}

// returns 0 in case it encountered a valid command, returns -1 in case the command is invalid.
// Note: returning 0 does not mean the command was executed correctly.
int ttymx_action(char* command)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	logger_detailed("TTYMX_ACTION command=%s", command);
	static int mute_cmd_delay;
	mute_cmd_delay = 0; // Reset check use
	strcpy(command_str, command);

	if (strcasecmp(command, "MUO") == 0)	// MUO: Mute On
	{
		if (mute_on == 1)
		{
			logger_error("%s:%d MUF: mute_on is 1 indicating mute is already on, returning (SHOULD NOT HAPPEN)", __FUNCTION__, __LINE__);
			return 0;
		}

		while (1)
		{
			if (pid_find(ODI_CAPTURE) != -1)
			{
				logger_error("%s:%d MUF: gst_capture not running. Cannot send commands to process", __FUNCTION__, __LINE__);
				break;
			}

			if (mute_cmd_delay++ > 40)
			{
				// 40 * 50000 uSec = 2 seconds
				logger_error("%s:%d MUF: Target process not running after 2 seconds. Cannot send mute command to gst_capture", __FUNCTION__, __LINE__);
				return -1;
			}
			usleep(50000);	// 0.05 sec delay
		}

		logger_info("%s:%d EVENT: Mute on, sending SIGRTMIN to gst_capture", __FUNCTION__, __LINE__);
		system("killall -34 gst_capture > /dev/null 2>&1"); // MarcoM: SIGRTMIN = 34);

		mute_on = 1;
		if (write_command_to_serial_port("MUO\r\n"))
		{
			logger_error("%s:%d Error writing ACK MUO to uC", __FUNCTION__, __LINE__);
			return -1;
		}
		return 0;
	}
	else if (strcasecmp(command, "MUF") == 0)	// MUF: Mute Off
	{
		if (mute_on == 0)
		{
			logger_error("%s:%d MUF: mute_on is 0 indicating mute is already off, returning (SHOULD NOT HAPPEN)", __FUNCTION__, __LINE__);
			return 0;
		}

		while (1)
		{
			if (pid_find(ODI_CAPTURE) != -1)
			{
				logger_error("%s:%d MUF: gst_capture not running. Cannot send commands to process", __FUNCTION__, __LINE__);
				break;
			}

			if (mute_cmd_delay++ > 40)
			{
				// 40 * 50000 uSec = 2 seconds
				logger_error("%s:%d MUF: Target process not running after 2 seconds. Cannot send mute command to gst_capture", __FUNCTION__, __LINE__);
				return -1;
			}
			usleep(50000);	// 0.05 sec delay
		}

		logger_info("%s:%d EVENT: Mute off, sending SIGRTMIN+1 to gst_capture", __FUNCTION__, __LINE__);
		system("killall -35 gst_capture > /dev/null 2>&1"); // MarcoM: SIGRTMIN + 1 = 35

		mute_on = 0;
		if (write_command_to_serial_port("MUF\r\n"))
		{
			logger_error("%s:%d Error writing ACK MUF to uC", __FUNCTION__, __LINE__);
			return -1;
		}
		return 0;
	}
	else if (strcasecmp(command, "RCO") == 0)	// RCO: Start Recording
	{
		logger_info("EVENT: RCO requested. Pre_event: %d", pre_event);
		stealth_time_state_save = 0;

		// If Pre_event is on send SIGCONT, else do normal record
		if (pre_event == 0)
		{
			if (!recording)
			{
				logger_debug("%s:%d Sending SIGINT to gst_capture processes", __FUNCTION__, __LINE__);
				system("killall -SIGINT gst_capture > /dev/null 2>&1");
				if (-1 == (int)pid_find(ODI_CAPTURE))
				{
					logger_debug("Killall successful");
				}
				else
				{
					logger_error("Killall gst_capture failed!");
				}
			}
			else
			{
				write_command_to_serial_port("RCO\r\n");
				return; // Ignore extra RCO commands
			}
		}
		recording = 1;
		pre_event_recording = 0;

		if (mute_on)
		{
			write_command_to_serial_port("MUF\r\n");
			sleep(1);
		}
		mute_on = 0;
		write_command_to_serial_port("RCO\r\n");
		logger_debug("Checking battery level and available space...");
		get_battery_level();

		if ((!chargerON && battery_level < MINIMUM_BATTERY) || available_space() < MINIMUM_SPACE)
		{
			if (battery_level < MINIMUM_BATTERY)
			{
				logger_error("Stopping recording due to low battery: %d.", battery_level);
			}
			else
			{
				logger_error("Stopping recording due to low disk space.");
			}
			//if(!stealth_on)
			write_command_to_serial_port("VID\r\n");
			recording = 0;
			return -1;
		}

		logger_info("EVENT: RCO starting recording");
		remove(CURRENT_FILE);
		current_found = 0;
		parseXML();

		system("touch /odi/log/recording");
		if (pre_event > 0)
		{
			//Allow trigger on Charger with pre_event terminated
			// Launch the pre_event first that do normal record
			if (-1 == (int)pid_find(ODI_CAPTURE))
			{
				logger_info("Camera is undocked: launch gst_capture for pre-event time %d s", pre_event);
				int i;
				time_t now = time(NULL);
				struct tm ts;
				char ymd[20];
				strftime(ymd, 20, "%Y%m%d", localtime_r(&now, &ts));
				char cmd[256];
				sprintf(cmd, "GST_DEBUG_NO_COLOR=1 GST_DEBUG_FILE=/odi/log/gst_capture_%s.log GST_DEBUG=2 /usr/local/bin/gst_capture &", ymd);
				system(cmd);
				sleep(2);
				//pre_event_recording = 1;
				//pre_event_started = 1;
				while (-1 == (int)pid_find(ODI_CAPTURE))
				{
					logger_info("Waiting for Pre_event ready: %d", i);
					sleep(1);
				}
			}
			logger_info("%s:%d Start main record in pre_event case, sending SIGCONT to gst_capture", __FUNCTION__, __LINE__);
			system("killall -SIGCONT gst_capture > /dev/null 2>&1");
		}
		else
		{
			start_capture(CAPTURE_BUTTON);
		}

		if (-1 == (int)pid_find(ODI_CAPTURE) && pre_event == 0) //11958 error log for HW < 4.0
		{
			logger_error("RCO start recording failed.");
			//write_command_to_serial_port("VID\r\n");
		}
		else
		{
			//write_command_to_serial_port("RCO\r\n");
			logger_debug("RCO sent, pid=%d", capture_pid);
		}

		chkXmlRdy = 0;                       // davis 11.22.2014
		remove("/odi/log/rdylog");           // davis 11.22.2014
		RecSizeChkState = 1;                 // davis 01.06.2015
		return 0;
	}
	else if (strcasecmp(command, "RCF") == 0)	// RCF: Stop Recording
	{
		logger_info("EVENT: RCF requested: %d", (int)pid_find(ODI_CAPTURE));
		if (recording_counter < 100 && RecSizeChkState != 0)
		{
			logger_info("Stop too quick. recording counter: %d", recording_counter);
			return;
		}
		if (recording == 1)
		{
			// If Pre_event is on send SIGQUIT, else do normal record
			if (pre_event > 0)
			{
				if (chargerON == 0)
				{
					logger_info("%s:%d Sending SIGQUIT to gst_capture", __FUNCTION__, __LINE__);
					system("killall -SIGQUIT gst_capture > /dev/null 2>&1");
				}
				else if (chargerON == 1)
				{
					logger_info("%s:%d Stop by SIGINT because pre_event and docked", __FUNCTION__, __LINE__);
					system("killall -s SIGINT gst_capture > /dev/null 2>&1");
				}
			}
			else
			{
				logger_info("%s:%d Sending SIGINT to gst_capture", __FUNCTION__, __LINE__);
				system("killall -s SIGINT gst_capture > /dev/null 2>&1");
			}
			write_command_to_serial_port("RCF\r\n");
			logger_info("EVENT RECORD STOP [BUTTON]. RCF: Stopping recording");
			system("echo 1 > /odi/log/stopreason");
			chkXmlRdy = 1;		// davis 11.22.2014
		}
		else
		{
			write_command_to_serial_port("RCF\r\n");
			logger_error("Wrong state RCF STOP Recording -- recording is OFF");
		}
		// Reset capture pid only when not pre_event
		if (pre_event == 0)
			capture_pid = -1;
		strcpy(current_file, "");
		stealth_time_state_save = 0;
		recording = 0;
		current_found = 0;
		remove("/odi/log/recording");
		return 0;
	}
	else if (strcasecmp(command, "CHO") == 0)	// CHO: Charger connected
	{
		logger_info("EVENT: Charger connected CHO");
		chargerON = 1;
		chargerIN = 1;
		stealth_disable = 0;
		stealth_time_state_save = 0;
		if (write_command_to_serial_port("CHO\r\n"))
		{
			logger_error("Error writing ACK CHO to uC");
			return -1;
		}
		if (stealth_on)
		{
			logger_info("EVENT: Sending stealth off");
			write_command_to_serial_port("STF\r\n");
			stealth_on = 0;
		}
		//DOCKED Handle case for pre_event send ETO to ignore REC switch
		if (pre_event > 0)
		{
			camera_docked = 1;
			//If GHECU
			//write_command_to_serial_port("ETO\r\n");
		}

		// Remove recording stop when charge ON
		//        if (capture_pid != -1) {
		//            logger_info("EVENT RECORD STOP [CHO]");
		//            system("echo 5 > /odi/log/stopreason");
		//            kill(capture_pid, SIGINT);
		//            write_command_to_serial_port("RCF\r\n");
		//            chkXmlRdy = 1;
		//            recording = 0;
		//            capture_pid = -1;
		//        }
		return 0;
	}
	else if (strcasecmp(command, "CHF") == 0)	// CHF:  Charger disconnected
	{
		logger_info("EVENT: Charger disconnected CHF");
		logger_debug("Charger disconnected CHF");
		if (write_command_to_serial_port("CHF\r\n"))
		{
			logger_error("Error writing ACK CHF to uC");
			return -1;
		}
		chargerON = 0;
		return 0;

	}
	else if (strcasecmp(command, "B0C") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B1C") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B2C") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B0F") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B0N") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B1N") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B2N") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "B2F") == 0)
	{
		return 0;
	}
	else if (strcasecmp(command, "BSD") == 0)     // Low battery shutdown
	{
		logger_debug("BSD received. Going to sleep now");
		stealth_disable = 1;
		return 0;
	}
	else if (strcasecmp(command, "BOP") == 0)
	{
		ack_bop = 0;
		logger_debug("Got BOP ACK");
		return 0;
	}
	else if (strcasecmp(command, "BOF") == 0)
	{
		logger_debug("Got BOF ACK");
		return 0;
	}
	else if (strcasecmp(command, "BOR") == 0)
	{
		logger_debug("Got BOR ACK");
		return 0;
	}
	else if (strcasecmp(command, "STO") == 0)
	{
		ack_sto = 0;
		system("touch /odi/log/stealth");
		logger_debug("Got STO ACK Stealth On");
		stealth_on = 1;
		return 0;
	}
	else if (strcasecmp(command, "STF") == 0)
	{
		ack_stf = 0;
		remove("/odi/log/stealth");
		logger_debug("Got STF ACK Stealth Off");
		stealth_on = 0;
		return 0;
	}
	else if (strcasecmp(command, "SSO") == 0)
	{
		ack_sso = 0;
		logger_debug("Got SSO ACK");
		return 0;
	}
	else if (strcasecmp(command, "TIO") == 0)
	{
		ack_tio = 0;
		logger_debug("Got TIO ACK");
		return 0;
	}
	else if (strcasecmp(command, "VID") == 0)
	{
		ack_vid = 0;
		logger_debug("Got VID ACK");
		return 0;
	}
	else if (strcasecmp(command, "ETO") == 0)
	{
		ack_eto = 0;
		logger_debug("Got ETO ACK");
		return 0;
	}
	else if (strcasecmp(command, "ETF") == 0)
	{
		ack_etf = 1;
		ack_eto = 0;
		logger_debug("Got ETF ACK");
		return 0;
	}
	else if (strcasecmp(command, "UPS") == 0)
	{
		ack_ups = 0;
		logger_debug("Got UPS ACK");
		return 0;
	}
	else if (strcasecmp(command, "UPD") == 0)
	{
		ack_upd = 0;
		logger_debug("Got UPD ACK");
		return 0;
	}
	else if (strcasecmp(command, "RFS") == 0)
	{
		ack_rfs = 0;
		logger_debug("Got RFS ACK");
		return 0;
	}
	else if (strcasecmp(command, "RFD") == 0)
	{
		ack_rfd = 0;
		logger_debug("Got RFD ACK");
		return 0;
	}
	else if (strcasecmp(command, "WAK") == 0)
	{
		logger_debug("Got WAK ACK");
		return 0;
	}
	else if (strcasecmp(command, "POR") == 0)
	{
		logger_debug("Got POR ACK");
		return 0;
	}
	else if (strcasecmp(command, "LSP") == 0)     // Low disk space
	{
		logger_info("EVENT: Got LSP");
		logger_debug("Got LSP ACK");
		low_disk_space = 1;
		return 0;
	}
	else if ((int)*command > 48 && (int)*command < 58)
	{
		strcpy(api_version, command);
		logger_debug("Got API ACK: %s", api_version);
		ack_api = 1;
		return 0;
	}
	else if (strcasecmp(command, "OFF") == 0)
	{
		//10418
		write_command_to_serial_port("OFF\r\n");
		logger_debug("Got OFF ACK");
		stealth_time_state_save = 0; // davis 2016.03.07b
		return 0;
	}
	else if (strcasecmp(command, "ETH") == 0)
	{
		logger_debug("Got ETF ACK. Connecting to a network");
		return 0;
	}
	else if (strcasecmp(command, "MDO") == 0)
	{
		logger_debug("Got MDO ACK. Mute on");
		return 0;
	}
	else if (strcasecmp(command, "MDF") == 0)
	{
		logger_debug("Got MDF ACK. Mute off");
		return 0;
	}
	else if (strcasecmp(command, "VIB") == 0)
	{
		logger_debug("Got VIB ACK. Vibrate");
		return 0;
	}
	else if (strcasecmp(command, "INI") == 0)
	{
		logger_debug("Got INI ACK. Idle");
		return 0;
	}
	else if (strcasecmp(command, "INR") == 0)
	{
		logger_debug("Got INR ACK. Recording");
		return 0;
	}
	else if (strcasecmp(command, "IND") == 0)
	{
		logger_debug("Got IND ACK. Downloading");
		return 0;
	}
	else if (strcasecmp(command, "INE") == 0)
	{
		logger_debug("Got INE ACK. Error");
		return 0;
	}
	else if (strcasecmp(command, "RST") == 0)
	{
		logger_debug("Got RST ACK. Reset");
		return 0;
	}

	logger_error("Invalid command: %s", command);
	return -1;
}

int stealth_time_button()
{
	if (stealth_disable)
	{
		return 0;
	}
	char buffer;

	size_t bytes_read = 0;
	int file_ptr = open(GPIO90_STATE_FILE, O_RDONLY);
	bytes_read = read(file_ptr, &buffer, 1);
	close(file_ptr);
	if (bytes_read != 1)
	{
		return -1;
	}

	return !atoi(&buffer);
}

int stealth_time_action(int gpio_state)
{
	int cstate = gpio_state;

	if (stealth_counter < 30 && !cstate && nstate90 && !recording && !stealth_on ||
		(!stealth_on && !cstate && stealth_time_state_save == 2))
	{
		char message[14];
		struct statvfs stat_buf;
		unsigned long long_free_blk;
		unsigned long long_data_size;
		memset(message, 0, 14);

		statvfs(ODI_DATA, &stat_buf);
		long_free_blk = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
		long_data_size = stat_buf.f_blocks * stat_buf.f_bsize / 1024000;
		// for free size get time 35MB per minute
		logger_debug("action_gpio90: size=%u free=%u", long_data_size, long_free_blk);
		float fminutes = (float)long_free_blk / (float)35;
		int hour = fminutes / 60;
		int minutes = (int)fminutes - (hour * 60);
		int seconds = (fminutes - (minutes + hour * 60)) * 60.;
		sprintf(message, "TM%.2d:%.2d:%.2d\r\n", hour, minutes, seconds);
		logger_info("Available time requested.");
		stealth_time_state_save = 0;

		logger_debug("Sending TM: %s", message);
		if (write_command_to_serial_port(message))
		{
			logger_error("Error writing command back to UART");
			return -1;
		}
		usleep(300000); // davis 2016.03.07
	}
	else if (stealth_counter < 30 && !cstate && nstate90 && pre_event_recording == 1 && !stealth_on ||
		(!stealth_on && !cstate && stealth_time_state_save == 2))
	{
		char message[14];
		struct statvfs stat_buf;
		unsigned long long_free_blk;
		unsigned long long_data_size;
		memset(message, 0, 14);

		statvfs(ODI_DATA, &stat_buf);
		long_free_blk = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
		long_data_size = stat_buf.f_blocks * stat_buf.f_bsize / 1024000;
		// for free size get time 35MB per minute
		logger_debug("action_gpio90: size=%u free=%u", long_data_size, long_free_blk);
		float fminutes = (float)long_free_blk / (float)35;
		int hour = fminutes / 60;
		int minutes = (int)fminutes - (hour * 60);
		int seconds = (fminutes - (minutes + hour * 60)) * 60.;
		sprintf(message, "TM%.2d:%.2d:%.2d\r\n", hour, minutes, seconds);
		logger_info("Available time requested.");
		stealth_time_state_save = 0;

		logger_debug("Sending TM: %s", message);
		if (write_command_to_serial_port(message))
		{
			logger_error("Error writing command back to UART");
			return -1;
		}
		usleep(300000); // davis 2016.03.07
	}

	if (stealth_time_state_save == 1)
	{
		stealth_time_state_save = 2;
	}
	nstate90 = cstate;

	if (camera_docked)
	{
		return;
	}

	if (stealth_counter > 60 && gpio_state)
	{
		stealth_counter = 0;
		if (!tstate90)
		{
			int temp;
			if (!stealth_on)
			{
				logger_info("Entering stealth mode.");
				temp = write_command_to_serial_port("STO\r\n");
				if (!temp)
				{
					ack_sto = 1;
				}
			}
			else
			{
				logger_info("Exiting stealth mode.");
				temp = write_command_to_serial_port("STF\r\n");
				if (!temp)
				{
					ack_stf = 1;
				}
				stealth_time_state_save = 0; // davis 2016.03.04
				stealth_counter = 31; // davis 2016.03.07
			}
			tstate90 = 1;
			if (temp)
			{
				logger_error("Error writing STO/STF command to UART");
				return -1;
			}
		}
	}
	else
	{
		if (gpio_state)
		{
			stealth_counter++;
		}
		else
		{
			tstate90 = 0;
			stealth_counter = 0;
		}
	}

	return 0;
}

int snap_trace_button()
{
	if (stealth_disable)
	{
		return 0;
	}
	char buffer;

	size_t bytes_read = 0;
	int file_ptr = open(GPIO101_STATE_FILE, O_RDONLY);
	bytes_read = read(file_ptr, &buffer, 1);
	close(file_ptr);
	if (bytes_read != 1)
	{
		return -1;
	}

	return !atoi(&buffer);
}

int snap_trace_action(int gpio_state)
{
	logger_detailed("Entering: %s", __FUNCTION__);
	int cstate = gpio_state;
	if (cstate && !nstate101 && (snap_trace > 0 && snap_trace < 4))
	{
		if (snap_trace == 1 || snap_trace == 3)
		{
			logger_info("Snap shot requested.");
		}
		if (snap_trace > 1)
		{
			logger_info("Trace point requested.");
		}
		logger_info("%s:%d Sending SIGUSR2 to gst_capture", __FUNCTION__, __LINE__);
		system("killall -SIGUSR2 gst_capture > /dev/null 2>&1");

		write_command_to_serial_port("SSO\r\n");
		ack_sso = 1;
	}
	nstate101 = cstate;

	return 0;
}

int check_for_ACK()
{
	logger_detailed("Entering: %s", __FUNCTION__);
	// check for ACK and retry
	if (ack_bop > 0)
	{
		if (ack_bop > 2)
		{
			logger_debug("No BOP Ack after 3 tries");
			ack_bop = 0;
		}
		else
		{
			write_command_to_serial_port("BOP\r\n");
			ack_bop++;
		}
	}
	if (ack_sto > 0)
	{
		if (ack_sto > 5)
		{
			logger_debug("No STO Ack after 6 tries"); // davis 2016.03.07a
			ack_sto = 0;
		}
		else
		{
			ack_sto++;
		}
	}
	if (ack_stf > 0)
	{
		if (ack_stf > 5)
		{
			logger_debug("No STF Ack after 6 tries"); // // davis 2016.03.07a
			ack_stf = 0;
		}
		else
		{
			ack_stf++;
		}
	}
	if (ack_sso > 0)
	{
		if (ack_sso > 2)
		{
			logger_debug("No SSO Ack after 3 tries");
			ack_sso = 0;
		}
		else
		{
			ack_sso++;
		}
	}
	if (ack_tio > 0)
	{
		if (ack_tio > 2)
		{
			logger_debug("No TIO Ack after 3 tries");
			ack_tio = 0;
		}
		else
		{
			ack_tio++;
		}
	}

	return 0;
}

int getField(ezxml_t xmlParent, char* fieldname, char* dest)
{
	ezxml_t xmlChild = ezxml_get(xmlParent, "config", 0, fieldname, -1);
	if (xmlChild != NULL)
	{
		char* szValue = xmlChild->txt;
		strcpy(dest, szValue);
		return 0;
	}
	return -1;
}

int parseXML()
{
	int ok = 0;
	char str[200];
	logger_detailed("Entering: %s", __FUNCTION__);

	// parsing xml file
	ezxml_t xmlParent = ezxml_parse_file(ODI_CONFIG_XML);
	if (xmlParent == NULL)
	{
		logger_error("Parse config file failed: %s", ODI_CONFIG_XML);
		system("rm -rf /odi/conf/config.xml");
		reset_config();
		return -1;
	}

	if (getField(xmlParent, (char*)"remotem_broadcast_ip", broadcast_ip))
	{
		//TODO put proper code to screen IPs field
		//        if (strlen(str) <= 15 && strlen(str) > 8) {
		//            strcpy(broadcast_ip, str);
		//        } else {
		logger_error("Invalid remotem_broadcast_ip in config xml");
		ok = -1;
		//        }
	}

	if (getField(xmlParent, (char*)"snap_trace", str))
	{
		logger_error("Could not find snap_trace in config xml");
		ok = -1;
	}
	else
	{
		snap_trace = atoi(str);
	}

	// Get RCO buzzing
	if (getField(xmlParent, (char*)"rec_buzz", str))
	{
		logger_error("Could not find rec_buzz in config xml");
		ok = -1;
		RCO_buzz_enable = 0;  //Set as default
	}
	else
	{
		RCO_buzz_enable = atoi(str);
	}

	logger_info("Buzzing enable = %d", RCO_buzz_enable);

	// Get RCO buzzing
	if (getField(xmlParent, (char*)"rec_pre", str))
	{
		logger_error("Could not find rec_buzz in config xml");
		ok = -1;
		pre_event = 0;  //Set as default
	}
	else
	{
		pre_event = atoi(str);
	}

	if ((int)hw_version[0] < 0x34)
	{
		logger_info("Hardware version less than 4.00. Force to turn off pre_event");
		pre_event = 0;
	}

	// Get Mute On/Off control
	if (getField(xmlParent, (char*)"mute_ctrl", str))
	{
		logger_error("Could not find mute_ctrl in config xml");
		ok = -1;
		mute_ctrl = 0;  //Set as default
	}
	else
	{
		if (0 == strcmp("true", str))
		{
			mute_ctrl = 1;
			write_command_to_serial_port("MDO\r\n");
		}
		else
		{
			mute_ctrl = 0;
			write_command_to_serial_port("MDF\r\n");
		}
	}

	// Get Assignable
	if (getField(xmlParent, (char*)"usb_login", str))
	{
		logger_error("Could not find usb_login in config xml");
		ok = -1;
		strcpy(assignable, "false");  //Set as default
	}
	else
	{
		if (0 == strcmp("true", str))
		{
			strcpy(assignable, "true");
		}
		else
		{
			strcpy(assignable, "false");
		}
	}

	// Get DVR name
	if (getField(xmlParent, (char*)"ops_carnum", str))
	{
		logger_error("Could not find ops_carnum in config xml");
		ok = -1;
		strcpy(dvr_name, "No Number");  //Set as default
	}
	else
	{
		if (strlen(str) <= 64 && strlen(str) > 1)
		{
			strcpy(dvr_name, str);
		}
		else
		{
			logger_error("Invalid ops_carnum in config xml");
			strcpy(dvr_name, "No Number");
		}
	}

	ezxml_free(xmlParent);
	dvr_id = (char *)getSerial();
	version_str = (char *)getVersion();

	return ok;
}

void checkLogs()
{
	char str_file_path[100];
	struct dirent* log_entry;
	time_t rawtime;
	char log_files[100][100];
	int log_count = 0;
	int log_dates[100];
	logger_detailed("Entering: %s", __FUNCTION__);

	time(&rawtime);

	DIR* ptr_dir = opendir(ODI_LOG);
	while ((log_entry = readdir(ptr_dir)))
	{
		if (strcmp(log_entry->d_name, ".") == 0 ||
			strcmp(log_entry->d_name, "..") == 0 ||
			strcmp(log_entry->d_name, "system.log") == 0 ||
			strcmp(log_entry->d_name, "rdylog") == 0 ||
			strcmp(log_entry->d_name, "stealth") == 0 ||
			log_entry->d_type == DT_DIR)
		{
			continue;
		}

		struct stat statbuf;
		sprintf(str_file_path, "%s/%s", ODI_LOG, log_entry->d_name);
		stat(str_file_path, &statbuf);
		if (statbuf.st_size > 10000000 || difftime(rawtime, statbuf.st_mtime) > 2600000)   //remove log file 30 old clean up
		{
			logger_info("Removing log file: %s %ld", str_file_path, statbuf.st_size);
			remove(str_file_path);
		}
		else
		{
			log_dates[log_count] = statbuf.st_mtime;
			strcpy(log_files[log_count++], str_file_path);
		}
	}
	closedir(ptr_dir);

	// Sort log files in ascending order
	int c, d, s;
	char st[100];
	for (c = 0; c < (log_count - 1); c++)
	{
		for (d = 0; d < log_count - c - 1; d++)
		{
			if (log_dates[d] > log_dates[d + 1])   /* For decreasing order use < */
			{
				s = log_dates[d];
				strcpy(st, log_files[d]);
				log_dates[d] = log_dates[d + 1];
				log_dates[d + 1] = s;
				strcpy(log_files[d], log_files[d + 1]);
				strcpy(log_files[d + 1], st);
			}
		}
	}

	struct statvfs stat_buf;
	unsigned long free_space;
	c = 0;
	while (1)    // Delete log files until 50% is remaining
	{
		if (statvfs(ODI_LOG, &stat_buf) == 0)
		{
			free_space = stat_buf.f_bavail * stat_buf.f_bsize / 1024000;
		}
		else
		{
			break;
		}

		if (free_space > 50 || c > log_count - 2)
		{
			break;
		}

		logger_info("Removing log file: %lu, %s", free_space, log_files[c]);
		remove(log_files[c++]);
	}
}

int reset_config()
{
	char filename[256];
	FILE* fp = NULL;
	logger_detailed("Entering: %s", __FUNCTION__);

	sprintf(filename, "%s/config.xml", ODI_CONF);
	fp = fopen(filename, "w");
	if (fp == NULL)
	{
		logger_error("Can't open config.xml file");
		return -1;
	}

	time_t now = time(NULL);
	struct tm tmi;
	char mdy[20], hms[20];
	strftime(mdy, 20, "%m/%d/%Y", localtime_r(&now, &tmi));
	strftime(hms, 20, "%H:%M:%S", localtime_r(&now, &tmi));

	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<config-metadata>\n   <config>\n");
	fprintf(fp, "      <dts_date>%s</dts_date>\n", mdy);
	fprintf(fp, "      <dts_dst>true</dts_dst>\n");
	fprintf(fp, "      <dts_time>%s</dts_time>\n", hms);
	fprintf(fp, "      <dts_tz>-05:00 Eastern Time</dts_tz>\n");
	fprintf(fp, "      <dvr_id>%s</dvr_id>\n", dvr_id);
	fprintf(fp, "      <eth_addr>192.168.10.155</eth_addr>\n");
	fprintf(fp, "      <eth_dhcp>true</eth_dhcp>\n");
	fprintf(fp, "      <eth_gate>192.168.10.1</eth_gate>\n");
	fprintf(fp, "      <eth_mask>255.255.255.0</eth_mask>\n");
	fprintf(fp, "      <mute_ctrl>true</mute_ctrl>\n");
	fprintf(fp, "      <officer_name>No Name</officer_name>\n");
	fprintf(fp, "      <officer_title>No Title</officer_title>\n");
	fprintf(fp, "      <officer_id>Officer Id</officer_id>\n");
	fprintf(fp, "      <ops_carnum>No Number</ops_carnum>\n");
	fprintf(fp, "      <remotem_broadcast_ip>255.255.255.255</remotem_broadcast_ip>\n");
	fprintf(fp, "      <rec_buzz>0</rec_buzz>\n");
	fprintf(fp, "      <rec_pre>0</rec_pre>\n");
	fprintf(fp, "      <rec_qual>4</rec_qual>\n");
	fprintf(fp, "      <reset>true</reset>\n");
	fprintf(fp, "      <snap_trace>3</snap_trace>\n");
	fprintf(fp, "      <usb_login>true</usb_login>\n");
	fprintf(fp, "   </config>\n</config-metadata>\n");
	fflush(fp);
	fclose(fp);
	return 0;
}

void set_planb_size(int sz)
{
	logger_debug("Plan B size=%d", sz);
	planb_size = sz;
}

void size_check()
{
	char video_file[100];
	logger_detailed("Entering: %s", __FUNCTION__);
	sprintf(video_file, "%s/%s", ODI_DATA, current_file);
	if (RecSizeChkState == 1)   // 01.06.2015 [davis] check mkv size after record on , Start
	{
		rec_time_cnt = 0;
		RecSizeChkState = 2;
	}

	if (RecSizeChkState == 2)
	{
		rec_time_cnt++;
		if (rec_time_cnt > 40)   // 2 sec to check
		{
			struct stat st;
			stat(video_file, &st);
			long long retChkmkvSizeValue = (long long)st.st_size;

			if (retChkmkvSizeValue > planb_size)
			{
				logger_info(" 1>Check mkv size is NORMAL: %d * 50ms , %lld, %s",
					rec_time_cnt, retChkmkvSizeValue, video_file);
				RecSizeChkState = 0;
			}
			else
			{
				logger_info(" 1>Check mkv size is BAD!!! < %d * 50ms >, %d ",
					rec_time_cnt, retChkmkvSizeValue);
				char cmd[256];
				logger_info("%s:%d Sending SIGINT to gst_capture", __FUNCTION__, __LINE__);
				system("killall -SIGINT gst_capture > /dev/null 2>&1");
				logger_info("Removing %s %d", video_file, remove(video_file));
				logger_info("RCO mkv check fail to start recording");

				start_capture(CAPTURE_FILE_MON);
				RecSizeChkState = 3;	// into 2nd check mkv cycle mode
				rec_time_cnt = 0;
			}
		}
	}

	if (RecSizeChkState == 3)
	{
		rec_time_cnt++;
		if (rec_time_cnt > 100)   // 5 sec to check
		{
			struct stat st;
			stat(video_file, &st);
			long long retChkmkvSizeValue = (long long)st.st_size;

			if (retChkmkvSizeValue > planb_size)
			{
				logger_info(" 2>Check mkv size is NORMAL: %d * 50ms , %lld %s",
					rec_time_cnt, retChkmkvSizeValue, video_file);
				RecSizeChkState = 0;
			}
			else
			{
				logger_info(" 2>Check mkv size is BAD!!! after RCO< %d * 50ms >, %d ",
					rec_time_cnt, retChkmkvSizeValue);
				system("touch /odi/log/planb");
				system("cp /var/log/messages /odi/log/");
				is_processing_done = 1; // this will cause main processing loop to exit
			}
		}
	}
}

//#define L_WATCHDOG
#define WD_SLEEP 3000000

void *gst_capture_WDOG()
{
	char logbuf[128];
	struct tm local, tmi;
	logger_detailed("Entering: %s", __FUNCTION__);
	while (1)
	{
		time_t t = time(NULL); //Get system UTC
		local = *localtime_r(&t, &tmi); //Get system local time
		sprintf(logbuf, "echo %02d%02d_%02d:%02d:%02d Dog bark from %s >> /odi/log/wdog.log",
			local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, __FUNCTION__);
		system(logbuf);
		usleep(WD_SLEEP);
	}
}

int checkhost()
{
	char* hostname;
	struct hostent *hostinfo;
	hostname = "www.google.com";

	if (ack_eto == 1) //check host only when Ethernet present
	{
		hostinfo = gethostbyname(hostname);
		if (hostinfo == NULL)
		{
			//logger_info("failed to gethostbyname: %s", hostname);
			return -1;
		}
		else
		{
			//logger_info("connect to host: %s", hostname);
			return 1;
		}
	}
	else
	{
		return -1;
	}
}

void cloud_main_task()
{
	int host = -1;
	logger_cloud("%s: Entering ... pid: %d", __FUNCTION__, getpid());

	char* hostname;
	struct hostent *hostinfo;
	hostname = "www.google.com";
	get_end_URL();
	while (1)
	{
		sleep(DEFAUTL_AWS_SLEEP); // task sleep loop

		if (!check_cloud_flag()) // If didn't see flag keep sleeping
		{
			logger_cloud("No cloud lock. Back to sleep");
			continue;
		}

		hostinfo = gethostbyname(hostname);
		if (hostinfo != NULL)
		{
			host = 1;
		}
		else
		{
			host = 0;
		}

		if (host == 1)
		{
			logger_cloud("Ethernet connected. Start searching for file to upload.");
			int result = -1;
			cloud_init = 1;
			result = BV_to_cloud();
			switch (result)
			{
				case HAD_NOFILE:
					logger_cloud("Unit has no file for upload");
					if (1 == check_log_push_time())
					{
						logfile_for_cloud();
						logger_cloud("Upload log to cloud");
					}
					else
					{
						logger_cloud("Logs was pushed less than 24h ago");
					}
					sleep(DEFAUTL_AWS_SLEEP * 6);  //Sleep more if no more file
					break;
				case UPDATE_ERR:
					logger_cloud("Media update error");
					cloud_init = 1; // Try to get new token
					break;
				case UPLOAD_ERR:
					logger_cloud("Media upload error");
					cloud_init = 1; // Try to get new token
					break;
				case SIGNEDURL_ERR:
					logger_cloud("Get SignedURL error");
					cloud_init = 1; // Try to get new token
					break;
				case GETTOKEN_ERR:
					logger_cloud("Get Token error");
					cloud_init = 1; // Try to get new token
					break;
			}

			if (get_time == 0)
			{
				bzero((void *)&main_token, TOKEN_SIZE);
				result = GetToken(&main_token, &refresh_token);
				logger_info("Call CLOUD GetTime when ever dock");
				GetTime();
				get_time = 1;
			}
		}
		else
		{
			logger_info("Out bound network can't be reached");
		}
	}
}

int main(int argc, char* argv[])
{
	int stealth_time_state;
	int snap_trace_state;
	int check_log_cnt = 0;
	int power_off_cnt = 0;
	int power_saver = 1;
	int pre_event_record_ready = 0;
	int pre_event_started = -1;

	logger_detailed("Entering: %s", __FUNCTION__);

#ifdef L_WATCHDOG
	pthread_t mon_id = -1;
	if (0 == pthread_create(&mon_id, NULL, &gst_capture_WDOG, NULL))
		system("echo GST_CAPTURE_WDOG start `date` >> /odi/log/wdog.log");
	else
		system("echo GST_CAPTURE_WDOG init failed >> /odi/log/wdog.log");
#endif

	int c;
	opterr = 0;

	log_level = MON_LOG_INFO;
	while ((c = getopt(argc, argv, "pdb")) != -1)
	{
		switch (c)
		{
			case 'b':
				planb_size = 1000000000;
				break;
			case 'p':
				power_saver = 0;
				break;
			case 'd':
				log_level = MON_LOG_DEBUG;
				break;
			default:
				abort();
		}
	}

	serial_port_fd = open_serial_port();
	char buf[10];
	int i, cnt;

	//10417
	strcpy(buf, "");
	write(serial_port_fd, "API\r\n", 5);
	sleep(1);
	for (i = 0; i<4; i++)
	{
		cnt = read(serial_port_fd, buf, 7);
		if (cnt != -1)
		{
			api_version[0] = buf[2];
			api_version[1] = '.';
			api_version[2] = buf[3];
			api_version[3] = buf[4];
			break;
		}
		usleep(50000);
	}

	strcpy(buf, "");
	strcpy(hw_version, "1.00");
	write(serial_port_fd, "APH\r\n", 5);
	sleep(1);
	for (i = 0; i<4; i++)
	{
		cnt = read(serial_port_fd, buf, 7);
		if (cnt != -1)
		{
			hw_version[0] = buf[2];
			hw_version[1] = '.';
			hw_version[2] = buf[3];
			hw_version[3] = '.';
			hw_version[4] = buf[4];
			break;
		}
		sleep(1);
	}
	// Dump HW version to file /odi/log/hw_version.txt to use for upgrade protection
	char cmd_buf[128];
	sprintf((char*)&cmd_buf[0], "echo \"%s \" > /odi/log/hw_version.txt", &hw_version);
	system((char*)&cmd_buf[0]);
	//end of 10417

	// If restart from a plan b reboot, don't send BOP (resets stealth mode)
	if (access("/odi/log/planb", 0) == 0)
	{
		remove("/odi/log/planb");
		if (access("/odi/log/stealth", 0) == 0)
		{
			stealth_on = 1;
		}
		else
		{
			write_command_to_serial_port("UPD\r\n");
		}
	}
	else
	{
		logger_info("System startup send BOP...");
		write(serial_port_fd, "BOP\r\n", 5);
		sleep(1);

		cnt = read(serial_port_fd, buf, 7);
		chargerON = 0;
		for (i = 0; i<7; i++)
		{
			cnt = read(serial_port_fd, buf, 7);
			if (cnt != -1)
			{
				if (buf[2] == 'C' && buf[3] == 'H' && buf[4] == 'O')
				{
					//If charger ON -- cold boot from power, should not do recording recovery
					chargerON = 1;
					remove("/odi/log/recording");
					write(serial_port_fd, "CHO\r\n", 5);
				}
				else if (buf[2] == 'R' && buf[3] == 'C' && buf[4] == 'O')
				{
					//recording = 1;
				}
				break;
			}
			sleep(1);
		}
		stealth_on = 0;
	}

	// get snap_trace value
	if (parseXML() < 0)
	{
		logger_error("monitor main:cannot read snap_trace value, set to default ");
	}

	char buffer[80];
	int fp = open("/proc/cmdline", O_RDONLY);
	read(fp, buffer, 80);
	close(fp);

	logger_info("Dvr ID: %s, Broadcast IP: %s, Boot device: %s",
		dvr_id, broadcast_ip, strtok(strstr(buffer, "/dev"), " "));

	logger_info("App version: %s, uCode version: %s, HW version: %s",
		version_str, api_version, hw_version);

	add_signal_handler();

	start_mongoose(get_eth0_state());
	initUDP(version_str, dvr_id, api_version, broadcast_ip);

	memset(serial_raw_read_buf, 0, COMM_BUFFER_SIZE + 1);
	if (pthread_create(&tty_read_tid, NULL, serial_raw_read, NULL))
	{
		logger_error("monitor main:cannot create read thread");
		return 1;
	}

	checkLogs();

	if (access("/odi/log/recording", 0) == 0)
	{
		if (pre_event == 0) 	// start video with normal case, no pre_event
		{
			consume_and_intrepret_UART_data();
			write_command_to_serial_port("RCO\r\n");
			sleep(1);
			consume_and_intrepret_UART_data();

			start_capture(CAPTURE_REBOOT);
			chkXmlRdy = 0;                       // davis 11.22.2014
			remove("/odi/log/rdylog");           // davis 11.22.2014
			RecSizeChkState = 1;                 // davis 01.06.2015
			recording = 1;
			pre_event_recording = 0;
		}
		else
		{
			pre_event_record_ready = 1;	// Set flag to record recovery can resume when pre_event launched
		}
	}

	// Start Cloud thread init. May use config.xml flag to enable/disable this feature
	// Setup default route path
	if (check_cloud_flag())
	{
		DES_CLOUD_connect = TO_CLOUD; // Set cloud flag so to lock to cloud
	}

	logger_info("Start init cloud thread");
	pthread_t cloud_id = -1, pub_id = -1, sub_id = -1, timer_id = -1;
	int result = -1;

	if (timer_id == -1)
	{
		result = pthread_create(&timer_id, NULL, timer_task, NULL);
		if (result == 0)
		{
			logger_info("Starting timer thread.");
		}
		else
		{
			logger_info("Timer thread launch failed");
		}
	}

	// fork all clouds tasks
	pid_t cloud_pid = -1;

	if (cloud_pid == -1)
	{
		cloud_pid = fork();
		if (cloud_pid == 0) // Start cloud task as child
		{
			memcpy(argv[0], "file_upload_main        \n", 26);
			cloud_main_task();
		}

		if (cloud_pid > 0)
		{
			logger_info("App done fork cloud task");
		}
	}

	if (sub_id == -1)
	{
		result = pthread_create(&sub_id, NULL, mqtt_sub_main_task, NULL);
		if (result == 0)
		{
			logger_info("Starting mqtt_sub_main_task thread.");
		}
		else
		{
			logger_info("Cloud Admin thread launch failed");
		}
	}

	if (pub_id == -1)
	{
		result = pthread_create(&pub_id, NULL, mqtt_pub_main_task, NULL);
		if (result == 0)
		{
			logger_info("Starting mqtt_pub_main_task thread.");
		}
		else
		{
			logger_info("Cloud Admin thread launch failed");
		}
	}

	// Check and set system default time if not valid
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (tv.tv_sec < SYSTEM_DEFAULT_TIME)
	{
		tv.tv_sec = SYSTEM_DEFAULT_TIME;
		settimeofday(&tv, NULL);
		system("hwclock -w");
	}

	tstate90 = 1;
	camera_docked = 0;
	int udp_count = 0, free_space_cnt = 0, reset_counter = 0, udp_ret = 0;
	int reset_config_pressed = 0;
	static int eth_cnt = 0;
	while (!is_processing_done)
	{
		if (eth_cnt++ > 100)  // check ETH0 once every 5 second
		{
			if (process_eth0_carrier_state_change(udp_ret) < 0)
			{
				perror("get carrier state error\n");
			}
			eth_cnt = 0;
		}

		stealth_time_state = stealth_time_button();
		if (stealth_time_state < 0)
		{
			logger_error("get GPIO 90 error");
		}

		if ((serial_raw_read_cur != serial_raw_read_last))
		{
			if (consume_and_intrepret_UART_data() == -1)
			{
				logger_debug("consume UART data error -1 retry");
				usleep(1000);
			}
		}

		// Stealth/Time button
		if (!snap_trace_state && stealth_time_action(stealth_time_state) < 0)
		{
			logger_error("Could not successfully complete stealth_time_action action");
		}

		snap_trace_state = snap_trace_button();
		if (stealth_time_state || snap_trace_state)
		{
			power_off_cnt = 0;
		}

		// If trigger during charge with pre_event also try to increment recording counter
		if (!camera_docked || (camera_docked == 1 && recording == 1 && pre_event > 0))
		{
			check_for_ACK();

			// Snap/Trace button
			if (recording && snap_trace && snap_trace_action(snap_trace_state) < 0)
			{
				logger_error("Could not successfully complete snap_trace_action action");
			}

			// Check space every minute
			if (recording && free_space_cnt > 1200)
			{
				logger_info("Checking battery level and disk space...");
				get_battery_level();

				int dspace = available_space();
				logger_info("Space available: %d MB, Battery level: %d%%", dspace, battery_level);

				if (dspace < MINIMUM_SPACE || battery_level < MINIMUM_BATTERY)
				{
					logger_info("%s:%d Sending SIGINT to gst_capture", __FUNCTION__, __LINE__);
					system("killall -s SIGINT gst_capture > /dev/null 2>&1");
					if (battery_level < MINIMUM_BATTERY)
					{
						logger_error("Stopping recording, low battery: %d", battery_level);
						logger_info("EVENT RECORD STOP [BAT]");
						system("echo 4 > /odi/log/stopreason");
					}
					else
					{
						logger_error("Stopping recording, low disk space: %d", dspace);
						logger_info("EVENT RECORD STOP [SPC]");
						system("echo 3 > /odi/log/stopreason");
					}
					//if(!stealth_on)
					write_command_to_serial_port("VID\r\n");
					remove("/odi/log/recording");
					recording = 0;
				}
				free_space_cnt = 0;
			}

			if (recording == 1)
			{
				power_off_cnt = 0;
				free_space_cnt++;
				recording_counter++;
				if (RCO_buzz_enable > 0 && RCO_buzzing == 1) // ODI fix stealth mode 11711 -r617
				{
					//Do recording buzz and reset flag
					logger_info("Send buzzing during record");
					write_command_to_serial_port("VIB\r\n");
					RCO_buzzing = 0;
					RCO_buzz_timer = 0;
				}
			}
			else
			{
				recording_counter = 0;
				free_space_cnt = 0;
			}
		}
		else
		{
			if (udp_count > 100 && net_up == 1)   // Send UDP every 5 seconds
			{
				udp_ret = sendUDP();
				if (udp_ret == -1)
				{
					net_up = 0;
				}
				udp_count = 0;
			}
			udp_count++;
		}

		// Checking for reset command (20 second delay)
		if (snap_trace_state && stealth_time_state && reset_counter > 400)
		{
			logger_info("####");
			logger_info("Restoring default config and rebooting...");
			logger_info("####");
			reset_config_pressed = 1;
			break;
		}

		if (snap_trace_state && stealth_time_state)
		{
			reset_counter++;
		}
		else
		{
			reset_counter = 0;
		}

		// check log files every 1 hours
		if (check_log_cnt > 1200 * 60 * 1)
		{
			logger_info("Deleting logs files older than 30 days or larger than 10MB.");
			checkLogs();
			check_log_cnt = 0;
		}
		check_log_cnt++;

		// power to suspend after .5 seconds; DO HIBERNATE ONLY WHEN PRE_EVENT IS OFF
		if (power_off_cnt >= 10 && power_saver)
		{
			if ((recording == 0) && (camera_docked == 0) && (chargerON == 0))
			{
				if (pre_event == 0)
				{
					logger_debug("All conditions exist for sleep now...");
					sync();  // before hibernate
					write_command_to_serial_port("SLP\r\n");
					system(ODI_POWER_SUSP);
					stealth_time_state_save = 1;
					//Restore default connection path DES_vs_CLOUD
					sleep(1);
				}
				else if (pre_event > 0 && -1 == (int)pid_find(ODI_CAPTURE))
				{
					logger_info("Camera is undocked: launch gst_capture for pre-event time %d s", pre_event);
					time_t now = time(NULL);
					struct tm ts;
					char ymd[20];
					strftime(ymd, 20, "%Y%m%d", localtime_r(&now, &ts));
					char cmd[256];
					sprintf(cmd, "GST_DEBUG_NO_COLOR=1 GST_DEBUG_FILE=/odi/log/gst_capture_%s.log GST_DEBUG=2 /usr/local/bin/gst_capture &", ymd);
					system(cmd);
					sleep(2);
					pre_event_recording = 1;
					pre_event_started = 1;
				}
			}
			power_off_cnt = 0;

		}

		if (!stealth_time_state && !recording && !chargerON && !chkXmlRdy)
		{
			power_off_cnt++;
		}

		if (chkXmlRdy == 1 && access("/odi/log/rdylog", 0) == 0)      // davis 11.22.2014 start
		{
			logger_debug("Found /odi/log/rdylog from gst_capture after RCF");
			write_command_to_serial_port("RDY\r\n");
			chkXmlRdy = 0;
		}

		if (current_found && recording)
		{
			size_check();
		}

		if (access("/odi/log/rdylog", 0) == 0 && access("/odi/log/recording", 0) == 0)
		{
			if ((pre_event_record_ready == 0) || ((pre_event_record_ready == 1 && 0 < (int)pid_find(ODI_CAPTURE))))
			{
				logger_info("gst_capture stop while record button at ON position");
				//Restart interrupted recording cause by Gstreamer IDF
				consume_and_intrepret_UART_data();
				write_command_to_serial_port("RCO\r\n");
				sleep(1);
				consume_and_intrepret_UART_data();
				logger_info("restart recording after interruption");
				sleep(5);
				parseXML();
				start_capture(CAPTURE_REBOOT);
				chkXmlRdy = 0;
				remove("/odi/log/rdylog");
				RecSizeChkState = 1;
				recording = 1;
			}
		}

		// Handling the starting of pre_event recording
		//start pre_event only when no gst_capture running and unit undock
		if (camera_docked == 1 && pre_event > 0 && pre_event_started == 1 && recording != 1)
		{
			static int pre_event_start_reset = 0;
			logger_info("Docked with Power only, try to stop gst_capture. Capture_pid: %d", capture_pid);
			sleep(1);
			system("killall -s SIGINT gst_capture > /dev/null 2>&1");
			sleep(1);
			logger_info("Stop gst_capture in pre_event condition");
			if (pre_event_start_reset++ > 5 || -1 == (int)pid_find(ODI_CAPTURE))
			{
				pre_event_started = 0;
				pre_event_start_reset = 0;
				pre_event_recording = 0;
			}
		}

		usleep(50000);
	}
	logger_info("Processing done...\n");

	if (dvr_id)
	{
		free(dvr_id);
		dvr_id = NULL;
	}

	if (reset_config_pressed)
	{
		write_command_to_serial_port("VID\r\n");
		for (i = 0; i<5; i++) sleep(1);
		write_command_to_serial_port("BOR\r\n"); //Change to fix 10675
		reset_config();

		// Reset cloud mode
		remove("/odi/conf/cloud.txt");
		remove("/odi/log/stealth");
		remove("/odi/log/recording");
	}
	else if (access("/odi/log/planb", 0) == 0)
	{
		if (!stealth_on)
		{
			write_command_to_serial_port("BOR\r\n"); //Change to fix 10675
		}
	}

	//    write_command_to_serial_port("RST\r\n");
	//    sleep(1);
	system("sync;reboot");
}
