/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com> */
/* Ryzen SMU Root Complex Communication */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/time.h>

#include "smu.h"

static struct {
  enum smu_processor_codename codename;

  // Optional RSMU mailbox addresses.
  u32 addr_rsmu_mb_cmd;
  u32 addr_rsmu_mb_rsp;
  u32 addr_rsmu_mb_args;

  // Mandatory MP1 mailbox addresses.
  enum smu_if_version mp1_if_ver;
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
  //  refresh the metrics table.
  u32 pm_jiffies;

  // Virtual addresses mapped to physical DRAM bases for PM table.
  u8 __iomem *pm_table_virt_addr;
  u8 __iomem *pm_table_virt_addr_alt;
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
//  independently from the SMU but the SMU requires access to the SMN to execute
//  commands.
static DEFINE_MUTEX(amd_pci_mutex);
static DEFINE_MUTEX(amd_smu_mutex);

int smu_smn_rw_address(struct pci_dev *dev, u32 address, u32 *value,
                       int write) {
  int err;

  // This may work differently for multi-NUMA systems.
  mutex_lock(&amd_pci_mutex);
  err = pci_write_config_dword(dev, SMU_PCI_ADDR_REG, address);

  if (!err) {
    err = (write ? pci_write_config_dword(dev, SMU_PCI_DATA_REG, *value)
                 : pci_read_config_dword(dev, SMU_PCI_DATA_REG, value));

    if (err)
      pr_warn("Error %s SMN address: 0x%x!\n", write ? "writing" : "reading",
              address);
  } else
    pr_warn("Error programming SMN address: 0x%x!\n", address);
  mutex_unlock(&amd_pci_mutex);

  return err;
}

enum smu_return_val smu_read_address(struct pci_dev *dev, u32 address,
                                     u32 *value) {
  return !smu_smn_rw_address(dev, address, value, 0) ? SMU_Return_OK
                                                     : SMU_Return_PCIFailed;
}

enum smu_return_val smu_write_address(struct pci_dev *dev, u32 address,
                                      u32 value) {
  return !smu_smn_rw_address(dev, address, &value, 1) ? SMU_Return_OK
                                                      : SMU_Return_PCIFailed;
}

void smu_args_init(smu_req_args_t *args, u32 value) {
  u32 i;

  args->args[0] = value;

  for (i = 1; i < SMU_REQ_MAX_ARGS; i++)
    args->args[i] = 0;
}

enum smu_return_val smu_send_command(struct pci_dev *dev, u32 op,
                                     smu_req_args_t *args,
                                     enum smu_mailbox mailbox) {
  u32 retries, tmp, i, rsp_addr, args_addr, cmd_addr;

  // == Pick the correct mailbox address. ==
  switch (mailbox) {
  case MAILBOX_TYPE_RSMU:
    rsp_addr = g_smu.addr_rsmu_mb_rsp;
    cmd_addr = g_smu.addr_rsmu_mb_cmd;
    args_addr = g_smu.addr_rsmu_mb_args;
    break;
  case MAILBOX_TYPE_MP1:
    rsp_addr = g_smu.addr_mp1_mb_rsp;
    cmd_addr = g_smu.addr_mp1_mb_cmd;
    args_addr = g_smu.addr_mp1_mb_args;
    break;
  case MAILBOX_TYPE_HSMP:
    rsp_addr = g_smu.addr_hsmp_mb_rsp;
    cmd_addr = g_smu.addr_hsmp_mb_cmd;
    args_addr = g_smu.addr_hsmp_mb_args;
    break;
  default:
    return SMU_Return_Unsupported;
  }

  // == In the unlikely event a mailbox is undefined, don't even attempt to
  // execute. ==
  if (!rsp_addr || !cmd_addr || !args_addr)
    return SMU_Return_Unsupported;

  pr_debug(
      "SMU Service Request: ID(0x%x) Args(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)",
      op, args->s.arg0, args->s.arg1, args->s.arg2, args->s.arg3, args->s.arg4,
      args->s.arg5);

  mutex_lock(&amd_smu_mutex);

  // Step 1: Wait until the RSP register is non-zero.
  retries = smu_timeout_attempts;
  do
    if (smu_read_address(dev, rsp_addr, &tmp) != SMU_Return_OK) {
      mutex_unlock(&amd_smu_mutex);
      pr_warn("Failed to perform initial probe on SMU RSP!\n");

      return SMU_Return_PCIFailed;
    }
  while (tmp == 0 && retries--);

  // Step 1.b: A command is still being processed meaning
  //  a new command cannot be issued.
  if (!retries && !tmp) {
    mutex_unlock(&amd_smu_mutex);
    pr_debug("SMU Service Request Failed: Timeout on initial wait for mailbox "
             "availability.");

    return SMU_Return_CommandTimeout;
  }

  // Step 2: Write zero (0) to the RSP register.
  smu_write_address(dev, rsp_addr, 0);

  // Step 3: Write the argument(s) into the argument register(s).
  for (i = 0; i < SMU_REQ_MAX_ARGS; i++)
    smu_write_address(dev, args_addr + (i * 4), args->args[i]);

  // Step 4: Write the message Id into the Message ID register.
  smu_write_address(dev, cmd_addr, op);

  // Step 5: Wait until the Response register is non-zero.
  do
    if (smu_read_address(dev, rsp_addr, &tmp) != SMU_Return_OK) {
      mutex_unlock(&amd_smu_mutex);
      pr_warn("Failed to perform probe on SMU RSP!\n");

      return SMU_Return_PCIFailed;
    }
  while (tmp == 0 && retries--);

  // Step 6: If the Response register contains OK, then SMU has finished
  // processing
  //  the message.
  if (tmp != SMU_Return_OK && !retries) {
    mutex_unlock(&amd_smu_mutex);

    // The RSP register is still 0, the SMU is still processing the request or
    // has frozen. Either way the command has timed out so indicate as such.
    if (!tmp) {
      pr_debug("SMU Service Request Failed: Timeout on command (0x%x) after %d "
               "attempts.",
               op, smu_timeout_attempts);

      return SMU_Return_CommandTimeout;
    }

    pr_debug("SMU Service Request Failed: Response %Xh was unexpected.", tmp);
    return tmp;
  }

  // Step 7: If a return argument is expected, the Argument register may be read
  //  at this time.
  for (i = 0; i < SMU_REQ_MAX_ARGS; i++)
    if (smu_read_address(dev, args_addr + (i * 4), &args->args[i]) !=
        SMU_Return_OK)
      pr_warn("Failed to fetch SMU ARG [%d]!\n", i);

  mutex_unlock(&amd_smu_mutex);

  pr_debug(
      "SMU Service Response: ID(0x%x) Args(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)",
      op, args->s.arg0, args->s.arg1, args->s.arg2, args->s.arg3, args->s.arg4,
      args->s.arg5);

  return SMU_Return_OK;
}

int smu_resolve_cpu_class(struct pci_dev *dev) {
  u32 cpuid, cpu_family, cpu_model, stepping, pkg_type;

  // https://en.wikichip.org/wiki/amd/cpuid
  // Res. + ExtFamily + ExtModel + Res. + BaseFamily + BaseModel + Stepping
  // See: CPUID_Fn00000001_EAX
  cpuid = cpuid_eax(0x00000001);

  cpu_family = ((cpuid & 0xf00) >> 8) + ((cpuid & 0xff00000) >> 20);
  cpu_model = ((cpuid & 0xf0000) >> 12) + ((cpuid & 0xf0) >> 4);
  stepping = cpuid & 0xf;

  // Combines "PkgType" and "Reserved"
  // See: CPUID_Fn80000001_EBX
  pkg_type = cpuid_ebx(0x80000001) >> 28;

  pr_info("CPUID: family 0x%X, model 0x%X, stepping 0x%X, package 0x%X",
          cpu_family, cpu_model, stepping, pkg_type);

  // Zen / Zen+ / Zen2
  if (cpu_family == 0x17) {
    switch (cpu_model) {
    case 0x01:
      if (pkg_type == 7)
        g_smu.codename = CODENAME_THREADRIPPER;
      else if (pkg_type == 4)
        g_smu.codename = CODENAME_NAPLES;
      else
        g_smu.codename = CODENAME_SUMMITRIDGE;
      break;
    case 0x08:
      if (pkg_type == 7 || pkg_type == 4)
        g_smu.codename = CODENAME_COLFAX;
      else
        g_smu.codename = CODENAME_PINNACLERIDGE;
      break;
    case 0x11:
      g_smu.codename = CODENAME_RAVENRIDGE;
      break;
    case 0x18:
      if (pkg_type == 2)
        g_smu.codename = CODENAME_RAVENRIDGE2;
      else
        g_smu.codename = CODENAME_PICASSO;
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
    default:
      pr_err(
          "CPUID: Unknown Zen/Zen+/Zen2 processor model: 0x%X (CPUID: 0x%08X)",
          cpu_model, cpuid);
      return -2;
    }
    return 0;
  }

  // Zen3/4 (model IDs for unreleased silicon not confirmed yet).
  else if (cpu_family == 0x19) {
    switch (cpu_model) {
    case 0x01:
      g_smu.codename = CODENAME_MILAN;
      break;
    case 0x08:
      g_smu.codename = CODENAME_CHAGALL;
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
    default:
      pr_err("CPUID: Unknown Zen3/4 processor model: 0x%X (CPUID: 0x%08X)",
             cpu_model, cpuid);
      return -2;
    }
    return 0;
  } else if (cpu_family == 0x1a) {
    switch (cpu_model) {
    case 0x24:
      g_smu.codename = CODENAME_STRIX;
      break;
    case 0x44:
      g_smu.codename = CODENAME_GRANITERIDGE;
      break;
    default:
      pr_err("CPUID: Unknown Zen5/6 processor model: 0x%X (CPUID: 0x%08X)",
             cpu_model, cpuid);
      return -2;
    }
    return 0;
  }

  else {
    pr_err("CPUID: failed to detect Zen/Zen+/Zen2/Zen3/Zen4 processor family "
           "(%Xh).",
           cpu_family);
    return -1;
  }
}

int smu_init(struct pci_dev *dev) {
  // This really should never be called twice however in case it is, consider it
  // initialized.
  if (g_smu.codename != CODENAME_UNDEFINED)
    return 0;

  if (smu_resolve_cpu_class(dev))
    return -ENODEV;

  // Detect RSMU mailbox address.
  switch (g_smu.codename) {
  case CODENAME_CASTLEPEAK:
  case CODENAME_MATISSE:
  case CODENAME_VERMEER:
  case CODENAME_MILAN:
  case CODENAME_CHAGALL:
  case CODENAME_RAPHAEL:
  case CODENAME_GRANITERIDGE:
    g_smu.addr_rsmu_mb_cmd = 0x3B10524;
    g_smu.addr_rsmu_mb_rsp = 0x3B10570;
    g_smu.addr_rsmu_mb_args = 0x3B10A40;
    goto LOG_RSMU;
  case CODENAME_COLFAX:
  case CODENAME_NAPLES:
  case CODENAME_SUMMITRIDGE:
  case CODENAME_THREADRIPPER:
  case CODENAME_PINNACLERIDGE:
    g_smu.addr_rsmu_mb_cmd = 0x3B1051C;
    g_smu.addr_rsmu_mb_rsp = 0x3B10568;
    g_smu.addr_rsmu_mb_args = 0x3B10590;
    goto LOG_RSMU;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
  case CODENAME_PICASSO:
  case CODENAME_CEZANNE:
  case CODENAME_RAVENRIDGE:
  case CODENAME_RAVENRIDGE2:
  case CODENAME_DALI:
  case CODENAME_REMBRANDT:
  case CODENAME_PHOENIX:
  case CODENAME_STRIX:
  case CODENAME_HAWKPOINT:
    g_smu.addr_rsmu_mb_cmd = 0x3B10A20;
    g_smu.addr_rsmu_mb_rsp = 0x3B10A80;
    g_smu.addr_rsmu_mb_args = 0x3B10A88;
    goto LOG_RSMU;
  case CODENAME_VANGOGH:
    pr_debug("RSMU Mailbox: Not supported or unknown, disabling use.");
    goto MP1_DETECT;
  default:
    pr_err("Unknown processor codename: %d", g_smu.codename);
    return -ENODEV;
  }

LOG_RSMU:
  pr_debug("RSMU Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)",
           g_smu.addr_rsmu_mb_cmd, g_smu.addr_rsmu_mb_rsp,
           g_smu.addr_rsmu_mb_args);

  // Detect HSMP mailbox address.
  switch (g_smu.codename) {
  case CODENAME_CASTLEPEAK:
  case CODENAME_MATISSE:
  case CODENAME_VERMEER:
  case CODENAME_MILAN:
  case CODENAME_CHAGALL:
  case CODENAME_RAPHAEL:
  case CODENAME_GRANITERIDGE:
    g_smu.addr_hsmp_mb_cmd = 0x3B10534;
    g_smu.addr_hsmp_mb_rsp = 0x3B10980;
    g_smu.addr_hsmp_mb_args = 0x3B109E0;
    goto LOG_HSMP;
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
  case CODENAME_STRIX:
  case CODENAME_HAWKPOINT:
    goto MP1_DETECT;
  default:
    pr_err("Unknown processor codename: %d", g_smu.codename);
    return -ENODEV;
  }

LOG_HSMP:
  pr_debug("HSMP Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)",
           g_smu.addr_hsmp_mb_cmd, g_smu.addr_hsmp_mb_rsp,
           g_smu.addr_hsmp_mb_args);

MP1_DETECT:
  // Detect MP1 SMU mailbox address.
  switch (g_smu.codename) {
  case CODENAME_COLFAX:
  case CODENAME_NAPLES:
  case CODENAME_SUMMITRIDGE:
  case CODENAME_THREADRIPPER:
  case CODENAME_PINNACLERIDGE:
    g_smu.mp1_if_ver = IF_VERSION_9;
    g_smu.addr_mp1_mb_cmd = 0x3B10528;
    g_smu.addr_mp1_mb_rsp = 0x3B10564;
    g_smu.addr_mp1_mb_args = 0x3B10598;
    break;
  case CODENAME_PICASSO:
  case CODENAME_RAVENRIDGE:
  case CODENAME_RAVENRIDGE2:
  case CODENAME_DALI:
    g_smu.mp1_if_ver = IF_VERSION_10;
    g_smu.addr_mp1_mb_cmd = 0x3B10528;
    g_smu.addr_mp1_mb_rsp = 0x3B10564;
    g_smu.addr_mp1_mb_args = 0x3B10998;
    break;
  case CODENAME_MATISSE:
  case CODENAME_VERMEER:
  case CODENAME_CASTLEPEAK:
  case CODENAME_MILAN:
  case CODENAME_CHAGALL:
  case CODENAME_RAPHAEL:
  case CODENAME_GRANITERIDGE:
    g_smu.mp1_if_ver = IF_VERSION_11;
    g_smu.addr_mp1_mb_cmd = 0x3B10530;
    g_smu.addr_mp1_mb_rsp = 0x3B1057C;
    g_smu.addr_mp1_mb_args = 0x3B109C4;
    break;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
  case CODENAME_CEZANNE:
    g_smu.mp1_if_ver = IF_VERSION_12;
    g_smu.addr_mp1_mb_cmd = 0x3B10528;
    g_smu.addr_mp1_mb_rsp = 0x3B10564;
    g_smu.addr_mp1_mb_args = 0x3B10998;
    break;
  case CODENAME_VANGOGH:
  case CODENAME_REMBRANDT:
  case CODENAME_PHOENIX:
  case CODENAME_HAWKPOINT:
    g_smu.mp1_if_ver = IF_VERSION_13;
    g_smu.addr_mp1_mb_cmd = 0x3B10528;
    g_smu.addr_mp1_mb_rsp = 0x3B10578;
    g_smu.addr_mp1_mb_args = 0x3B10998;
    break;
  case CODENAME_STRIX:
    g_smu.mp1_if_ver = IF_VERSION_13;
    g_smu.addr_mp1_mb_cmd = 0x3b10928;
    g_smu.addr_mp1_mb_rsp = 0x3b10978;
    g_smu.addr_mp1_mb_args = 0x3b10998;
    break;
  default:
    pr_err("Unknown processor codename: %d", g_smu.codename);
    return -ENODEV;
  }

  pr_debug("MP1 Mailbox: (cmd: 0x%X, rsp: 0x%X, args: 0x%X)",
           g_smu.addr_mp1_mb_cmd, g_smu.addr_mp1_mb_rsp,
           g_smu.addr_mp1_mb_args);

  pr_info("Family Codename: %s", getCodeName(g_smu.codename));

  return 0;
}

const char *getCodeName(enum smu_processor_codename codename) {
  switch (codename) {
  case CODENAME_COLFAX:
    return "Colfax";
  case CODENAME_RENOIR:
    return "Renoir";
  case CODENAME_PICASSO:
    return "Picasso";
  case CODENAME_MATISSE:
    return "Matisse";
  case CODENAME_THREADRIPPER:
    return "ThreadRipper";
  case CODENAME_CASTLEPEAK:
    return "CastelPeak";
  case CODENAME_RAVENRIDGE:
    return "RavenRidge";
  case CODENAME_RAVENRIDGE2:
    return "RavenRidge2";
  case CODENAME_SUMMITRIDGE:
    return "SummitRidge";
  case CODENAME_PINNACLERIDGE:
    return "PinnacleRidge";
  case CODENAME_REMBRANDT:
    return "Rembrandt";
  case CODENAME_VERMEER:
    return "Vermeer";
  case CODENAME_VANGOGH:
    return "VanGogh";
  case CODENAME_CEZANNE:
    return "Cezanne";
  case CODENAME_MILAN:
    return "Milan";
  case CODENAME_DALI:
    return "Dali";
  case CODENAME_LUCIENNE:
    return "Lucienne";
  case CODENAME_NAPLES:
    return "Naples";
  case CODENAME_CHAGALL:
    return "Chagall";
  case CODENAME_RAPHAEL:
    return "Raphael";
  case CODENAME_GRANITERIDGE:
    return "GraniteRidge";
  case CODENAME_PHOENIX:
    return "Phoenix";
  case CODENAME_STRIX:
    return "Strix";
  case CODENAME_HAWKPOINT:
    return "Hawk Point";
  default:
    return "Undefined";
  }
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

enum smu_processor_codename smu_get_codename(void) { return g_smu.codename; }

u32 smu_get_version(struct pci_dev *dev, enum smu_mailbox mb) {
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

enum smu_if_version smu_get_mp1_if_version(void) { return g_smu.mp1_if_ver; }

u64 smu_get_dram_base_address(struct pci_dev *dev) {
  u32 fn[3], ret, parts[2];
  smu_req_args_t args;

  const enum smu_mailbox type = MAILBOX_TYPE_RSMU;

  smu_args_init(&args, 0);

  switch (g_smu.codename) {
  case CODENAME_NAPLES:
  case CODENAME_SUMMITRIDGE:
  case CODENAME_THREADRIPPER:
    fn[0] = 0xa;
    goto BASE_ADDR_CLASS_1;
  case CODENAME_VERMEER:
  case CODENAME_MATISSE:
  case CODENAME_CASTLEPEAK:
  case CODENAME_MILAN:
  case CODENAME_CHAGALL:
    fn[0] = 0x06;
    goto BASE_ADDR_CLASS_1;
  case CODENAME_RAPHAEL:
  case CODENAME_GRANITERIDGE:
    fn[0] = 0x04;
    goto BASE_ADDR_CLASS_1;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
  case CODENAME_CEZANNE:
  case CODENAME_REMBRANDT:
  case CODENAME_PHOENIX:
  case CODENAME_STRIX:
  case CODENAME_HAWKPOINT:
    fn[0] = 0x66;
    goto BASE_ADDR_CLASS_1;
  case CODENAME_COLFAX:
  case CODENAME_PINNACLERIDGE:
    fn[0] = 0x0b;
    fn[1] = 0x0c;
    goto BASE_ADDR_CLASS_2;
  case CODENAME_DALI:
  case CODENAME_PICASSO:
  case CODENAME_RAVENRIDGE:
  case CODENAME_RAVENRIDGE2:
    fn[0] = 0x0a;
    fn[1] = 0x3d;
    fn[2] = 0x0b;
    goto BASE_ADDR_CLASS_3;
  default:
    return SMU_Return_Unsupported;
  }

BASE_ADDR_CLASS_1:
  args.s.arg0 = args.s.arg1 = 1;
  ret = smu_send_command(dev, fn[0], &args, type);

  return ret != SMU_Return_OK ? ret : args.s.arg0 | ((u64)args.s.arg1 << 32);

BASE_ADDR_CLASS_2:
  ret = smu_send_command(dev, fn[0], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  smu_args_init(&args, 0);
  ret = smu_send_command(dev, fn[1], &args, type);

  return ret != SMU_Return_OK ? ret : args.s.arg0;

BASE_ADDR_CLASS_3:
  // == Part 1 ==
  args.s.arg0 = 3;
  ret = smu_send_command(dev, fn[0], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  smu_args_init(&args, 3);
  ret = smu_send_command(dev, fn[2], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  // 1st Base.
  parts[0] = args.s.arg0;
  // == Part 1 End ==

  // == Part 2 ==
  smu_args_init(&args, 3);
  ret = smu_send_command(dev, fn[1], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  smu_args_init(&args, 5);
  ret = smu_send_command(dev, fn[0], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  smu_args_init(&args, 5);
  ret = smu_send_command(dev, fn[2], &args, type);
  if (ret != SMU_Return_OK)
    return ret;

  // 2nd base.
  parts[1] = args.s.arg0;
  // == Part 2 End ==

  return (u64)parts[1] << 32 | parts[0];
}

enum smu_return_val smu_transfer_table_to_dram(struct pci_dev *dev) {
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
    fn = 0x03;
    break;
  case CODENAME_CEZANNE:
    fn = 0x65;
    break;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
  case CODENAME_REMBRANDT:
  case CODENAME_PHOENIX:
  case CODENAME_STRIX:
  case CODENAME_HAWKPOINT:
    args.s.arg0 = 3;
    fn = 0x65;
    break;
  case CODENAME_COLFAX:
  case CODENAME_PINNACLERIDGE:
  case CODENAME_PICASSO:
  case CODENAME_RAVENRIDGE:
  case CODENAME_RAVENRIDGE2:
    args.s.arg0 = 3;
    fn = 0x3d;
    break;
  default:
    return SMU_Return_Unsupported;
  }

  return smu_send_command(dev, fn, &args, MAILBOX_TYPE_RSMU);
}

enum smu_return_val smu_transfer_2nd_table_to_dram(struct pci_dev *dev) {
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
  case CODENAME_RAVENRIDGE2:
    args.s.arg0 = 5;
    fn = 0x3d;
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

enum smu_return_val smu_get_pm_table_version(struct pci_dev *dev,
                                             u32 *version) {
  enum smu_return_val ret;
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
    fn = 0x05;
    break;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
  case CODENAME_CEZANNE:
  case CODENAME_REMBRANDT:
  case CODENAME_PHOENIX:
  case CODENAME_STRIX:
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

u32 smu_update_pmtable_size(u32 version) {
  // These sizes are actually accurate and not just "guessed".
  // Source: Ryzen Master.
  switch (g_smu.codename) {
  case CODENAME_CASTLEPEAK:
  case CODENAME_MATISSE:
    switch (version) {
    case 0x240902:
      g_smu.pm_dram_map_size = 0x514;
      break;
    case 0x240903:
      g_smu.pm_dram_map_size = 0x518;
      break;
    case 0x240802:
      g_smu.pm_dram_map_size = 0x7E0;
      break;
    case 0x240803:
      g_smu.pm_dram_map_size = 0x7E4;
      break;
    default:
    UNKNOWN_PM_TABLE_VERSION:
      return SMU_Return_Unsupported;
    }
    break;
  case CODENAME_VERMEER:
  case CODENAME_CHAGALL:
    switch (version) {
    case 0x2D0903:
      g_smu.pm_dram_map_size = 0x594;
      break;
    case 0x380904:
      g_smu.pm_dram_map_size = 0x5A4;
      break;
    case 0x380005: // 64
      g_smu.pm_dram_map_size = 0x1BB0;
      break;
    case 0x380505: // 32
      g_smu.pm_dram_map_size = 0xF30;
      break;
    case 0x380605: // 24
      g_smu.pm_dram_map_size = 0xC10;
      break;
    case 0x380705: // 16
      g_smu.pm_dram_map_size = 0x8F0;
      break;
    case 0x380905: // 8
      g_smu.pm_dram_map_size = 0x5D0;
      break;
    case 0x2D0803:
      g_smu.pm_dram_map_size = 0x894;
      break;
    case 0x380804:
      g_smu.pm_dram_map_size = 0x8A4;
      break;
    case 0x380805:
      g_smu.pm_dram_map_size = 0x8F0;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_MILAN:
    switch (version) {
    case 0x2D0008:
      g_smu.pm_dram_map_size = 0x1AB0;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_RENOIR:
  case CODENAME_LUCIENNE:
    switch (version) {
    case 0x370000:
      g_smu.pm_dram_map_size = 0x794;
      break;
    case 0x370001:
      g_smu.pm_dram_map_size = 0x884;
      break;
    case 0x370002:
    case 0x370003:
      g_smu.pm_dram_map_size = 0x88C;
      break;
    case 0x370004:
      g_smu.pm_dram_map_size = 0x8AC;
      break;
    case 0x370005:
      g_smu.pm_dram_map_size = 0x8F0;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_CEZANNE:
    switch (version) {
    case 0x400005:
      g_smu.pm_dram_map_size = 0x944;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_REMBRANDT:
    switch (version) {
    case 0x450004:
    case 0x450005:
      g_smu.pm_dram_map_size = 0xA44;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_PICASSO:
  case CODENAME_RAVENRIDGE:
  case CODENAME_RAVENRIDGE2:
    // These codenames have two PM tables, a larger (primary) one and a smaller
    // one. The size is always fixed to 0x608 and 0xA4 bytes each. Source: Ryzen
    // Master.
    g_smu.pm_dram_map_size_alt = 0xA4;
    g_smu.pm_dram_map_size = 0x608 + g_smu.pm_dram_map_size_alt;

    // Split DRAM base into high/low values.
    g_smu.pm_dram_base_alt = g_smu.pm_dram_base >> 32;
    g_smu.pm_dram_base &= 0xFFFFFFFF;
    break;
  case CODENAME_RAPHAEL:
    switch (version) {
    case 0x540104:
      g_smu.pm_dram_map_size = 0x6A8;
      break;
    case 0x000400:
      g_smu.pm_dram_map_size = 0x948;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_GRANITERIDGE:
    g_smu.pm_dram_map_size = 0x948;
    break;
  case CODENAME_PHOENIX:
    switch (version) {
    case 0x4C0006:
    case 0x4C0007:
    case 0x4C0008:
      g_smu.pm_dram_map_size = 0xAA0;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_HAWKPOINT:
    switch (version) {
    case 0x4C0008:
      g_smu.pm_dram_map_size = 0xA00;
      break;
    default:
      goto UNKNOWN_PM_TABLE_VERSION;
    }
    break;
  case CODENAME_STRIX:
    g_smu.pm_dram_map_size = 0xAA0;
    break;
  default:
    return SMU_Return_Unsupported;
  }

  return SMU_Return_OK;
}

enum smu_return_val smu_read_pm_table(struct pci_dev *dev, unsigned char *dst,
                                      size_t *len) {
  u32 ret, version, size;

  // The DRAM base does not change after boot meaning it only needs to be
  //  fetched once.
  // From testing, it also seems they are always mapped to the same address as
  // well,
  //  at least when running the same AGESA version.
  if (g_smu.pm_dram_base == 0 || g_smu.pm_dram_map_size == 0) {
    g_smu.pm_dram_base = smu_get_dram_base_address(dev);

    // Verify returned value isn't an SMU return value.
    if (g_smu.pm_dram_base < 0xFF && g_smu.pm_dram_base >= 0) {
      pr_err("Unable to receive the DRAM base address: %X",
             (u8)g_smu.pm_dram_base);
      return g_smu.pm_dram_base;
    }

    // Should help us catch where we missed table version initialization in the
    // future.
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
        g_smu.codename == CODENAME_STRIX ||
        g_smu.codename == CODENAME_CEZANNE ||
        g_smu.codename == CODENAME_CHAGALL ||
        g_smu.codename == CODENAME_MILAN ||
        g_smu.codename == CODENAME_HAWKPOINT) {
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

    pr_debug("Determined PM mapping size as (%xh,%xh) bytes.",
             g_smu.pm_dram_map_size, g_smu.pm_dram_map_size_alt);
  }

  // Validate output buffer size.
  // N.B. In the case of Picasso/RavenRidge 2, we include the secondary PM Table
  // size as well
  if (*len < g_smu.pm_dram_map_size) {
    pr_warn(
        "Insufficient buffer size for PM table read: %lu < %d version: 0x%X",
        *len, g_smu.pm_dram_map_size, version);

    *len = g_smu.pm_dram_map_size;
    return SMU_Return_InsufficientSize;
  }

  // Clamp output size
  *len = g_smu.pm_dram_map_size;

  // Check if we should tell the SMU to refresh the table via jiffies.
  // Use a minimum interval of 1 ms.
  if (!g_smu.pm_jiffies ||
      time_after(jiffies, g_smu.pm_jiffies + msecs_to_jiffies(1))) {
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
      pr_err("Failed to map DRAM base: %llX (0x%X B)", g_smu.pm_dram_base,
             size);
      return SMU_Return_MappedError;
    }

    // In Picasso/RavenRidge 2, we map the secondary (high) address as well.
    if (g_smu.pm_dram_map_size_alt) {
      g_smu.pm_table_virt_addr_alt =
          ioremap_cache(g_smu.pm_dram_base_alt, g_smu.pm_dram_map_size_alt);

      if (g_smu.pm_table_virt_addr_alt == NULL) {
        pr_err("Failed to map DRAM alt base: %X (0x%X B)",
               g_smu.pm_dram_base_alt, g_smu.pm_dram_map_size_alt);
        return SMU_Return_MappedError;
      }
    }
  }

  // memcpy() seems to work as well but according to Linux, for physically
  // mapped addresses,
  //  we should use _fromio().
  memcpy_fromio(dst, g_smu.pm_table_virt_addr, size);

  // Append secondary table if required.
  if (g_smu.pm_dram_map_size_alt)
    memcpy_fromio(dst + size, g_smu.pm_table_virt_addr_alt,
                  g_smu.pm_dram_map_size_alt);

  return SMU_Return_OK;
}
