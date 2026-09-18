CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_fh8626_graphv2

.PHONY: all test clean
all: $(TARGET)

$(TARGET): ../../src/hal/full/native/osd/fh8626_graphv2.c test_fh8626_graphv2.c
	$(CC) $(CFLAGS) -I../../src/hal/full/native/osd $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
