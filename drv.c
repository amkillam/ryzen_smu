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
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <uapi/linux/stat.h>
#include <linux/version.h>

#include "smu.h"

#ifndef KBUILD_MODNAME
    #define KBUILD_MODNAME "ryzen_smu"
#endif

MODULE_AUTHOR("Leonardo Gates <leogatesx9r@protonmail.com>");
MODULE_DESCRIPTION("AMD Ryzen SMU Command Driver");
MODULE_VERSION("0.1.7");
MODULE_LICENSE("GPL");

#define MSEC_TO_NSEC(x)                    (x * 1000000)


#define PCI_DEVICE_ID_AMD_17H_ROOT          0x1450
#define PCI_DEVICE_ID_AMD_17H_M10H_ROOT     0x15d0
#define PCI_DEVICE_ID_AMD_17H_M60H_ROOT     0x1630
#define PCI_DEVICE_ID_AMD_17H_M30H_ROOT     0x1480

// https://github.com/torvalds/linux/blob/master/arch/x86/kernel/amd_nb.c
#define PCI_DEVICE_ID_AMD_1AH_M00H_ROOT     0x153a
#define PCI_DEVICE_ID_AMD_1AH_M20H_ROOT     0x1507
#define PCI_DEVICE_ID_AMD_1AH_M60H_ROOT     0x1122
#define PCI_DEVICE_ID_AMD_17H_MA0H_ROOT     0x14b5
#define PCI_DEVICE_ID_AMD_19H_M10H_ROOT     0x14a4
#define PCI_DEVICE_ID_AMD_19H_M40H_ROOT     0x14b5
#define PCI_DEVICE_ID_AMD_19H_M60H_ROOT     0x14d8
#define PCI_DEVICE_ID_AMD_19H_M70H_ROOT     0x14e8
#define PCI_DEVICE_ID_AMD_MI200_ROOT        0x14bb
#define PCI_DEVICE_ID_AMD_MI300_ROOT        0x14f8

#define MAX_ATTRS_LEN                      12

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
    #error "Unsupported kernel version. Minimum: v4.19"
#endif

/* bin_attribute read callbacks take a const attribute pointer from v6.16. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 16, 0)
    #define BIN_ATTR_CONST const
#else
    #define BIN_ATTR_CONST
#endif

#define __RO_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR | S_IRGRP | S_IROTH, attr##_show, attr_store_null);

#define __RW_ATTR(attr) \
    static struct kobj_attribute dev_attr_##attr = \
        __ATTR(attr, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, attr##_show, attr##_store);

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
    bool                    pm_table_file;
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
    .pm_table_file        = false,
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

/* Serialises readers of the shared table buffer. */
static DEFINE_MUTEX(pm_table_mutex);
static unsigned int pm_passes_open;      /* passes begun at offset 0 and not yet read to the end */
static unsigned long pm_last_refresh;    /* jiffies of the last refresh */

/* PM tables can exceed a page (Storm Peak 0x1E3C), so the table is a binary attribute: the kernel
 * serves it in page-sized chunks and, because .size is set before the file is created, clamps every
 * read to the table. Readers start at offset 0 and read until they have pm_table_size bytes (see
 * lib/libsmu.c): the read at offset 0 refreshes the table from the SMU, the following chunks of the
 * same pass serve that snapshot. So that overlapping readers do not tear each other's passes, the
 * snapshot is not refreshed while another pass is still between its first and last chunk; a pass
 * that never reads to the end holds it for at most one second. A read that does not begin at
 * offset 0 returns the current snapshot. */
static ssize_t pm_table_read(struct file *filp, struct kobject *kobj, BIN_ATTR_CONST struct bin_attribute *attr,
                             char *buf, loff_t off, size_t count) {
    size_t len = PM_TABLE_MAX_SIZE;
    size_t size;

    if (off < 0)
        return 0;

    mutex_lock(&pm_table_mutex);
    size = g_driver.pm_table_read_size;
    if ((size_t)off >= size) {
        mutex_unlock(&pm_table_mutex);
        return 0;
    }
    if (count > size - off)
        count = size - off;
    if (off == 0) {
        if (!pm_passes_open || time_after_eq(jiffies, pm_last_refresh + HZ)) {
            if (smu_read_pm_table(g_driver.device, g_driver.pm_table, &len) != SMU_Return_OK) {
                mutex_unlock(&pm_table_mutex);
                return -EIO;
            }
            pm_last_refresh = jiffies;
            pm_passes_open = 0;
        }
        pm_passes_open++;
    }
    memcpy(buf, g_driver.pm_table + off, count);
    if (off + count >= size && pm_passes_open)
        pm_passes_open--;
    mutex_unlock(&pm_table_mutex);
    return count;
}

static struct bin_attribute dev_battr_pm_table = {
    .attr = { .name = "pm_table", .mode = S_IRUSR | S_IRGRP | S_IROTH },
    .read = pm_table_read,
    /* .size is assigned from the probe's first successful read, before sysfs_create_bin_file(). */
};

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

    // PM Table Optional Pointers (size, version; the table itself is a binary attribute)
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
    int err;

    // Singleton driver: the first root complex to claim the state owns it. Parts
    // with several matching root complexes probe more than once, concurrently
    // under asynchronous probing, so the claim is an atomic compare-and-swap. A
    // later probe returns -ENODEV (which the driver core logs at debug level
    // only) and must neither overwrite the first device's pointer nor tear its
    // state down when its own kobject creation fails on the duplicate name.
    if (cmpxchg(&g_driver.device, NULL, dev) != NULL)
        return -ENODEV;

    // The optional attribute slots are filled only by the probe that creates the files.
    drv_attrs[MAX_ATTRS_LEN - 4] = NULL;
    drv_attrs[MAX_ATTRS_LEN - 3] = NULL;
    drv_attrs[MAX_ATTRS_LEN - 2] = NULL;

    pr_info("loading version: %s", THIS_MODULE->version);

    // Clamp values.
    if (smu_timeout_attempts > SMU_RETRIES_MAX)
        smu_timeout_attempts = SMU_RETRIES_MAX;
    if (smu_timeout_attempts < SMU_RETRIES_MIN)
        smu_timeout_attempts = SMU_RETRIES_MIN;

    // Detect processor class & figure out MP1/RSMU support.
    if (smu_init(g_driver.device) != 0) {
        pr_err("Failed to initialize the SMU for use");
        g_driver.device = NULL;
        return -ENODEV;
    }

    // Check if MP1 is working as we guarantee this support.
    if (ryzen_smu_get_version(MAILBOX_TYPE_MP1, 1) != 0) {
        pr_err("Failed to obtain the SMU version");
        smu_cleanup();
        g_driver.device = NULL;
        return -EINVAL;
    }

    // Check if RSMU is valid to determine if to skip PM table setup.
    if (ryzen_smu_get_version(MAILBOX_TYPE_RSMU, 0) == 0) {
        // We do something absolutely stupid here and use relative offsets to overwrite offsets
        //  in the drv_attrs[] array.
        //
        // This shouldn't *typically* cause errors unless the array structure is messed with.
        // So, we left a warning above to not touch it.
        drv_attrs[MAX_ATTRS_LEN - 4] = &dev_attr_rsmu_cmd.attr;
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
            pr_debug("Probe succeeded: read %zu bytes", g_driver.pm_table_read_size);

            // A non-zero size publishes the binary pm_table attribute below; the size and
            // version attributes follow it only if that file was created.
            dev_battr_pm_table.size = g_driver.pm_table_read_size;
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
        err = -ENOMEM;
        goto FAIL;
    }

    // The table file first: its size and version attributes are only offered when it exists.
    if (dev_battr_pm_table.size) {
        if (sysfs_create_bin_file(g_driver.drv_kobj, &dev_battr_pm_table)) {
            pr_err("Unable to create the pm_table attribute -- disabling feature");
            dev_battr_pm_table.size = 0;
        }
        else {
            g_driver.pm_table_file = true;
            drv_attrs[MAX_ATTRS_LEN - 3] = &dev_attr_pm_table_size.attr;
            if (g_driver.pm_table_version)
                drv_attrs[MAX_ATTRS_LEN - 2] = &dev_attr_pm_table_version.attr;
        }
    }

    err = sysfs_create_group(g_driver.drv_kobj, &drv_attr_group);
    if (err) {
        pr_err("Unable to create sysfs attribute group (%d)", err);
        if (g_driver.pm_table_file) {
            sysfs_remove_bin_file(g_driver.drv_kobj, &dev_battr_pm_table);
            g_driver.pm_table_file = false;
        }
        kobject_put(g_driver.drv_kobj);
        g_driver.drv_kobj = NULL;
        goto FAIL;
    }

    return 0;

FAIL:
    if (g_driver.pm_table) {
        kfree(g_driver.pm_table);
        g_driver.pm_table = NULL;
    }
    dev_battr_pm_table.size = 0;
    smu_cleanup();
    g_driver.device = NULL;
    return err;
}

static void ryzen_smu_remove(struct pci_dev *dev) {
    // Free allocated resources as well as the SMU
    if (g_driver.pm_table_file) {
        sysfs_remove_bin_file(g_driver.drv_kobj, &dev_battr_pm_table);
        g_driver.pm_table_file = false;
    }
    dev_battr_pm_table.size = 0;

    if (g_driver.pm_table) {
        kfree(g_driver.pm_table);
        g_driver.pm_table = NULL;
    }

    if (g_driver.drv_kobj) {
        kobject_put(g_driver.drv_kobj);
        g_driver.drv_kobj = NULL;
    }

    drv_attrs[MAX_ATTRS_LEN - 4] = NULL;
    drv_attrs[MAX_ATTRS_LEN - 3] = NULL;
    drv_attrs[MAX_ATTRS_LEN - 2] = NULL;
    pm_passes_open = 0;

    smu_cleanup();
    g_driver.device = NULL;
}

static struct pci_device_id ryzen_smu_id_table[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M10H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M30H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_M60H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_1AH_M00H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_1AH_M20H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_1AH_M60H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_17H_MA0H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M10H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M40H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M60H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_19H_M70H_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_MI200_ROOT) },
    { PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_MI300_ROOT) },
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
