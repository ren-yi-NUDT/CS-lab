CC = gcc
CFLAGS = -O2 -Wall -Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-to-pointer-cast
TARGET = rowcol_test.exe

SRCS = rowcol.c rowcol_test.c cpe.c fcyc.c clock.c lsquare.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	del /Q $(OBJS) $(TARGET) 2>nul

.PHONY: all run clean
