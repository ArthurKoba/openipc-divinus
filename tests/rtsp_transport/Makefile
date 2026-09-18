CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_rtsp_transport

.PHONY: all test clean

all: $(TARGET)

$(TARGET): test_rtsp_transport.c
	$(CC) $(CFLAGS) -I../../src $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
