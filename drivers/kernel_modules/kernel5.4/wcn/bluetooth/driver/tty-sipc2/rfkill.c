/*
 * Copyright (C) 2015 Spreadtrum Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>

#include "rfkill.h"
#include "unisoc_bt_log.h"

#include <misc/wcn_integrate_platform.h>

static struct rfkill *bt_rfk;
static const char bt_name[] = "bluetooth";
extern struct device *ttyBT_dev;

static int bluetooth_set_power(void *data, bool blocked)
{
	int ret;

	dev_unisoc_bt_info(ttyBT_dev,
						"%s: start_block: %d\n",
						__func__, blocked);

	if (!blocked)
	ret = start_marlin(WCN_MARLIN_BLUETOOTH);
	else
        ret = stop_marlin(WCN_MARLIN_BLUETOOTH);
	dev_unisoc_bt_info(ttyBT_dev,
						"%s: end_block: %d, ret: %d\n",
						__func__, blocked, ret);
	return ret;
}

static struct rfkill_ops rfkill_bluetooth_ops = {
	.set_block = bluetooth_set_power,
};

int rfkill_bluetooth_init(struct platform_device *pdev)
{

	int rc = 0;

	dev_unisoc_bt_info(ttyBT_dev,
						"-->%s\n",
						__func__);
	bt_rfk = rfkill_alloc(bt_name, &pdev->dev, RFKILL_TYPE_BLUETOOTH,
			&rfkill_bluetooth_ops, NULL);
	if (!bt_rfk) {
		rc = -ENOMEM;
		goto err_rfkill_alloc;
	}
	/* userspace cannot take exclusive control */
	rfkill_init_sw_state(bt_rfk, false);
	rc = rfkill_register(bt_rfk);
	if (rc)
		goto err_rfkill_reg;

	dev_unisoc_bt_info(ttyBT_dev,
						"<--%s\n",
						__func__);

	return 0;

err_rfkill_reg:
	rfkill_destroy(bt_rfk);
err_rfkill_alloc:
	return rc;
}

int rfkill_bluetooth_remove(struct platform_device *dev)
{
	dev_unisoc_bt_info(ttyBT_dev,
						"-->%s\n",
						__func__);
	rfkill_unregister(bt_rfk);
	rfkill_destroy(bt_rfk);
	dev_unisoc_bt_info(ttyBT_dev,
						"<--%s\n",
						__func__);
	return 0;
}

