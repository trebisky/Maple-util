# Makefile for maple-util
# Tom Trebisky  11-2-2020

OBJS = main.o

all: maple-util

maple-util:	$(OBJS)
	cc -o maple-util $(OBJS)
