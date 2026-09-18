CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -DFH8626_NATIVE_STUB
INC = -I../../src/hal/full -I../../src/hal
SRC = ../../src/hal/full/fh8626_contract.c \
      ../../src/hal/full/fh8626_stream_backend.c \
      ../../src/hal/full/fh8626_native_adapter.c \
      ../../src/hal/full/fh8626_native_runtime.c \
      ../../src/hal/full/fh8626_provider.c \
      ../../src/hal/full/fh8626_hal.c \
      test_fh8626_hal_stub.c

all: test

test_fh8626_hal_stub: $(SRC)
	$(CC) $(CFLAGS) $(INC) $(SRC) -pthread -o $@

test: test_fh8626_hal_stub
	./test_fh8626_hal_stub

clean:
	rm -f test_fh8626_hal_stub
