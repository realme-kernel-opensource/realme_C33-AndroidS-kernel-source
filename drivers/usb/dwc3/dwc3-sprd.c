/**
 * dwc3-sprd.c - Spreadtrum DWC3 Specific Glue layer
 *
 * Copyright (c) 2018 Spreadtrum Co., Ltd.
 *		http://www.spreadtrum.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/phy.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/wait.h>
#include <linux/extcon.h>
#include <linux/regmap.h>
#include <linux/usb/sprd_usbm.h>

#include "core.h"
#include "gadget.h"
#include "io.h"

struct dwc3_sprd {
	struct device		*dev;
	void __iomem		*base;
	struct platform_device	*dwc3;
	int			irq;

	struct clk		*core_clk;
	struct clk		*ref_clk;
	struct clk		*susp_clk;
	struct clk		*ipa_usb_ref_clk;
	struct clk		*ipa_usb_ref_parent;
	struct clk		*ipa_usb_ref_default;
	struct clk		*ipa_dpu1_clk;
	struct clk		*ipa_dptx_clk;
	struct clk		*ipa_tca_clk;
	struct clk		*ipa_usb31pll_clk;

	struct usb_phy		*hs_phy;
	struct usb_phy		*ss_phy;
	struct extcon_dev	*edev;
	struct extcon_dev	*id_edev;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;
	struct notifier_block	audio_nb;
	struct regulator	*vbus;

	bool			hibernate_en;
	enum usb_dr_mode	dr_mode;
	enum usb_dr_mode	wq_mode;

	struct wakeup_source		*wake_lock;
	spinlock_t		lock;

	bool			vbus_active;
	bool			block_active;
	bool			charging_mode;
	bool			suspend;
	bool			is_audio_dev;
	wait_queue_head_t	wait;
	struct work_struct	work;
};

#define DWC3_SUSPEND_COUNT	100
#define DWC3_UDC_START_COUNT	1000
#define DWC3_START_TIMEOUT	200
#define DWC3_EXTCON_DELAY	1000

static int boot_charging;
static bool boot_calibration;

static int dwc3_sprd_suspend_child(struct device *dev, void *data);
static int dwc3_sprd_resume_child(struct device *dev, void *data);

static ssize_t maximum_speed_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;

	if (!sdwc)
		return -EINVAL;

	dwc = platform_get_drvdata(sdwc->dwc3);
	if (!dwc)
		return -EINVAL;

	return sprintf(buf, "%s\n", usb_speed_string(dwc->gadget.max_speed));
}

static ssize_t maximum_speed_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;
	u32 max_speed;

	if (!sdwc)
		return -EINVAL;

	if (kstrtouint(buf, 0, &max_speed))
		return -EINVAL;

	if (max_speed <= USB_SPEED_UNKNOWN || max_speed > USB_SPEED_SUPER_PLUS)
		return -EINVAL;

	dwc = platform_get_drvdata(sdwc->dwc3);
	if (!dwc)
		return -EINVAL;

	dwc->gadget.max_speed = max_speed;
	return size;
}
static DEVICE_ATTR_RW(maximum_speed);

static ssize_t u1u2_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;

	if (!sdwc)
		return -EINVAL;

	dwc = platform_get_drvdata(sdwc->dwc3);
	if (!dwc)
		return -EINVAL;
	//todo fixme
/*
	if (dwc->u1u2_enable)
		return sprintf(buf, "enabled\n");
*/
	return sprintf(buf, "disabled\n");
}

static ssize_t u1u2_enable_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;

	if (!sdwc)
		return -EINVAL;

	dwc = platform_get_drvdata(sdwc->dwc3);
	if (!dwc)
		return -EINVAL;
	//todo fixme
/*	if (!strncmp(buf, "enable", 6))
		dwc->u1u2_enable = true;
	else if (!strncmp(buf, "disable", 7))
		dwc->u1u2_enable = false;
	else
		return -EINVAL;
*/
	return size;
}
static DEVICE_ATTR_RW(u1u2_enable);

static ssize_t current_speed_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);
	struct dwc3 *dwc;

	if (!sdwc)
		return -EINVAL;

	dwc = platform_get_drvdata(sdwc->dwc3);
	if (!dwc)
		return -EINVAL;

	return sprintf(buf, "%s\n", usb_speed_string(dwc->gadget.speed));
}
static DEVICE_ATTR_RO(current_speed);

static struct attribute *dwc3_sprd_attrs[] = {
	&dev_attr_u1u2_enable.attr,
	&dev_attr_maximum_speed.attr,
	&dev_attr_current_speed.attr,
	NULL
};
ATTRIBUTE_GROUPS(dwc3_sprd);

static void dwc3_flush_all_events(struct dwc3_sprd *sdwc)
{
	struct dwc3 *dwc = platform_get_drvdata(sdwc->dwc3);
	struct dwc3_event_buffer *evt;
	unsigned long flags;
	u32 reg;

	/* Skip remaining events on disconnect */
	spin_lock_irqsave(&dwc->lock, flags);

	reg = readl(dwc->regs + DWC3_GEVNTSIZ(0) - DWC3_GLOBALS_REGS_START);
	reg |= DWC3_GEVNTSIZ_INTMASK;
	writel(reg, dwc->regs + DWC3_GEVNTSIZ(0) - DWC3_GLOBALS_REGS_START);

	evt = dwc->ev_buf;
	evt->lpos = (evt->lpos + evt->count) % DWC3_EVENT_BUFFERS_SIZE;
	evt->count = 0;
	evt->flags &= ~DWC3_EVENT_PENDING;
	spin_unlock_irqrestore(&dwc->lock, flags);
}

static int dwc3_sprd_charger_mode(void)
{
	struct device_node *cmdline_node;
	const char *cmdline, *mode;
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);

	if (ret) {
		pr_err("Can't not parse bootargs\n");
		return 0;
	}

	mode = strstr(cmdline, "androidboot.mode=charger");

	if (mode)
		return 1;
	else
		return 0;

}

static int dwc3_sprd_calibration_mode(void)
{
	struct device_node *cmdline_node;
	const char *cmdline, *mode;
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmdline);

	if (ret) {
		pr_err("Can't not parse bootargs\n");
		return 0;
	}

	mode = strstr(cmdline, "androidboot.mode=cali");

	if (mode)
		return 1;
	else {
		mode = strstr(cmdline, "androidboot.mode=autotest");
		if (mode)
			return 1;
		else
			return 0;
	}
}

static int dwc3_sprd_is_udc_start(struct dwc3_sprd *sdwc)
{
	struct dwc3 *dwc = platform_get_drvdata(sdwc->dwc3);
	unsigned long flags;

	spin_lock_irqsave(&dwc->lock, flags);
	if (!dwc->gadget_driver) {
		spin_unlock_irqrestore(&dwc->lock, flags);
		return 0;
	}

	spin_unlock_irqrestore(&dwc->lock, flags);
	return 1;
}

static bool dwc3_sprd_is_connect_host(struct dwc3_sprd *sdwc)
{
	struct usb_phy *usb_phy = sdwc->ss_phy;
	enum usb_charger_type type = usb_phy->charger_detect(usb_phy);

	dev_info(sdwc->dev, "charger type:0x%x\n", type);
	if (type == SDP_TYPE || type == CDP_TYPE)
		return true;
	return false;
}

static int dwc3_sprd_start(struct dwc3_sprd *sdwc, enum usb_dr_mode mode)
{
	struct dwc3 *dwc = platform_get_drvdata(sdwc->dwc3);
	int ret, cnt = DWC3_SUSPEND_COUNT;
	unsigned long flags;
	unsigned long m_t_j = msecs_to_jiffies(5000);

	/*
	 * Need to notify the gadget state to change the usb charger state, when
	 * there are cables connected.
	 */
	if (mode == USB_DR_MODE_PERIPHERAL)
		usb_gadget_set_state(&dwc->gadget, USB_STATE_ATTACHED);

	/*
	 * If the charger type is not SDP or CDP type, it does not need to
	 * resume the dwc3 device, just charging.
	 */
	if ((mode == USB_DR_MODE_PERIPHERAL &&
	    !dwc3_sprd_is_connect_host(sdwc)) || boot_charging) {
		spin_lock_irqsave(&sdwc->lock, flags);
		sdwc->charging_mode = true;
		spin_unlock_irqrestore(&sdwc->lock, flags);

		dev_info(sdwc->dev,
			 "Don't need resume dwc3 device in charging mode!\n");
		return 0;
	}

	/*
	 * After dwc3 core initialization, the dwc3 core will enter suspend mode
	 * firstly. So if there is one cabel is always connecting from starting
	 * the system, then let the dwc3 core enter suspend firstly in case
	 * disturb the PM runtime.
	 */
	while (!pm_runtime_suspended(sdwc->dev) && (--cnt > 0))
		msleep(DWC3_START_TIMEOUT);

	if (cnt <= 0) {
		dev_err(sdwc->dev,
			"Wait for dwc3 core enter suspend failed!\n");
		return -EAGAIN;
	}

	/*
	 * We also need to wait for the udc start and set function for dwc3 from
	 * configfs. But especial for calibration mode, it need almost 200
	 * seconds to start UDC, thus we need to wait for at least 200 seconds
	 * here to work this situation.
	 *
	 * In host mode, we don't need to wait for the configuration from
	 * configfs.
	 */
	cnt = DWC3_UDC_START_COUNT;
	while ((mode == USB_DR_MODE_PERIPHERAL) &&
	       !dwc3_sprd_is_udc_start(sdwc) && (--cnt > 0))
		msleep(DWC3_START_TIMEOUT);

	if (cnt <= 0) {
		/*
		 * If it did not start the UDC from configfs, then we think
		 * system is in charging mode, which means it does not need to
		 * resume the dwc3 device.
		 */
		spin_lock_irqsave(&sdwc->lock, flags);
		sdwc->charging_mode = true;
		spin_unlock_irqrestore(&sdwc->lock, flags);

		dev_info(sdwc->dev,
			 "Don't resume dwc3 device in charging mode!\n");
		return 0;
	}

	if (mode == USB_DR_MODE_HOST) {
		ret = wait_event_timeout(sdwc->wait, !sdwc->suspend,
			 m_t_j);
		if (ret == 0)
			dev_err(sdwc->dev, "wait for dwc3 resume timeout!\n");

		/*
		 * Before enable OTG power, we should disable VBUS irq, in case
		 * extcon notifies the incorrect connecting events.
		 */

		/* If vbus is NULL, we should get vbus regulator again */
		if (!sdwc->vbus) {
			sdwc->vbus = devm_regulator_get(sdwc->dev, "vbus");
			if (IS_ERR(sdwc->vbus)) {
				dev_err(sdwc->dev, "unable to get vbus supply\n");
				sdwc->vbus = NULL;
			}
		}

		if (sdwc->vbus && !regulator_is_enabled(sdwc->vbus)) {
			ret = regulator_enable(sdwc->vbus);
			if (ret) {
				dev_err(sdwc->dev,
					"Failed to enable vbus: %d\n", ret);
				return ret;
			}
		}
	}

	dwc->dr_mode = (mode == USB_DR_MODE_HOST) ?
		DWC3_GCTL_PRTCAP_HOST : DWC3_GCTL_PRTCAP_DEVICE;

	ret = pm_runtime_get_sync(sdwc->dev);
	if (ret) {
		dev_err(sdwc->dev, "Resume dwc3 device failed!\n");
		return ret;
	}

	ret = device_for_each_child(sdwc->dev, NULL, dwc3_sprd_resume_child);
	if (ret) {
		pm_runtime_put_sync(sdwc->dev);
		dev_err(sdwc->dev, "Resume dwc3 core failed!\n");
		return ret;
	}

	/*
	 * We have resumed the dwc3 device to do enumeration, thus clear the
	 * charging mode flag.
	 */
	spin_lock_irqsave(&sdwc->lock, flags);
	sdwc->charging_mode = false;
	spin_unlock_irqrestore(&sdwc->lock, flags);

	return 0;
}

static int dwc3_sprd_stop(struct dwc3_sprd *sdwc, enum usb_dr_mode mode)
{
	struct dwc3 *dwc = platform_get_drvdata(sdwc->dwc3);
	bool charging_only = false;
	unsigned long flags;
	int ret;

	if (mode == USB_DR_MODE_PERIPHERAL)
		usb_gadget_set_state(&dwc->gadget, USB_STATE_NOTATTACHED);

	spin_lock_irqsave(&sdwc->lock, flags);
	charging_only = sdwc->charging_mode;
	spin_unlock_irqrestore(&sdwc->lock, flags);

	/*
	 * If dwc3 parent device is still in suspended status, just return.
	 *
	 * Note: If the system enters into suspend state, system will disable
	 * every device's runtime PM until resuming the system. Thus if the
	 * cable plugout event resume the system, we will check the device's
	 * runtime state before the system enable the device's runtime PM,
	 * which will get the wrong device's runtime PM status to crash dwc3.
	 *
	 * Here we should check the charging status to avoid this situation,
	 * since it always be in suspend state when it is in charging status.
	 */
	if (charging_only || pm_runtime_suspended(sdwc->dev)) {
		dev_info(sdwc->dev,
			 "dwc3 device had been in suspend status!\n");
		return 0;
	}

	if (mode == USB_DR_MODE_PERIPHERAL)
		dwc3_flush_all_events(sdwc);
	else if (mode == USB_DR_MODE_HOST && sdwc->vbus &&
		 regulator_is_enabled(sdwc->vbus)) {
		ret = regulator_disable(sdwc->vbus);
		if (ret) {
			dev_err(sdwc->dev,
				"Failed to enable vbus: %d\n", ret);
			return ret;
		}
	}

	ret = device_for_each_child(sdwc->dev, NULL, dwc3_sprd_suspend_child);
	if (ret) {
		dev_err(sdwc->dev, "Dwc3 core suspend failed!\n");
		return ret;
	}

	ret = pm_runtime_put_sync(sdwc->dev);
	if (ret) {
		dev_err(sdwc->dev, "Dwc3 sprd suspend failed!\n");
		return ret;
	}
	return 0;
}

static void dwc3_sprd_hot_plug(struct dwc3_sprd *sdwc)
{
	enum usb_dr_mode current_mode;
	int current_state;
	bool charging_only = false;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sdwc->lock, flags);
	current_mode = sdwc->wq_mode;
	current_state = sdwc->vbus_active;
	sdwc->wq_mode = USB_DR_MODE_UNKNOWN;
	spin_unlock_irqrestore(&sdwc->lock, flags);

	if (current_state) {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->block_active) {
			dev_err(sdwc->dev, "USB core is already activated\n");
			spin_unlock_irqrestore(&sdwc->lock, flags);
			return;
		}

		sdwc->dr_mode = current_mode;
		spin_unlock_irqrestore(&sdwc->lock, flags);
		ret = dwc3_sprd_start(sdwc, current_mode);

		spin_lock_irqsave(&sdwc->lock, flags);
		if (ret)
			sdwc->dr_mode = USB_DR_MODE_UNKNOWN;
		else
			sdwc->block_active = true;

		charging_only = sdwc->charging_mode;
		spin_unlock_irqrestore(&sdwc->lock, flags);

		if (ret) {
			dev_err(sdwc->dev, "failed to run as %s\n",
				current_mode == USB_DR_MODE_HOST ? "HOST" : "DEVICE");
			return;
		}

		if (charging_only)
			__pm_relax(sdwc->wake_lock);

		dev_info(sdwc->dev, "is running as %s\n",
			 current_mode == USB_DR_MODE_HOST ? "HOST" : "DEVICE");
	} else {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (!sdwc->block_active) {
			dev_err(sdwc->dev, "USB core is already deactivated\n");
			spin_unlock_irqrestore(&sdwc->lock, flags);
			return;
		}

		sdwc->dr_mode = USB_DR_MODE_UNKNOWN;
		spin_unlock_irqrestore(&sdwc->lock, flags);
		dwc3_sprd_stop(sdwc, current_mode);

		/*
		 * When OTG power off, then we can enable the VBUS irq to detect
		 * device connection.
		 */

		spin_lock_irqsave(&sdwc->lock, flags);
		sdwc->block_active = false;
		charging_only = sdwc->charging_mode;
		sdwc->charging_mode = false;
		spin_unlock_irqrestore(&sdwc->lock, flags);

		if (!charging_only)
			__pm_relax(sdwc->wake_lock);

		dev_info(sdwc->dev, "is shut down\n");
	}
}


static void dwc3_sprd_notifier_work(struct work_struct *work)
{
	struct dwc3_sprd *sdwc = container_of(work, struct dwc3_sprd, work);

	dwc3_sprd_hot_plug(sdwc);
}

static int dwc3_sprd_vbus_notifier(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	struct dwc3_sprd *sdwc = container_of(nb, struct dwc3_sprd, vbus_nb);
	unsigned long flags;

	/* In usb audio mode, we turn off dwc3, but still keep the vbus on.
	 * It should income invalid vbus notifier, filter them
	 */
	if (sdwc->is_audio_dev) {
		dev_info(sdwc->dev, "ignore device connection detected from VBUS GPIO for is_audio_dev.\n");
		return 0;
	}

	if (event) {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->vbus_active == true ||
			sdwc->dr_mode == USB_DR_MODE_HOST) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore device connection detected from VBUS GPIO.\n");
			return 0;
		}

		__pm_stay_awake(sdwc->wake_lock);

		sdwc->vbus_active = true;
		sdwc->wq_mode = USB_DR_MODE_PERIPHERAL;
		queue_work(system_unbound_wq, &sdwc->work);
		spin_unlock_irqrestore(&sdwc->lock, flags);
		dev_info(sdwc->dev,
			"device connection detected from VBUS GPIO.\n");
	} else {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->vbus_active == false ||
			sdwc->dr_mode == USB_DR_MODE_HOST) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore device disconnect detected from VBUS GPIO.\n");
			return 0;
		}

		sdwc->vbus_active = false;
		sdwc->wq_mode = USB_DR_MODE_PERIPHERAL;
		queue_work(system_unbound_wq, &sdwc->work);
		spin_unlock_irqrestore(&sdwc->lock, flags);
		dev_info(sdwc->dev,
			"device disconnect detected from VBUS GPIO.\n");
	}
	return 0;
}

static int dwc3_sprd_id_notifier(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct dwc3_sprd *sdwc = container_of(nb, struct dwc3_sprd, id_nb);
	unsigned long flags;

	if (event) {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->vbus_active == true ||
			sdwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore host connection detected from ID GPIO.\n");
			return 0;
		}

		__pm_stay_awake(sdwc->wake_lock);

		sdwc->vbus_active = true;
		sdwc->wq_mode = USB_DR_MODE_HOST;
		queue_work(system_unbound_wq, &sdwc->work);
		spin_unlock_irqrestore(&sdwc->lock, flags);
		dev_info(sdwc->dev,
			"host connection detected from ID GPIO.\n");
	} else {
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->is_audio_dev) {
			sdwc->is_audio_dev = false;
			spin_unlock_irqrestore(&sdwc->lock, flags);

			/* notify musb to stop */
			call_sprd_usbm_event_notifiers(SPRD_USBM_EVENT_HOST_MUSB, false, NULL);

			return 0;
		}

		if (sdwc->vbus_active == false ||
			sdwc->dr_mode == USB_DR_MODE_PERIPHERAL) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore host disconnect detected from ID GPIO.\n");
			return 0;
		}

		sdwc->vbus_active = false;
		sdwc->wq_mode = USB_DR_MODE_HOST;
		queue_work(system_unbound_wq, &sdwc->work);
		spin_unlock_irqrestore(&sdwc->lock, flags);
		dev_info(sdwc->dev,
			"host disconnect detected from ID GPIO.\n");
	}
	return 0;
}

static void dwc3_sprd_detect_cable(struct dwc3_sprd *sdwc)
{
	unsigned long flags;
	enum usb_dr_mode mode = USB_DR_MODE_UNKNOWN;
	struct extcon_dev *id_ext = sdwc->id_edev ? sdwc->id_edev : sdwc->edev;

	spin_lock_irqsave(&sdwc->lock, flags);
	if (extcon_get_state(sdwc->edev, EXTCON_USB) == true) {
		if (sdwc->vbus_active == true) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore device connection detected from VBUS GPIO.\n");
			return;
		}

		__pm_stay_awake(sdwc->wake_lock);

		sdwc->vbus_active = true;
		sdwc->wq_mode = USB_DR_MODE_PERIPHERAL;
		mode = sdwc->wq_mode;
		queue_work(system_unbound_wq, &sdwc->work);
	} else if (extcon_get_state(id_ext, EXTCON_USB_HOST) == true) {
		if (sdwc->vbus_active == true) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore host connection detected from ID GPIO.\n");
			return;
		}

		__pm_stay_awake(sdwc->wake_lock);

		sdwc->vbus_active = true;
		sdwc->wq_mode = USB_DR_MODE_HOST;
		mode = sdwc->wq_mode;
		queue_work(system_unbound_wq, &sdwc->work);
	}
	spin_unlock_irqrestore(&sdwc->lock, flags);

	if (mode == USB_DR_MODE_PERIPHERAL)
		dev_info(sdwc->dev,
			"device connection detected from VBUS GPIO.\n");
	else if (mode == USB_DR_MODE_HOST)
		dev_info(sdwc->dev,
			"host connection detected from ID GPIO.\n");
}

int dwc3_sprd_audio_notifier(struct notifier_block *nb, unsigned long event,
    void *data)
{
	struct dwc3_sprd *sdwc = container_of(nb, struct dwc3_sprd, audio_nb);
	unsigned long flags;

	dev_dbg(sdwc->dev, "[%s]event(%ld)\n", __func__, event);

	/* dwc3 only need to proccess "false" event */
	if (!event) {
		spin_lock_irqsave(&sdwc->lock, flags);
		sdwc->is_audio_dev = true;
		spin_unlock_irqrestore(&sdwc->lock, flags);

		/* start musb */
		call_sprd_usbm_event_notifiers(SPRD_USBM_EVENT_HOST_MUSB, true, NULL);

		/* suspend usb3 */
		spin_lock_irqsave(&sdwc->lock, flags);
		if (sdwc->vbus_active == false) {
			spin_unlock_irqrestore(&sdwc->lock, flags);
			dev_info(sdwc->dev,
				"ignore host disconnect detected from ID audio.\n");
			return 0;
		}
		sdwc->vbus_active = false;
		sdwc->wq_mode = USB_DR_MODE_HOST;
		spin_unlock_irqrestore(&sdwc->lock, flags);
		queue_work(system_unbound_wq, &sdwc->work);
		dev_dbg(sdwc->dev, "[%s]done\n", __func__);
	}
	return 0;
}

static int dwc3_sprd_clk_probe(struct device *dev, struct dwc3_sprd *sdwc)
{
	int ret = 0;

	sdwc->ipa_dpu1_clk = devm_clk_get(dev, "ipa_dpu1_clk");
	if (IS_ERR(sdwc->ipa_dpu1_clk)) {
		dev_err(dev, "no dpu1 clk specified\n");
		return PTR_ERR(sdwc->ipa_dpu1_clk);
	} else {
		ret = clk_prepare_enable(sdwc->ipa_dpu1_clk);
		if (ret)
			dev_err(dev, "ipa-dpu1-clock enable failed\n");
	}

	sdwc->ipa_dptx_clk = devm_clk_get(dev, "ipa_dptx_clk");
	if (IS_ERR(sdwc->ipa_dptx_clk)) {
		dev_err(dev, "no dptx clk specified\n");
		return PTR_ERR(sdwc->ipa_dptx_clk);
	} else {
		ret = clk_prepare_enable(sdwc->ipa_dptx_clk);
		if (ret)
			dev_err(dev, "ipa-dptx-clock enable failed\n");
	}

	sdwc->ipa_tca_clk = devm_clk_get(dev, "ipa_tca_clk");
	if (IS_ERR(sdwc->ipa_tca_clk)) {
		dev_err(dev, "no tca clk specified\n");
		return PTR_ERR(sdwc->ipa_tca_clk);
	} else {
		ret = clk_prepare_enable(sdwc->ipa_tca_clk);
		if (ret)
			dev_err(dev, "ipa-tca-clock enable failed\n");
	}

	sdwc->ipa_usb31pll_clk = devm_clk_get(dev, "ipa_usb31pll_clk");
	if (IS_ERR(sdwc->ipa_usb31pll_clk)) {
		dev_err(dev, "no usb31pll clk specified\n");
		return PTR_ERR(sdwc->ipa_usb31pll_clk);
	} else {
		ret = clk_prepare_enable(sdwc->ipa_usb31pll_clk);
		if (ret)
			dev_err(dev, "ipa-usb31pll-clock enable failed\n");
	}

	return ret;
}

static int usb_clk_prepare_enable(struct dwc3_sprd *sdwc)
{
	if (clk_prepare_enable(sdwc->ipa_dpu1_clk))
		dev_err(sdwc->dev, "ipa dpu1 clk enable error.\n");
	if (clk_prepare_enable(sdwc->ipa_dptx_clk))
		dev_err(sdwc->dev, "ipa dptx clk enable error.\n");
	if (clk_prepare_enable(sdwc->ipa_tca_clk))
		dev_err(sdwc->dev, "ipa tca clk enable error.\n");
	if (clk_prepare_enable(sdwc->ipa_usb31pll_clk))
		dev_err(sdwc->dev, "ipa usb31pll clk enable error.\n");
	return 0;
}


static int usb_clk_prepare_disable(struct dwc3_sprd *sdwc)
{

	clk_disable_unprepare(sdwc->ipa_dpu1_clk);
	clk_disable_unprepare(sdwc->ipa_dptx_clk);
	clk_disable_unprepare(sdwc->ipa_tca_clk);
	clk_disable_unprepare(sdwc->ipa_usb31pll_clk);
	return 0;
}

static int dwc3_sprd_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node, *dwc3_node;

	struct device *dev = &pdev->dev;
	struct dwc3_sprd *sdwc;
	const char *usb_mode;
	u64 dma_mask;
	int ret;

	if (!node) {
		dev_err(dev, "can not find device node\n");
		return -ENODEV;
	}

	sdwc = devm_kzalloc(dev, sizeof(*sdwc), GFP_KERNEL);
	if (!sdwc)
		return -ENOMEM;

	dma_mask = DMA_BIT_MASK(64);
	ret = dma_coerce_mask_and_coherent(dev, dma_mask);
	if (ret)
		return ret;

	dwc3_node = of_get_next_available_child(node, NULL);
	if (!dwc3_node) {
		dev_err(dev, "failed to find dwc3 child\n");
		return PTR_ERR(dwc3_node);
	}

	if (dwc3_sprd_clk_probe(dev, sdwc))
		goto err_ipa_clk;

	sdwc->hs_phy = devm_usb_get_phy_by_phandle(dev,
			"usb-phy", 0);
	if (IS_ERR(sdwc->hs_phy)) {
		dev_err(dev, "unable to get usb2.0 phy device\n");
		ret = PTR_ERR(sdwc->hs_phy);
		goto err_ipa_clk;
	}
	sdwc->ss_phy = devm_usb_get_phy_by_phandle(dev,
			"usb-phy", 1);
	if (IS_ERR(sdwc->ss_phy)) {
		dev_err(dev, "unable to get usb3.0 phy device\n");
		ret = PTR_ERR(sdwc->ss_phy);
		goto err_ipa_clk;
	}

	if (IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE) ||
	    IS_ENABLED(CONFIG_USB_DWC3_HOST)) {
		sdwc->vbus = devm_regulator_get(dev, "vbus");
		if (IS_ERR(sdwc->vbus)) {
			dev_warn(dev, "unable to get vbus supply\n");
			sdwc->vbus = NULL;
		}
	}

	sdwc->ipa_usb_ref_clk = devm_clk_get(dev, "ipa_usb_ref");
	if (IS_ERR(sdwc->ipa_usb_ref_clk)) {
		dev_warn(dev, "usb3 can't get the ipa usb ref clock\n");
		sdwc->ipa_usb_ref_clk = NULL;
	}

	sdwc->ipa_usb_ref_parent = devm_clk_get(dev, "ipa_usb_ref_source");
	if (IS_ERR(sdwc->ipa_usb_ref_parent)) {
		dev_warn(dev, "usb can't get the ipa usb ref source\n");
		sdwc->ipa_usb_ref_parent = NULL;
	}

	sdwc->ipa_usb_ref_default = devm_clk_get(dev, "ipa_usb_ref_default");
	if (IS_ERR(sdwc->ipa_usb_ref_default)) {
		dev_warn(dev, "usb can't get the ipa usb ref default\n");
		sdwc->ipa_usb_ref_default = NULL;
	}

	if (sdwc->ipa_usb_ref_clk && sdwc->ipa_usb_ref_parent)
		clk_set_parent(sdwc->ipa_usb_ref_clk, sdwc->ipa_usb_ref_parent);

	/* perpare clock */
	sdwc->core_clk = devm_clk_get(dev, "core_clk");
	if (IS_ERR(sdwc->core_clk)) {
		dev_warn(dev, "no core clk specified\n");
		sdwc->core_clk = NULL;
	} else {
		ret = clk_prepare_enable(sdwc->core_clk);
		if (ret) {
			dev_err(dev, "core-clock enable failed\n");
			goto err_ipa_clk;
		}
	}
	sdwc->ref_clk = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(sdwc->ref_clk)) {
		dev_warn(dev, "no ref clk specified\n");
		sdwc->ref_clk = NULL;
	} else {
		ret = clk_prepare_enable(sdwc->ref_clk);
		if (ret) {
			dev_err(dev, "ref-clock enable failed\n");
			goto err_core_clk;
		}
	}
	sdwc->susp_clk = devm_clk_get(dev, "susp_clk");
	if (IS_ERR(sdwc->susp_clk)) {
		dev_warn(dev, "no suspend clk specified\n");
		sdwc->susp_clk = NULL;
	} else {
		ret = clk_prepare_enable(sdwc->susp_clk);
		if (ret) {
			dev_err(dev, "suspend-clock enable failed\n");
			goto err_ref_clk;
		}
	}

	if (IS_ENABLED(CONFIG_USB_DWC3_GADGET))
		usb_mode = "PERIPHERAL";
	else if (IS_ENABLED(CONFIG_USB_DWC3_HOST))
		usb_mode = "HOST";
	else
		usb_mode = "DRD";

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to add create dwc3 core\n");
		goto err_susp_clk;
	}

	sdwc->dwc3 = of_find_device_by_node(dwc3_node);
	of_node_put(dwc3_node);
	if (!sdwc->dwc3) {
		dev_err(dev, "failed to get dwc3 platform device\n");
		ret = PTR_ERR(sdwc->dwc3);
		goto err_susp_clk;
	}

	INIT_WORK(&sdwc->work, dwc3_sprd_notifier_work);
	init_waitqueue_head(&sdwc->wait);
	spin_lock_init(&sdwc->lock);
	sdwc->suspend = false;
	sdwc->dev = dev;

	/* get vbus/id gpios extcon device */
	if (of_property_read_bool(node, "extcon")) {
		struct device_node *extcon_node;

		sdwc->edev = extcon_get_edev_by_phandle(sdwc->dev, 0);
		if (IS_ERR(sdwc->edev)) {
			dev_err(dev, "failed to find vbus extcon device.\n");
			ret = PTR_ERR(sdwc->edev);
			goto err_susp_clk;
		}

		sdwc->vbus_nb.notifier_call = dwc3_sprd_vbus_notifier;
		ret = extcon_register_notifier(sdwc->edev, EXTCON_USB,
						   &sdwc->vbus_nb);
		if (ret) {
			dev_err(dev,
				"failed to register extcon USB notifier.\n");
			goto err_susp_clk;
		}

		sdwc->id_edev = extcon_get_edev_by_phandle(sdwc->dev, 1);
		if (IS_ERR(sdwc->id_edev)) {
			sdwc->id_edev = NULL;
			dev_info(dev, "No separate ID extcon device.\n");
		}

		sdwc->id_nb.notifier_call = dwc3_sprd_id_notifier;
		if (sdwc->id_edev)
			ret = extcon_register_notifier(sdwc->id_edev,
					 EXTCON_USB_HOST, &sdwc->id_nb);
		else
			ret = extcon_register_notifier(sdwc->edev,
					 EXTCON_USB_HOST, &sdwc->id_nb);
		if (ret) {
			dev_err(dev,
			"failed to register extcon USB HOST notifier.\n");
			goto err_extcon_vbus;
		}

		extcon_node = of_parse_phandle(node, "extcon", 0);
		if (!extcon_node) {
			dev_err(dev, "failed to find extcon node.\n");
			goto err_extcon_id;
		}

	} else {
		/*
		 * In some cases, FPGA, USB Core and PHY may be always powered
		 * on.
		 */
		sdwc->vbus_active = true;

		if (boot_calibration) {
			sdwc->dr_mode = USB_DR_MODE_PERIPHERAL;
		} else {
			if (IS_ENABLED(CONFIG_USB_DWC3_HOST) ||
			    IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE))
				sdwc->dr_mode = USB_DR_MODE_HOST;
			else
				sdwc->dr_mode = USB_DR_MODE_PERIPHERAL;
		}

		dev_info(dev, "DWC3 is always running as %s\n",
			 sdwc->dr_mode == USB_DR_MODE_PERIPHERAL ? "DEVICE" : "HOST");
	}

	sdwc->audio_nb.notifier_call = dwc3_sprd_audio_notifier;
	ret = register_sprd_usbm_notifier(&sdwc->audio_nb, SPRD_USBM_EVENT_HOST_DWC3);
	if (ret) {
		dev_err(sdwc->dev, "failed to register usb event\n");
		goto err_extcon_id;
	}

	platform_set_drvdata(pdev, sdwc);

	ret = sysfs_create_groups(&sdwc->dev->kobj, dwc3_sprd_groups);
	if (ret) {
		dev_err(sdwc->dev, "failed to create dwc3 attributes\n");
		goto err_extcon_id;
	}
	sdwc->wake_lock = wakeup_source_create("dwc3-sprd");
	wakeup_source_add(sdwc->wake_lock);

	boot_charging = dwc3_sprd_charger_mode();
	boot_calibration = dwc3_sprd_calibration_mode();
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	if (of_property_read_bool(node, "extcon"))
		dwc3_sprd_detect_cable(sdwc);
	else
		queue_work(system_unbound_wq, &sdwc->work);

	return 0;

err_extcon_id:
	if (sdwc->edev)
		extcon_unregister_notifier(sdwc->edev, EXTCON_USB_HOST,
					   &sdwc->id_nb);
err_extcon_vbus:
	if (sdwc->edev)
		extcon_unregister_notifier(sdwc->edev, EXTCON_USB,
					   &sdwc->vbus_nb);

err_susp_clk:
	clk_disable_unprepare(sdwc->susp_clk);
err_ref_clk:
	clk_disable_unprepare(sdwc->ref_clk);
err_core_clk:
	clk_disable_unprepare(sdwc->core_clk);
err_ipa_clk:
	usb_clk_prepare_disable(sdwc);

	return ret;
}

static int dwc3_sprd_remove_child(struct device *dev, void *data)
{
	device_unregister(dev);
	return 0;
}

static int dwc3_sprd_remove(struct platform_device *pdev)
{
	struct dwc3_sprd *sdwc = platform_get_drvdata(pdev);

	device_for_each_child(&pdev->dev, NULL, dwc3_sprd_remove_child);

	clk_disable_unprepare(sdwc->core_clk);
	clk_disable_unprepare(sdwc->ref_clk);
	clk_disable_unprepare(sdwc->susp_clk);
	usb_clk_prepare_disable(sdwc);

	usb_phy_shutdown(sdwc->hs_phy);
	usb_phy_shutdown(sdwc->ss_phy);

	sysfs_remove_groups(&sdwc->dev->kobj, dwc3_sprd_groups);
	if (sdwc->edev) {
		extcon_unregister_notifier(sdwc->edev, EXTCON_USB,
					   &sdwc->vbus_nb);
		extcon_unregister_notifier(sdwc->edev, EXTCON_USB_HOST,
					   &sdwc->id_nb);
	}

	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_sprd_pm_suspend(struct device *dev)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);

	sdwc->suspend = true;
	return 0;
}

static int dwc3_sprd_pm_resume(struct device *dev)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);

	sdwc->suspend = false;
	wake_up(&sdwc->wait);

	return 0;
}
#endif

static void dwc3_sprd_enable(struct dwc3_sprd *sdwc)
{
	if (usb_clk_prepare_enable(sdwc))
		dev_err(sdwc->dev, "usb clk enable error.\n");

	if (sdwc->ipa_usb_ref_clk && sdwc->ipa_usb_ref_parent)
		clk_set_parent(sdwc->ipa_usb_ref_clk, sdwc->ipa_usb_ref_parent);
	if (clk_prepare_enable(sdwc->core_clk))
		dev_err(sdwc->dev, "core clk enable error.\n");
	if (clk_prepare_enable(sdwc->ref_clk))
		dev_err(sdwc->dev, "ref clk enable error.\n");
	if (clk_prepare_enable(sdwc->susp_clk))
		dev_err(sdwc->dev, "susp clk enable error.\n");

	usb_phy_init(sdwc->hs_phy);
	usb_phy_init(sdwc->ss_phy);
}

static void dwc3_sprd_disable(struct dwc3_sprd *sdwc)
{
	usb_phy_shutdown(sdwc->hs_phy);
	usb_phy_shutdown(sdwc->ss_phy);
	clk_disable_unprepare(sdwc->susp_clk);
	clk_disable_unprepare(sdwc->ref_clk);
	clk_disable_unprepare(sdwc->core_clk);
	usb_clk_prepare_disable(sdwc);
	/*
	 * set usb ref clock to default when dwc3 was not used,
	 * or else the clock can't be really switch to another
	 * parent within dwc3_sprd_enable.
	 */
	if (sdwc->ipa_usb_ref_clk && sdwc->ipa_usb_ref_default)
		clk_set_parent(sdwc->ipa_usb_ref_clk,
			       sdwc->ipa_usb_ref_default);
}

static int dwc3_sprd_resume_child(struct device *dev, void *data)
{
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret) {
		dev_err(dev, "dwc3 child device enters resume failed!!!\n");
		return ret;
	}

	return 0;
}

static int dwc3_sprd_suspend_child(struct device *dev, void *data)
{
	int ret, cnt = DWC3_SUSPEND_COUNT;

	ret = pm_runtime_put_sync(dev);
	if (ret) {
		dev_err(dev, "enters suspend failed, ret = %d\n", ret);
		return ret;
	}

	while (!pm_runtime_suspended(dev) && --cnt > 0)
		msleep(500);

	if (cnt <= 0) {
		dev_err(dev, "dwc3 child device enters suspend failed!!!\n");
		return -EAGAIN;
	}

	return 0;
}

#ifdef CONFIG_PM
static int dwc3_sprd_runtime_suspend(struct device *dev)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);

	dwc3_sprd_disable(sdwc);
	usb_phy_vbus_off(sdwc->ss_phy);
	dev_info(dev, "enter into suspend mode\n");
	return 0;
}

static int dwc3_sprd_runtime_resume(struct device *dev)
{
	struct dwc3_sprd *sdwc = dev_get_drvdata(dev);

	if (sdwc->dr_mode == USB_DR_MODE_HOST)
		usb_phy_vbus_on(sdwc->ss_phy);
	dwc3_sprd_enable(sdwc);
	dev_info(dev, "enter into resume mode\n");
	return 0;
}

static int dwc3_sprd_runtime_idle(struct device *dev)
{
	dev_info(dev, "enter into idle mode\n");
	return 0;
}
#endif

static const struct dev_pm_ops dwc3_sprd_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		dwc3_sprd_pm_suspend,
		dwc3_sprd_pm_resume)

	SET_RUNTIME_PM_OPS(
		dwc3_sprd_runtime_suspend,
		dwc3_sprd_runtime_resume,
		dwc3_sprd_runtime_idle)
};

static const struct of_device_id sprd_dwc3_match[] = {
	{ .compatible = "sprd,roc1-dwc3" },
	{ .compatible = "sprd,orca-dwc3" },
	{ .compatible = "sprd,qogirn6pro-dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, sprd_dwc3_match);

static struct platform_driver dwc3_sprd_driver = {
	.probe		= dwc3_sprd_probe,
	.remove		= dwc3_sprd_remove,
	.driver		= {
		.name	= "dwc3-sprd",
		.of_match_table = sprd_dwc3_match,
		.pm = &dwc3_sprd_dev_pm_ops,
	},
};

static int __init dwc3_sprd_driver_init(void)
{
	return platform_driver_register(&dwc3_sprd_driver);
}

static void __exit dwc3_sprd_driver_exit(void)
{
	platform_driver_unregister(&dwc3_sprd_driver);
}

late_initcall(dwc3_sprd_driver_init);
module_exit(dwc3_sprd_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 SPRD Glue Layer");
