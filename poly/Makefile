CC = gcc
CFLAGS = -O2 -Wall -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-to-pointer-cast
TARGET = poly_test.exe

SRCS = poly.c poly_test.c cpe.c fcyc.c clock.c lsquare.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) $(STUID)

clean:
	del /Q $(OBJS) $(TARGET) 2>nul

.PHONY: all run clean
