/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com> */
/* Ryzen SMU Root Complex Communication */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <asm/io.h>

#include "smu.h"

static struct {
    smu_processor_codename codename;

    // Optional RSMU mailbox addresses.
    u32 addr_rsmu_mb_cmd;
    u32 addr_rsmu_mb_rsp;
    u32 addr_rsmu_mb_args;

    // Mandatory MP1 mailbox addresses.
    smu_if_version mp1_if_ver;
    u32 addr_mp1_mb_cmd;
    u32 addr_mp1_mb_rsp;
    u32 addr_mp1_mb_args;

    u32 addr_hsmp_mb_cmd;
    u32 addr_hsmp_mb_rsp;
    u32 addr_hsmp_mb_args;

    // Optional PM table information.
    u64 pm_dram_base;
    u32 pm_dram_base_alt;
    u32 pm_dram_map_size;
    u32 pm_dram_map_size_alt;

    // Internal tracker to determine the minimum interval required to
    // refresh the metrics table.
    u32 pm_jiffies;

    // Virtual addresses mapped to physical DRAM bases for PM table.
    u8 __iomem* pm_table_virt_addr;
    u8 __iomem* pm_table_virt_addr_alt;
} g_smu = {
    .codename = CODENAME_UNDEFINED,

    .addr_rsmu_mb_cmd = 0,
    .addr_rsmu_mb_rsp = 0,
    .addr_rsmu_mb_args = 0,

    .mp1_if_ver = IF_VERSION_COUNT,
    .addr_mp1_mb_cmd = 0,
    .addr_mp1_mb_rsp = 0,
    .addr_mp1_mb_args = 0,

    .addr_hsmp_mb_cmd = 0,
    .addr_hsmp_mb_rsp = 0,
    .addr_hsmp_mb_args = 0,

    .pm_dram_base = 0,
    .pm_dram_base_alt = 0,
    .pm_dram_map_size = 0,
    .pm_dram_map_size_alt = 0,
    .pm_jiffies = 0,

    .pm_table_virt_addr = NULL,
    .pm_table_virt_addr_alt = NULL,
};

// Both mutexes are defined separately because the SMN address space can be used
//  independently from the SMU but the SMU requires access to the SMN to execute commands.
static DEFINE_MUTEX(amd_pci_mutex);
static DEFINE_MUTEX(amd_smu_mutex);

int smu_smn_rw_address(const struct pci_dev* dev, const u32 address, u32* value, const int write) {
    mutex_lock(&amd_pci_mutex); // This may work differently for multi-NUMA systems.

    int err = pci_write_config_dword(dev, SMU_PCI_ADDR_REG, address);

    if (err) {
        pr_warn("Error programming SMN address: 0x%x!\n", address);
        goto exit;
    }

    err = write ? pci_write_config_dword(dev, SMU_PCI_DATA_REG, *value) : pci_read_config_dword(dev, SMU_PCI_DATA_REG, value);
    if (err)
        pr_warn("Error %s SMN address: 0x%x!\n", write ? "writing" : "reading", address);

exit:
    mutex_unlock(&amd_pci_mutex);
    return err;
}

smu_return_val smu_read_address(const struct pci_dev* dev, const u32 address, u32* value) {
    return !smu_smn_rw_address(dev, address, value, 0) ? SMU_Return_OK : SMU_Return_PCIFailed;
}

smu_return_val smu_write_address(const struct pci_dev* dev, const u32 address, u32 value) {
    return !smu_smn_rw_address(dev, address, &value, 1) ? SMU_Return_OK : SMU_Return_PCIFailed;
}

void smu_args_init(smu_req_args_t* args, const u32 value) {
    args->args[0] = value;

    for (u32 i = 1; i < SMU_REQ_MAX_ARGS; ++i)
        args->args[i] = 0;
}

smu_return_val smu_send_command(const struct pci_dev* dev, const u32 op, smu_req_args_t* args, const smu_mailbox mailbox) {
    u32 retries, tmp, rsp_addr, args_addr, cmd_addr;

    // Pick the correct mailbox address.
    switch (mailbox) {
        case MAILBOX_TYPE_RSMU: {
            rsp_addr = g_smu.addr_rsmu_mb_rsp;
            cmd_addr = g_smu.addr_rsmu_mb_cmd;
            args_addr = g_smu.addr_rsmu_mb_args;
        }
            break;
        case MAILBOX_TYPE_MP1: {
            rsp_addr = g_smu.addr_mp1_mb_rsp;
            cmd_addr = g_smu.addr_mp1_mb_cmd;
            args_addr = g_smu.addr_mp1_mb_args;
        }
            break;
        case MAILBOX_TYPE_HSMP: {
            rsp_addr = g_smu.addr_hsmp_mb_rsp;
            cmd_addr = g_smu.addr_hsmp_mb_cmd;
            args_addr = g_smu.addr_hsmp_mb_args;
        }
            break;
        default:
            return SMU_Return_Unsupported;
    }

    // In the unlikely event a mailbox is undefined, don't even attempt to execute.
    if (!rsp_addr || !cmd_addr || !args_addr)
        return SMU_Return_Unsupported;

    pr_debug("SMU Service Request: ID(0x%x) Args(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)",
        op, args->s.arg0, args->s.arg1, args->s.arg2, args->s.arg3, args->s.arg4, args->s.arg5);

    mutex_lock(&amd_smu_mutex);

    // Step 1: Wait until the RSP register is non-zero.
    retries = smu_timeout_attempts;
    do {
        if (smu_read_address(dev, rsp_addr, &tmp) != SMU_Return_OK) {
            mutex_unlock(&amd_smu_mutex);
            pr_warn("Failed to perform initial probe on SMU RSP!\n");
            return SMU_Return_PCIFailed;
        }
    } while (tmp == 0 && retries--);

    // Step 1.b: A command is still being processed meaning a new command cannot be issued.
    if (!retries && !tmp) {
        mutex_unlock(&amd_smu_mutex);
        pr_debug("SMU Service Request Failed: Timeout on initial wait for mailbox availability.");
        return SMU_Return_CommandTimeout;
    }

    // Step 2: Write zero (0) to the RSP register.
    smu_write_address(dev, rsp_addr, 0);

    // Step 3: Write the argument(s) into the argument register(s).
    for (u32 i = 0; i < SMU_REQ_MAX_ARGS; ++i)
        smu_write_address(dev, args_addr + (i * 4), args->args[i]);

    // Step 4: Write the message Id into the Message ID register.
    smu_write_address(dev, cmd_addr, op);

    // Step 5: Wait until the Response register is non-zero.
    do {
        if (smu_read_address(dev, rsp_addr, &tmp) != SMU_Return_OK) {
            mutex_unlock(&amd_smu_mutex);
            pr_warn("Failed to perform probe on SMU RSP!\n");
            return SMU_Return_PCIFailed;
        }
    } while(tmp == 0 && retries--);

    // Step 6: If the Response register contains OK, then SMU has finished processing the message.
    if (tmp != SMU_Return_OK && !retries) {
        mutex_unlock(&amd_smu_mutex);

        // The RSP register is still 0, the SMU is still processing the request or has frozen.
        // Either way the command has timed out so indicate as such.
        if (!tmp) {
            pr_debug("SMU Service Request Failed: Timeout on command (0x%x) after %d attempts.", op, smu_timeout_attempts);
            return SMU_Return_CommandTimeout;
        }

        pr_debug("SMU Service Request Failed: Response %Xh was unexpected.", tmp);
        return tmp;
    }

    // Step 7: If a return argument is expected, the Argument register may be read at this time.
    for (u32 i = 0; i < SMU_REQ_MAX_ARGS; ++i) {
        if (smu_read_address(dev, args_addr + (i * 4), &args->args[i]) != SMU_Return_OK)
            pr_warn("Failed to fetch SMU ARG [%d]!\n", i);
    }

    mutex_unlock(&amd_smu_mutex);

    pr_debug("SMU Service Response: ID(0x%x) Args(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)",
        op, args->s.arg0, args->s.arg1, args->s.arg2, args->s.arg3, args->s.arg4, args->s.arg5);

    return SMU_Return_OK;
}

int smu_resolve_cpu_class(void) {
    // https://en.wikichip.org/wiki/amd/cpuid
    // Res. + ExtFamily + ExtModel + Res. + BaseFamily + BaseModel + Stepping
    // See: CPUID_Fn00000001_EAX
    const u32 cpuid = cpuid_eax(0x00000001);
    const u32 cpu_family = ((cpuid & 0xf00) >> 8) + ((cpuid & 0xff00000) >> 20);
    const u32 cpu_model = ((cpuid & 0xf0000) >> 12) + ((cpuid & 0xf0) >> 4);

    // Combines "PkgType" and "Reserved"
    // See: CPUID_Fn80000001_EBX
    const u32 pkg_type = cpuid_ebx(0x80000001) >> 28;

    pr_info("CPUID: family 0x%X, model 0x%X, package 0x%X", cpu_family, cpu_model, pkg_type);

    switch (cpu_family) {
        case 0x17: { // Zen, Zen+, Zen 2
            switch (cpu_model) {
                case 0x01: {
                    switch (pkg_type) {
                        case 4: g_smu.codename = CODENAME_NAPLES; break;
                        case 7: g_smu.codename = CODENAME_THREADRIPPER; break;
                        default: g_smu.codename = CODENAME_SUMMITRIDGE; break;
                    }
                }
                    break;
                case 0x08: {
                    switch (pkg_type) {
                        case 4:
                        case 7: g_smu.codename = CODENAME_COLFAX; break;
                        default: g_smu.codename = CODENAME_PINNACLERIDGE; break;
                    }
                }
                    break;
                case 0x11:
                    g_smu.codename = CODENAME_RAVENRIDGE;
                    break;
                case 0x18: {
                    switch (pkg_type) {
                        case 2: g_smu.codename = CODENAME_RAVENRIDGE2; break;
                        default: g_smu.codename = CODENAME_PICASSO; break;
                    }
                }
                    break;
                case 0x20:
                    g_smu.codename = CODENAME_DALI;
                    break;
                case 0x31:
                    g_smu.codename = CODENAME_CASTLEPEAK;
                    break;
                case 0x60:
                    g_smu.codename = CODENAME_RENOIR;
                    break;
                case 0x68:
                    g_smu.codename = CODENAME_LUCIENNE;
                    break;
                case 0x71:
                    g_smu.codename = CODENAME_MATISSE;
                    break;
                case 0x90:
                    g_smu.codename = CODENAME_VANGOGH;
                    break;
                default: {
                    pr_err("CPUID: Unknown Zen/Zen+/Zen2 processor model: 0x%X (CPUID: 0x%08X)", cpu_model, cpuid);
                    return -2;
                }
            }
        }
            break;
        case 0x19: { // Zen3, Zen4
            // At least from Zen3 onward AMD reserves 16 model IDs per generation
            // Chagall: 0x00-0x0F, Stormpeak: 0x10-0x1f, etc...
            // Ryzen Master uses this full reserved range to identify and probe CPUs unlike us
            switch (cpu_model) {
                case 0x01:
                    g_smu.codename = CODENAME_MILAN;
                    break;
                case 0x08:
                    g_smu.codename = CODENAME_CHAGALL;
                    break;
                case 0x18:
                    g_smu.codename = CODENAME_STORMPEAK;
                    break;
                case 0x20:
                case 0x21:
                    g_smu.codename = CODENAME_VERMEER;
                    break;
                case 0x40:
                case 0x44:
                    g_smu.codename = CODENAME_REMBRANDT;
                    break;
                case 0x50:
                    g_smu.codename = CODENAME_CEZANNE;
                    break;
                case 0x61:
                    g_smu.codename = CODENAME_RAPHAEL;
                    break;
                case 0x74:
                    g_smu.codename = CODENAME_PHOENIX;
                    break;
                case 0x75:
                    g_smu.codename = CODENAME_HAWKPOINT;
                    break;
                default: {
                    pr_err("CPUID: Unknown Zen3/4 processor model: 0x%X (CPUID: 0x%08X)", cpu_model, cpuid);
                    return -2;
                }
            }
        }
            break;
        case 0x1a: { // Zen 5
            switch (cpu_model) {
                case 0x24:
                    g_smu.codename = CODENAME_STRIXPOINT;
                    break;
                case 0x44:
                    g_smu.codename = CODENAME_GRANITERIDGE;
                    break;
                case 0x70: // Strix Halo (AI MAX+ 395)
                default: {
                    pr_err("CPUID: Unknown Zen5/6 processor model: 0x%X (CPUID: 0x%08X)", cpu_model, cpuid);
                    return -2;
                }
            }
        }
            break;
        default: {
            pr_err("CPUID: Unknown Zen processor family (%Xh).", cpu_family);
            return -1;
        }
    }

    return 0;
}

static int detect_rsmu_address(void) {
    switch (g_smu.codename) {
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK: {
            g_smu.addr_rsmu_mb_cmd  = 0x3B10524;
            g_smu.addr_rsmu_mb_rsp  = 0x3B10570;
            g_smu.addr_rsmu_mb_args = 0x3B10A40;
        }
            break;
        case CODENAME_COLFAX:
        case CODENAME_NAPLES:
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_PINNACLERIDGE: {
            g_smu.addr_rsmu_mb_cmd  = 0x3B1051C;
            g_smu.addr_rsmu_mb_rsp  = 0x3B10568;
            g_smu.addr_rsmu_mb_args = 0x3B10590;
        }
            break;
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_PICASSO:
        case CODENAME_CEZANNE:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
        case CODENAME_DALI:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_STRIXPOINT:
        case CODENAME_HAWKPOINT: {
            g_smu.addr_rsmu_mb_cmd  = 0x3B10A20;
            g_smu.addr_rsmu_mb_rsp  = 0x3B10A80;
            g_smu.addr_rsmu_mb_args = 0x3B10A88;
        }
            break;
        case CODENAME_VANGOGH: {
            pr_debug("RSMU Mailbox: Not supported or unknown, disabling use.");
            return 0;
        }
        default:
            return -1;
    }

    pr_debug("RSMU Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)", g_smu.addr_rsmu_mb_cmd, g_smu.addr_rsmu_mb_rsp, g_smu.addr_rsmu_mb_args);
    return 0;
}

static int detect_hsmp_address(void) {
    switch (g_smu.codename) {
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK: {
            g_smu.addr_hsmp_mb_cmd = 0x3B10534;
            g_smu.addr_hsmp_mb_rsp = 0x3B10980;
            g_smu.addr_hsmp_mb_args = 0x3B109E0;
        }
            break;
        case CODENAME_CEZANNE:
        case CODENAME_COLFAX:
        case CODENAME_NAPLES:
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_PINNACLERIDGE:
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
        case CODENAME_DALI:
        case CODENAME_VANGOGH:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_STRIXPOINT:
        case CODENAME_HAWKPOINT:
            return 0;
        default:
            return -1;
    }

    pr_debug("HSMP Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)", g_smu.addr_hsmp_mb_cmd, g_smu.addr_hsmp_mb_rsp, g_smu.addr_hsmp_mb_args);
    return 0;
}

static int detect_mp1_address(void) {
    switch (g_smu.codename) {
        case CODENAME_COLFAX:
        case CODENAME_NAPLES:
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_PINNACLERIDGE: {
            g_smu.mp1_if_ver        = IF_VERSION_9;
            g_smu.addr_mp1_mb_cmd   = 0x3B10528;
            g_smu.addr_mp1_mb_rsp   = 0x3B10564;
            g_smu.addr_mp1_mb_args  = 0x3B10598;
        }
            break;
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
        case CODENAME_DALI: {
            g_smu.mp1_if_ver        = IF_VERSION_10;
            g_smu.addr_mp1_mb_cmd   = 0x3B10528;
            g_smu.addr_mp1_mb_rsp   = 0x3B10564;
            g_smu.addr_mp1_mb_args  = 0x3B10998;
        }
            break;
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_CASTLEPEAK:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK: {
            g_smu.mp1_if_ver        = IF_VERSION_11;
            g_smu.addr_mp1_mb_cmd   = 0x3B10530;
            g_smu.addr_mp1_mb_rsp   = 0x3B1057C;
            g_smu.addr_mp1_mb_args  = 0x3B109C4;
        }
            break;
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_CEZANNE: {
            g_smu.mp1_if_ver        = IF_VERSION_12;
            g_smu.addr_mp1_mb_cmd   = 0x3B10528;
            g_smu.addr_mp1_mb_rsp   = 0x3B10564;
            g_smu.addr_mp1_mb_args  = 0x3B10998;
        }
            break;
        case CODENAME_VANGOGH:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_HAWKPOINT: {
            g_smu.mp1_if_ver       = IF_VERSION_13;
            g_smu.addr_mp1_mb_cmd   = 0x3B10528;
            g_smu.addr_mp1_mb_rsp   = 0x3B10578;
            g_smu.addr_mp1_mb_args  = 0x3B10998;
        }
            break;
        case CODENAME_STRIXPOINT: {
            g_smu.mp1_if_ver       = IF_VERSION_13;
            g_smu.addr_mp1_mb_cmd   = 0x3b10928;
            g_smu.addr_mp1_mb_rsp   = 0x3b10978;
            g_smu.addr_mp1_mb_args  = 0x3b10998;
        }
            break;
        default:
            return -1;
    }

    pr_debug("MP1 Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)", g_smu.addr_mp1_mb_cmd, g_smu.addr_mp1_mb_rsp, g_smu.addr_mp1_mb_args);
    return 0;
}

int smu_init(void) {
    // This really should never be called twice however in case it is, consider it initialized.
    if (g_smu.codename != CODENAME_UNDEFINED)
        return 0;

    if (smu_resolve_cpu_class() < 0)
        return -ENODEV;

    if (detect_rsmu_address() < 0 || detect_hsmp_address() < 0 || detect_mp1_address() < 0) {
        pr_err("Unknown processor codename: %d", g_smu.codename);
        return -ENODEV;
    }

    pr_info("Family Codename: %s", get_code_name(g_smu.codename));
    return 0;
}

void smu_cleanup(void) {
    // Unmap DRAM Base if required after SMU use.
    if (g_smu.pm_table_virt_addr) {
        iounmap(g_smu.pm_table_virt_addr);
        g_smu.pm_table_virt_addr = NULL;
    }

    if (g_smu.pm_table_virt_addr_alt) {
        iounmap(g_smu.pm_table_virt_addr_alt);
        g_smu.pm_table_virt_addr_alt = NULL;
    }

    // Set SMU state to uninitialized, requiring a call to smu_init() again.
    g_smu.codename = CODENAME_UNDEFINED;
}

const char *smu_get_codename(void) {
    return get_code_name(g_smu.codename);
}

u32 smu_get_version(const struct pci_dev* dev, const smu_mailbox mb) {
    smu_req_args_t args;
    u32 ret;

    // First value is always 1.
    smu_args_init(&args, 1);

    // OP 0x02 is consistent with all platforms meaning
    //  it can be used directly.
    ret = smu_send_command(dev, 0x02, &args, mb);
    if (ret != SMU_Return_OK)
        return ret;

    return args.s.arg0;
}

smu_if_version smu_get_mp1_if_version(void) {
    return g_smu.mp1_if_ver;
}

static u64 smu_get_dram_base_address_class_1(const struct pci_dev* dev, const u32 fn) {
    smu_req_args_t args;
    u32 ret;

    smu_args_init(&args, 0);

    args.s.arg0 = args.s.arg1 = 1;
    ret = smu_send_command(dev, fn, &args, MAILBOX_TYPE_RSMU);

    return ret != SMU_Return_OK ? ret : args.s.arg0 | ((u64)args.s.arg1 << 32);
}

static u64 smu_get_dram_base_address_class_2(const struct pci_dev* dev, const u32 fn1, const u32 fn2) {
    smu_req_args_t args;
    u32 ret;

    smu_args_init(&args, 0);

    ret = smu_send_command(dev, fn1, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    smu_args_init(&args, 0);

    ret = smu_send_command(dev, fn2, &args, MAILBOX_TYPE_RSMU);

    return ret != SMU_Return_OK ? ret : args.s.arg0;
}

static u64 smu_get_dram_base_address_class_3(const struct pci_dev* dev, const u32 fn1, const u32 fn2, const u32 fn3) {
    smu_req_args_t args;
    u32 parts[2];
    u32 ret;

    smu_args_init(&args, 0);

    // == Part 1 ==
    args.s.arg0 = 3;
    ret = smu_send_command(dev, fn1, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    smu_args_init(&args, 3);

    ret = smu_send_command(dev, fn3, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    // 1st Base.
    parts[0] = args.s.arg0;
    // == Part 1 End ==

    // == Part 2 ==
    smu_args_init(&args, 3);
    ret = smu_send_command(dev, fn2, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    smu_args_init(&args, 5);

    ret = smu_send_command(dev, fn1, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    smu_args_init(&args, 5);

    ret = smu_send_command(dev, fn3, &args, MAILBOX_TYPE_RSMU);
    if (ret != SMU_Return_OK)
        return ret;

    // 2nd base.
    parts[1] = args.s.arg0;
    // == Part 2 End ==

    return (u64)parts[1] << 32 | parts[0];
}

u64 smu_get_dram_base_address(const struct pci_dev* dev) {
    switch (g_smu.codename) {
        case CODENAME_NAPLES:
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
            return smu_get_dram_base_address_class_1(dev, 0xa);
        case CODENAME_VERMEER:
        case CODENAME_MATISSE:
        case CODENAME_CASTLEPEAK:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
            return smu_get_dram_base_address_class_1(dev, 0x06);
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK:
            return smu_get_dram_base_address_class_1(dev, 0x04);
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_CEZANNE:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_STRIXPOINT:
        case CODENAME_HAWKPOINT:
            return smu_get_dram_base_address_class_1(dev, 0x66);
        case CODENAME_COLFAX:
        case CODENAME_PINNACLERIDGE:
            return smu_get_dram_base_address_class_2(dev, 0x0b, 0x0c);
        case CODENAME_DALI:
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2:
            return smu_get_dram_base_address_class_3(dev, 0x0a, 0x3d, 0x0b);
        default:
            break;
    }

    return SMU_Return_Unsupported;
}

smu_return_val smu_transfer_table_to_dram(const struct pci_dev* dev) {
    smu_req_args_t args;
    u32 fn;

    /**
     * Probes (updates) the PM Table.
     * SMC Message corresponds to TransferTableSmu2Dram.
     * Physically mapped at the DRAM Base address(es).
     */

    // Arg[0] here specifies the PM table when set to 0.
    // For GPU ASICs, it seems there's more tables that can be found but for CPUs,
    //  it seems this value is ignored.
    smu_args_init(&args, 0);

    switch (g_smu.codename) {
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_NAPLES:
            fn = 0x0a;
            break;
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
            fn = 0x05;
            break;
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK:
            fn = 0x03;
            break;
        case CODENAME_CEZANNE:
            fn = 0x65;
            break;
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_STRIXPOINT:
        case CODENAME_HAWKPOINT: {
            args.s.arg0 = 3;
            fn = 0x65;
        }
            break;
        case CODENAME_COLFAX:
        case CODENAME_PINNACLERIDGE:
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2: {
            args.s.arg0 = 3;
            fn = 0x3d;
        }
            break;
        default:
            return SMU_Return_Unsupported;
    }

    return smu_send_command(dev, fn, &args, MAILBOX_TYPE_RSMU);
}

smu_return_val smu_transfer_2nd_table_to_dram(const struct pci_dev* dev) {
    smu_req_args_t args;
    u32 fn;

    /**
     * Probes (updates) the secondary PM Table.
     * SMC Message corresponds to TransferTableSmu2Dram.
     * Physically mapped at the DRAM Base address(es).
     */

    // Arg[0] here specifies the PM table when set to 0.
    // For GPU ASICs, it seems there's more tables that can be found but for CPUs,
    //  it seems this value is ignored.
    smu_args_init(&args, 0);

    switch (g_smu.codename) {
        case CODENAME_COLFAX:
        case CODENAME_PINNACLERIDGE:
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2: {
            args.s.arg0 = 5;
            fn = 0x3d;
        }
            break;
        case CODENAME_SUMMITRIDGE:
        case CODENAME_THREADRIPPER:
        case CODENAME_NAPLES:
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_MILAN:
        case CODENAME_CEZANNE:
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        default:
            return SMU_Return_Unsupported;
    }

    return smu_send_command(dev, fn, &args, MAILBOX_TYPE_RSMU);
}


smu_return_val smu_get_pm_table_version(const struct pci_dev* dev, u32* version) {
    smu_return_val ret;
    smu_req_args_t args;
    u32 fn;

    /**
     * For some codenames, there are different PM tables for each chip.
     * SMC Message corresponds to TableVersionId.
     * Based on AGESA FW revision.
     */
    switch (g_smu.codename) {
        case CODENAME_RAVENRIDGE:
        case CODENAME_PICASSO:
            fn = 0x0c;
            break;
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE:
        case CODENAME_VERMEER:
        case CODENAME_MILAN:
        case CODENAME_CHAGALL:
            fn = 0x08;
            break;
        case CODENAME_RAPHAEL:
        case CODENAME_GRANITERIDGE:
        case CODENAME_STORMPEAK:
            fn = 0x05;
            break;
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE:
        case CODENAME_CEZANNE:
        case CODENAME_REMBRANDT:
        case CODENAME_PHOENIX:
        case CODENAME_STRIXPOINT:
        case CODENAME_HAWKPOINT:
            fn = 0x06;
            break;
        default:
            return SMU_Return_Unsupported;
    }

    smu_args_init(&args, 0);

    ret = smu_send_command(dev, fn, &args, MAILBOX_TYPE_RSMU);
    *version = args.s.arg0;

    return ret;
}

u32 smu_update_pmtable_size(const u32 version) {
    // These sizes are actually accurate and not just "guessed".
    // Source: Ryzen Master.
    switch (g_smu.codename) {
        case CODENAME_CASTLEPEAK:
        case CODENAME_MATISSE: {
            switch (version) {
                case 0x240003: g_smu.pm_dram_map_size = 0x18AC; break;
                case 0x240503: g_smu.pm_dram_map_size = 0xD7C; break;
                case 0x240603: g_smu.pm_dram_map_size = 0xAB0; break;
                case 0x240902: g_smu.pm_dram_map_size = 0x514; break;
                case 0x240903: g_smu.pm_dram_map_size = 0x518; break;
                case 0x240802: g_smu.pm_dram_map_size = 0x7E0; break;
                case 0x240703:
                case 0x240803: g_smu.pm_dram_map_size = 0x7E4; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_VERMEER:
        case CODENAME_CHAGALL: {
            switch (version) {
                case 0x2D0803: g_smu.pm_dram_map_size = 0x894; break;
                case 0x2D0903: g_smu.pm_dram_map_size = 0x594; break;
                case 0x380005: g_smu.pm_dram_map_size = 0x1BB0; break;
                case 0x380505: g_smu.pm_dram_map_size = 0xF30; break;
                case 0x380605: g_smu.pm_dram_map_size = 0xC10; break;
                case 0x380804: g_smu.pm_dram_map_size = 0x8A4; break;
                case 0x380705:
                case 0x380805: g_smu.pm_dram_map_size = 0x8F0; break;
                case 0x380904: g_smu.pm_dram_map_size = 0x5A4; break;
                case 0x380905: g_smu.pm_dram_map_size = 0x5D0; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_MILAN: {
            switch (version) {
                case 0x2D0008: g_smu.pm_dram_map_size = 0x1AB0; break; // Don't exist in RM.
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_RENOIR:
        case CODENAME_LUCIENNE: {
            switch (version) {
                case 0x370000: g_smu.pm_dram_map_size = 0x794; break;
                case 0x370001: g_smu.pm_dram_map_size = 0x884; break;
                case 0x370002: g_smu.pm_dram_map_size = 0x88C; break;
                case 0x370003: g_smu.pm_dram_map_size = 0x8AC; break;
                case 0x370005: g_smu.pm_dram_map_size = 0x8C8; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_CEZANNE: {
            switch (version) {
                case 0x400005: g_smu.pm_dram_map_size = 0x944; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_REMBRANDT: {
            switch (version) {
                case 0x450004: g_smu.pm_dram_map_size = 0xAA4; break;
                case 0x450005: g_smu.pm_dram_map_size = 0xAB0; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_PICASSO:
        case CODENAME_RAVENRIDGE:
        case CODENAME_RAVENRIDGE2: {
            // These codenames have two PM tables, a larger (primary) one and a smaller one.
            // The size is always fixed to 0x608 and 0xA4 bytes each.
            // Source: Ryzen Master.
            g_smu.pm_dram_map_size_alt = 0xA4;
            g_smu.pm_dram_map_size = 0x608 + g_smu.pm_dram_map_size_alt;

            // Split DRAM base into high/low values.
            g_smu.pm_dram_base_alt = g_smu.pm_dram_base >> 32;
            g_smu.pm_dram_base &= 0xFFFFFFFF;
        }
            break;
        case CODENAME_RAPHAEL: {
            switch (version) {
                case 0x000400: g_smu.pm_dram_map_size = 0x948; break; // Some ES-time table? Don't exist in RM.
                case 0x540000: g_smu.pm_dram_map_size = 0x828; break;
                case 0x540001: g_smu.pm_dram_map_size = 0x82C; break;
                case 0x540002: g_smu.pm_dram_map_size = 0x87C; break;
                case 0x540003: g_smu.pm_dram_map_size = 0x89C; break;
                case 0x540004: g_smu.pm_dram_map_size = 0x8BC; break;
                case 0x540005: g_smu.pm_dram_map_size = 0x8C8; break;
                case 0x540100: g_smu.pm_dram_map_size = 0x618; break;
                case 0x540101: g_smu.pm_dram_map_size = 0x61C; break;
                case 0x540102: g_smu.pm_dram_map_size = 0x66C; break;
                case 0x540103: g_smu.pm_dram_map_size = 0x68C; break;
                case 0x540104: g_smu.pm_dram_map_size = 0x6A8; break;
                case 0x540105: g_smu.pm_dram_map_size = 0x6B4; break;
                case 0x540108: g_smu.pm_dram_map_size = 0x6BC; break;
                case 0x540208: g_smu.pm_dram_map_size = 0x8D0; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_GRANITERIDGE: {
            switch (version) {
                case 0x620105: g_smu.pm_dram_map_size = 0x724; break;
                case 0x620205: g_smu.pm_dram_map_size = 0x994; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_PHOENIX:
        case CODENAME_HAWKPOINT: {
            switch (version) {
                case 0x4C0003: g_smu.pm_dram_map_size = 0xB18; break;
                case 0x4C0004: g_smu.pm_dram_map_size = 0xB1C; break;
                case 0x4C0005: g_smu.pm_dram_map_size = 0xAF8; break;
                case 0x4C0006: g_smu.pm_dram_map_size = 0xAFC; break;
                case 0x4C0008: g_smu.pm_dram_map_size = 0xAF0; break;
                case 0x4C0007:
                case 0x4C0009: g_smu.pm_dram_map_size = 0xB00; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_STRIXPOINT: {
            switch (version) {
                case 0x5D0008: g_smu.pm_dram_map_size = 0xD54; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        case CODENAME_STORMPEAK: {
            switch (version) {
                case 0x5C0002: g_smu.pm_dram_map_size = 0x1E3C; break;
                case 0x5C0003: g_smu.pm_dram_map_size = 0x1E48; break;
                case 0x5C0102: g_smu.pm_dram_map_size = 0x1A14; break;
                case 0x5C0103: g_smu.pm_dram_map_size = 0x1A20; break;
                case 0x5C0202: g_smu.pm_dram_map_size = 0x15EC; break;
                case 0x5C0203: g_smu.pm_dram_map_size = 0x15F8; break;
                case 0x5C0302: g_smu.pm_dram_map_size = 0xD9C; break;
                case 0x5C0303: g_smu.pm_dram_map_size = 0xDA8; break;
                case 0x5C0402: g_smu.pm_dram_map_size = 0x974; break;
                case 0x5C0403: g_smu.pm_dram_map_size = 0x980; break;
                default: return SMU_Return_Unsupported;
            }
        }
            break;
        default:
            return SMU_Return_Unsupported;
    }

    return SMU_Return_OK;
}

smu_return_val smu_read_pm_table(const struct pci_dev* dev, unsigned char* dst, size_t* len) {
    u32 ret, version, size;

    // The DRAM base does not change after boot meaning it only needs to be fetched once.
    // From testing, it also seems they are always mapped to the same address as well,
    // at least when running the same AGESA version.
    if (g_smu.pm_dram_base == 0 || g_smu.pm_dram_map_size == 0) {
        g_smu.pm_dram_base = smu_get_dram_base_address(dev);

        // Verify returned value isn't an SMU return value.
        if (g_smu.pm_dram_base < 0xFF) {
            pr_err("Unable to receive the DRAM base address: %X", (u8)g_smu.pm_dram_base);
            return g_smu.pm_dram_base;
        }

        // Should help us catch where we missed table version initialization in the future.
        version = 0xDEADC0DE;

        // These models require finding the PM table version to determine its size.
        if (g_smu.codename == CODENAME_VERMEER ||
            g_smu.codename == CODENAME_MATISSE ||
            g_smu.codename == CODENAME_RAPHAEL ||
            g_smu.codename == CODENAME_GRANITERIDGE ||
            g_smu.codename == CODENAME_RENOIR ||
            g_smu.codename == CODENAME_LUCIENNE ||
            g_smu.codename == CODENAME_REMBRANDT ||
            g_smu.codename == CODENAME_PHOENIX ||
            g_smu.codename == CODENAME_STRIXPOINT ||
            g_smu.codename == CODENAME_CEZANNE ||
            g_smu.codename == CODENAME_CHAGALL ||
            g_smu.codename == CODENAME_MILAN ||
            g_smu.codename == CODENAME_HAWKPOINT ||
            g_smu.codename == CODENAME_STORMPEAK) {
            ret = smu_get_pm_table_version(dev, &version);

            if (ret != SMU_Return_OK) {
                pr_err("Failed to get PM Table version with error: %X\n", ret);
                return ret;
            }
        }

        ret = smu_update_pmtable_size(version);
        if (ret != SMU_Return_OK) {
            pr_err("Unknown PM table version: 0x%08X", version);
            return ret;
        }

        pr_debug("Determined PM mapping size as (%xh,%xh) bytes.", g_smu.pm_dram_map_size, g_smu.pm_dram_map_size_alt);
    }

    // Validate output buffer size.
    // N.B. In the case of Picasso/RavenRidge 2, we include the secondary PM Table size as well
    if (*len < g_smu.pm_dram_map_size) {
        pr_warn("Insufficient buffer size for PM table read: %lu < %d version: 0x%X", *len, g_smu.pm_dram_map_size, version);

        *len = g_smu.pm_dram_map_size;
        return SMU_Return_InsufficientSize;
    }

    // Clamp output size
    *len = g_smu.pm_dram_map_size;

    // Check if we should tell the SMU to refresh the table via jiffies.
    // Use a minimum interval of 1 ms.
    if (!g_smu.pm_jiffies || time_after(jiffies, g_smu.pm_jiffies + msecs_to_jiffies(1))) {
        g_smu.pm_jiffies = jiffies;

        ret = smu_transfer_table_to_dram(dev);
        if (ret != SMU_Return_OK)
            return ret;

        if (g_smu.pm_dram_map_size_alt) {
            ret = smu_transfer_2nd_table_to_dram(dev);
            if (ret != SMU_Return_OK)
                return ret;
        }
    }

    // Primary PM Table size
    size = g_smu.pm_dram_map_size - g_smu.pm_dram_map_size_alt;

    // We only map the DRAM base(s) once for use.
    if (g_smu.pm_table_virt_addr == NULL) {
        // From Linux documentation, it seems we should use _cache() for ioremap().
        g_smu.pm_table_virt_addr = ioremap_cache(g_smu.pm_dram_base, size);

        if (g_smu.pm_table_virt_addr == NULL) {
            pr_err("Failed to map DRAM base: %llX (0x%X B)", g_smu.pm_dram_base, size);
            return SMU_Return_MappedError;
        }

        // In Picasso/RavenRidge 2, we map the secondary (high) address as well.
        if (g_smu.pm_dram_map_size_alt) {
            g_smu.pm_table_virt_addr_alt = ioremap_cache(
                g_smu.pm_dram_base_alt,
                g_smu.pm_dram_map_size_alt
            );

            if (g_smu.pm_table_virt_addr_alt == NULL) {
                pr_err("Failed to map DRAM alt base: %X (0x%X B)", g_smu.pm_dram_base_alt, g_smu.pm_dram_map_size_alt);
                return SMU_Return_MappedError;
            }
        }
    }

    // memcpy() seems to work as well but according to Linux, for physically mapped addresses,
    //  we should use _fromio().
    memcpy_fromio(dst, g_smu.pm_table_virt_addr, size);

    // Append secondary table if required.
    if (g_smu.pm_dram_map_size_alt)
        memcpy_fromio(dst + size, g_smu.pm_table_virt_addr_alt, g_smu.pm_dram_map_size_alt);

    return SMU_Return_OK;
}
