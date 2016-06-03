#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char pause_time_buffer[2000];
static void logger_WD(char* str_log) {
    time_t now = time(NULL);
    struct tm tmi;
    char time_buf[20];
    char fileName[500];

    strftime(time_buf, 20, "%Y%m%d", localtime_r(&now, &tmi));
    sprintf(fileName, "/odi/log/status_%s.log", time_buf);
    FILE* fp = fopen(fileName, "a");
    strftime(time_buf, 17, "(%Y%m%d%H%M%S)", localtime_r(&now, &tmi));
    sprintf((char *) &pause_time_buffer, "%s WD: %s \n", time_buf, str_log);
    fwrite((char *) &pause_time_buffer, strlen((char *) &pause_time_buffer), 1, fp);
    fclose(fp);
}

int get_mon_status()
{
    FILE *fp; char buf[5];
    fp = fopen("/tmp/wd_mon", "r");
    if(fp < 0)
	return 0;
    int read = fread((char *) &buf, 1, 4, fp);
    if(read > 0)
    {
        int count = atoi((char *) &buf);
        if(count > 1)
	{
	    if(fp > 0)
		fclose(fp);
 	    return 1;
	}
    }
    if(fp > 0)
        fclose(fp);
    return 0;
}

int get_fileupload_status()
{
    FILE *fp; char buf[5];
    fp = fopen("/tmp/wd_upload", "r");
    if(fp < 0)
	return 0;
    int read = fread((char *) &buf, 1, 4, fp);
    if(read > 0)
    {
        int count = atoi((char *) &buf);
        if(count > 1)
	{
            if(fp > 0)
                fclose(fp);
 	    return 1;
	}
    }
    if(fp > 0)
        fclose(fp);
    return 0;
}

int main()
{
    static int restart_mon = 0, restart_upload = 0;
    logger_WD("Starting WD on monitor\n");
    sleep(120);
    while(1) {
        system("/usr/local/bin/wd.sh");
        sleep(5);

        if(0 == get_mon_status())
	{
	    restart_mon++;
	}
	else
	    restart_mon = 0; //Reset if system recovered

        if(0 == get_fileupload_status())
	{
	    restart_upload++;
	}
	else
	    restart_upload = 0; //Reset if system recovered
	
	if(restart_mon > 1)
	{
	    logger_WD("!!! ERROR !!! Monitor stop running. Restart");
	    system("reboot");
	}
	
	if(restart_upload > 1)
	{
	    logger_WD("!!! ERROR !!! Fileupload stop running. Restart");
	    system("reboot");
	}

        sleep(55);
    }
}
