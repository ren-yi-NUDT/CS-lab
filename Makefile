# Copyright 2022 by mars

# Description: Makefile for building a Cache Simulator.
#


LDFLAGS += 

LDLIBS   += -lzstd

CPPFLAGS := -O3 -Wall -Wextra -Winline -Winit-self -Wno-sequence-point\
           -Wno-unused-function -Wno-inline -fPIC -W -Wcast-qual -Wpointer-arith -Icbsl/include

#CPPFLAGS := -g
PROGRAMS := Cache

objects = Cache.o CacheHelper.o getopt.o cbsl/src/buffer.o cbsl/src/file.o cbsl/src/flush.o cbsl/src/read.o cbsl/src/record.o cbsl/src/utils.o cbsl/src/write.o

all: $(PROGRAMS)

Cache : $(objects)
	gcc $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	rm -f $(objects)

clean:
	rm -f $(PROGRAMS) $(objects)
