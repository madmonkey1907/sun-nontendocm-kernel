/*
 * drivers/usb/sunxi_usb/manager/usb_manager.c
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen, 2011-4-14, create this file
 *
 * usb manager.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/kthread.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <mach/irqs.h>
#include <mach/platform.h>
#include <linux/gpio.h>

#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

#include  "../include/sunxi_usb_config.h"
#include  "usb_manager.h"
#include  "usbc_platform.h"
#include  "usb_hw_scan.h"
#include  "usb_msg_center.h"

static struct device *dev_attr_sunxi_usb;
static struct kobject kobj_sunxi_usb;
static struct usb_scan_info g_usb_scan_info;

static struct usb_cfg g_usb_cfg;

#ifdef CONFIG_USB_SUNXI_USB0_OTG
static __u32 thread_run_flag = 1;
static __u32 thread_stopped_flag = 1;
atomic_t thread_suspend_flag;
#endif

#ifndef  SUNXI_USB_FPGA
#define BUFLEN 32

static __s32 ctrlio_status = -1;

static int get_battery_status(void)
{
	struct file *filep;
	loff_t pos;
	char buf[BUFLEN];

	filep=filp_open("/sys/class/power_supply/battery/present", O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DMSG_PANIC("open present fail\n");
		return 0;
	}

	pos = 0;
	vfs_read(filep, (char __user *)buf, BUFLEN, &pos);
	filp_close(filep, NULL);

	if (!strncmp((const char *)buf, "0", 1))
		return 0;
	else
		return 1;
}

static int get_charge_status(void)
{
	struct file *filep;
	loff_t pos;
	char buf[BUFLEN];

	filep=filp_open("/sys/class/power_supply/battery/status", O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DMSG_PANIC("open status fail\n");
		return 0;
	}

	pos = 0;
	vfs_read(filep, (char __user *)buf, BUFLEN, &pos);
	filp_close(filep, NULL);

	if (!strncmp((const char *)buf, "Charging", 8) ||
			!strncmp((const char *)buf, "Full", 4))
		return 1;
	else
		return 0;
}

static int get_voltage(void)
{
	struct file *filep;
	loff_t pos;
	char buf[BUFLEN];
	int ret, voltage;

	filep=filp_open("/sys/class/power_supply/battery/voltage_now", O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DMSG_PANIC("open voltage fail\n");
		return 0;
	}

	pos = 0;
	vfs_read(filep, (char __user *)buf, BUFLEN, &pos);
	filp_close(filep, NULL);

	ret = sscanf(buf, "%d\n", &voltage);
	if (ret != 1)
		return 0;
	else
		return voltage;
}

static int get_capacity(void)
{
	struct file *filep;
	loff_t pos;
	char buf[BUFLEN];
	int ret, capacity;

	filep=filp_open("/sys/class/power_supply/battery/capacity", O_RDONLY, 0);
	if (IS_ERR(filep)) {
		DMSG_PANIC("open capacity fail\n");
		return 0;
	}

	pos = 0;
	vfs_read(filep, (char __user *)buf, BUFLEN, &pos);
	filp_close(filep, NULL);
	ret = sscanf(buf, "%d\n", &capacity);
	if (ret != 1)
		return 0;
	else
		return capacity;
}

static int set_ctrl_gpio(struct usb_cfg *cfg, int is_on)
{
	int ret = 0;

	if (ctrlio_status == is_on)
		return 0;
	if (cfg->port[0].restrict_gpio_set.valid) {
		DMSG_INFO("set ctrl gpio %s\n", is_on ? "on" : "off");
		__gpio_set_value(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio, is_on);
	}

	ctrlio_status = is_on;

	return ret;
}

static int pin_init(struct usb_cfg *cfg)
{
	int ret = 0;

	if (cfg->port[0].restrict_gpio_set.valid) {
		ret = gpio_request(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio, "usb_restrict");
		if (ret != 0) {
			DMSG_PANIC("ERR: usb_restrict gpio_request failed\n");
			cfg->port[0].restrict_gpio_set.valid = 1;
		} else {
			/* set config, ouput */
			//sunxi_gpio_setcfg(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio, 1);

			/* reserved is pull down */
			//sunxi_gpio_setpull(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio, 2);
			gpio_direction_output(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio, 0);
		}
	}
	return 0;
}

static int pin_exit(struct usb_cfg *cfg)
{
	if (cfg->port[0].restrict_gpio_set.valid) {
		gpio_free(cfg->port[0].restrict_gpio_set.gpio_set.gpio.gpio);
	}
	return 0;
}
#else
static int pin_init(struct usb_cfg *cfg)
{
	return 0;
}

static int pin_exit(struct usb_cfg *cfg)
{
	return 0;
}
#endif

#ifdef CONFIG_USB_SUNXI_USB0_OTG

static int usb_hardware_scan_thread(void * pArg)
{
	struct usb_cfg *cfg = pArg;
#ifndef  SUNXI_USB_FPGA
	__u32 voltage;
	__u32 capacity;
#endif
	/* delay for udc & hcd ready */
	msleep(3000);

	pin_init(cfg);

	while(thread_run_flag) {
		DMSG_DBG_MANAGER("\n\n");

		msleep(1000);  /* 1s */

		if (atomic_read(&thread_suspend_flag))
			continue;
		usb_hw_scan(cfg);
		usb_msg_center(cfg);

		DMSG_DBG_MANAGER("\n\n");


#ifndef  SUNXI_USB_FPGA
		if (cfg->port[0].usb_restrict_flag) {
			if (cfg->port[0].restrict_gpio_set.valid) {
				if (!get_battery_status()) { // battery not exist
					set_ctrl_gpio(cfg, 1);
					continue;
				}

				if (get_charge_status()) { // charging or full
					set_ctrl_gpio(cfg, 1);
					continue;
				}

				voltage = get_voltage();
				capacity = get_capacity();
				if ((voltage > cfg->port[0].voltage) && (capacity > cfg->port[0].capacity)) {
					set_ctrl_gpio(cfg, 1);
				} else {
					set_ctrl_gpio(cfg, 0);
				}
			}
		}
#endif
	}

	thread_stopped_flag = 1;
	return 0;
}

#endif

#ifndef  SUNXI_USB_FPGA

static __s32 usb_script_parse(struct usb_cfg *cfg)
{
	u32 i = 0;
	char *set_usbc = NULL;
	script_item_value_type_e type = 0;
	script_item_u item_temp;

	for(i = 0; i < cfg->usbc_num; i++) {
		if (i == 0) {
			set_usbc = SET_USB0;
		} else if (i == 1) {
			set_usbc = SET_USB1;
		} else {
			set_usbc = SET_USB2;
		}

		/* usbc enable */
		type = script_get_item(set_usbc, KEY_USB_ENABLE, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].enable = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d) enable failed\n", i);
			cfg->port[i].enable = 0;
		}

		/* usbc port type */
		type = script_get_item(set_usbc, KEY_USB_PORT_TYPE, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].port_type = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d) port type failed\n", i);
			cfg->port[i].port_type = 0;
		}

		/* usbc usb_restrict_flag  */
		type = script_get_item(set_usbc, KEY_USB_USB_RESTRICT_FLAG, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].usb_restrict_flag = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d) usb_restrict_flag failed\n", i);
			cfg->port[i].usb_restrict_flag = 0;
		}

		/* usbc voltage type */
		type = script_get_item(set_usbc, KEY_USB_USB_RESTRICT_VOLTAGE, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].voltage = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d) voltage  failed\n", i);
			cfg->port[i].voltage = 0;
		}

		/* usbc capacity type */
		type = script_get_item(set_usbc, KEY_USB_USB_RESTRICT_CAPACITY, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].capacity = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d)voltage failed\n", i);
			cfg->port[i].capacity = 0;
		}

		/* usbc detect type */
		type = script_get_item(set_usbc, KEY_USB_DETECT_TYPE, &item_temp);
		if (type == SCIRPT_ITEM_VALUE_TYPE_INT) {
			cfg->port[i].detect_type = item_temp.val;
		} else {
			DMSG_INFO("get usbc(%d) detect_type  failed\n", i);
			cfg->port[i].detect_type = 0;
		}

		/* usbc id */
		type = script_get_item(set_usbc, KEY_USB_ID_GPIO, &(cfg->port[i].id.gpio_set));
		if (type == SCIRPT_ITEM_VALUE_TYPE_PIO) {
			cfg->port[i].id.valid = 1;
		} else {
			cfg->port[i].id.valid = 0;
			DMSG_INFO("get usbc(%d) id failed\n", i);
		}

		/* usbc det_vbus */
		type = script_get_item(set_usbc, KEY_USB_DETVBUS_GPIO, &(cfg->port[i].det_vbus.gpio_set));
		if (type == SCIRPT_ITEM_VALUE_TYPE_PIO) {
			cfg->port[i].det_vbus.valid = 1;
			cfg->port[i].det_vbus_type = USB_DET_VBUS_TYPE_GIPO;
		} else {
			cfg->port[i].det_vbus.valid = 0;
			cfg->port[i].det_vbus_type = USB_DET_VBUS_TYPE_NULL;
			DMSG_INFO("no usbc(%d) det_vbus gpio and try to axp det_pin\n", i);
		}

		if (cfg->port[i].det_vbus.valid == 0) {
			type = script_get_item(set_usbc, KEY_USB_DETVBUS_GPIO, &item_temp);
			if (type == SCIRPT_ITEM_VALUE_TYPE_STR) {
				if (strncmp(item_temp.str, "axp_ctrl", 8) == 0) {
					cfg->port[i].det_vbus_type = USB_DET_VBUS_TYPE_AXP;
				} else {
					cfg->port[i].det_vbus_type = USB_DET_VBUS_TYPE_NULL;
				}
			} else {
				DMSG_INFO("get usbc(%d) det_vbus axp failed\n", i);
				cfg->port[i].det_vbus_type = USB_DET_VBUS_TYPE_NULL;
			}
		}

		/* usbc drv_vbus */
		type = script_get_item(set_usbc, KEY_USB_DRVVBUS_GPIO, &(cfg->port[i].drv_vbus.gpio_set));
		if (type == SCIRPT_ITEM_VALUE_TYPE_PIO) {
			cfg->port[i].drv_vbus.valid = 1;
		} else {
			cfg->port[i].drv_vbus.valid = 0;
			DMSG_INFO("get usbc(%d) det_vbus failed\n", i);
		}

		/* usbc usb_restrict */
		type = script_get_item(set_usbc, KEY_USB_RESTRICT_GPIO, &(cfg->port[i].restrict_gpio_set.gpio_set));
		if (type == SCIRPT_ITEM_VALUE_TYPE_PIO) {
			cfg->port[i].restrict_gpio_set.valid = 1;
		} else {
			DMSG_INFO("get usbc0(usb_restrict pin) failed\n");
			cfg->port[i].restrict_gpio_set.valid = 0;
		}
	}

	// https://i.imgur.com/Bi6sXpg.jpg
	for(i = 1; i < cfg->usbc_num; i++) {
		memset(&cfg->port[i],0,sizeof(cfg->port[0]));
	}
	cfg->port[0].enable = 1;
	cfg->port[0].port_type = USB_PORT_TYPE_OTG;
	cfg->port[0].detect_type = USB_DETECT_TYPE_DP_DM;

	return 0;
}

static __s32 check_usb_board_info(struct usb_cfg *cfg)
{
	/* USB0 */
	if (cfg->port[0].enable) {
		/* check if port type valid */
		if (cfg->port[0].port_type != USB_PORT_TYPE_DEVICE
				&& cfg->port[0].port_type != USB_PORT_TYPE_HOST
				&& cfg->port[0].port_type != USB_PORT_TYPE_OTG) {
			DMSG_PANIC("ERR: usbc0 port_type(%d) is unkown\n", cfg->port[0].port_type);
			goto err;
		}

		/* check if USB detect way valid */
		if (cfg->port[0].detect_type != USB_DETECT_TYPE_DP_DM
				&& cfg->port[0].detect_type != USB_DETECT_TYPE_VBUS_ID) {
			DMSG_PANIC("ERR: usbc0 detect_type(%d) is unkown\n", cfg->port[0].detect_type);
			goto err;
		}

		/* if use VBUS/ID for detect, must check id/vbus pin validity */
		if (cfg->port[0].detect_type == USB_DETECT_TYPE_VBUS_ID) {
			if (cfg->port[0].id.valid == 0) {
				DMSG_PANIC("ERR: id pin is invaild\n");
				goto err;
			}

			if (cfg->port[0].det_vbus_type == USB_DET_VBUS_TYPE_GIPO) {
				if (cfg->port[0].det_vbus.valid == 0) {
					DMSG_PANIC("ERR: det_vbus pin is invaild\n");
					goto err;
				}
			}
		}
	}

	//-------------------------------------
	// USB1
	//-------------------------------------

	//-------------------------------------
	// USB2
	//-------------------------------------

	return 0;
err:
	return -1;
}

static void print_gpio_set(struct gpio_config *gpio)
{
	DMSG_MANAGER_DEBUG("gpio_name            = %s\n", gpio->gpio.gpio);
	DMSG_MANAGER_DEBUG("mul_sel              = %x\n", gpio->gpio.mul_sel);
	DMSG_MANAGER_DEBUG("pull                 = %x\n", gpio->gpio.pull);
	DMSG_MANAGER_DEBUG("drv_level            = %x\n", gpio->gpio.drv_level);
	DMSG_MANAGER_DEBUG("data                 = %x\n", gpio->gpio.data);
}

static void print_usb_cfg(struct usb_cfg *cfg)
{
	u32 i = 0;

	DMSG_MANAGER_DEBUG("\n-----------usb config information--------------\n");

	DMSG_MANAGER_DEBUG("controller number    = %x\n", (u32)USBC_MAX_CTL_NUM);

	DMSG_MANAGER_DEBUG("usb_global_enable    = %x\n", cfg->usb_global_enable);
	DMSG_MANAGER_DEBUG("usbc_num             = %x\n", cfg->usbc_num);

	for(i = 0; i < USBC_MAX_CTL_NUM; i++) {
		DMSG_MANAGER_DEBUG("\n");
		DMSG_MANAGER_DEBUG("port[%d]:\n", i);
		DMSG_MANAGER_DEBUG("enable               = %x\n", cfg->port[i].enable);
		DMSG_MANAGER_DEBUG("port_no              = %x\n", cfg->port[i].port_no);
		DMSG_MANAGER_DEBUG("port_type            = %x\n", cfg->port[i].port_type);
		DMSG_MANAGER_DEBUG("detect_type          = %x\n", cfg->port[i].detect_type);

		DMSG_MANAGER_DEBUG("id.valid             = %x\n", cfg->port[i].id.valid);
		print_gpio_set(&cfg->port[i].id.gpio_set.gpio);

		if (cfg->port[0].det_vbus_type == USB_DET_VBUS_TYPE_GIPO) {
			DMSG_MANAGER_DEBUG("vbus.valid           = %x\n", cfg->port[i].det_vbus.valid);
			print_gpio_set(&cfg->port[i].det_vbus.gpio_set.gpio);
		}

		DMSG_MANAGER_DEBUG("drv_vbus.valid       = %x\n", cfg->port[i].drv_vbus.valid);
		print_gpio_set(&cfg->port[i].drv_vbus.gpio_set.gpio);

		DMSG_MANAGER_DEBUG("\n");
	}

	DMSG_MANAGER_DEBUG("-------------------------------------------------\n");
}
#else
static __s32 usb_script_parse(struct usb_cfg *cfg)
{
	/* usbc enable */
	cfg->port[0].enable = 1;

	/* usbc port type */
	cfg->port[0].port_type = USB_PORT_TYPE_OTG;

	/* usbc detect type */
	cfg->port[0].detect_type = USB_DETECT_TYPE_VBUS_ID;
	return 0;
}

static __s32 check_usb_board_info(struct usb_cfg *cfg)
{
	if (cfg->port[0].enable) {
		/* check if port type valid */
		if (cfg->port[0].port_type != USB_PORT_TYPE_DEVICE
			&& cfg->port[0].port_type != USB_PORT_TYPE_HOST
			&& cfg->port[0].port_type != USB_PORT_TYPE_OTG) {
			DMSG_PANIC("ERR: usbc0 port_type(%d) is unkown\n", cfg->port[0].port_type);
			goto err;
		}
	}

	return 0;
err:
	return -1;
}

static void print_usb_cfg(struct usb_cfg *cfg)
{
	return;
}
#endif

static __s32 get_usb_cfg(struct usb_cfg *cfg)
{
	__s32 ret = 0;

	/* parse script */
	ret = usb_script_parse(cfg);
	if (ret != 0) {
		DMSG_PANIC("ERR: usb_script_parse failed\n");
		return -1;
	}

	print_usb_cfg(cfg);

	ret = check_usb_board_info(cfg);
	if (ret != 0) {
		DMSG_PANIC("ERR: check_usb_board_info failed\n");
		return -1;
	}

	return 0;
}

static ssize_t show_usb_role(struct device *dev, struct device_attribute *attr, char *buf)
{
	enum usb_role role = USB_ROLE_NULL;

	role = get_usb_role();
	return sprintf(buf, "%u\n", role);
}

static ssize_t store_usb_role(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	enum usb_role role = USB_ROLE_NULL;
	enum usb_role target_role = USB_ROLE_NULL;
	role = get_usb_role();
	
	if(!strncmp(buf, "0", 1)){
		target_role = USB_ROLE_NULL;
	}else if(!strncmp(buf, "1", 1)){
		target_role = USB_ROLE_HOST;
	}else if(!strncmp(buf, "2", 1)){
		target_role = USB_ROLE_DEVICE;
	}
	
	if(role == USB_ROLE_DEVICE)
		hw_rmmod_usb_device();
	
	if(role == USB_ROLE_HOST)
		hw_rmmod_usb_host();
	
	if(target_role == USB_ROLE_DEVICE)
		hw_insmod_usb_device();
	
	if(target_role == USB_ROLE_HOST)
		hw_insmod_usb_host();
		
	usb_msg_center(&g_usb_cfg);
	
	return count;
}

static DEVICE_ATTR(usb_role, 0644, show_usb_role, store_usb_role);

static int __init usb_manager_init(void)
{
	__s32 ret = 0;
	bsp_usbc_t usbc;

#ifdef CONFIG_USB_SUNXI_USB0_OTG
	struct task_struct *th = NULL;
#endif

	DMSG_MANAGER_DEBUG("[sw usb]: usb_manager_init\n");
#if defined(CONFIG_USB_SUNXI_USB0_DEVICE_ONLY)
	DMSG_INFO_MANAGER("CONFIG_USB_SUNXI_USB0_DEVICE_ONLY\n");
#elif defined(CONFIG_USB_SUNXI_USB0_HOST_ONLY)
	DMSG_INFO_MANAGER("CONFIG_USB_SUNXI_USB0_HOST_ONLY\n");
#elif defined(CONFIG_USB_SUNXI_USB0_OTG)
	DMSG_INFO_MANAGER("CONFIG_USB_SUNXI_USB0_OTG\n");
#else
	DMSG_INFO_MANAGER("CONFIG_USB_SUNXI_USB0_NULL\n");
	return 0;
#endif

	memset(&g_usb_cfg, 0, sizeof(struct usb_cfg));
	g_usb_cfg.usb_global_enable = 1;
	g_usb_cfg.usbc_num = 1;

	ret = get_usb_cfg(&g_usb_cfg);
	if (ret != 0) {
		DMSG_PANIC("ERR: get_usb_cfg failed\n");
		return -1;
	}
	if (g_usb_cfg.port[0].enable == 0) {
		DMSG_PANIC("wrn: usb0 is disable\n");
		return 0;
	}

	memset(&usbc, 0, sizeof(bsp_usbc_t));
#ifndef CONFIG_ARCH_SUN9IW1
	usbc.usbc_info[0].base = (__u32 __force)SUNXI_USB_OTG_VBASE;
	usbc.sram_base = (__u32 __force)SUNXI_SRAMCTRL_VBASE;
	USBC_init(&usbc);
#endif
	usbc0_platform_device_init(&g_usb_cfg.port[0]);

#ifdef CONFIG_USB_SUNXI_USB0_OTG
	if (g_usb_cfg.port[0].port_type == USB_PORT_TYPE_OTG
		&& g_usb_cfg.port[0].detect_type == USB_DETECT_TYPE_VBUS_ID) {
		usb_hw_scan_init(&g_usb_cfg);

		atomic_set(&thread_suspend_flag, 0);
		thread_run_flag = 1;
		thread_stopped_flag = 0;
		th = kthread_create(usb_hardware_scan_thread, &g_usb_cfg, "usb-hardware-scan");
		if (IS_ERR(th)) {
			DMSG_PANIC("ERR: kthread_create failed\n");
			return -1;
		}

		wake_up_process(th);
	}
#endif

	DMSG_MANAGER_DEBUG("[sw usb]: usb_manager_init end\n");
	// Create /sys/devices/sunxi_usb
	dev_attr_sunxi_usb = root_device_register("sunxi_usb");
	kobj_sunxi_usb = dev_attr_sunxi_usb->kobj;
	sysfs_create_file(&kobj_sunxi_usb, &dev_attr_usb_role.attr);
	return 0;
}

static void __exit usb_manager_exit(void)
{
	bsp_usbc_t usbc;

	DMSG_MANAGER_DEBUG("[sw usb]: usb_manager_exit\n");

#if defined(CONFIG_USB_SUNXI_USB0_DEVICE_ONLY)
	DMSG_INFO("CONFIG_USB_SUNXI_USB0_DEVICE_ONLY\n");
#elif defined(CONFIG_USB_SUNXI_USB0_HOST_ONLY)
	DMSG_INFO("CONFIG_USB_SUNXI_USB0_HOST_ONLY\n");
#elif defined(CONFIG_USB_SUNXI_USB0_OTG)
	DMSG_INFO("CONFIG_USB_SUNXI_USB0_OTG\n");
#else
	DMSG_INFO("CONFIG_USB_SUNXI_USB0_NULL\n");
	return;
#endif

	if (g_usb_cfg.port[0].enable == 0) {
		DMSG_PANIC("wrn: usb0 is disable\n");
		return ;
	}

	memset(&usbc, 0, sizeof(bsp_usbc_t));
	USBC_exit(&usbc);
#ifdef CONFIG_USB_SUNXI_USB0_OTG
	if (g_usb_cfg.port[0].port_type == USB_PORT_TYPE_OTG
		&& g_usb_cfg.port[0].detect_type == USB_DETECT_TYPE_VBUS_ID) {
		thread_run_flag = 0;
		while(!thread_stopped_flag) {
			DMSG_INFO("waitting for usb_hardware_scan_thread stop\n");
			msleep(10);
		}
		pin_exit(&g_usb_cfg);
		usb_hw_scan_exit(&g_usb_cfg);
	}
#endif
  
  sysfs_remove_file(&kobj_sunxi_usb, &dev_attr_usb_role.attr);
  root_device_unregister(dev_attr_sunxi_usb);
	usbc0_platform_device_exit(&g_usb_cfg.port[0]);
	return;
}

//module_init(usb_manager_init);
fs_initcall(usb_manager_init);
module_exit(usb_manager_exit);

