# Makefile —— 计算机系统实验10 Web 服务器

CC      = gcc
CFLAGS  = -O2 -Wall -Werror -std=c99
TARGET  = webserver
SRC     = webserver_202402720028.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) webserver.log *.o core
