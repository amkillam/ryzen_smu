#!/bin/python3

import os
import struct
from time import sleep

FS_PATH = '/sys/kernel/ryzen_smu_drv/'

SMN_PATH = FS_PATH + 'smn'
VER_PATH = FS_PATH + 'version'
PM_PATH  = FS_PATH + 'pm_table'

def is_root():
    return os.getenv("SUDO_USER") is not None or os.geteuid() == 0

def driver_loaded():
    return os.path.isfile(VER_PATH)

def pm_table_supported():
    return os.path.isfile(PM_PATH)

def write_file32(file, value):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<I", value))
        fp.close()

    return result == 4

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
    
    value = read_file_str(SMN_PATH)
    
    if value == False:
        print("Failed to read SMN address: {:08X}".format(addr))
        return 0
    
    return int(value, base=16)

def read_pm_table():
    with open("/sys/kernel/ryzen_smu_drv/pm_table", "rb") as file:
        content = file.read()
        file.close()

    return content

def read_float(buffer, offset):
    return struct.unpack("@f", buffer[offset:(offset + 4)])[0]

def read_double(buffer, offset):
    return struct.unpack("@d", buffer[offset:(offset + 8)])[0]

def read_int(buffer, offset):
    return struct.unpack("@I", buffer[offset:(offset + 4)])[0]

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
    # TODO: Expand this for 12+ core processors
    return 8

def parse_pm_table():
    while True:
        print("\033c")

        pm = read_pm_table()

        print("===========  CPU INFO  ===========")

        ccds = getCCDCount()
        cores = getCoreCount()

        print("CCDs: " + str(ccds))
        print("Cores: " + str(cores))

        totalA = peakFreq = i = 0
        while i < cores:
            freq = read_float(pm, 0x30C + i * 4) * 1000.0
            activity = read_float(pm, 0x32C + i * 4)

            if peakFreq < freq:
                peakFreq = freq
                peakActivity = activity

            totalA = totalA + activity

            if activity >= 6.0:
                print("Core #{:d}: {:4.0f} MHz ({:4.2f} %)".format(i, freq, activity))
            else:
                print("Core #{:d}: Sleeping  ({:4.2f} %)".format(i, activity))
            i = i + 1

        print("Peak Frequency: {:.0f} MHz".format(peakFreq))
        
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

        if edcU < tdcU:
            edcU = tdcU

        print("TjMax: {:4.2f} °C".format(tjMax))
        print("Temp:  {:4.2f} °C".format(tempC))
        print("Core:  {:4.2f} W".format(CorP))
        print("SoC:   {:4.2f} W".format(SoCP))
        print("PPT:   {:4.2f} W / {:4.0f} W ({:3.2f}%)".format(pptU, pptW, (pptU / pptW * 100)))
        print("TDC:   {:4.2f} A / {:4.0f} A ({:3.2f}%)".format(tdcU, tdcA, (tdcU / tdcA * 100)))
        print("EDC:   {:4.2f} A / {:4.0f} A ({:3.2f}%)".format(edcU, edcA, (edcU / edcA * 100)))
        print("==================================\n")

        fclkMHz = read_float(pm, 0x118)
        uclkMHz = read_float(pm, 0x128)
        mclkMHz = read_float(pm, 0x138)
        if uclkMHz == mclkMHz:
            coupledMode = "ON"
        else:
            coupledMode = "OFF"

        vddpV = read_float(pm, 0x1F4)
        vddgV = read_float(pm, 0x1F8)

        print("===========   MEMORY   ===========")
        print("Coupled Mode: " + coupledMode)
        print("FCLK:         {:.0f} MHz".format(fclkMHz))
        print("UCLK:         {:.0f} MHz".format(uclkMHz))
        print("MCLK:         {:.0f} MHz".format(mclkMHz))
        print("VDDP:         {:.4f} V".format(vddpV))
        print("VDDG:         {:.4f} V".format(vddgV))
        print("==================================\n")

        sleep(1)

def main():
    if is_root() == False:
        print("Script must be run with root privileges.")
        return

    if driver_loaded() == False:
        print("The driver doesn't seem to be loaded.")
        return

    if pm_table_supported():
        print("PM Table: Supported")
        parse_pm_table()
    else:
        print("PM Table: Unsupported")

main()