/*
 * drivers/usb/sunxi_usb/manager/usb_msg_center.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen, 2011-4-14, create this file
 *
 * usb msg distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef  __USB_MSG_CENTER_H__
#define  __USB_MSG_CENTER_H__

/* usb role mode */
typedef enum usb_role{
	USB_ROLE_NULL = 0,
	USB_ROLE_HOST,
	USB_ROLE_DEVICE,
}usb_role_t;

typedef struct usb_msg{
	u8  app_drv_null;		//not install any driver
	u8  app_insmod_host;
	u8  app_rmmod_host;
	u8  app_insmod_device;
	u8  app_rmmod_device;

	u8  hw_insmod_host;
	u8  hw_rmmod_host;
	u8  hw_insmod_device;
	u8  hw_rmmod_device;
}usb_msg_t;

typedef struct usb_msg_center_info{
	struct usb_cfg *cfg;

	struct usb_msg msg;
	enum usb_role role;

	u32 skip;			//if skip, not enter msg process
	//mainly to omit invalid msg
}usb_msg_center_info_t;

void hw_insmod_usb_host(void);
void hw_rmmod_usb_host(void);
void hw_insmod_usb_device(void);
void hw_rmmod_usb_device(void);

enum usb_role get_usb_role(void);
void _set_usb_role(enum usb_role role);
void usb_msg_center(struct usb_cfg *cfg);

s32 usb_msg_center_init(struct usb_cfg *cfg);
s32 usb_msg_center_exit(struct usb_cfg *cfg);

#endif   //__USB_MSG_CENTER_H__

