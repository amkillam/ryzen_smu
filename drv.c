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

#ifndef KBUILD_MODNAME
    #define KBUILD_MODNAME "ryzen_smu"
#endif

MODULE_AUTHOR("Leonardo Gates <leogatesx9r@protonmail.com>");
MODULE_DESCRIPTION("AMD Ryzen SMU Command Driver");
MODULE_VERSION("0.1.3");
MODULE_LICENSE("GPL");

#define MSEC_TO_NSEC(x)                    (x * 1000000)

#define PCI_DEVICE_ID_AMD_17H_ROOT         0x1450
#define PCI_DEVICE_ID_AMD_17H_M10H_ROOT    0x15d0
#define PCI_DEVICE_ID_AMD_17H_M60H_ROOT    0x1630
#define PCI_DEVICE_ID_AMD_17H_M30H_ROOT    0x1480

#define MAX_ATTRS_LEN                      13

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
    #error "Unsupported kernel version. Minimum: v4.19"
#endif

#define __RO_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR, attr##_show, attr_store_null);

#define __RW_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR | S_IWUSR, attr##_show, attr##_store);

static struct ryzen_smu_data {
    struct pci_dev*         device;
    struct kobject*         drv_kobj;

    char                    smu_version[64];
    smu_req_args_t          smu_args;
    u32                     smu_rsp;

    u32                     smn_result;

    u8*                     pm_table;
    u32                     pm_table_version;
    size_t                  pm_table_read_size;
} g_driver = {
    .device               = NULL,

    .drv_kobj             = NULL,

    .smu_version          = { 0 },
    .smu_args             = { .args = { 0, 0, 0, 0, 0, 0 } },
    .smu_rsp              = SMU_Return_OK,

    .smn_result           = 0,

    .pm_table             = NULL,
    .pm_table_version     = 0,
    .pm_table_read_size   = PM_TABLE_MAX_SIZE,
};

/* SMU Command Parameters. */
uint smu_timeout_attempts = 8192;

static ssize_t attr_store_null(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    return 0;
}

static ssize_t drv_version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%s\n", THIS_MODULE->version);
}

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%s\n", g_driver.smu_version);
}

static ssize_t mp1_if_version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%d\n", smu_get_mp1_if_version());
}

static ssize_t codename_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    return sprintf(buff, "%02d\n", smu_get_codename());
}

static ssize_t pm_table_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    if (smu_read_pm_table(g_driver.device, g_driver.pm_table, &g_driver.pm_table_read_size) != SMU_Return_OK)
        return 0;

    memcpy(buff, g_driver.pm_table, g_driver.pm_table_read_size);
    return g_driver.pm_table_read_size;
}

static ssize_t pm_table_version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.pm_table_version);

    memcpy(buff, &g_driver.pm_table_version, sz);
    return sz;
}

static ssize_t pm_table_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.pm_table_read_size);

    memcpy(buff, &g_driver.pm_table_read_size, sz);
    return sz;
}

static ssize_t rsmu_cmd_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.smu_rsp);

    memcpy(buff, &g_driver.smu_rsp, sz);
    return sz;
}

static ssize_t rsmu_cmd_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    u32 op;

    // To date, there has never been a command that actually exceeds FFh
    //  so 32 bits is overkill but still support it.
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

    g_driver.smu_rsp = smu_send_command(g_driver.device, op, &g_driver.smu_args, MAILBOX_TYPE_RSMU);
    return count;
}

static ssize_t mp1_smu_cmd_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.smu_rsp);

    memcpy(buff, &g_driver.smu_rsp, sz);
    return sz;
}

static ssize_t mp1_smu_cmd_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    u32 op;

    // To date, there has never been a command that actually exceeds FFh
    //  so 32 bits is overkill but still support it.
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

    g_driver.smu_rsp = smu_send_command(g_driver.device, op, &g_driver.smu_args, MAILBOX_TYPE_MP1);
    return count;
}

static ssize_t hsmp_smu_cmd_show(struct kobject* kobj, struct kobj_attribute* attr, char* buff) {
    ssize_t sz = sizeof(g_driver.smu_rsp);

    memcpy(buff, &g_driver.smu_rsp, sz);
    return sz;
}

static ssize_t hsmp_smu_cmd_store(struct kobject* kobj, struct kobj_attribute* attr, const char* buff, size_t count) {
    u32 op;

    // To date, there has never been a command that actually exceeds FFh
    //  so 32 bits is overkill but still support it.
    switch (count) {
        case sizeof(u32) :
            op = *(u32*)buff;
            break;
            case sizeof(u8) :
                op = *(u8*)buff;
                break;
            default:
                return 0;
    }

    g_driver.smu_rsp = smu_send_command(g_driver.device, op, &g_driver.smu_args, MAILBOX_TYPE_HSMP);
    return count;
}

static ssize_t smu_args_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.smu_args);

    memcpy(buff, &g_driver.smu_args.args, sz);
    return sz;
}

static ssize_t smu_args_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff, size_t count) {
    if (count != sizeof(u32) * 6)
        return 0;

    memcpy(g_driver.smu_args.args, buff, count);
    return count;
}

static ssize_t smn_show(struct kobject *kobj, struct kobj_attribute *attr, char *buff) {
    ssize_t sz = sizeof(g_driver.smn_result);

    memcpy(buff, &g_driver.smn_result, sz);
    return sz;
}

static ssize_t smn_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buff,
size_t count) {
    u32 address, value;

    switch (count) {
        case sizeof(u32):
            // One word written means we read this address at buff[0]
            address = *(u32*)buff;

            if (smu_read_address(g_driver.device, address, &g_driver.smn_result) != SMU_Return_OK)
                pr_debug("Failed to read SMN address 0x%x\n", address);
            break;
        case (sizeof(u32) * 2):
            // Two words written means we write the second word to the address of the first word
            address = *(u32*)buff;
            value = *(u32*)(buff + sizeof(u32));

            if (smu_write_address(g_driver.device, address, value) != SMU_Return_OK) {
                pr_debug("Failed to write SMN address 0x%x with value 0x%x\n", address, value);
                g_driver.smn_result = SMU_Return_PCIFailed;
            }
            else
                g_driver.smn_result = SMU_Return_OK;
            break;
        default:
            return 0;
    }

    return count;
}

__RO_ATTR (drv_version);
__RO_ATTR (version);
__RO_ATTR (mp1_if_version);
__RO_ATTR (codename);

__RO_ATTR (pm_table);
__RO_ATTR (pm_table_size);
__RO_ATTR (pm_table_version);

__RW_ATTR (rsmu_cmd);
__RW_ATTR (mp1_smu_cmd);
__RW_ATTR (hsmp_smu_cmd);
__RW_ATTR (smu_args);

__RW_ATTR (smn);

static struct attribute *drv_attrs[MAX_ATTRS_LEN] = {
    &dev_attr_drv_version.attr,
    &dev_attr_version.attr,
    &dev_attr_mp1_if_version.attr,
    &dev_attr_codename.attr,

    &dev_attr_smu_args.attr,
    &dev_attr_mp1_smu_cmd.attr,
    &dev_attr_hsmp_smu_cmd.attr,

    &dev_attr_smn.attr,

    // -- NOTE: Do not edit below here. --

    // RSMU Optional Pointer
    NULL,

    // PM Table Optional Pointers
    NULL,
    NULL,
    NULL,

    // Termination Pointer
    NULL,
};

static struct attribute_group drv_attr_group = {
    .attrs = drv_attrs,
};

static int ryzen_smu_get_version(enum smu_mailbox mb, int show) {
    u32 ver;

    ver = smu_get_version(g_driver.device, mb);
    if (ver >= 0 && ver <= 0xFF) {
        pr_err("Failed to query the %sSMU version: %d",
            mb == MAILBOX_TYPE_RSMU ? "R" : "MP1 ", ver);
        return -EINVAL;
    }

    // In case this just tests for mailbox functionality, we don't need to output anything.
    if (show) {
        if (ver & 0xFF000000)
            sprintf(g_driver.smu_version, "%d.%d.%d.%d",
                (ver >> 24) & 0xff, (ver >> 16) & 0xff, (ver >> 8) & 0xff, ver & 0xff);
        else
            sprintf(g_driver.smu_version, "%d.%d.%d", (ver >> 16) & 0xff, (ver >> 8) & 0xff, ver & 0xff);

        pr_info("SMU v%s", g_driver.smu_version);
    }

    return 0;
}

static int ryzen_smu_probe(struct pci_dev *dev, const struct pci_device_id *id) {
    enum smu_return_val ret;

    g_driver.device = dev;

    // Clamp values.
    if (smu_timeout_attempts > SMU_RETRIES_MAX)
        smu_timeout_attempts = SMU_RETRIES_MAX;
    if (smu_timeout_attempts < SMU_RETRIES_MIN)
        smu_timeout_attempts = SMU_RETRIES_MIN;

    // Detect processor class & figure out MP1/RSMU support.
    if (smu_init(g_driver.device) != 0) {
        pr_err("Failed to initialize the SMU for use");
        return -ENODEV;
    }

    // Check if MP1 is working as we guarantee this support.
    if (ryzen_smu_get_version(MAILBOX_TYPE_MP1, 1) != 0) {
        pr_err("Failed to obtain the SMU version");
        return -EINVAL;
    }

    // Check if RSMU is valid to determine if to skip PM table setup.
    if (ryzen_smu_get_version(MAILBOX_TYPE_RSMU, 0) == 0) {
        // We do something absolutely stupid here and use relative offsets to overwrite offsets
        //  in the drv_attrs[] array.
        //
        // This shouldn't *typically* cause errors unless the array structure is messed with.
        // So, we left a warning above to not touch it.
        drv_attrs[MAX_ATTRS_LEN - 5] = &dev_attr_rsmu_cmd.attr;
    }
    else {
        pr_info("RSMU Mailbox: Disabled or not responding to commands.");
        goto _CONTINUE_SETUP;
    }

    // Check that PM table options are supported before adding it to the attr list
    ret = smu_transfer_table_to_dram(g_driver.device);
    if (ret == SMU_Return_OK) {
        ret = smu_get_pm_table_version(g_driver.device, &g_driver.pm_table_version);
        if (ret != SMU_Return_OK && ret != SMU_Return_Unsupported) {
            pr_err("Unable to resolve which PM table version the system uses -- disabling "
                "feature (%d)", ret);
            goto _CONTINUE_SETUP;
        }

        g_driver.pm_table = kzalloc(PM_TABLE_MAX_SIZE, GFP_KERNEL);
        if (g_driver.pm_table == NULL) {
            pr_err("Unable to allocate kernel buffer for PM table mapping -- disabling PM table "
                "feature");
            goto _CONTINUE_SETUP;
        }

        // Perform an initial fill of the data for when the device is queued, saving time
        pr_debug("Probing the PM table for state changes");
        ret = smu_read_pm_table(dev, g_driver.pm_table, &g_driver.pm_table_read_size);
        if (ret == SMU_Return_OK) {
            pr_debug("Probe succeeded: read %ld bytes", g_driver.pm_table_read_size);

            drv_attrs[MAX_ATTRS_LEN - 4] = &dev_attr_pm_table_size.attr;
            drv_attrs[MAX_ATTRS_LEN - 3] = &dev_attr_pm_table.attr;

            if (g_driver.pm_table_version)
                drv_attrs[MAX_ATTRS_LEN - 2] = &dev_attr_pm_table_version.attr;
        }
        else
            pr_err("Failed to probe the PM table -- disabling feature (%d)", ret);
    }
    else {
        pr_debug("Notice: PM tables are not supported for the current platform (%d)", ret);
    }

_CONTINUE_SETUP:
    // Allocate the sysfs attr group with the parameters for use
    g_driver.drv_kobj = kobject_create_and_add("ryzen_smu_drv", kernel_kobj);
    if (!g_driver.drv_kobj) {
        pr_err("Unable to create sysfs interface");
        return -ENOMEM;
    }

    if (sysfs_create_group(g_driver.drv_kobj, &drv_attr_group))
        kobject_put(g_driver.drv_kobj);

    return 0;
}

static void ryzen_smu_remove(struct pci_dev *dev) {
    // Free allocated resources as well as the SMU
    if (g_driver.pm_table)
        kfree(g_driver.pm_table);

    if (g_driver.drv_kobj)
        kobject_put(g_driver.drv_kobj);

    smu_cleanup();
}

static struct pci_device_id ryzen_smu_id_table[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M10H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M30H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M60H_ROOT) },
    { }
};
MODULE_DEVICE_TABLE(pci, ryzen_smu_id_table);

static struct pci_driver ryzen_smu_driver = {
    .id_table = ryzen_smu_id_table,
    .remove = ryzen_smu_remove,
    .probe = ryzen_smu_probe,
    .name = KBUILD_MODNAME,
};

static int __init ryzen_smu_driver_init(void) {
    // By default the driver will not be used to communicate with the
    //  northbridge so we forcefully tell the system to use it.
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

module_param(smu_timeout_attempts, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(smu_timeout_attempts, "When executing an SMU command, the driver will retry this many times before considering a command to have timed out. Default: 8192");
