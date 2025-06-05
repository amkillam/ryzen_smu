CC = gcc
STRIP = strip

CFLAGS = -O3 -mtune=native -march=native
LDFLAGS = -lm

SFLAGS = --strip-all

PATHS = -I"../lib"

OUT = monitor_cpu

SRC = monitor_cpu.c ../lib/libsmu.c ../lib/smu_common.c

all: monitor_cpu.c ../lib/libsmu.c ../lib/smu_common.c
	$(CC) $(PATHS) $(CFLAGS) $(LDFLAGS) -o $(OUT) $(SRC)
	$(STRIP) $(SFLAGS) $(OUT)
