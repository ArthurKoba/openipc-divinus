CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror

TARGET = test_fh86_h264
SOURCE = ../../src/source/fh86_h264.c test_fh86_h264.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
