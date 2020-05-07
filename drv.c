/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com> */
/* Ryzen SMU Command Driver */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <uapi/linux/stat.h>
#include <linux/version.h>

#include "smu.h"

MODULE_AUTHOR("Leonardo Gates <leogatesx9r@protonmail.com>");
MODULE_DESCRIPTION("AMD Ryzen SMU Command Driver");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");

#define PCI_VENDOR_ID_AMD                  0x1022
#define PCI_DEVICE_ID_AMD_17H_ROOT         0x1450
#define PCI_DEVICE_ID_AMD_17H_M10H_ROOT    0x15d0
#define PCI_DEVICE_ID_AMD_17H_M30H_ROOT    0x1480

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
    #error "Unsupported kernel version. Minimum: v4.19"
#endif

static struct ryzen_smu_data {
    struct pci_dev*         device;
    struct kobject*         drv_kobj;

    char                    smu_version[64];
    u32                     smu_args[6];
    u32                     smu_rsp;

    u32                     smn_result;

    u8*                     pm_table;
    size_t                  pm_table_read_size;
} g_driver = {
    .device               = NULL,

    .drv_kobj             = NULL,

    .smu_version          = { 0 },
    .smu_args             = { 0, 0, 0, 0, 0, 0 },
    .smu_rsp              = SMU_Return_OK,

    .smn_result           = 0,

    .pm_table             = NULL,
    .pm_table_read_size   = PM_TABLE_MAX_SIZE,
};

/* SMU Command Parameters. */
uint smu_pm_update_ms = 1000;
uint smu_timeout_ms = 1000;

static ssize_t attr_store_null(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    return 0;
}

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%s\n", g_driver.smu_version);
}

static ssize_t codename_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%d\n", smu_get_codename());
}

static ssize_t pm_table_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    if (smu_read_pm_table(g_driver.device, g_driver.pm_table, &g_driver.pm_table_read_size) != SMU_Return_OK)
        return 0;

    memcpy(buff, g_driver.pm_table, g_driver.pm_table_read_size);
    return g_driver.pm_table_read_size;
}

static ssize_t pm_table_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%ld\n", g_driver.pm_table_read_size);
}

static ssize_t smu_cmd_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%02x\n", g_driver.smu_rsp);
}

static ssize_t smu_cmd_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    u32 op;

    switch (count) {
        case sizeof(u32):
            op = *(u32*)buff;
            break;
        case sizeof(u8):
            op = *(u8*)buff;
            break;
        default:
            return 0;
    }

    g_driver.smu_rsp = smu_send_command(g_driver.device, op, g_driver.smu_args, 6);
    return count;
}

static ssize_t smu_args_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%08x%08x%08x%08x%08x%08x\n",
        g_driver.smu_args[0], g_driver.smu_args[1], g_driver.smu_args[2],
        g_driver.smu_args[3], g_driver.smu_args[4], g_driver.smu_args[5]);
}

static ssize_t smu_args_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    if (count != sizeof(u32) * 6)
        return 0;

    #define BUFF_STORE_WORD(n)  g_driver.smu_args[n] = *(u32*)(buff + (n * sizeof(u32)));
    BUFF_STORE_WORD(0);
    BUFF_STORE_WORD(1);
    BUFF_STORE_WORD(2);
    BUFF_STORE_WORD(3);
    BUFF_STORE_WORD(4);
    BUFF_STORE_WORD(5);

    return count;
}

static ssize_t smn_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%08x\n", g_driver.smn_result);
}

static ssize_t smn_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    switch (count) {
        case sizeof(u32):
            g_driver.smn_result = smu_read_address(g_driver.device, *(u32*)buff);
            break;
        case (sizeof(u32) * 2):
            smu_write_address(g_driver.device, *(u32*)buff, *(u32*)(buff + sizeof(u32)));
            g_driver.smn_result = 0;
            break;
        default:
            return 0;
    }

    return count;
}

#define __RO_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR, attr##_show, attr_store_null);

#define __RW_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR | S_IWUSR, attr##_show, attr##_store);


__RO_ATTR (version);
__RO_ATTR (codename);

__RO_ATTR (pm_table);
__RO_ATTR (pm_table_size);

__RW_ATTR (smu_cmd);
__RW_ATTR (smu_args);

__RW_ATTR (smn);

static struct attribute *drv_attrs[8] = {
    &dev_attr_version.attr,
    &dev_attr_codename.attr,
    &dev_attr_smu_args.attr,
    &dev_attr_smu_cmd.attr,
    &dev_attr_smn.attr,
    // PM Table Optional Pointers
    NULL,
    NULL,
    // Termination Pointer
    NULL,
};

static struct attribute_group drv_attr_group = {
    .attrs = drv_attrs,
};

static int ryzen_smu_get_version(void) {
    u32 ver;

    ver = smu_get_version(g_driver.device);
    if (ver >= 0 && ver <= 0xFF) {
        pr_err("Failed to query the SMU version: %d", ver);
        return -EINVAL;
    }

    sprintf(g_driver.smu_version, "%d.%d.%d", (ver >> 16) & 0xff, (ver >> 8) & 0xff, ver & 0xff);
    pr_info("SMU v%s", g_driver.smu_version);

    return 0;
}

static int ryzen_smu_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    g_driver.device = dev;

    /* Clamp values. */
    if (smu_pm_update_ms > PM_TABLE_MAX_UPDATE_TIME_MS)
        smu_pm_update_ms = PM_TABLE_MAX_UPDATE_TIME_MS;
    if (smu_pm_update_ms < PM_TABLE_MIN_UPDATE_TIME_MS)
        smu_pm_update_ms = PM_TABLE_MIN_UPDATE_TIME_MS;
    
    if (smu_timeout_ms > SMU_TIMEOUT_MAX_MS)
        smu_timeout_ms = SMU_TIMEOUT_MAX_MS;
    if (smu_timeout_ms < SMU_TIMEOUT_MIN_MS)
        smu_timeout_ms = SMU_TIMEOUT_MIN_MS;

    if (smu_init(g_driver.device) != 0) {
        pr_err("Failed to initialize the SMU for use");
        return -ENODEV;
    }

    if (ryzen_smu_get_version() != 0) {
        pr_err("Failed to obtain the SMU version");
        return -EINVAL;
    }

    if (smu_probe_pm_table(g_driver.device) == SMU_Return_OK) {
        g_driver.pm_table = kzalloc(PM_TABLE_MAX_SIZE, GFP_KERNEL);

        if (g_driver.pm_table == NULL) {
            pr_err("Unable to allocate kernel buffer for PM table mapping");
            goto _CONTINUE_SETUP;
        }

        pr_debug("Probing the PM table for state changes");
        if (smu_read_pm_table(dev, g_driver.pm_table, &g_driver.pm_table_read_size) == SMU_Return_OK) {
            pr_debug("Probe succeeded: read %ld bytes\n", g_driver.pm_table_read_size);
            drv_attrs[5] = &dev_attr_pm_table_size.attr;
            drv_attrs[6] = &dev_attr_pm_table.attr;
        }
        else
            pr_err("Failed to probe the PM table. Disabling feature.");
    }

_CONTINUE_SETUP:

    g_driver.drv_kobj = kobject_create_and_add("ryzen_smu_drv", kernel_kobj);
    if (!g_driver.drv_kobj) {
        pr_err("Unable to create sysfs interface");
        goto _CONTINUE_SETUP;
    }

    if (sysfs_create_group(g_driver.drv_kobj, &drv_attr_group))
        kobject_put(g_driver.drv_kobj);

    return 0;
}

static void ryzen_smu_remove(struct pci_dev *dev) {
    if (g_driver.pm_table)
        kfree(g_driver.pm_table);

    if (g_driver.drv_kobj)
        kobject_put(g_driver.drv_kobj);
}

static struct pci_device_id ryzen_smu_id_table[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M10H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M30H_ROOT) },
    { }
};
MODULE_DEVICE_TABLE(pci, ryzen_smu_id_table);

static struct pci_driver ryzen_smu_driver = {
    .id_table = ryzen_smu_id_table,
    .remove = ryzen_smu_remove,
    .probe = ryzen_smu_probe,
    .name = "ryzen_smu",
};

static int __init ryzen_smu_driver_init(void) {
    if (pci_register_driver(&ryzen_smu_driver) < 0) {
        pr_err("Failed to register the PCI driver.");
        return 1;
    }

    return 0;
}

static void ryzen_smu_driver_exit(void) {
    pci_unregister_driver(&ryzen_smu_driver);
}

module_init(ryzen_smu_driver_init);
module_exit(ryzen_smu_driver_exit);

module_param(smu_pm_update_ms, uint, 0644);
MODULE_PARM_DESC(smu_pm_update_ms, "Controls how often in milliseconds, the SMU is commanded to update the PM table. Default: 1000ms");

module_param(smu_timeout_ms, uint, 0644);
MODULE_PARM_DESC(smu_timeout_ms, "Waits at most, this many milliseconds till an executing SMU command is determined to have timed out. Default: 1000ms");