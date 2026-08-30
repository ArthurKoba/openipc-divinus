CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic

TARGET = test_fh86_wire
SOURCE = ../../src/source/fh86_wire.c test_fh86_wire.c

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
