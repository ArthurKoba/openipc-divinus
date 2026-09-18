CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_fh8626_saturation

.PHONY: all test clean
all: $(TARGET)

$(TARGET): ../../src/hal/full/native/isp/fh8626_saturation.c test_fh8626_saturation.c
	$(CC) $(CFLAGS) -I../../src/hal/full/native/isp $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
