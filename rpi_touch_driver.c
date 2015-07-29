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
#include <libudev.h>
#include <locale.h>
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

#define croak(str, ...) do { \
        perror(str); \
	syslog(LOG_ERR, str, ##__VA_ARGS__); \
    } while(0)

int uinput_fd;
int usbraw_fd;
int fifo_fd;

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
		case ABS_X:
			strcpy(ccode, "ABS_X");
			break;
		case ABS_MT_POSITION_X:
			strcpy(ccode, "ABS_MT_POSITION_X");
			break;
		case ABS_Y:
			strcpy(ccode, "ABS_Y");
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

/* Open hidraw device and translate events until failure */
void handle_hidraw_device(char *path)
{
	struct uinput_user_dev device;
	unsigned char data[25];
	int prev_state[5];

	/* Open usbraw-device, communicated from udev through the fifo */
	usbraw_fd = open(path, O_RDONLY);
	if (usbraw_fd < 0) {
		croak("error: open usbraw device : %s", path);
		return;
	}
	syslog(LOG_INFO, "Starting : %s", path);

	/* Iniialize uinput */
	memset(&device, 0, sizeof(device));
	strcpy(device.name, "RPI_TOUCH_uinput");
	device.id.bustype = BUS_VIRTUAL;
	device.id.vendor = 1;
	device.id.product = 1;
	device.id.version = 1;
	device.absmax[ABS_X] = 800;
	device.absmax[ABS_Y] = 480;
	device.absmax[ABS_MT_POSITION_X] = 800;
	device.absmax[ABS_MT_POSITION_Y] = 480;
	device.absmax[ABS_MT_SLOT] = 5;
	device.absmax[ABS_MT_TRACKING_ID] = 5;

	uinput_fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
	if (uinput_fd < 0) {
		uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
		if (uinput_fd < 0)
			die("error: open uinput device");
	}

	if (write(uinput_fd, &device, sizeof(device)) != sizeof(device))
		die("error: setup device");

	if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0)
		die("error: evbit key\n");

	if (ioctl(uinput_fd, UI_SET_EVBIT, EV_SYN) < 0)
		die("error: evbit syn\n");

	if (ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS) < 0)
		die("error: evbit abs\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_X) < 0)
		die("error: abs x\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_Y) < 0)
		die("error: abs y\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_SLOT) < 0)
		die("error: abs slot\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID) < 0)
		die("error: abs track id\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X) < 0)
		die("error: abs mt x\n");

	if (ioctl(uinput_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y) < 0)
		die("error: abs mt y\n");

	if (ioctl(uinput_fd, UI_SET_KEYBIT, BTN_TOUCH) < 0)
		die("error: evbit touch\n");

	if (ioctl(uinput_fd, UI_DEV_CREATE) < 0)
		die("error: create\n");


	/* Enter input loop */
	while (1) {
		int state[5];
		int x[5];
		int y[5];
		int i;
		int n = read(usbraw_fd, data, sizeof(data));
		if (n < 0)
			break; /* Unplug? */
		if (n != sizeof(data)) {
			croak("Short input : %d\n", n);
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
		for (i = 0; i < 5; i++) {
			if (state[i]) {
				send_uevent(uinput_fd, EV_ABS, ABS_X, x[i]);
				send_uevent(uinput_fd, EV_ABS, ABS_Y, y[i]);
				send_uevent(uinput_fd, EV_KEY, BTN_TOUCH, 1);
				break;
			}
		}
		if (i == 5)
			send_uevent(uinput_fd, EV_KEY, BTN_TOUCH, 0);
		for (i = 0; i < 5; i++) {
			if (state[i]) {
				send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, i);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, i);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_X, x[i]);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, y[i]);
			} else if (prev_state[i]) {
				send_uevent(uinput_fd, EV_ABS, ABS_MT_SLOT, i);
				send_uevent(uinput_fd, EV_ABS, ABS_MT_TRACKING_ID, -1);
			}
			prev_state[i] = state[i];
		}
		send_uevent(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
	}
	if (usbraw_fd >= 0)
		close(usbraw_fd);
	if (uinput_fd >= 0) {
		ioctl(uinput_fd, UI_DEV_DESTROY);
		close(uinput_fd);
	}
}

char rpi_dev[64];
char *find_rpi_touch()
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;

	/* Create the udev object */
	udev = udev_new();
	if (!udev)
		die("Can't create udev");

	/* Create a list of the devices in the 'hidraw' subsystem. */
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "hidraw");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	/* For each item enumerated, print out its information.
	   udev_list_entry_foreach is a macro which expands to
	   a loop. The loop will be executed for each member in
	   devices, setting dev_list_entry to a list entry
	   which contains the device's path in /sys. */
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;

		/* Get the filename of the /sys entry for the device
		   and create a udev_device object (dev) representing it */
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		strncpy(rpi_dev, udev_device_get_devnode(dev), 64);

		/* The device pointed to by dev contains information about
		   the hidraw device. In order to get information about the
		   USB device, get the parent device with the
		   subsystem/devtype pair of "usb"/"usb_device". This will
		   be several levels up the tree, but the function will find
		   it.*/
		dev = udev_device_get_parent_with_subsystem_devtype(
		       dev, "usb", "usb_device");
		if (!dev)
			die("Unable to find parent usb device.");

		if (strcmp(udev_device_get_sysattr_value(dev, "idVendor"), "0eef")
		 || strcmp(udev_device_get_sysattr_value(dev, "idProduct"), "0005")) {
			rpi_dev[0] = 0;
		}
		udev_device_unref(dev);
		if (rpi_dev[0] != 0)
			break;
	}
	/* Free the enumerator object */
	udev_enumerate_unref(enumerate);

	udev_unref(udev);

	return rpi_dev[0] == 0 ? NULL : rpi_dev;
}

int main(int argc, char **argv)
{
	int numc;
	struct udev *udev;
	struct udev_device *dev;
	struct udev_monitor *mon;
	char *devname;

	/* Syslog */
	openlog("rpi_touch_driver", LOG_NOWAIT, LOG_DAEMON);
	/* Deamonize? */
	if (!(argc > 1 && strlen(argv[1]) > 1 && !strcmp(argv[1], "-d"))) {
		if (daemon(0, 0))
			die("error: daemonize\n");
	}

	/* Create the udev object */
	udev = udev_new();
	if (!udev)
		die("Can't create udev");

	mon = udev_monitor_new_from_netlink(udev, "udev");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", NULL);
	udev_monitor_enable_receiving(mon);

	devname = find_rpi_touch();
	if (devname != NULL)
		handle_hidraw_device(devname);

	while (1)
	{
		/* Block waiting for an event */
		dev = udev_monitor_receive_device(mon);
		/* Release immediately */
		udev_device_unref(dev);
		devname = find_rpi_touch();
		if (devname != NULL)
			handle_hidraw_device(devname);
	}
	
	udev_unref(udev);
	return 0;
}
