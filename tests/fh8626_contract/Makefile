CC ?= cc
CFLAGS ?= -std=gnu99 -Wall -Wextra -Werror
TARGET = test_fh8626_contract

.PHONY: all test clean
all: $(TARGET)

$(TARGET): ../../src/hal/full/fh8626_contract.c test_fh8626_contract.c
	$(CC) $(CFLAGS) -I../../src/hal/full -I../../src/hal $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
