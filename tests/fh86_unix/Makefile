CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic

TARGET = test_fh86_unix
SOURCE = ../../src/source/fh86_wire.c ../../src/source/fh86_stream.c ../../src/source/fh86_unix.c test_fh86_unix.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
