CC = gcc
CFLAGS = -Wall -pthread

all: SearchRandom Thread_202402720028

SearchRandom: SearchRandom.c
	$(CC) -o SearchRandom SearchRandom.c

Thread_202402720028: Thread_202402720028.c
	$(CC) $(CFLAGS) -mavx2 -o Thread_202402720028 Thread_202402720028.c

clean:
	rm -f SearchRandom Thread_202402720028

.PHONY: all clean
