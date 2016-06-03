#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include "BVcloud.h"
#include <netdb.h>

enum {
    TYPE_MP4=0,
    TYPE_PNG,
    TYPE_JPG,
    TYPE_XML,
    TYPE_VVT
} FILE_TYPE;

char filelist[FILE_LIST_SIZE][FILE_NAME_SIZE];
char main_token[TOKEN_SIZE];
char refresh_token[TOKEN_SIZE];
char signedURL[SIGNEDURL_SIZE];
char filetype[64];
int cloud_init = -1;

extern int had_nofile;

int get_file_list()
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
                        //logger_cloud("File: %s\n", entries->d_name);
   			//Get mkv file first
                        if(strstr(entries->d_name, "mkv"))
			{
                            logger_cloud("Found MKV: %s", entries->d_name);
			    strcpy(filelist[i++], entries->d_name);
			    //After get mkv file go and get the rest of the group file
        		    bzero((char*)&base_name[0], 25);
        		    strncpy((char  *) &base_name, entries->d_name, 24);
        		    base_name[25] = '\0';
        		    logger_cloud("%s: Base file name: %s", __FUNCTION__, (char *) &base_name);
			    file_found = 1;
			    had_nofile = 0;	//Set for PUB information
			    break;
		    	}
                    }
                }
            }
        }
    }
    if(file_found == 1) //MP4 File existed use this as base name for group file and create MD5SUM file
    {
        //Generate MD5 sum for video file. This may take long but OK
        char cmd_md5[128];
        char md5_file[64];
        int try_cnt = 0;
        logger_cloud("Generate MD5 sum for file: %s.mkv", (char *) &base_name);
        sprintf((char *) &cmd_md5,"/usr/bin/md5sum /odi/data/%s.mkv > /odi/data/%s.md5", 
			(char *) &base_name, (char *) &base_name);
        system((char *) &cmd_md5);
        //See if md5summ file complete then brake out to group the rest of files
        while(1) {
	    sprintf((char *) &md5_file,"/odi/data/%s.md5", (char *) &base_name);
	    if(access((char *) &md5_file, 0) == 0)
	        break;
            sleep(5);
            logger_cloud("try to read md5sum from %s", (char *) &md5_file);
            if(try_cnt > 40)
	    {
		logger_cloud("Can't seem to locate MD5SUM after 200s");
		break; 
	    }
	    try_cnt++;
	}
    }
    else // Try the to get base file name for fragment grou
    {
	rewinddir(directory); //restart file searching from begin
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
                        if(strstr(entries->d_name, "xml")  || 
			    strstr(entries->d_name, "jpg") ||
			    strstr(entries->d_name, "vtt"))
			{
                            logger_cloud("Found base file : %s", entries->d_name);
			    strcpy(filelist[i++], entries->d_name);
			    //After get mkv file go and get the rest of the group file
        		    bzero((char *) &base_name, 25);
        		    strncpy((char  *) &base_name, entries->d_name, 24);
        		    base_name[25] = '\0';
        		    logger_cloud("%s: Base file name: %s", __FUNCTION__, (char *) &base_name);
			    file_found = 1;
			    break;
		    	}
                    }
                }
            }
        }
    }

    rewinddir(directory);
    //logger_cloud("---------- Start search for the rest of group ------------------");
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
                    //logger_cloud("File: %s\n", entries->d_name);
		    //Try to match base name first
                    if(strstr(entries->d_name, (char *) &base_name))
		    {
                        if(strstr(entries->d_name, "xml") ||
                            strstr(entries->d_name, "jpg") ||
                            strstr(entries->d_name, "md5") ||
                            strstr(entries->d_name, "vtt") )
                            strcpy(filelist[i++], entries->d_name);
		    }
                }
            }
        }
    }
    close(directory);

    return i; //There are files to process
}

int get_token()
{
    int result = -1;
    logger_detailed("%s: Entering ...",__FUNCTION__);
    bzero((void *) &main_token, TOKEN_SIZE);
    result = GetToken(&main_token, &refresh_token);
    if(result == BV_FAILURE)
    {
	logger_cloud("Error: GetToken failure\n");
        return BV_FAILURE;
    }
    return result;
}

char *get_file_type(char *filename)
{
    bzero((char *) &filetype, 64);
    if(strstr(filename, "mkv"))
        strcat((char *) &filetype,"Content-Type: application/octet-stream ");
    else if(strstr(filename, "jpg"))
        strcat((char *) &filetype,"Content-Type: image/jpeg ");
    else if(strstr(filename, "xml"))
        strcat((char *) &filetype,"Content-Type: text/xml ");
    else if(strstr(filename, "vtt"))
        strcat((char *) &filetype,"Content-Type: text/plain ");
    else if(strstr(filename, "log"))
        strcat((char *) &filetype,"Content-Type: text/plain ");
    else
	logger_cloud("File name invalid\n");

    return(char *) &filetype;
}
int BV_to_cloud( )
{
    logger_detailed("%s: Entering ...", __FUNCTION__);
    int numoffile= -1, i;
    int result = BV_FAILURE;
    char l_cmd[64];
    char test_file[64];
    char md5_file[64];
    char md5cksum[33];
    static int to_cloud_cnt = 0;
    
    p_main_token = (char *) &main_token;
    numoffile = get_file_list();
    if(numoffile > BV_OK)
    {
	logger_cloud("Search file return with error: %d", numoffile);
	return numoffile;
    } 
    //logger_cloud("There are %d file(s) to process", numoffile);
    if(numoffile == 0)
    {
	had_nofile = 1;
	return HAD_NOFILE;
    }

    for(i=0; i < numoffile; i++)
	logger_cloud("Group file: %s", filelist[i]);

    if(cloud_init == 1)
    {
        do { 
            bzero((void *) &main_token, TOKEN_SIZE);
            result = GetToken(&main_token, &refresh_token);
	    to_cloud_cnt++;
	    if(to_cloud_cnt > TOCLOUDATTEMPT)
		return GETTOKEN_ERR;
        } while(result != BV_SUCCESS);
	to_cloud_cnt = 0;
	cloud_init = -1;
    }

    //Upload xml first then the rest would not be in any particular order
    for(i=0; i < numoffile; i++)
    {
	if(strstr(filelist[i], "xml"))
	{
	    sprintf((char *) &test_file,"/odi/data/%s",filelist[i]);
	    if(access((char *) &test_file,0) != 0)
	    {
	        logger_cloud("File: %s not existed. Move on to the next file",(char *) &test_file);
	        continue;
	    }
	    else
	        logger_cloud("Procesing file: %s -- index: %d",(char *) &test_file, i);
restart_xml:
            to_cloud_cnt = 0;
            do { //Get SignedURL
		bzero((void *) &signedURL, SIGNEDURL_SIZE);
                result = GetSignedURL(&main_token, &signedURL, filelist[i]);
		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Refresh Token",__FUNCTION__);
                    RefreshToken((char *) &main_token, (char *) &refresh_token);
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return SIGNEDURL_ERR;
	    }while(result != BV_SUCCESS);
	    to_cloud_cnt = 0; //reset 

	    do { //Upload File to S3
		char *filetype;
                filetype = get_file_type(filelist[i]);
		result = FileUpload(&signedURL, filelist[i], filetype);
		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Go back to try again",__FUNCTION__);
                    RefreshToken((char *) &main_token, (char *) &refresh_token);
		    goto restart_xml;
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return UPLOAD_ERR;
	    } while(result != BV_SUCCESS);
	    to_cloud_cnt = 0; //reset 

	    do { //Update Media
	        result = UpdateStatus(&main_token, filelist[i], NULL);
		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Refresh Token",__FUNCTION__);
                    RefreshToken((char *) &main_token, (char *) &refresh_token);
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return UPDATE_ERR;
	    } while(result != BV_SUCCESS);
		
	    sprintf((char *) &l_cmd, "rm -f /odi/data/%s", filelist[i]);
	    system((char *) &l_cmd);
	    logger_cloud("%s: Success upload. Remove %s",__FUNCTION__, filelist[i]);
	    to_cloud_cnt = 0; //reset 
	}
	else if(strstr(filelist[i], "md5"))
	{
	    // Open md5 sum file and extract checksum for mkv send, if this failed stop the group
	    FILE *fp = NULL;
	    sprintf((char *) &md5_file,"/odi/data/%s",filelist[i]);
	    fp = fopen((char *) &md5_file,"r");
	    if(fp != NULL)
	    {
		fread((char *) &md5cksum, 1, 32, fp);
		md5cksum[32] = '\0';
	        close(fp);
	    }
	    logger_cloud("File: %s has md5sum: %s", filelist[i], (char *) &md5cksum);
	}
    }
    // Now do for the rest of set, if see xml skip it.
    for(i=0; i < numoffile; i++)
    {
	if(strstr(filelist[i], "xml") || strstr(filelist[i], "md5"))
	    continue;
        else
	{
	    sprintf((char *) &test_file,"/odi/data/%s",filelist[i]);
	    if(access((char *) &test_file,0) != 0)
	    {
	        logger_cloud("File: %s not existed. Move on to the next file",(char *) &test_file);
	        continue;
	    }
	    else
	        logger_cloud("Procesing file: %s -- index: %d",(char *) &test_file, i);
restart_all:
            do { //Get SignedURL
		bzero((void *) &signedURL, SIGNEDURL_SIZE);
                result = GetSignedURL(&main_token, &signedURL, filelist[i]);
		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Refresh Token",__FUNCTION__);
                    RefreshToken((char *) &main_token, (char *) &refresh_token);
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return SIGNEDURL_ERR;
	    }while(result != BV_SUCCESS);
	    to_cloud_cnt = 0; //reset 

	    do { //Upload File to S3
		char *filetype;
                filetype = get_file_type(filelist[i]);
		if(strstr(filelist[i], "mkv"))
		{
		    // Only flashing LEDs for mkv files
		    write_command_to_serial_port("UPS\r\n");
	            result = FileUpload(&signedURL, filelist[i], filetype);
		    write_command_to_serial_port("UPD\r\n");
		    if(result == UPLOAD_ERR)
			return UPLOAD_ERR;
		}
		else
		    result = FileUpload(&signedURL, filelist[i], filetype);

		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Go back to try again",__FUNCTION__);
                    RefreshToken((char *) &main_token, (char *) &refresh_token);
		    goto restart_all;
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return UPLOAD_ERR;
	    } while(result != BV_SUCCESS);
	    to_cloud_cnt = 0; //reset 

	    do { //Update Media
                if(strstr(filelist[i], "mkv"))
	            result = UpdateStatus(&main_token, filelist[i], md5cksum);
		else
		    result = UpdateStatus(&main_token, filelist[i], NULL);
		if(result == HTTP_401)
		{
		    logger_cloud("%s: HTTP 401. Refresh Token",__FUNCTION__);
                    GetToken((char *) &main_token, (char *) &refresh_token);
		}	
	        to_cloud_cnt++;
	        if(to_cloud_cnt > TOCLOUDATTEMPT)
		    return UPDATE_ERR;
	    } while(result != BV_SUCCESS);
	    sprintf((char *) &l_cmd, "rm -f /odi/data/%s", filelist[i]);
	    system((char *) &l_cmd);
	    logger_cloud("%s: Success upload. Remove %s",__FUNCTION__, filelist[i]);
	    //If remove mkv, md5 file should be removed at the same time
	    remove((char *) &md5_file);
	    to_cloud_cnt = 0; //reset 
	}
    }
    return 0;
}

int logfile_upload(char *filename)
{
    int result;
    bzero((void *) &signedURL, SIGNEDURL_SIZE);
    result = LogGetSignedURL(&main_token, &signedURL, filename);
    if(result == BV_SUCCESS)
        result = LogUpload(&signedURL,filename, get_file_type(filename));
    else
	logger_cloud("Failed to get log signedURL");

    return result;
}

int today_log_file(char *logfilename)
{
    char today[16];
    char logdate[16];
    char *tem = logfilename;
    int i = 0;
    time_t now = time(NULL);

    while(*tem++ != '_');
    
    //Copy date
    while(*tem != '.') 
    {
	logdate[i++] = *tem++;
    }
    logdate[i] = '\0';
    //
    strftime(today, 16, "%Y%m%d", localtime(&now));
    //logger_cloud("Today date: %s logdate: %s", &today, &logdate);
    if(0 == strcmp((char *) &logdate, (char *) &today))
	return 1; //If equal skip today log rename
    else
	return 0;
}

int logfile_for_cloud()
{
    struct dirent *entries;
    DIR    *directory;
    char base_name[64];
    char src_name[64];
    int result;
    static int did_upload = 0;
    logger_detailed("%s: Entering ...", __FUNCTION__);
    get_token();
    directory = opendir("/odi/log");
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
                        //Get mkv file first
                        if(strstr(entries->d_name, "status") && strstr(entries->d_name, "log"))
                        {
                            logger_cloud("Found log: %s", entries->d_name);
			    result = logfile_upload(entries->d_name);
			    if(result = BV_SUCCESS) // Log upload OK. rename to keep a copy
			    {
				did_upload = 1;
                                if(today_log_file(entries->d_name)) //Don't rename today log
                                    continue;
				strcpy((char *) &base_name, "/odi/log/");
				strcpy((char *) &src_name, "/odi/log/");
				strcat((char *) &base_name, entries->d_name);
				strcat((char *) &src_name, entries->d_name);
				base_name[25] = '\0';  //Terminate "log" to attach "cld" extension
				strcat((char *) &base_name, "cld");
				rename(src_name, base_name);
			    }
			    else
				logger_cloud("Failed to upload log file");
                        }
                    }
                }
            }
        }
    }
    // Generate 24h time stamp for log push to cloud
    if((access("/odi/log/log_date", 0) != 0) || did_upload == 1)
    {
	logger_cloud("Update log upload flag");
        system("date +\"%Y%m%d%H\" > /odi/log/log_date");
	did_upload = 0;
    }
    else
	logger_cloud("No log flag change");
}

int check_log_push_time()
{ 
    FILE *fd;
    char time_buf[16];

    if(access("/odi/log/log_date", 0) != 0) //If log time stamp not existed do log upload and create 
	return 1;

    //logger_cloud("%s: Entering ...", __FUNCTION__);
    time_t now = time(NULL);
    fd = fopen("/odi/log/log_date", "r");
    if(fd < 0)
	return 1; // Push and update log time if no log timer was found

    fread(&time_buf, 1, 16, fd);
    fclose(fd);
    int lasttime = atoi((char *) &time_buf);

    strftime(time_buf, 16, "%Y%m%d%H", localtime(&now)); 
    int curtime = atoi((char *) &time_buf);

    //logger_cloud("Last time: %d cur: %d", lasttime, curtime);
    if(curtime - lasttime > 24)
	return 1;
    else
	return 0; 
}

