CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
LDLIBS ?= -pthread

TARGET = test_rtsp_thread
SOURCE = test_rtsp_thread.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) $(LDLIBS) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
