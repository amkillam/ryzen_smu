#!/bin/python3

import os
import sys
import struct
from time import sleep

# Hack to import local cpuid.py without installing it
sys.path.append(os.path.abspath("."))
import cpuid

_cpuid   = cpuid.CPUID()

FS_PATH  = '/sys/kernel/ryzen_smu_drv/'
SMN_PATH = FS_PATH + 'smn'
VER_PATH = FS_PATH + 'version'
PM_PATH  = FS_PATH + 'pm_table'
PMT_PATH = FS_PATH + 'pm_table_version'
CN_PATH  = FS_PATH + 'codename'

PM_TABLE_FP = False

def is_root():
    return os.getenv("SUDO_USER") is not None or os.geteuid() == 0

def driver_loaded():
    return os.path.isfile(VER_PATH)

def read_file32(file):
    with open(file, "rb") as fp:
        result = fp.read(4)
        result = struct.unpack("<I", result)[0]
        fp.close()

    return result

def write_file32(file, value):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<I", value))
        fp.close()

    return result == 4

def pm_table_supported():
    return os.path.isfile(PM_PATH)

def write_file64(file, value1, value2):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<II", value1, value2))
        fp.close()

    return result == 8

def write_file192(file, v1, v2, v3, v4, v5, v6):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<IIIIII", v1, v2, v3, v4, v5, v6))
        fp.close()

    return result == 24

def read_file_str(file, expectedLen = 9):
    with open(file, "r") as fp:
        result = fp.read(expectedLen)
        fp.close()
    
    if len(result) != expectedLen:
        print("Read file ({0}) failed with {1}".format(file, len(result)))
        return False
    
    return result

def read_smn_addr(addr):
    if write_file32(SMN_PATH, addr) == False:
        print("Failed to read SMN address: {:08X}".format(addr))
        return 0
    
    value = read_file32(SMN_PATH)
    
    if value == False:
        print("Failed to read SMN address: {:08X}".format(addr))
        return 0
    
    return value

def read_pm_table():
    global PM_TABLE_FP

    if PM_TABLE_FP == False:
        PM_TABLE_FP = open(PM_PATH, "rb")

    PM_TABLE_FP.seek(0, os.SEEK_SET)
    content = PM_TABLE_FP.read()

    return content

def read_float(buffer, offset):
    return struct.unpack("@f", buffer[offset:(offset + 4)])[0]

def read_double(buffer, offset):
    return struct.unpack("@d", buffer[offset:(offset + 8)])[0]

def read_int(buffer, offset):
    return struct.unpack("@I", buffer[offset:(offset + 4)])[0]

def getCodeName():
    codenames = [
        "Unspecified",
        "Colfax",
        "Renoir",
        "Picasso",
        "Matisse",
        "Threadripper",
        "Castle Peak",
        "Raven Ridge",
        "Raven Ridge 2",
        "Summit Ridge",
        "Pinnacle Ridge"

    ]
    args = read_file_str(CN_PATH, 2)

    if args != False and int(args) != 0:
        return codenames[int(args)]

    return False

def getCCDCount():
    ccdCount = 0

    value1 = (read_smn_addr(0x5D21A) >> 22) & 0xff
    value2 = (read_smn_addr(0x5D21B) >> 30) & 0xff
    value3 = read_smn_addr(0x5D21C) & 0x3f

    value4 = value2 | (4 * value3)

    if ((value1 & 1) == 0 or value4 & 1):
      ccdCount = ccdCount;
    else:
      ccdCount = ccdCount + 1

    if (0x80000000 & value1 and (0x80000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x40000000 & value1 and (0x40000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x20000000 & value1 and (0x20000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x10000000 & value1 and (0x10000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x8000000 & value1 and (0x8000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x4000000 & value1 and (0x4000000 & value4) == 0):
        ccdCount = ccdCount + 1

    if (0x2000000 & value1 and (0x2000000 & value4) == 0):
        ccdCount = ccdCount + 1

    return ccdCount

def getCoreCount():
    eax, ebx, ecx, edx = _cpuid(0x00000001)
    logicalCores = (ebx >> 16) & 0xFF

    eax, ebx, ecx, edx = _cpuid(0x8000001E)
    threadsPerCore = ((ebx >> 8) & 0xF) + 1

    if threadsPerCore == 0:
        return logicalCores

    return int(logicalCores / threadsPerCore)

def intToStr(val):
    return '{:c}{:c}{:c}{:c}' \
        .format(val & 0xff, val >> 8 & 0xff, val >> 16 & 0xff, val >> 24 & 0xff)

def getCpuModel():
    model = ''

    eax, ebx, ecx, edx = _cpuid(0x80000002)
    model = model + intToStr(eax) + intToStr(ebx) + intToStr(ecx) + intToStr(edx)

    eax, ebx, ecx, edx = _cpuid(0x80000003)
    model = model + intToStr(eax) + intToStr(ebx) + intToStr(ecx) + intToStr(edx)

    eax, ebx, ecx, edx = _cpuid(0x80000004)
    model = model + intToStr(eax) + intToStr(ebx) + intToStr(ecx) + intToStr(edx)

    return model.rstrip(' ')

def getCoreVoltage(pm, cores):
    totalV = i = 0
    peakV = read_float(pm, 0x0A0)
    idle = read_float(pm, 0x21C)
    avgV = (peakV * (1.0 - (idle * 0.01))) + (0.002 * idle)

    while i < cores:
        if read_float(pm, 0x2EC + (i * 4)) != 0.0:
            sleepTime = read_float(pm, 0x36C + (4 * i)) / 100.0
            totalV = totalV + ((1.0 - sleepTime) * avgV) + (0.2 * sleepTime)
        i = i + 1

    avgV = totalV / cores

    return read_float(pm, 0x02C), peakV, avgV

def parse_pm_table():
    codename = getCodeName()
    ccds     = getCCDCount()
    cores    = getCoreCount()
    model    = getCpuModel()

    pm       = read_pm_table()

    while True:
        print("\033c================  CPU INFO  ================")

        if codename != False:
            print("Code Name: " + codename)
        print("CCDs: {0} | Cores: {1}".format(ccds, cores))
        print("Model: " + model)

        totalA = peakFreq = i = 0
        while i < cores:
            freq = read_float(pm, 0x30C + i * 4) * 1000.0
            activity = read_float(pm, 0x32C + i * 4)
            power = read_float(pm, 0x24C + i * 4)

            if peakFreq < freq:
                peakFreq = freq

            totalA = totalA + activity

            if activity >= 6.0:
                print("Core #{:d}: {:4.0f} MHz  @ {:4.4f} W ({:4.2f} %)".format(i, freq, power, activity))
            else:
                print("Core #{:d}: Sleeping  @ {:4.4f} W ({:4.2f} %)".format(i, power, activity))
            i = i + 1

        print("Peak Frequency:  {:.0f} MHz".format(peakFreq))

        svi2V, peakV, avgV = getCoreVoltage(pm, cores)
        print("SVI2 Voltage:    {:2.6f} V".format(svi2V))
        print("Peak Voltage:    {:2.6f} V".format(peakV))
        print("Average Voltage: {:2.6f} V".format(avgV))
        
        print("============================================\n")
        print("================ PBO LIMITS ================")
        pptW  = read_float(pm, 0x000)
        pptU  = read_float(pm, 0x004)
        tdcA  = read_float(pm, 0x008)
        tdcU  = read_float(pm, 0x00C)
        tjMax = read_float(pm, 0x010)
        tempC = read_float(pm, 0x014)

        edcA  = read_float(pm, 0x020)
        edcU  = read_float(pm, 0x024) * (totalA / cores / 100)

        CorP  = read_float(pm, 0x060)
        SoCP  = read_float(pm, 0x064)

        SoCV = read_float(pm, 0xB4)
        SoCC = read_float(pm, 0xB8)

        if edcU < tdcU:
            edcU = tdcU

        print("TjMax: {:4.2f} °C".format(tjMax))
        print("Temp:  {:4.2f} °C".format(tempC))
        print("Core:  {:4.4f} W".format(CorP))
        print("SoC:   {:4.2f} W / {:4.2f} A / {:4.6f} V".format(SoCP, SoCC, SoCV))
        print("PPT:   {:4.2f} W / {:4.0f} W ({:4.2f}%)".format(pptU, pptW, (pptU / pptW * 100)))
        print("TDC:   {:4.2f} A / {:4.0f} A ({:4.2f}%)".format(tdcU, tdcA, (tdcU / tdcA * 100)))
        print("EDC:   {:4.2f} A / {:4.0f} A ({:4.2f}%)".format(edcU, edcA, (edcU / edcA * 100)))
        print("============================================\n")

        SoCV  = read_float(pm, 0x0B0)
        vddpV = read_float(pm, 0x1F4)
        vddgV = read_float(pm, 0x1F8)

        fclkMHz    = read_float(pm, 0xC0)
        fclkAvgMHz = read_float(pm, 0xC4)
        uclkMHz    = read_float(pm, 0x128)
        mclkMHz    = read_float(pm, 0x138)

        if uclkMHz == mclkMHz:
            coupledMode = "ON"
        else:
            coupledMode = "OFF"

        # TODO: Verify these?
        # 0x108 (BCLK?)
        # 0x1D0 (+5V ?)
        # 0x1F0 (MEM VTT?)
        # 0x208 (Peak Core Freq?)

        print("================   MEMORY   ================")
        print("Coupled Mode: " + coupledMode)
        print("FCLK (Avg):   {:.0f} MHz".format(fclkAvgMHz))
        print("FCLK:         {:.0f} MHz".format(fclkMHz))
        print("UCLK:         {:.0f} MHz".format(uclkMHz))
        print("MCLK:         {:.0f} MHz".format(mclkMHz))
        print("VDDCR_SoC:    {:.4f} V".format(SoCV))
        print("cLDO_VDDP:    {:.4f} V".format(vddpV))
        print("cLDO_VDDG:    {:.4f} V".format(vddgV))
        print("============================================")

        sleep(1)

        pm = read_pm_table()

def main():
    if is_root() == False:
        print("Script must be run with root privileges.")
        return

    if driver_loaded() == False:
        print("The driver doesn't seem to be loaded.")
        return

    if pm_table_supported():
        if read_file32(PMT_PATH) != 0x240903:
            print("WARNING: PM Table version is unsupported. Press any key to continue anyway. Output may be wrong.")
            input()
        parse_pm_table()
    else:
        print("PM Table: Unsupported")

main()
