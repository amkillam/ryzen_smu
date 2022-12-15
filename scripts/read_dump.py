#!/bin/python

import os
import sys
import struct

def read_pm():
    with open("dump.bin", "rb") as fp:
        content = fp.read()
        fp.close()
    return content

def read_float(buffer, offset):
    return struct.unpack("@f", buffer[offset:(offset + 4)])[0]

def read_double(buffer, offset):
    return struct.unpack("@d", buffer[offset:(offset + 8)])[0]

def read_int(buffer, offset):
    return struct.unpack("@I", buffer[offset:(offset + 4)])[0]

def dump_float():
    c = read_pm()
    size = len(c)

    i = 0
    while i < size:
        v = read_float(c, i)
        print("0x{:04X} -> {:8.6f}".format(i, v))
        i = i + 4


dump_float()