DEFS=-DHAVE_LIBGSTREAMER_0_10=1 -DHAVE_LIBXML2=1 -DHAVE_LIBCRYPTO=1 -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 \
 -DHAVE_SYS_STAT_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DHAVE_INTTYPES_H=1 \
 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DHAVE_STDLIB_H=1 -DHAVE_OPENSSL_SHA_H=1 -DHAVE_SETENV=1

export PKG_CONFIG_PATH=../../../fs/fsdev/usr/lib/pkgconfig:../../../fs/fsdev/lib/pkgconfig
#CFLAGS+=-O2
CFLAGS+=-ggdb3

MACHINE:=$(shell uname -m)

ROOTFS=$(PWD)/../../fs/fs/
FSDEV=$(PWD)/../../../fs/fsdev/

#CC=arm-linux-gnueabi-gcc
CC=arm-unknown-linux-gnueabihf-gcc
INCLUDES=-I. -I$(FSDEV)/usr/include/gstreamer-0.10 -I$(FSDEV)/usr/include
#INCLUDES+= -I$(FSDEV)/usr/include/glib-2.0 -I$(FSDEV)/usr/include/libxml2 
INCLUDES+=`pkg-config --cflags libxml-2.0 gobject-2.0 glib-2.0 gstreamer-0.10 openssl libssl libcrypto libffi libcurl`

LIBS=`pkg-config --libs libxml-2.0 gobject-2.0 glib-2.0 gstreamer-0.10 openssl libssl libcrypto libffi json libcurl libcares` -lm -L../../../fs/fsdev/usr/lib/ -lz -lpthread -lmosquitto -ldl


MQTT_DEF= -DWITH_TLS -DWITH_TLS_PSK

all: build_info gst_capture monitor hashfile watch_mon

build_info:
	@echo "Build base machine: $(MACHINE)"
ezxml.o: 
	$(CC) -ggdb -c ezxml.c

watch_mon:
	$(CC) -o watch_mon watch_mon.c

remotem.o: remotem.c odi-config.h version.h
	$(CC) $(CFLAGS) $(INCLUDES) -D_FILE_OFFSET_BITS=64 -c remotem.c -I$(FSDEV)/usr/include/libxml2

mongoose.o: odi-config.h
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -c mongoose.c -I$(FSDEV)/usr/include/libxml2

hashfile: hashfile.c Flash2AppSM_hash.c Flash2AppSM_hash.h
	$(CC) $(INCLUDES) -o hashfile Flash2AppSM_hash.c hashfile.c $(LIBS)

Flash2AppSM.o:
	$(CC) -c Flash2AppSM.c $(CFLAGS) $(INCLUDES) $(LIBS)

gst_capture: gst_capture.c ezxml.o gst_capture.h odi-config.h Flash2AppSM_internal.h Flash2AppSM.o
	$(CC) $(CFLAGS) $(INCLUDES) -o gst_capture gst_capture.c ezxml.o Flash2AppSM.o $(LIBS)
	
monitor: monitor.c ezxml.o monitor.h mongoose.o remotem.o udp_util.o BVcloud.o \
	FileUpload.o FileDownload.o GetSignedURL.o GetToken.o Parse_Json.o UpdateStatus.o GetTime.o \
	RefreshToken.o mqtt_common.o mqtt_sub.o mqtt_sub_init.o mqtt_token.o mqtt_pub.o Configupdate.o \
	LogGetSignedURL.o LogUpload.o
	$(CC) $(CFLAGS) -o monitor -D_FILE_OFFSET_BITS=64 $(INCLUDES) monitor.c mongoose.o \
		remotem.o udp_util.o ezxml.o BVcloud.o FileUpload.o FileDownload.o GetSignedURL.o GetTime.o \
		GetToken.o Parse_Json.o  UpdateStatus.o RefreshToken.o Configupdate.o \
		mqtt_sub.o mqtt_common.o mqtt_sub_init.o mqtt_token.o mqtt_pub.o \
		LogGetSignedURL.o LogUpload.o \
		$(LIBS) 

#Cloud build
BVcloud.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c BVcloud.c

FileUpload.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c FileUpload.c

FileDownload.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c FileDownload.c

GetSignedURL.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c GetSignedURL.c 

GetToken.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c GetToken.c

Parse_Json.o:
	$(CC) -ggdb -std=c99 $(INCLUDES) -c Parse_Json.c

UpdateStatus.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c UpdateStatus.c 

RefreshToken.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c RefreshToken.c 

GetTime.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c GetTime.c 

Configupdate.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c Configupdate.c 

LogGetSignedURL.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c LogGetSignedURL.c 

LogUpload.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c LogUpload.c $(LIBS)

#Cloud config
mqtt_sub.o : mqtt_sub.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mqtt_sub.c $(MQTT_DEF)

mqtt_sub_init.o : mqtt_sub_init.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mqtt_sub_init.c $(MQTT_DEF)

mqtt_pub.o : mqtt_pub.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mqtt_pub.c $(MQTT_DEF)

mqtt_common.o: mqtt_common.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mqtt_common.c $(MQTT_DEF)

mqtt_token.o: mqtt_token.c
	$(CC) $(CFLAGS) $(INCLUDES) -c mqtt_token.c

pub.o: pub.c
	$(CC) $(CFLAGS) $(INCLUDES) -c pub.c

#mon_watchdog: ezxml.o
#	$(CC) $(CFLAGS) $(INCLUDES) -o mon_watchdog mon_watchdog.c ezxml.o

clean:
	rm -f gst_capture monitor hashfile *.o watch_mon
	
install:
	cp gst_capture $(ROOTFS)/usr/local/bin/
	cp monitor $(ROOTFS)/usr/local/bin/
	cp hashfile $(ROOTFS)/usr/local/bin/
	cp watch_mon $(ROOTFS)/usr/local/bin/

install-gst_capture:
	cp gst_capture $(ROOTFS)/usr/local/bin/

install-monitor:
	cp monitor $(ROOTFS)/usr/local/bin/
