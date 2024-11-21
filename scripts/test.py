#!/bin/python

import os
import struct
from time import sleep

FS_PATH = "/sys/kernel/ryzen_smu_drv/"

VER_PATH = FS_PATH + "version"
SMN_PATH = FS_PATH + "smn"
SMU_ARGS = FS_PATH + "smu_args"
RSMU_CMD = FS_PATH + "rsmu_cmd"
CN_PATH = FS_PATH + "codename"
PM_PATH = FS_PATH + "pm_table"


def is_root():
    return os.getenv("SUDO_USER") is not None or os.geteuid() == 0


def driver_loaded():
    return os.path.isfile(VER_PATH)


def pm_table_supported():
    return os.path.isfile(PM_PATH)


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


def write_file64(file, value1, value2):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<II", value1, value2))
        fp.close()

    return result == 8


def read_file192(file):
    with open(file, "rb") as fp:
        result = fp.read(24)
        result = struct.unpack("<IIIIII", result)
        fp.close()

    return result


def write_file192(file, v1, v2, v3, v4, v5, v6):
    with open(file, "wb") as fp:
        result = fp.write(struct.pack("<IIIIII", v1, v2, v3, v4, v5, v6))
        fp.close()

    return result == 24


def read_file_str(file, expectedLen=9):
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
        return 0

    return value


def write_smn_addr(addr, value):
    if write_file64(SMN_PATH, addr, value) == False:
        print(
            "Failed to write SMN address {:08X} with value: {:08X}".format(addr, value)
        )
        return False

    return True


def smu_command(op, arg1, arg2=0, arg3=0, arg4=0, arg5=0, arg6=0):
    check = True

    # Check if SMU is currently executing a command
    value = read_file32(RSMU_CMD)
    if value != False:
        while int(value) == 0:
            print("Wating for existing SMU command to complete ...")
            sleep(1)
            value = read_file32(RSMU_CMD)
    else:
        print("Failed to get SMU status response")
        return False

    # Write all arguments to the appropriate files
    if write_file192(SMU_ARGS, arg1, arg2, arg3, arg4, arg5, arg6) == False:
        print("Failed to write SMU arguments")
        return False

    # Write the command
    if write_file32(RSMU_CMD, op) == False:
        print("Failed to execute the SMU command: {:08X}".format(op))

    # Check for the result:
    value = read_file32(RSMU_CMD)
    if value != False:
        while value == 0:
            print("Wating for existing SMU command to complete ...")
            sleep(1)
            value = read_file32(RSMU_CMD)
    else:
        print("SMU OP readback returned false")
        return False

    if value != 1:
        print("SMU Command Result Failed: " + value)
        return False

    args = read_file192(SMU_ARGS)

    if args == False:
        print("Failed to read SMU response arguments")
        return False

    return args


def test_get_version():
    args = smu_command(0x02, 1)

    if args == False:
        return False

    v_test = "{:d}.{:d}.{:d}\n".format(
        args[0] >> 16 & 0xFF, args[0] >> 8 & 0xFF, args[0] & 0xFF
    )

    if v_test == read_file_str(VER_PATH, 8):
        print("Retrieved SMU Version: v{0}".format(v_test.split("\n")[0]))
        return True

    print("SMU Test: Failed!")
    return False


def test_get_codename():
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
        "Pinnacle Ridge",
        "Rembrandt",
        "Vermeer",
        "Vangogh",
        "Cezanne",
        "Milan",
        "Dali",
        "Lucienne",
        "Naples",
        "Chagall",
        "Raphael",
        "Phoenix" "Hawk Point",
    ]
    args = read_file_str(CN_PATH, 3)

    if args != False and int(args) != 0 and int(args) < len(codenames):
        print("Processor Code Name: " + codenames[int(args)])
        return True

    print("Failed to detect processor code name!")
    return False


def main():
    if is_root() == False:
        print("Script must be run with root privileges.")
        return

    if driver_loaded() == False:
        print("The driver doesn't seem to be loaded.")
        return

    if test_get_version() == False:
        return

    if test_get_codename() == False:
        return

    if pm_table_supported():
        print("PM Table: Supported")
    else:
        print("PM Table: Unsupported")

    val = read_smn_addr(0x50200)

    if val != False:
        print("SMN Offset[0x50200]: 0x{:08x}\n".format(val))
        print("Everything seems to be working properly!")
    else:
        print("Failed to read SMN address. Is your system supported?")


main()

