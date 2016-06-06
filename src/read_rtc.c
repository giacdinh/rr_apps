#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#define I2C_RETRIES 0x0701
#define I2C_TIMEOUT 0x0702
#define I2C_RDWR 0x0707

#define debug_on 1

int bin2bcd(int x)
{
	return (x % 10) | ((x / 10) << 4);
}

int bcd2bin(int x)
{
	return (x >> 4) * 10 + (x & 0x0f);
}

int main(int argc, char* argv[])
{
	int fd, ret;
	int reg, cnt;
	time_t clock;
	struct i2c_rdwr_ioctl_data e2prom_data;
	struct  tm timeinfo_ptr;
	char* tmr_string;
	int sizeofTM;

	sizeofTM = sizeof(struct tm);
	timeinfo_ptr = (struct tm*)malloc(sizeofTM);

	// printf("\n\t Orginal time is ");
	system("date");
	struct tm tmi;
	time(&clock);
	timeinfo_ptr = *localtime_r(&clock, &tmi);

#if debug_on
	// printf("\n\n\tOrginal clock is %d \n\n",clock);
#endif

	fd = open("/dev/i2c-1", O_RDWR);

	if (fd < 0)
	{
		perror("open error");
	}
	e2prom_data.nmsgs = 2;

	e2prom_data.msgs = (struct i2c_msg*)malloc(e2prom_data.nmsgs * sizeof(struct i2c_msg));

	if (!e2prom_data.msgs)
	{
		perror("malloc error");
		exit(1);
	}

	ioctl(fd, I2C_TIMEOUT, 1);
	ioctl(fd, I2C_RETRIES, 2);

#if 0
	/***write data to e2prom**/
	e2prom_data.nmsgs = 1;
	(e2prom_data.msgs[0]).len = 8; //e2prom
	(e2prom_data.msgs[0]).addr = 0x69;//e2prom
	(e2prom_data.msgs[0]).flags = 0; //write
	(e2prom_data.msgs[0]).buf = (unsigned char*)malloc(8);
	(e2prom_data.msgs[0]).buf[0] = 0x00;// e2prom

	// printf("\t%d:%d:%d\n",timeinfo_ptr.tm_hour,timeinfo_ptr.tm_min,timeinfo_ptr.tm_sec);
	(e2prom_data.msgs[0]).buf[1] = 0x00;//the data to write
	(e2prom_data.msgs[0]).buf[2] = timeinfo_pt.tm_sec;	//the data to write
	(e2prom_data.msgs[0]).buf[3] = timeinfo_ptr.tm_min;	//the data to write
	(e2prom_data.msgs[0]).buf[4] = timeinfo_ptr.tm_hour;	//the data to write
	(e2prom_data.msgs[0]).buf[5] = timeinfo_ptr.tm_mday;	//the data to write
	(e2prom_data.msgs[0]).buf[6] = timeinfo_ptr.tm_mon;	//the data to write
	(e2prom_data.msgs[0]).buf[7] = timeinfo_ptr.tm_year;	//the data to write

	ret = ioctl(fd, I2C_RDWR, (unsigned long)&e2prom_data);

	if (ret < 0)
	{
		perror("ioctl error1");
	}
	sleep(1);

#endif

#if 1
	/******read data from e2prom*******/
	e2prom_data.nmsgs = 2;
	(e2prom_data.msgs[0]).len = 1; 			//e2prom

	(e2prom_data.msgs[0]).addr = 0x69;		// e2prom
	(e2prom_data.msgs[0]).flags = 0;		// write

	(e2prom_data.msgs[0]).buf = (unsigned char*)malloc(2);
	(e2prom_data.msgs[0]).buf[0] = 0x00;	//e2prom

	(e2prom_data.msgs[1]).len = 64;
	(e2prom_data.msgs[1]).addr = 0x69;
	(e2prom_data.msgs[1]).flags = I2C_M_RD;	//read

	(e2prom_data.msgs[1]).buf = (unsigned char*)malloc(64);

	for (reg = 0; reg < 64; reg++)
	{
		(e2prom_data.msgs[1]).buf[reg] = 0;
	}

	if (ioctl(fd, I2C_SLAVE, 0x69) < 0)
	{
		printf("write slave address error \n");
	}
	else
	{

		ret = ioctl(fd, I2C_RDWR, (unsigned long)&e2prom_data);
		printf("\t");
		for (cnt = 0; cnt < 16; cnt++)
		{
			printf(" %02x", cnt);
		}

		printf("\n\t");
		for (cnt = 0; cnt < 16; cnt++)
		{
			printf("===");
		}
		printf("\n\t");

		for (cnt = 0; cnt < 4; cnt++)
		{
			for (reg = 0; reg < 16; reg++)
			{
				printf(" %02x", (e2prom_data.msgs[1]).buf[reg + cnt * 16]);
			}

			printf("\n\t");
		}
	}
#endif

#if 1

	timeinfo_ptr.tm_hour = bcd2bin((e2prom_data.msgs[1]).buf[3]);
	timeinfo_ptr.tm_min = bcd2bin((e2prom_data.msgs[1]).buf[2]);
	timeinfo_ptr.tm_sec = bcd2bin((e2prom_data.msgs[1]).buf[1]);
	timeinfo_ptr.tm_mday = bcd2bin((e2prom_data.msgs[1]).buf[4]);
	timeinfo_ptr.tm_mon = bcd2bin((e2prom_data.msgs[1]).buf[5]) - 1;
	timeinfo_ptr.tm_year = bcd2bin((e2prom_data.msgs[1]).buf[6]) + 100;
#endif

	//	timeinfo_ptr.tm_mday=1;
	timeinfo_ptr.tm_wday = 0;
	timeinfo_ptr.tm_yday = 0;

	clock = mktime(timeinfo_ptr);

#if debug_on
	// printf("\n\n\tnow clock changed to %d \n",clock);
	printf("\n tm_sec = %d", timeinfo_ptr.tm_sec);
	printf(",tm_min = %d", timeinfo_ptr.tm_min);
	printf(",tm_hour = %d", timeinfo_ptr.tm_hour);
	printf(",tm_mday = %d", timeinfo_ptr.tm_mday);
	printf(",tm_mon = %d", timeinfo_ptr.tm_mon);
	printf(",tm_year = %d", timeinfo_ptr.tm_year);
	printf("\n\n");
#endif

	if (stime(&clock) < 0)
	{
		printf("set time error");
	}

	//	printf("\n\t Now sync to ");
	system("date");

	printf("\n");

	close(fd);

	return 0;
}


















