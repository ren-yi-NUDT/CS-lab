CC = gcc
CFLAGS = -Wall -pthread

all: SearchRandom Thread

SearchRandom: SearchRandom.c
	$(CC) -o SearchRandom SearchRandom.c

Thread: Thread.c
	$(CC) $(CFLAGS) -mavx2 -o Thread Thread.c

clean:
	rm -f SearchRandom Thread

.PHONY: all clean
