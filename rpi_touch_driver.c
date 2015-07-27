/* User-mode multi-touch driver for 
 * usb 6-2: New USB device found, idVendor=0eef, idProduct=0005
 * usb 6-2: New USB device strings: Mfr=1, Product=2, SerialNumber=3
 * usb 6-2: Product: By ZH851
 * usb 6-2: Manufacturer: RPI_TOUCH
 *
 * Touch events consists of 25 bytes, one example is
 * aa 01 03 1b 01 d2 bb 03 01 68 02 cc 00 5d 01 ef 01 5f 01 fe 00 fb 02 37 cc
 * Offset:
 *     0 : Start byte (aa)
 *     1 : Any touch (0=off,1=on)
 *   2-3 : First touch X
 *   4-5 : First touch Y
 *     6 : Multi-touch start (bb)
 *     7 : Bitmask for all touches (bit 0-4 (first-fifth), 0=off, 1=on)
 *   8-9 : Second touch X
 * 10-11 : Second touch Y
 * 12-13 : Third touch X
 * 14-15 : Third touch Y
 * 16-17 : Fourth touch X
 * 18-19 : Fourth touch Y
 * 20-21 : Fifth touch X
 * 22-23 : Fifth touch Y
 *    24 : End byte (cc or 00)
 *
 * This user mode driver decodes the touch events and injects them back into the
 * kernel using uinput.
 * 
 * Copyright (c) 2015 Bjarne Steinsbo
 *
 * Code and inspiration from http://thiemonge.org/getting-started-with-uinput
 * and the CyanogenMod userspace touchscreen driver for cypress ctma395.
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

#define die(str, ...) do { \
        perror(str); \
	syslog(LOG_ERR, str, ##__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while(0)

int uinput_fd;
int usbraw_fd;
int fifo_fd;
char devname[1024];

#define EVENT_DEBUG 0

int send_uevent(int fd, __u16 type, __u16 code, __s32 value)
{
	struct input_event event;

#if EVENT_DEBUG
	char ctype[20], ccode[20];
	switch (type) {
		case EV_ABS:
			strcpy(ctype, "EV_ABS");
			break;
		case EV_KEY:
			strcpy(ctype, "EV_KEY");
			break;
		case EV_SYN:
			strcpy(ctype, "EV_SYN");
			break;
	}
	switch (code) {
		case ABS_MT_SLOT:
			strcpy(ccode, "ABS_MT_SLOT");
			break;
		case ABS_MT_TRACKING_ID:
			strcpy(ccode, "ABS_MT_TRACKING_ID");
			break;
		case ABS_MT_TOUCH_MAJOR:
			strcpy(ccode, "ABS_MT_TOUCH_MAJOR");
			break;
		case ABS_MT_POSITION_X:
			strcpy(ccode, "ABS_MT_POSITION_X");
			break;
		case ABS_MT_POSITION_Y:
			strcpy(ccode, "ABS_MT_POSITION_Y");
			break;
		case SYN_MT_REPORT:
			strcpy(ccode, "SYN_MT_REPORT");
			break;
		case SYN_REPORT:
			strcpy(ccode, "SYN_REPORT");
			break;
		case BTN_TOUCH:
			strcpy(ccode, "BTN_TOUCH");
			break;
	}
	printf("event type: '%s' code: '%s' value: %i \n", ctype, ccode, value);
#endif

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;

	if (write(fd, &event, sizeof(event)) != sizeof(event)) {
		fprintf(stderr, "Error on send_event %lu", sizeof(event));
		return -1;
	}

	return 0;
}


int main(int argc, char **argv)
{
	struct uinput_user_dev device;
	unsigned char data[25];
	int numc;
	int prev_state[5];

	if (argc != 2)
		die("Usage: rpi_touch_driver -d|usbraw-device");
	if (strlen(argv[1]) > 1 && strcmp(argv[1], "-d") != 0) {
		/* Send device name to daemon and then exit */
		fifo_fd = open("/var/run/.rpi_touch_driver.fifo", O_WRONLY | O_NONBLOCK);
		if ((fifo_fd >= 0) && (write(fifo_fd, argv[1], strlen(argv[1]) > 0)))
			exit(0);
		die("Failed to write to fifo");
	}
	/* This is the daemon.. */

	/* Create fifo */
	if (mkfifo("/var/run/.rpi_touch_driver.fifo", 0600) && (errno != EEXIST))
		die("Failed to create fifo");

	/* Syslog */
	openlog("rpi_touch_driver", LOG_NOWAIT, LOG_DAEMON);

	/* Daemonize */
	if (daemon(0, 0))
		die("error: daemonize\n");

	while (1) {
		/* Wait for device attach */
		fifo_fd = open("/var/run/.rpi_touch_driver.fifo", O_RDONLY);
		if (fifo_fd < 0)
			die("error: open fifo");
		numc = read(fifo_fd, devname, 1023);
		if (numc <= 0)
			die("Failed to read fifo");
		close(fifo_fd);
		devname[numc] = 0;

		/* Iniialize uinput */
		memset(&device, 0, sizeof(device));
		strcpy(device.name, "RPI_TOUCH_uinput");
		device.id.bustype = BUS_VIRTUAL;
		device.id.vendor = 1;
		device.id.product = 1;
		device.id.version = 1;
		device.absmax[ABS_MT_POSITION_X] = 800;
		device.absmax[ABS_MT_POSITION_Y] = 480;
	
		uinput_fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
		if (uinput_fd < 0) {
			uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
			if (uinput_fd < 0)
				die("error: open uinput device");
		}
	
		if (write(uinput_fd, &device, sizeof(device)) != sizeof(device))
			die("error: setup device");

		if (ioctl(uinput_fd, UI_SET_EVBIT, EV_SYN) < 0)
			die("error: evbit syn\n");

		if (ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS) < 0)
			die("error: evbit abs\n");

		if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_SLOT) < 0)
			die("error: abs slot\n");

		if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) < 0)
			die("error: abs track id\n");

		if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) < 0)
			die("error: abs x\n");

		if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) < 0)
			die("error: abs y\n");

		if (ioctl(uinput_fd, UI_DEV_CREATE) < 0)
			die("error: create\n");

		/* Open usbraw-device, communicated from udev through the fifo */
		usbraw_fd = open(devname, O_RDONLY);
		if (usbraw_fd < 0)
			die("error: open usbraw device");

		/* Enter input loop */
		while (1) {
			int state[5];
			int x[5];
			int y[5];
			int i;
			int need_syn;
			int n = read(usbraw_fd, data, sizeof(data));
			if (n < 0)
				break; /* Unplug? */
			if (n != sizeof(data)) {
				syslog(LOG_ERR, "Short input : %d\n", n);
				continue;
			}

			/* Decode raw data */
			state[0] = (data[7] & 1) != 0;
			x[0] = data[2] * 256 + data[3];
			y[0] = data[4] * 256 + data[5];
			for (i = 0; i < 4; i++) {
				state[i + 1] = (data[7] & (2 << i)) != 0;
				x[i + 1] = data[i * 2 + 8] * 256 + data[i * 2 + 9];
				y[i + 1] = data[i * 2 + 10] * 256 + data[i * 2 + 11];
			}

			/* Send input events */
			need_syn = 0;
			for (i = 0; i < 5; i++) {
				if (state[i]) {
					send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, i);
					send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, i);
					send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_X, x[i]);
					send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, y[i]);
					need_syn = 1;
				} else if (prev_state[i]) {
					send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, i);
					send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
					need_syn = 1;
				}
				prev_state[i] = state[i];
			}
			if (need_syn) {
				send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
			}
		}
		close(usbraw_fd);
		ioctl(uinput_fd, UI_DEV_DESTROY);
		close(uinput_fd);
	}
	syslog(LOG_INFO, "Exiting\n");
	return 0;
}
