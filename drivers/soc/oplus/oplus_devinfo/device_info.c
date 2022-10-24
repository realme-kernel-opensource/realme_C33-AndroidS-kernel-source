// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <soc/oplus/device_info.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include "../../../../fs/proc/internal.h"
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/iio/consumer.h>
#include <linux/of_fdt.h>
#include <linux/of.h>

#define DEVINFO_NAME "devinfo"

#define dev_msg(msg, arg...) pr_err("devinfo:" msg, ##arg);
#if 0
#define BOARD_GPIO_SUPPORT 4
#define MAIN_BOARD_SUPPORT 256
#endif
static struct proc_dir_entry *g_parent = NULL;

#if 0
struct device_info {
	struct device *dev;
	struct pinctrl *p_ctrl;
	struct pinctrl_state *active[BOARD_GPIO_SUPPORT], *sleep[BOARD_GPIO_SUPPORT];
	struct pinctrl_state *idle[BOARD_GPIO_SUPPORT];
	struct list_head dev_list;
};

static struct device_info *g_dev_info = NULL;
static int reinit_aboard_id(struct device *dev,
			    struct manufacture_info *info);

bool
check_id_match(const char *label, const char *id_match, int id)
{
	struct o_hw_id *pos = NULL;

	list_for_each_entry(pos, & (g_dev_info->dev_list), list)
{
		if (sizeof(label) != sizeof(pos->label)) {
			continue;
		}
		if (!strcasecmp(pos->label, label)) {
			if (id_match) {
				if (!strcasecmp(pos->match, id_match)) {
					return true;
				}
			} else {
				if (pos->id == id) {
					return true;
				}
			}
		}
}

	return false;
}
#endif
static int devinfo_read_func(struct seq_file *s, void *v)
{
	struct manufacture_info *info = (struct manufacture_info *) s->private;
#if 0
	if (strcmp(info->name, "audio_mainboard") == 0) {
		reinit_aboard_id(NULL, info);
	}
#endif
	if (info->version) {
		seq_printf(s, "Device version:\t\t%s\n", info->version);
	}

	if (info->manufacture) {
		seq_printf(s, "Device manufacture:\t\t%s\n", info->manufacture);
	}

	if (info->fw_path) {
		seq_printf(s, "Device fw_path:\t\t%s\n", info->fw_path);
	}

	return 0;
}

static int device_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, devinfo_read_func, PDE_DATA(inode));
}

static const struct file_operations device_node_fops = {
	.owner = THIS_MODULE,
	.open = device_info_open,
	.read = seq_read,
	.release = single_release,
};
#if 0
static int devinfo_read_ufsplus_func(struct seq_file *s, void *v)
{
	struct o_ufsplus_status *ufsplus_status  = (struct o_ufsplus_status *)s->private;
	if (!ufsplus_status) {
		return -EINVAL;
	}
	seq_printf(s, "HPB status: %d\n", * (ufsplus_status->hpb_status));
	seq_printf(s, "TW status: %d\n", * (ufsplus_status->tw_status));
	return 0;
}

static int device_info_for_ufsplus_open(struct inode *inode, struct file *file)
{
	return single_open(file, devinfo_read_ufsplus_func, PDE_DATA(inode));
}


static const struct file_operations device_node_for_ufsplus_fops = {
	.owner = THIS_MODULE,
	.open = device_info_for_ufsplus_open,
	.read = seq_read,
	.release = single_release,
};

static int deviceid_read_func(struct seq_file *s, void *v)
{
	struct o_hw_id *info = (struct o_hw_id *) s->private;

	if (info->match) {
		seq_printf(s, "%s", info->match);
	} else {
		seq_printf(s, "%d", info->id);
	}

	return 0;
}

static int device_id_open(struct inode *inode, struct file *file)
{
	return single_open(file, deviceid_read_func, PDE_DATA(inode));
}

static const struct file_operations device_id_fops = {
	.owner = THIS_MODULE,
	.open = device_id_open,
	.read = seq_read,
	.release = single_release,
};

int register_device_id(struct device_info *dev_info, const char *label, const char *id_match, int id)
{
	struct o_hw_id *hw_id = NULL;

	hw_id = (struct o_hw_id *) kzalloc(sizeof(*hw_id), GFP_KERNEL);
	if (!hw_id) {
		return -ENOMEM;
	}

	hw_id->label = label;
	hw_id->match = id_match;
	hw_id->id = id;

	list_add(& (hw_id->list), & (dev_info->dev_list));

	if (!proc_create_data(label, S_IRUGO, g_parent, &device_id_fops, hw_id)) {
		dev_msg("failed to create entry %s \n", label);
	}

	return 0;
}
#endif
int register_devinfo(char *name, struct manufacture_info *info)
{
	struct proc_dir_entry *d_entry;
	if (!info) {
		return -EINVAL;
	}
	if (!g_parent) {
		g_parent = proc_mkdir("devinfo", NULL);
		if (!g_parent) {
			pr_err("register_devinfo proc_mkdir failed \n");
			return -ENODEV;
		}
	}
	d_entry = proc_create_data(name, S_IRUGO, g_parent, &device_node_fops, info);
	if (!d_entry) {
		pr_err("register_devinfo s% failed\n",name);
		return -EINVAL;
	}
	else 
		pr_err("register_devinfo s% success\n",name);
	return 0;
}
EXPORT_SYMBOL(register_devinfo);
#if 0
int register_device_proc_for_ufsplus(char *name, int *hpb_status, int *tw_status)
{
	struct proc_dir_entry *d_entry;
	struct o_ufsplus_status *ufsplus_status;
	ufsplus_status = (struct o_ufsplus_status *)kzalloc(sizeof(*ufsplus_status), GFP_KERNEL);
	if (!ufsplus_status) {
		return -ENOMEM;
	}

	ufsplus_status->hpb_status = hpb_status;
	ufsplus_status->tw_status = tw_status;

	d_entry = proc_create_data(name, S_IRUGO, g_parent, &device_node_for_ufsplus_fops, ufsplus_status);
	if (!d_entry) {
		kfree(ufsplus_status);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(register_device_proc_for_ufsplus);
#endif
int register_device_proc(char *name, char *version, char *vendor)
{
	struct manufacture_info *info;
	if (!g_parent) {
		return -ENOMEM;
	}
	info = (struct manufacture_info *) kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		return -ENOMEM;
	}
	if (version) {
		info->version = (char *) kzalloc(32, GFP_KERNEL);
		if (!info->version) {
			kfree(info);
			return -ENOMEM;
		}
		memcpy(info->version, version, strlen(version) > 31?31:strlen(version));
	}
	if (vendor) {
		info->manufacture = (char *) kzalloc(32, GFP_KERNEL);
		pr_err("dregister_device_proc info->manufacture start\n");
		if (!info->manufacture) {
			kfree(info->version);
			kfree(info);
			return -ENOMEM;
		}
		memcpy(info->manufacture, vendor, strlen(vendor) > 31?31:strlen(vendor));
	}

	return register_devinfo(name, info);
}
EXPORT_SYMBOL(register_device_proc);
#ifdef OPLUS_DEVICE_INFO
static ssize_t fork_para_monitor_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        //char page[256] = {0};
        int ret = 0;
        //ret = snprintf(page, 255, " times:%d\n father pid:%d\n child pid:%d\n", happend_times, fork_pid_father, fork_pid_child);

        //ret = simple_read_from_buffer(buf, count, off, page, strlen(page));
        return ret;
}

struct file_operations fork_para_monitor_proc_fops = {
        .read = fork_para_monitor_read_proc,
        .write = NULL,
};

static void recursive_fork_para_monitor(void)
{
		struct proc_dir_entry *pentry;

		pentry = proc_create("fork_monitor", S_IRUGO, g_parent, &fork_para_monitor_proc_fops);
        if (!pentry) {
                pr_err("create /devinfo/fork_monitor proc failed.\n");
        }
}
static int parse_gpio_dts(struct device *dev, struct device_info *dev_info)
{
#ifdef CONFIG_MTK_PLATFORM
	int i;
	char tmp[INFO_LEN] = {0};
	dev_info->p_ctrl = devm_pinctrl_get(dev);
	for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
		if (!IS_ERR_OR_NULL(dev_info->p_ctrl)) {
			snprintf(tmp, INFO_LEN, "aboard_gpio%d_active", i);
			dev_info->active[i] = pinctrl_lookup_state(dev_info->p_ctrl, tmp);
			if (IS_ERR_OR_NULL(dev_info->active[i])) {
				 dev_msg("Failed to get active[%d], check dts\n", i);
				 continue;
			}
			snprintf(tmp, INFO_LEN, "aboard_gpio%d_sleep", i);
			dev_info->sleep[i] = pinctrl_lookup_state(dev_info->p_ctrl, tmp);
			if (IS_ERR_OR_NULL(dev_info->sleep[i])) {
				 dev_msg("Failed to get sleep[%d], check dts\n", i);
				 continue;
			}
			snprintf(tmp, INFO_LEN, "aboard_gpio%d_idle", i);
			dev_info->idle[i] = pinctrl_lookup_state(dev_info->p_ctrl, tmp);
			if (IS_ERR_OR_NULL(dev_info->idle[i])) {
				 dev_msg("Failed to get idle[%d] , check dts\n", i);
				 continue;
			}
		}
	}
#else
        dev_info->p_ctrl = devm_pinctrl_get(dev);
        if (!IS_ERR_OR_NULL(dev_info->p_ctrl)) {
		dev_info->active[0] = pinctrl_lookup_state(dev_info->p_ctrl, "active");
		dev_info->sleep[0] = pinctrl_lookup_state(dev_info->p_ctrl, "sleep");
	}
#endif
	return 0;
}

static void set_gpios_active(struct device_info *dev_info)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
		if (!IS_ERR_OR_NULL(dev_info->p_ctrl) && !IS_ERR_OR_NULL(dev_info->active[i])) {
			ret = pinctrl_select_state(dev_info->p_ctrl, dev_info->active[i]);
			dev_msg("set gpio active ret[%d - %d]\n", i, ret);
		} else {
			dev_msg("pinctrl active is Null[%d]\n", i);
		}
	}
}

static void set_gpios_sleep(struct device_info *dev_info)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
		if (!IS_ERR_OR_NULL(dev_info->p_ctrl) && !IS_ERR_OR_NULL(dev_info->sleep[i])) {
			ret = pinctrl_select_state(dev_info->p_ctrl, dev_info->sleep[i]);
			dev_msg("set gpio sleep ret[%d - %d]\n", i, ret);
		} else {
			dev_msg("pinctrl sleep is Null[%d]\n", i);
		}
	}
}

static void set_gpios_idle(struct device_info *dev_info)
{
	int i = 0;
	int ret = 0;

	for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
		if (!IS_ERR_OR_NULL(dev_info->p_ctrl) && !IS_ERR_OR_NULL(dev_info->idle[i])) {
			ret = pinctrl_select_state(dev_info->p_ctrl, dev_info->idle[i]);
			dev_msg("set gpio idle ret[%d - %d]\n", i, ret);
		} else {
			dev_msg("pinctrl idle is Null[%d]\n", i);
		}
	}
}

static int init_other_hw_ids(struct platform_device *pdev)
{
	struct device_node *np;
	struct device_info *dev_info = platform_get_drvdata(pdev);
	const char *label = NULL, *name = NULL;
	int ret = 0, i = 0, size = 0;
	int gpio = 0, id = 0;
	uint8_t hw_mask = 0;
	char tmp[24];
	bool fail = false;
	uint32_t hw_combs[16];

	for_each_compatible_node(np, NULL, "hw, devices")
{
		ret = of_property_read_string(np, "label", &label);
		if (ret < 0 || !label) {
			continue;
		}

		fail = false;
		hw_mask = 0;
		/*get hw mask */
		for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
			snprintf(tmp, 24, "hw-id%d", i);
			gpio = of_get_named_gpio(np, tmp, 0);
			if (gpio < 0) {
				continue;
			}
			ret = gpio_request(gpio, tmp);
			if (ret < 0) {
				fail = true;
				dev_msg("failed to request gpio %d\n", gpio);
				break;
			}

			id = gpio_get_value(gpio);
			hw_mask |= (((uint8_t) id & 0x01) << i);
		}

		if (fail) {
			continue;
		}

		dev_msg("%s hwid mask %d\n", label, hw_mask);

		/*get hw mask name */
		size =
			of_property_count_elems_of_size(np, "hw-combs", sizeof(uint32_t));
		if (size < 0) {
			continue;
		}
		of_property_read_u32_array(np, "hw-combs", hw_combs, size);
		for (i = 0; i < size; i++) {
			if (hw_combs[i] == hw_mask) {
				break;
			}
		}
		if (i == size) {
			continue;
		}

		/*get hw names */
		size = of_property_count_strings(np, "hw-names");
		if (size >= i) {
			ret = of_property_read_string_index(np, "hw-names", i, &name);
			if (ret < 0) {
				dev_msg("failed to find hw name %d\n", i);
				continue;
			}
		}

		/*register hw id */
		register_device_id(dev_info, label, name, hw_mask);
}

	return 0;
}


static int gpio_get_submask(struct device_node *np)
{
	int i = 0, ret = 0;
	int gpio, id = 0;
        int count = 0;
	char tmp[INFO_LEN] = { 0 };

	for (i = 0; i < BOARD_GPIO_SUPPORT; i++) {
		snprintf(tmp, INFO_LEN, "aboard-gpio%d", i);
		gpio = of_get_named_gpio(np, tmp, 0);
		if (gpio < 0) {
			dev_msg("failed to get named gpio [%s] gpio[%d]\n", tmp, gpio);
			continue;
		}
		ret = gpio_request(gpio, tmp);
		if (ret) {
			dev_msg("failed to request gpio %d\n", gpio);
			ret = -EINVAL;
			goto gpio_request_failed;
		}

		id = gpio_get_value(gpio);
		dev_msg("gpio%d= %d\n", gpio, id);
		count |= (((uint8_t) id & 0x01) << i);
		gpio_free(gpio);
	}
        dev_msg("count= %d\n", count);
	return count;

gpio_request_failed:

	return -EINVAL;
}

static int
pmic_get_submask(struct device_node *np, struct device *dev)
{
	int size = 0, ret = 0;
	int adc_value = 0, low = 0, high = 0;
	uint32_t *adc_ranges = NULL;
	int i = 0;
	struct iio_channel *ADC_channel = NULL;

	size =
		of_property_count_elems_of_size(np, "adc_ranges", sizeof(uint32_t));
	if (size < 0 || (size % 2)) {
		dev_msg("adc ranges should be odd\n");
		return -EINVAL;
	}

	adc_ranges = (uint32_t *) kzalloc(sizeof(uint32_t) * size, GFP_KERNEL);
	if (!adc_ranges) {
		return -ENOMEM;
	}

	ADC_channel = iio_channel_get(dev, "vph_pwr_voltage_sub");
	if (IS_ERR(ADC_channel)) {
		dev_msg("failed to get adc channel\n");
		ret = -EINVAL;
		goto end;
	}

	if (iio_read_channel_processed(ADC_channel, &adc_value) < 0) {
		dev_msg("failed to read channel\n");
		ret = -EINVAL;
		goto end;
	}
	iio_channel_release(ADC_channel);

	adc_value /= 1000;
	dev_msg("adc value %d\n", adc_value);

	if (adc_value > 1750) {
		ret = -100;
		kfree(adc_ranges);
		return ret;
	}

	if (of_property_read_u32_array(np, "adc_ranges", adc_ranges, size) < 0) {
		ret = -ENODEV;
		goto end;
	}

	for (i = 0; i < size / 2; i++) {
		low = adc_ranges[2 * i];
		if (low < high) {
			dev_msg("adc value not increase %d %d\n", low, high);
			ret = -ENODEV;
			goto end;
		}

		high = adc_ranges[2 * i + 1];
		if (low > high) {
			dev_msg("adc value not increase %d %d\n", low, high);
			ret = -ENODEV;
			goto end;
		}
		if (low <= adc_value && adc_value <= high) {
			break;
		}
	}

	if (i == size / 2) {
		dev_msg("adc not match %d\n", adc_value);
		ret = -ENODEV;
		goto end;
	}

	ret = i;

end:
	kfree(adc_ranges);
	return ret;
}

static int
init_aboard_info(struct device_info *dev_info)
{
	struct manufacture_info *info = NULL;

	info = (struct manufacture_info *) kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		return -ENOMEM;
	}

	return register_devinfo("audio_mainboard", info);
}

static int
reinit_aboard_id_for_brandon(struct device *dev, struct device_info *dev_info)
{
	int adc_value = 0;
	int operate = 0;
	struct iio_channel *ADC_channel = NULL;
	int ret = 0;

	dev_msg("sub mainboard verification for brandon\n");
	set_gpios_active(dev_info);
	ADC_channel = iio_channel_get(dev, "sub_adc_gpio3");
	if (IS_ERR(ADC_channel)) {
		dev_msg("failed to get adc channel\n");
		ret = -EINVAL;
		return ret;
	}
	if (iio_read_channel_processed(ADC_channel, &adc_value) < 0) {
		dev_msg("failed to read channel\n");
		ret = -EINVAL;
		return ret;
	}
	iio_channel_release(ADC_channel);
	set_gpios_sleep(dev_info);

	adc_value /= 1000;
	dev_msg("adc value %d\n", adc_value);

	operate = get_Operator_Version();
	if ((2 == operate) || (8 == operate)) {
		if ((adc_value >= 250) && (adc_value <= 400)) {
			ret = 0;
		} else {
			ret = -EINVAL;
		}
	} else if ((12 == operate) || (13 == operate)) {
		if (((adc_value >= 570) && (adc_value <= 690)) || ((adc_value >= 410) && (adc_value <= 520))) {
			ret = 0;
		} else {
			ret = -EINVAL;
		}
	} else if (14 == operate) {
		if ((adc_value >= 410) && (adc_value <= 520)) {
			ret = 0;
		} else {
			ret = -EINVAL;
		}
	} else if (15 == operate) {
		if ((adc_value >= 700) && (adc_value <= 810)) {
			ret = 0;
		} else {
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int
reinit_aboard_id(struct device *dev, struct manufacture_info *info)
{
	struct device_node *np;
	int32_t hw_mask = 0;
	int i = 0, ret = 0;
	int id_size = 0;
	uint32_t *main_val = NULL, *sub_val = NULL, *rf_val = NULL;
	int active_val = 0, sleep_val = 0, idle_val = 0;
	struct device_info *dev_info = g_dev_info;
	bool match = false;

	if (!dev) {
		dev = dev_info->dev;
	}

	if (!info) {
		return -ENODEV;
	}

	if (of_property_read_bool(dev->of_node, "sub_brandon_support")) {
		ret = reinit_aboard_id_for_brandon(dev, dev_info);
		goto seccess;
	}

	np = of_find_compatible_node(dev->of_node, NULL, "hw-match, main-sub");
	if (!np) {
		dev_msg("failed to find node\n");
		return -ENODEV;
	}

	id_size =
		of_property_count_elems_of_size(np, "aboard-patterns",
						sizeof(uint32_t));
	if (id_size > MAIN_BOARD_SUPPORT) {
		return -ENODEV;
	} else if (id_size == -EINVAL) {
		/*ignore sub board id */
		dev_msg("have no abord id node\n");
		ret = 0;
		goto seccess;
	}

	sub_val = (uint32_t *) kzalloc(sizeof(uint32_t) * id_size, GFP_KERNEL);
	if (!sub_val) {
		return -ENOMEM;
	}

	main_val = (uint32_t *) kzalloc(sizeof(uint32_t) * id_size, GFP_KERNEL);
	if (!main_val) {
		kfree(sub_val);
		return -ENOMEM;
	}

	if (of_property_read_bool(np, "rf_match_support")) {
		rf_val = (uint32_t *) kzalloc(sizeof(uint32_t) * id_size, GFP_KERNEL);
		if (!rf_val) {
			kfree(main_val);
			kfree(sub_val);
			return -ENOMEM;
		}

		of_property_read_u32_array(np, "rf-patterns", rf_val, id_size);
	} else {
		rf_val = NULL;
	}

	of_property_read_u32_array(np, "aboard-patterns", sub_val, id_size);
	of_property_read_u32_array(np, "match-projects", main_val, id_size);

	if (of_property_read_bool(np, "use_pmic_adc")) {
		hw_mask = pmic_get_submask(np, dev);
		if (hw_mask < 0) {
			ret = -EINVAL;
			goto read_failed;
		}
	} else if (of_property_read_bool(np, "use_tristate_gpio")) {
		dev_msg("tristate gpio judgement\n");
		set_gpios_active(dev_info);
		active_val = gpio_get_submask(np);
		set_gpios_sleep(dev_info);
		sleep_val  = gpio_get_submask(np);
		set_gpios_idle(dev_info);
		if (active_val == 1 && sleep_val == 0) {		/*high-resistance*/
			hw_mask = 0;
		} else if (active_val == 1 && sleep_val == 1) {		/*external pull-up*/
			hw_mask = 2;
		} else if (active_val == 0 && sleep_val == 0) {		/*external pull-down*/
			hw_mask = 1;
		}
		dev_msg("active_val[%d] sleep_val[%d] hw_mask[%d]\n", active_val, sleep_val, hw_mask);
		if (hw_mask < 0) {
			ret = -EINVAL;
			goto read_failed;
		}
	} else {
		dev_msg("normal gpio judgement\n");
		set_gpios_active(dev_info);
		hw_mask = gpio_get_submask(np);
		set_gpios_sleep(dev_info);
		if (hw_mask < 0) {
			ret = -EINVAL;
			goto read_failed;
		}
	}

	dev_msg("aboard mask 0x%x\n", hw_mask);

	for (i = 0; i < id_size; i++) {
		if (* (main_val + i) != get_project()) {
			continue;
		}

		if (* (sub_val + i) == hw_mask) {
			if (!rf_val) {
				dev_msg("rf_val is null, matched\n");
				match = true;
			} else {
				if (* (rf_val + i) == get_Modem_Version()) {
					match = true;
				} else {
					match = false;
				}
			}
		}

		if (match) {
			ret = 0;
			break;
		}
	}

	if (!match) {
		dev_msg("id not match\n");
		ret = -ENODEV;
		goto read_failed;
	}

read_failed:
	kfree(sub_val);
	kfree(main_val);
	if (rf_val) {
		kfree(rf_val);
	}

seccess:
	if (!ret) {
		info->manufacture = "rf-match";
	} else {
		info->manufacture = "rf-notmatch";
	}

#ifdef CONFIG_MTK_PLATFORM
	info->version = "MTK";
#else
	info->version = "QCOM";
#endif

	return ret;
}

#ifndef CONFIG_MTK_PLATFORM
static int __attribute__((__unused__)) init_ddr_type(struct device_info *dev_info)
{
	uint32_t ddr_type = DDR_TYPE_LPDDR5;
	struct manufacture_info *info = NULL;

	info = (struct manufacture_info *) kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		return -ENOMEM;
	}

	ddr_type = of_fdt_get_ddrtype();

	if (ddr_type == DDR_TYPE_LPDDR5 || ddr_type == DDR_TYPE_LPDDR5X) {
		info->version = "DDR5";
	} else if (ddr_type == DDR_TYPE_LPDDR4 || ddr_type == DDR_TYPE_LPDDR4X) {
		info->version = "DDR4";
	} else {
		info->version = "unknown";
	}

	return register_devinfo("ddr_type", info);
}
#endif
#endif  //OPLUS_DEVICE_INFO

static int devinfo_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s: ok\n", __func__);
#if 0
	struct device_info *dev_info;

	dev_info = kzalloc(sizeof(struct device_info), GFP_KERNEL);
	if (!dev_info) {
		dev_msg("failed to alloc memory\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&dev_info->dev_list);

	g_dev_info = dev_info;
	g_dev_info->dev = &pdev->dev;

	platform_set_drvdata(pdev, dev_info);
	/*parse dts first */
	parse_gpio_dts(&pdev->dev, dev_info);

	init_aboard_info(g_dev_info);

	/*init other hw id */
	set_gpios_active(dev_info);
	init_other_hw_ids(pdev);
	set_gpios_sleep(dev_info);
#ifndef CONFIG_MTK_PLATFORM
	/*register oplus special node*/
	init_ddr_type(dev_info);
#endif
#endif

	return 0;
}


static int devinfo_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s: ok\n", __func__);
#if 0
	if (g_parent) {
		remove_proc_entry("ufsplus_status", g_parent);
	}
	remove_proc_entry(DEVINFO_NAME, NULL);
#endif
	return 0;
}

static const struct of_device_id devinfo_id[] = {
    {.compatible = "oplus-devinfo", },
	{},
};

static struct platform_driver devinfo_platform_driver = {
	.probe = devinfo_probe,
	.remove = devinfo_remove,
	.driver = {
		.name = DEVINFO_NAME,
		.of_match_table = devinfo_id,
	},
};

static int __init
device_info_init(void)
{
	g_parent = proc_mkdir("devinfo", NULL);

	if (!g_parent) {
		dev_msg("proc_mkdir failed\n");
		return -ENODEV;
	}
	return platform_driver_register(&devinfo_platform_driver);
}

fs_initcall(device_info_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ming.He");
MODULE_DESCRIPTION("deviceinfo");
