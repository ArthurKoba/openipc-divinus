CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror -I../../src/hal/full

all: test_provider_normal test_provider_stub

test_provider_normal: ../../src/hal/full/fh8626_provider.c test_fh8626_provider_boundary.c
	$(CC) $(CFLAGS) ../../src/hal/full/fh8626_provider.c test_fh8626_provider_boundary.c -o $@

test_provider_stub: ../../src/hal/full/fh8626_provider.c test_fh8626_provider_boundary.c
	$(CC) $(CFLAGS) -DFH8626_NATIVE_STUB ../../src/hal/full/fh8626_provider.c test_fh8626_provider_boundary.c -o $@

test: all
	./test_provider_normal normal
	./test_provider_stub stub

clean:
	rm -f test_provider_normal test_provider_stub
