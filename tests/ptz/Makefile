CC ?= cc
CFLAGS ?= -O2

test_ptz: test_ptz.c ../../src/ptz.c ../../src/ptz.h
	$(CC) $(CFLAGS) -std=gnu11 -Wall -Wextra -Werror -pthread -I../../src \
		test_ptz.c ../../src/ptz.c -o $@

test: test_ptz
	./test_ptz

clean:
	rm -f test_ptz

.PHONY: clean test
