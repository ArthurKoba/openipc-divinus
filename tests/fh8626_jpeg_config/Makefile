CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_fh8626_jpeg_config

.PHONY: all test clean
all: $(TARGET)

$(TARGET): ../../src/hal/full/native/jpeg/fh8626_jpeg_config.c test_fh8626_jpeg_config.c
	$(CC) $(CFLAGS) -I../../src/hal/full/native/jpeg $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
