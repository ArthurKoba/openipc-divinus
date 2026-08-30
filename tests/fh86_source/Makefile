CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic

TARGET = test_fh86_source
SOURCE = ../../src/source/fh86_wire.c ../../src/source/fh86_stream.c ../../src/source/fh86_unix.c ../../src/source/fh86_source.c test_fh86_source.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
