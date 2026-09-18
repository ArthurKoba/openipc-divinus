CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_fh8626_native_runtime

.PHONY: all test clean
all: $(TARGET)

$(TARGET): ../../src/hal/full/fh8626_contract.c ../../src/hal/full/fh8626_stream_backend.c ../../src/hal/full/fh8626_native_adapter.c ../../src/hal/full/fh8626_native_runtime.c test_fh8626_native_runtime.c
	$(CC) $(CFLAGS) -I../../src/hal/full -I../../src/hal $^ -o $@

test: all
	./$(TARGET)

clean:
	rm -f $(TARGET)
