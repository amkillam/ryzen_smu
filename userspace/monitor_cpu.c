/**
 * Ryzen SMU Userspace Sensor Monitor
 * Copyright (C) 2020 Leonardo Gates <leogatesx9r@protonmail.com>
 *
 * This program is free software: you can redistribute it &&/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/

#define _GNU_SOURCE

#include <math.h>
#include <sched.h>
#include <ctype.h>
#include <fcntl.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libsmu.h>

#define PROGRAM_VERSION                 "1.0"
#define PM_TABLE_SUPPORTED_VERSION      0x240903

#define READ_SMN_V1(offs) { if (smu_read_smn_addr(&obj, offs + offset, &value1) != SMU_Return_OK) goto _READ_ERROR; }
#define READ_SMN_V2(offs) { if (smu_read_smn_addr(&obj, offs + offset, &value2) != SMU_Return_OK) goto _READ_ERROR; }

// Ryzen 3700X/3800X
typedef struct {
    float PPT_LIMIT;
    float PPT_VALUE;
    float TDC_LIMIT;
    float TDC_VALUE;
    float THM_LIMIT;
    float THM_VALUE;
    float FIT_LIMIT;
    float FIT_VALUE;
    float EDC_LIMIT;
    float EDC_VALUE;
    float VID_LIMIT;
    float VID_VALUE;
    float PPT_WC;
    float PPT_ACTUAL;
    float TDC_WC;
    float TDC_ACTUAL;
    float THM_WC;
    float THM_ACTUAL;
    float FIT_WC;
    float FIT_ACTUAL;
    float EDC_WC;
    float EDC_ACTUAL;
    float VID_WC;
    float VID_ACTUAL;
    float VDDCR_CPU_POWER;
    float VDDCR_SOC_POWER;
    float VDDIO_MEM_POWER;
    float VDD18_POWER;
    float ROC_POWER;
    float SOCKET_POWER;
    float PPT_FREQUENCY;
    float TDC_FREQUENCY;
    float THM_FREQUENCY;
    float PROCHOT_FREQUENCY;
    float VOLTAGE_FREQUENCY;
    float CCA_FREQUENCY;
    float FIT_VOLTAGE;
    float FIT_PRE_VOLTAGE;
    float LATCHUP_VOLTAGE;
    float CPU_SET_VOLTAGE;
    float CPU_TELEMETRY_VOLTAGE;
    float CPU_TELEMETRY_CURRENT;
    float CPU_TELEMETRY_POWER;
    float CPU_TELEMETRY_POWER_ALT;
    float SOC_SET_VOLTAGE;
    float SOC_TELEMETRY_VOLTAGE;
    float SOC_TELEMETRY_CURRENT;
    float SOC_TELEMETRY_POWER;
    float FCLK_FREQ;
    float FCLK_FREQ_EFF;
    float UCLK_FREQ;
    float MEMCLK_FREQ;
    float FCLK_DRAM_SETPOINT;
    float FCLK_DRAM_BUSY;
    float FCLK_GMI_SETPOINT;
    float FCLK_GMI_BUSY;
    float FCLK_IOHC_SETPOINT;
    float FCLK_IOHC_BUSY;
    float FCLK_XGMI_SETPOINT;
    float FCLK_XGMI_BUSY;
    float CCM_READS;
    float CCM_WRITES;
    float IOMS;
    float XGMI;
    float CS_UMC_READS;
    float CS_UMC_WRITES;
    float FCLK_RESIDENCY[4];
    float FCLK_FREQ_TABLE[4];
    float UCLK_FREQ_TABLE[4];
    float MEMCLK_FREQ_TABLE[4];
    float FCLK_VOLTAGE[4];
    float LCLK_SETPOINT_0;
    float LCLK_BUSY_0;
    float LCLK_FREQ_0;
    float LCLK_FREQ_EFF_0;
    float LCLK_MAX_DPM_0;
    float LCLK_MIN_DPM_0;
    float LCLK_SETPOINT_1;
    float LCLK_BUSY_1;
    float LCLK_FREQ_1;
    float LCLK_FREQ_EFF_1;
    float LCLK_MAX_DPM_1;
    float LCLK_MIN_DPM_1;
    float LCLK_SETPOINT_2;
    float LCLK_BUSY_2;
    float LCLK_FREQ_2;
    float LCLK_FREQ_EFF_2;
    float LCLK_MAX_DPM_2;
    float LCLK_MIN_DPM_2;
    float LCLK_SETPOINT_3;
    float LCLK_BUSY_3;
    float LCLK_FREQ_3;
    float LCLK_FREQ_EFF_3;
    float LCLK_MAX_DPM_3;
    float LCLK_MIN_DPM_3;
    float XGMI_SETPOINT;
    float XGMI_BUSY;
    float XGMI_LANE_WIDTH;
    float XGMI_DATA_RATE;
    float SOC_POWER;
    float SOC_TEMP;
    float DDR_VDDP_POWER;
    float DDR_VDDIO_MEM_POWER;
    float GMI2_VDDG_POWER;
    float IO_VDDCR_SOC_POWER;
    float IOD_VDDIO_MEM_POWER;
    float IO_VDD18_POWER;
    float TDP;
    float DETERMINISM;
    float V_VDDM;
    float V_VDDP;
    float V_VDDG;
    float PEAK_TEMP;
    float PEAK_VOLTAGE;
    float AVG_CORE_COUNT;
    float CCLK_LIMIT;
    float MAX_VOLTAGE;
    float DC_BTC;
    float CSTATE_BOOST;
    float PROCHOT;
    float PC6;
    float PWM;
    float SOCCLK;
    float SHUBCLK;
    float MP0CLK;
    float MP1CLK;
    float MP5CLK;
    float SMNCLK;
    float TWIXCLK;
    float WAFLCLK;
    float DPM_BUSY;
    float MP1_BUSY;
    float CORE_POWER[8];
    float CORE_VOLTAGE[8];
    float CORE_TEMP[8];
    float CORE_FIT[8];
    float CORE_IDDMAX[8];
    float CORE_FREQ[8];
    float CORE_FREQEFF[8];
    float CORE_C0[8];
    float CORE_CC1[8];
    float CORE_CC6[8];
    float CORE_CKS_FDD[8];
    float CORE_CI_FDD[8];
    float CORE_IRM[8];
    float CORE_PSTATE[8];
    float CORE_CPPC_MAX[8];
    float CORE_CPPC_MIN[8];
    float CORE_SC_LIMIT[8];
    float CORE_SC_CAC[8];
    float CORE_SC_RESIDENCY[8];
    float L3_LOGIC_POWER[2];
    float L3_VDDM_POWER[2];
    float L3_TEMP[2];
    float L3_FIT[2];
    float L3_IDDMAX[2];
    float L3_FREQ[2];
    float L3_CKS_FDD[2];
    float L3_CCA_THRESHOLD[2];
    float L3_CCA_CAC[2];
    float L3_CCA_ACTIVATION[2];
    float L3_EDC_LIMIT[2];
    float L3_EDC_CAC[2];
    float L3_EDC_RESIDENCY[2];
    float MP5_BUSY[1];
} pm_table_0x240903, *ppm_table_0x240903;

static smu_obj_t obj;
static int update_time_s = 1;

void print_memory_timings() {
    const char* bool_str[2] = { "Disabled", "Enabled" };
    unsigned int value1, value2, offset;

    READ_SMN_V1(0x50200);
    offset = value1 == 0x300 ? 0x100000 : 0;

    READ_SMN_V1(0x50050); READ_SMN_V2(0x50058);
    fprintf(stdout, "BankGroupSwap: %s\n",
        bool_str[!(value1 == value2 && value1 == 0x87654321)]);

    READ_SMN_V1(0x500D0); READ_SMN_V2(0x500D4);
    fprintf(stdout, "BankGroupSwapAlt: %s\n",
        bool_str[(value1 >> 4 & 0x7F) != 0 || (value2 >> 4 & 0x7F) != 0]);

    READ_SMN_V1(0x50200); READ_SMN_V2(0x50204);
    fprintf(stdout, "Memory Clock: %.0f MHz\nGDM: %s\nCR: %s\nTcl: %d\nTras: %d\nTrcdrd: %d\nTrcdwr: %d\n",
        (value1 & 0x7f) / 3.f * 100.f,
        bool_str[((value1 >> 11) & 1) == 1],
        ((value1 & 0x400) >> 10) != 0 ? "2T" : "1T",
        value2 & 0x3f,
        value2 >> 8 & 0x7f,
        value2 >> 16 & 0x3f,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50208); READ_SMN_V2(0x5020C);
    fprintf(stdout, "Trc: %d\nTrp: %d\nTrrds: %d\nTrrdl: %d\nTrtp: %d\n",
        value1 & 0xff,
        value1 >> 16 & 0x3f,
        value2 & 0x1f,
        value2 >> 8 & 0x1f,
        value2 >> 24 & 0x1f);

    READ_SMN_V1(0x50210); READ_SMN_V2(0x50214);
    fprintf(stdout, "Tfaw: %d\nTcwl: %d\nTwtrs: %d\nTwtrl: %d\n",
        value1 & 0xff,
        value2 & 0x3f,
        value2 >> 8 & 0x1f,
        value2 >> 16 & 0x3f);

    READ_SMN_V1(0x50218); READ_SMN_V2(0x50220);
    fprintf(stdout, "Twr: %d\nTrdrddd: %d\nTrdrdsd: %d\nTrdrdsc: %d\nTrdrdscl: %d\n",
        value1 & 0xff,
        value2 & 0xf,
        value2 >> 8 & 0xf,
        value2 >> 16 & 0xf,
        value2 >> 24 & 0x3f);

    READ_SMN_V1(0x50224); READ_SMN_V2(0x50228);
    fprintf(stdout, "Twrwrdd: %d\nTwrwrsd: %d\nTwrwrsc: %d\nTwrwrscl: %d\nTwrrd: %d\nTrdwr: %d\n",
        value1 & 0xf,
        value1 >> 8 & 0xf,
        value1 >> 16 & 0xf,
        value1 >> 24 & 0x3f,
        value2 & 0xf,
        value2 >> 8 & 0x1f);

    READ_SMN_V1(0x50254);
    fprintf(stdout, "Tcke: %d\n", value1 >> 24 & 0x1f);

    READ_SMN_V1(0x50260); READ_SMN_V2(0x50264);
    if (value1 != value2 && value1 == 0x21060138)
        value1 = value2;

    fprintf(stdout, "Trfc: %d\nTrfc2: %d\nTrfc4: %d\n",
        value1 & 0x3ff,
        value1 >> 11 & 0x3ff,
        value1 >> 22 & 0x3ff);

    exit(0);

_READ_ERROR:
    fprintf(stderr, "Unable to read SMN address space.");
    exit(1);
}

void append_u32_to_str(char* buffer, unsigned int val) {
    char tmp[12] = { 0 };

    sprintf(tmp, "%c%c%c%c", val & 0xff, val >> 8 & 0xff, val >> 16 & 0xff, val >> 24 & 0xff);
    strcat(buffer, tmp);
}

const char* get_processor_name() {
    unsigned int eax, ebx, ecx, edx, l;
    static char buffer[50] = { 0 }, *p;

    __get_cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    __get_cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    __get_cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
    append_u32_to_str(buffer, eax);
    append_u32_to_str(buffer, ebx);
    append_u32_to_str(buffer, ecx);
    append_u32_to_str(buffer, edx);

    // Trim whitespaces
    p = buffer;
    l = strlen(p);
    while(isspace(p[l - 1])) p[--l] = 0;
    while(* p && isspace(* p)) ++p, --l;

    return buffer;
}

unsigned int count_set_bits(unsigned int v) {
    unsigned int result = 0;

    while(v != 0) {
        if (v & 1)
            result++;

        v >>= 1;
    }

    return result;
}

void get_fuse_topology(int fam, int model, unsigned int* ccds_enabled, unsigned int* ccds_disabled,
    unsigned int* cores_disabled, unsigned int* smt_enabled) {
    unsigned int ccds_down, ccds_present, core_fuse, core_fuse_addr, ccd_fuse1, ccd_fuse2;

    ccd_fuse1 = 0x5D218;
    ccd_fuse2 = 0x5D21C;

    if (fam == 0x17 && model != 0x71) {
        ccd_fuse1 += 0x40;
        ccd_fuse2 += 0x40;
    }

    if (smu_read_smn_addr(&obj, ccd_fuse1, &ccds_present) != SMU_Return_OK ||
        smu_read_smn_addr(&obj, ccd_fuse2, &ccds_down) != SMU_Return_OK) {
        perror("Failed to read CCD fuses");
        exit(-1);
    }

    *ccds_disabled = ((ccds_down & 0x3F) << 2) | ((ccds_present >> 30) & 0x3);

    ccds_present = (ccds_present >> 22) & 0xFF;
    *ccds_enabled = ccds_present;

    if (fam == 0x19)
        core_fuse_addr = (0x30081800 + 0x598) |
            ((((*ccds_disabled & ccds_present) & 1) == 1) ? 0x2000000 : 0);
    else
        core_fuse_addr = (0x30081800 + 0x238) | (((ccds_present & 1) == 0) ? 0x2000000 : 0);

    if (smu_read_smn_addr(&obj, core_fuse_addr, &core_fuse) != SMU_Return_OK) {
        perror("Failed to read core fuse");
        exit(-1);
    }

    *cores_disabled = core_fuse & 0xFF;
    *smt_enabled = (core_fuse & (1 << 8)) != 0;
}

void get_processor_topology(unsigned int* ccds, unsigned int *ccxs,
    unsigned int *cores_per_ccx, unsigned int* cores) {
    unsigned int  ccds_enabled, ccds_disabled, core_disable_map, logical_cores,
        smt, fam, model, eax, ebx, ecx, edx;

    __get_cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
    fam = ((eax & 0xf00) >> 8) + ((eax & 0xff00000) >> 20);
    model = ((eax & 0xf0000) >> 12) + ((eax & 0xf0) >> 4);
    logical_cores = (ebx >> 16) & 0xFF;

    get_fuse_topology(fam, model, &ccds_enabled, &ccds_disabled, &core_disable_map, &smt);

    *ccds = count_set_bits(ccds_enabled);

    if (fam == 0x19) {
        *ccxs = *ccds;
        *cores_per_ccx = 8 - count_set_bits(core_disable_map);
    }
    else {
        *ccxs = *ccds * 2;
        *cores_per_ccx = (8 - count_set_bits(core_disable_map)) / 2;
    }

    *cores = logical_cores;
    if (smt)
        *cores /= 2;
}

void print_line(const char* label, const char* value_format, ...) {
    static char buffer[1024];
    va_list list;

    va_start(list, value_format);
    vsnprintf(buffer, sizeof(buffer), value_format, list);
    va_end(list);

    fprintf(stdout, "│ %46s │ %47s │\n", label, buffer);
}

void _print_core_line(const char* label, const char* value_format, ...) {
    static char buffer[1024];
    va_list list;

    va_start(list, value_format);
    vsnprintf(buffer, sizeof(buffer), value_format, list);
    va_end(list);

    fprintf(stdout, "│ %7s │ %86s │\n", label, buffer);
}

#define core_print_line(core, value, ...) { \
    char buffer[1024]; \
    \
    sprintf(buffer, "Core %d", core); \
    _print_core_line(buffer, value, __VA_ARGS__); \
}

unsigned int get_max_cpu_freq(smu_obj_t* obj) {
    smu_arg_t args;
    smu_return_val err;

    if (obj->codename != CODENAME_MATISSE)
        return 0;

    memset(&args, 0, sizeof(args));
    if (smu_send_command(obj, 0x6E, &args, TYPE_RSMU) != SMU_Return_OK)
        return 0;

    return args.args[0];
}

const char* get_pbo_scalar(smu_obj_t* obj) {
    static char buf[16] = { 0 };

    smu_arg_t args;
    smu_return_val err;

    if (obj->codename != CODENAME_MATISSE && obj->codename != CODENAME_VERMEER)
        return 0;

    memset(&args, 0, sizeof(args));
    if (smu_send_command(obj, 0x6C, &args, TYPE_RSMU) != SMU_Return_OK)
        return "?";

    sprintf(buf, "%.fx", args.args_f[0]);
    return buf;
}

void start_pm_monitor(int force) {
    float total_usage, peak_core_frequency, core_voltage, core_frequency, total_core_voltage,
        average_voltage, package_sleep_time, core_sleep_time, edc_value, total_core_C6;

    const char* name, *codename, *smu_fw_ver, *scalar;
    unsigned int cores, ccds, ccxs, cores_per_ccx, max_freq, if_ver, i;
    ppm_table_0x240903 pmt;
    unsigned char *pm_buf;

    if (!smu_pm_tables_supported(&obj)) {
        fprintf(stderr, "PM Tables are not supported on this platform.\n");
        exit(0);
    }

    if (!force && obj.pm_table_version != PM_TABLE_SUPPORTED_VERSION) {
        fprintf(stderr, "PM Table version is not currently suppported. Run with \"-f\" flag to ignore this.\n");
        exit(0);
    }

    name        = get_processor_name();
    codename    = smu_codename_to_str(&obj);
    smu_fw_ver  = smu_get_fw_version(&obj);
    max_freq    = get_max_cpu_freq(&obj);
    scalar      = get_pbo_scalar(&obj);

    get_processor_topology(&ccds, &ccxs, &cores_per_ccx, &cores);

    pm_buf = calloc(obj.pm_table_size, sizeof(unsigned char));
    pmt = (ppm_table_0x240903)pm_buf;

    switch (obj.smu_if_version) {
        case IF_VERSION_9:
            if_ver = 9;
            break;
        case IF_VERSION_10:
            if_ver = 10;
            break;
        case IF_VERSION_11:
            if_ver = 11;
            break;
        case IF_VERSION_12:
            if_ver = 12;
            break;
        case IF_VERSION_13:
            if_ver = 13;
            break;
        default:
            if_ver = 0;
            break;
    }

    while(1) {
        if (smu_read_pm_table(&obj, pm_buf, obj.pm_table_size) != SMU_Return_OK)
            continue;

        fprintf(stdout, "\e[1;1H\e[2J");

        fprintf(stdout, "╭────────────────────────────────────────────────┬─────────────────────────────────────────────────╮\n");
        print_line("CPU Model", name);
        print_line("Processor Code Name", codename);
        print_line("Core Configuration", "%d (%d-%d-%d)", cores, ccds, ccxs, cores_per_ccx);
        if (max_freq)
            print_line("Maximum Frequency", "%d MHz", max_freq);
        print_line("Overdrive Scalar", scalar);
        print_line("SMU FW Version", "v%s", smu_fw_ver);
        print_line("MP1 IF Version", "v%d", if_ver);
        fprintf(stdout, "╰────────────────────────────────────────────────┴─────────────────────────────────────────────────╯\n");

        total_core_C6 = total_usage = total_core_voltage = peak_core_frequency = 0;

        package_sleep_time = pmt->PC6 / 100.f;
        average_voltage = (pmt->CPU_TELEMETRY_VOLTAGE - (0.2 * package_sleep_time)) /
            (1.0 - package_sleep_time);

        fprintf(stdout, "╭─────────┬────────────────┬─────────┬─────────┬─────────┬─────────────┬─────────────┬─────────────╮\n");
        for (i = 0; i < cores; i++) {
            core_frequency = pmt->CORE_FREQEFF[i] * 1000.f;

            if (peak_core_frequency < core_frequency)
                peak_core_frequency = core_frequency;

            total_usage += pmt->CORE_C0[i];
            total_core_C6 += pmt->CORE_CC6[i];

            // "Real core frequency" -- excluding gating
            if (pmt->CORE_FREQ[i] != 0.f) {
                core_sleep_time = pmt->CORE_CC6[i] / 100.f;
                core_voltage = ((1.0 - core_sleep_time) * average_voltage) + (0.2 * core_sleep_time);
                total_core_voltage += core_voltage;
            }

            // AMD denotes a sleeping core as having spent less than 6% of the time in C0.
            // Source: Ryzen Master
            if (pmt->CORE_C0[i] >= 6.f) {
                core_print_line(i,
                    "%4.f MHz | %4.3f W | %1.3f V | %5.2f C | C0: %5.1f %% | C1: %5.1f %% | C6: %5.1f %%",
                    core_frequency, pmt->CORE_POWER[i], core_voltage, pmt->CORE_TEMP[i],
                    pmt->CORE_C0[i], pmt->CORE_CC1[i], pmt->CORE_CC6[i]);
            }
            else
                core_print_line(i,
                    "Sleeping | %4.3f W | %1.3f V | %5.2f C | C0: %5.1f %% | C1: %5.1f %% | C6: %5.1f %%",
                    pmt->CORE_POWER[i], core_voltage, pmt->CORE_TEMP[i], pmt->CORE_C0[i],
                    pmt->CORE_CC1[i], pmt->CORE_CC6[i]);
        }
        fprintf(stdout, "╰─────────┴────────────────┴─────────┴─────────┴─────────┴─────────────┴─────────────┴─────────────╯\n");

        fprintf(stdout, "╭────────────────────────────────────────────────┬─────────────────────────────────────────────────╮\n");
        average_voltage = total_core_voltage / cores;
        edc_value = pmt->EDC_VALUE * (total_usage / cores / 100);

        if (edc_value < pmt->TDC_VALUE)
            edc_value = pmt->TDC_VALUE;

        total_core_C6 /= cores;

        print_line("Peak Core Frequency", "%8.0f MHz", peak_core_frequency);
        print_line("Peak Temperature", "%8.2f C", pmt->PEAK_TEMP);
        print_line("Package Power", "%8.4f W", pmt->SOCKET_POWER);
        print_line("Peak Core(s) Voltage", "%2.6f V", pmt->CPU_TELEMETRY_VOLTAGE);
        print_line("Average Core Voltage", "%2.6f V", average_voltage);
        print_line("Package C6 Residency", "%3.6f %%", pmt->PC6);
        print_line("Core C6 Residency", "%3.6f %%", total_core_C6);
        fprintf(stdout, "╰────────────────────────────────────────────────┴─────────────────────────────────────────────────╯\n");

        fprintf(stdout, "╭────────────────────────────────────────────────┬─────────────────────────────────────────────────╮\n");
        print_line("Thermal Junction Limit", "%8.2f C", pmt->THM_LIMIT);
        print_line("Current Temperature", "%8.2f C", pmt->THM_VALUE);
        print_line("SoC Temperature", "%8.2f C", pmt->SOC_TEMP);
        print_line("Core Power", "%8.4f W", pmt->VDDCR_CPU_POWER);
        print_line("SoC Power", "%4.4f W | %8.4f A | %8.6f V", pmt->SOC_TELEMETRY_POWER,
            pmt->SOC_TELEMETRY_CURRENT, pmt->SOC_TELEMETRY_VOLTAGE);
        print_line("PPT", "%4.4f W | %7.0f  W | %8.2f %%", pmt->PPT_VALUE, pmt->PPT_LIMIT,
            (pmt->PPT_VALUE / pmt->PPT_LIMIT * 100));
        print_line("TDC", "%4.4f A | %7.0f  A | %8.2f %%", pmt->TDC_VALUE, pmt->TDC_LIMIT,
            (pmt->TDC_VALUE / pmt->TDC_LIMIT * 100));
        print_line("EDC", "%4.4f A | %7.0f  A | %8.2f %%", edc_value, pmt->EDC_LIMIT,
            (edc_value / pmt->EDC_LIMIT * 100));
        print_line("Frequency Limit", "%8.0f MHz", pmt->CCLK_LIMIT * 1000.f);
        print_line("FIT Limit", "%f %%", (pmt->FIT_VALUE / pmt->FIT_LIMIT) * 100.f);
        fprintf(stdout, "╰────────────────────────────────────────────────┴─────────────────────────────────────────────────╯\n");

        fprintf(stdout, "╭────────────────────────────────────────────────┬─────────────────────────────────────────────────╮\n");
        print_line("Coupled Mode", "%8s", pmt->UCLK_FREQ == pmt->MEMCLK_FREQ ? "ON" : "OFF");
        print_line("Fabric Clock (Average)", "%5.f MHz", pmt->FCLK_FREQ_EFF);
        print_line("Fabric Clock", "%5.f MHz", pmt->FCLK_FREQ);
        print_line("Uncore Clock", "%5.f MHz", pmt->UCLK_FREQ);
        print_line("Memory Clock", "%5.f MHz", pmt->MEMCLK_FREQ);
        print_line("DRAM Read Bandwidth", "%3.3f GiB/s", pmt->CS_UMC_READS);
        print_line("DRAM Write Bandwidth", "%3.3f GiB/s", pmt->CS_UMC_WRITES);
        print_line("VDDIO_Mem", "%7.4f W", pmt->VDDIO_MEM_POWER);
        print_line("VDDCR_SoC", "%7.4f V", pmt->SOC_SET_VOLTAGE);
        print_line("cLDO_VDDM", "%7.4f V", pmt->V_VDDM);
        print_line("cLDO_VDDP", "%7.4f V", pmt->V_VDDP);
        print_line("cLDO_VDDG", "%7.4f V", pmt->V_VDDG);
        fprintf(stdout, "╰────────────────────────────────────────────────┴─────────────────────────────────────────────────╯\n");

        // Hide Cursor
        fprintf(stdout, "\e[?25l");

        fflush(stdout);
        sleep(update_time_s);
    }
}

void print_version() {
    fprintf(stdout, "SMU Monitor " PROGRAM_VERSION "\n");
    exit(0);
}

void show_help(char* program) {
    fprintf(stdout,
        "SMU Monitor " PROGRAM_VERSION "\n\n"

        "Usage: %s <option(s)>\n\n"

        "Options:\n"
            "\t-h - Show this help screen.\n"
            "\t-v - Show program version.\n"
            "\t-m - Print DRAM Timings and exit.\n"
            "\t-f - Force PM table monitoring even if the PM table version is not supported.\n"
            "\t-u<seconds> - Update the monitoring only after this number of second(s) have passed. Defaults to 1.\n",
        program
    );
}

void parse_args(int argc, char** argv) {
    int c = 0, force, core;

    core = 0;
    force = 0;

    while ((c = getopt(argc, argv, "vmfuh:")) != -1) {
        switch (c) {
            case 'v':
                print_version();
                exit(0);
            case 'm':
                print_memory_timings();
                exit(0);
            case 'f':
                force = 1;
                break;
            case 'u':
                // TODO
                break;
            case 'h':
                show_help(argv[0]);
                exit(0);
            case '?':
                exit(0);
            default:
                break;
        }
    }

    start_pm_monitor(force);
}

void signal_interrupt(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGABRT:
        case SIGTERM:
            // Re-enable the cursor.
            fprintf(stdout, "\e[?25h");
            exit(0);
        default:
            break;
    }
}

// Checks if the program has the required permissions for the driver.
// If it doesn't, it attempts to re-execute the program using `sudo`.
// If the sudo executable cannot be located, it will bail with an error message.
int elevate_if_necessary(int argc, char** argv) {
    static const char* access_paths[] = { "/bin", "/sbin", "/usr/bin", "/usr/sbin" };

    char buf[1024], cmd[1024];
    int euid, found, i;

    if (geteuid() == 0)
        return 1;

    found = 0;
    for (i = 0; i < sizeof(access_paths) / sizeof(access_paths[0]); i++) {
        sprintf(buf, "%s/sudo", access_paths[i]);

        if (!access(buf, F_OK)) {
            found = 1;
            break;
        }
    }

    sprintf(cmd, "%s -S ", buf);
    if (!found || !readlink("/proc/self/exe", buf, sizeof(buf))) {
        fprintf(stderr, "Program must be run as root.\n");
        exit(-2);
    }

    strcat(cmd, buf);
    for (i = 1; i < argc; i++) {
        sprintf(buf, " %s", argv[i]);
        strcat(cmd, buf);
    }

    system(cmd);
    return 0;
}

int main(int argc, char** argv) {
    smu_return_val ret;

    if ((signal(SIGABRT, signal_interrupt) == SIG_ERR) ||
        (signal(SIGTERM, signal_interrupt) == SIG_ERR) ||
        (signal(SIGINT, signal_interrupt) == SIG_ERR)) {
        fprintf(stderr, "Can't set up signal hooks.\n");
        exit(-1);
    }

    if (!elevate_if_necessary(argc, argv))
        exit(0);

    ret = smu_init(&obj);
    if (ret != SMU_Return_OK) {
        fprintf(stderr, "%s\n", smu_return_to_str(ret));
        exit(-2);
    }

    parse_args(argc, argv);

    return 0;
}
