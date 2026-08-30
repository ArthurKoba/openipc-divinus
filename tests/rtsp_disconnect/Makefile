CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_rtsp_disconnect

.PHONY: all clean
all: $(TARGET)

$(TARGET): test_rtsp_disconnect.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
