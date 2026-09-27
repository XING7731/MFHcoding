# Makefile for Smart Home Embedded Lab

CC=gcc
CFLAGS=-Wall -Wextra -pthread -lrt

TARGETS=sensor_daemon control_center

all: $(TARGETS)

sensor_daemon: sensor_daemon.c common.h
	$(CC) $(CFLAGS) sensor_daemon.c -o sensor_daemon

control_center: control_center.c common.h
	$(CC) $(CFLAGS) control_center.c -o control_center

clean:
	rm -f $(TARGETS) smart_home.log

.PHONY: all clean
