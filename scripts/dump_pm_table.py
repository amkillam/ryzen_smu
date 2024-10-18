#!/bin/python3

import os
import sys
import struct
import subprocess
from time import sleep

FS_PATH  = '/sys/kernel/ryzen_smu_drv/'
VER_PATH = FS_PATH + 'version'
PM_PATH  = FS_PATH + 'pm_table'
PMV_PATH = FS_PATH + 'pm_table_version'
CN_PATH  = FS_PATH + 'codename'

def is_root():
    return os.getenv("SUDO_USER") is not None or os.geteuid() == 0

def driver_loaded():
    return os.path.isfile(VER_PATH)

def pm_table_supported():
    return os.path.isfile(PM_PATH)

def read_pm_table():
    try:
        with open(PM_PATH, "rb") as file:
            content = file.read()
            file.close()
    except:
        return False

    return content

def read_file32(file):
    try:
        with open(file, "rb") as fp:
            result = fp.read(4)
            result = struct.unpack("<I", result)[0]
            fp.close()
    except:
        return False
    
    return result

def read_file_str(file, expectedLen = 9):
    try:
        with open(file, "r") as fp:
            result = fp.read(expectedLen)
            fp.close()
    except:
        return False
    
    if len(result) != expectedLen:
        print("Read file ({0}) failed with {1}".format(file, len(result)))
        return False
    
    return result

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
        "Pinnacle Ridge",
        "Rembrandt",
        "Vermeer",
        "Vangogh",
        "Cezanne",
        "Milan",
        "Dali",
        "Hawk Point"

    ]
    args = read_file_str(CN_PATH, 2)

    if args != False and int(args) != 0:
        return codenames[int(args)]

    return False

def dump(prefix, codename, version, idx):
    BASEDIR = "./pm_dumps"

    if os.path.exists(BASEDIR) == False:
        os.mkdir(BASEDIR)

    # [BASEDIR]/[prefix]_[codename]_[version]_[idx].bin
    pathname = BASEDIR + "/{:s}_{:s}_{:08x}_{:d}.bin" \
        .format(prefix, codename.replace(" ", "_").lower(), version, idx)

    try:
        print("Writing file: {0} ...".format(pathname))
        with open(pathname, "wb+") as fp:
            fp.write(read_pm_table())
            fp.close()
    except:
        print("Failed to write file: {0}".format(pathname))
        exit(5)

def findBenchPath():
    SEARCH_PATHS = ['/bin/', '/usr/bin/', '/sbin/', '/usr/sbin/']
    PROGRAM_LIST = ['pigz', 'gzip']
    pathName = False
    program = False

    for SP in SEARCH_PATHS:
        for PG in PROGRAM_LIST:
            PATH = SP + PG

            if os.path.isfile(PATH):
                pathName = PATH
                program = PG
                break
        
        if pathName != False:
            break

    if program == False:
        print("Unable to find either gzip or pigz installed on the system for stress testing. Please install one of them.")
        exit(6)

    return program, pathName

def dumperPreInit():
    if is_root() == False:
        print("Script must be run as root.")
        exit(1)

    if driver_loaded() == False:
        print("The driver does not seem to be loaded.")
        exit(2)
    
    if pm_table_supported() == False:
        print("PM Tables are not supported for this model of processor.")
        exit(3)
    
    codename = getCodeName()

    if codename == False:
        print("Unable to retrieve the processor codename")
        exit(4)
    
    version = read_file32(PMV_PATH)

    if version == False:
        version = 0
    
    tester, testerPath = findBenchPath()
    return [codename, version, tester, testerPath]

def main():
    SAMPLES = 15
    DELAY   = 1

    codename, version, tester, testerPath = dumperPreInit()

    print("Code Name: {0}".format(codename))
    print("PM Table Version: 0x{:08X}".format(version))
    print("Tester: \"{0}\"".format(tester))

    print("Dumping {:d} instances of the PM table while idle ...".format(SAMPLES))

    i = 0
    while i < SAMPLES:
        dump("idle", codename, version, i)
        i = i + 1
        sleep(DELAY)
    
    print("Dumping {:d} instances of the PM table during full load ...".format(SAMPLES))
    subprocess.Popen("sh -c \"{0} -11 -c -f > /dev/null 2>&1 < /dev/zero\"".format(testerPath), shell=True)
    
    i = 0
    while i < SAMPLES:
        dump("load", codename, version, i)
        i = i + 1
        sleep(DELAY)
    
    subprocess.run("killall -9 {0}".format(tester), shell=True)
    print("Done!")

main()