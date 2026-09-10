CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
SIG_CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function
TARGET = test_fh8626_native_adapter
SIGNATURE = test_sink_signature.o

.PHONY: all test clean
all: $(TARGET) $(SIGNATURE)

$(TARGET): ../../src/hal/full/fh8626_contract.c ../../src/hal/full/fh8626_stream_backend.c ../../src/hal/full/fh8626_native_adapter.c test_fh8626_native_adapter.c
	$(CC) $(CFLAGS) -I../../src/hal/full -I../../src/hal $^ -o $@

$(SIGNATURE): test_sink_signature.c
	$(CC) $(SIG_CFLAGS) -I../../src -c $< -o $@

test: all
	./$(TARGET)

clean:
	rm -f $(TARGET) $(SIGNATURE)
