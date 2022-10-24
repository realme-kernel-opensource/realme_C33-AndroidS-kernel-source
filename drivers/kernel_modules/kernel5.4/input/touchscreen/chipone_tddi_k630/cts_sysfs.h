#ifndef CTS_SYSFS_H
#define CTS_SYSFS_H

#include "cts_config.h"

struct device;

extern u16 cts_spi_speed;

#ifdef CONFIG_CTS_SYSFS
extern int cts_sysfs_add_device(struct device *dev);
extern void cts_sysfs_remove_device(struct device *dev);
#ifdef CONFIG_ARCH_SPRD
extern int cts_sysfs_add_suspend_device(struct device *dev);
extern void cts_sysfs_remove_suspend_device(struct device *dev);
#endif
#else /* CONFIG_CTS_SYSFS */
static inline int cts_sysfs_add_device(struct device *dev) {return -ENOTSUPP;}
static inline void cts_sysfs_remove_device(struct device *dev) {}
#endif /* CONFIG_CTS_SYSFS */

#endif /* CTS_SYSFS_H */
