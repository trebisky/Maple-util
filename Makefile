# Makefile for maple-util
# Tom Trebisky  11-2-2020

OBJS = main.o dfu_load.o dfu.o

all: maple-util

# This nonsense is required to find libusb.h
CFLAGS += -I/usr/include/libusb-1.0

maple-util:	$(OBJS)
	cc -o maple-util $(OBJS) -lusb-1.0
