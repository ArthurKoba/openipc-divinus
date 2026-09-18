SRC = $(shell find ./ -name '*.c')
OBJ = ../divinus
GIT_REV := $(shell git rev-parse --short HEAD)$(shell git diff --quiet HEAD || echo "-mod")
FH8626_NATIVE_INC = \
	-I./hal/full/native/isp \
	-I./hal/full/native/sensor/gc1054 \
	-I./hal/full/native/ae \
	-I./hal/full/native/awb_ccm \
	-I./hal/full/native/h264 \
	-I./hal/full/native/media

.PHONY: clean divinus
divinus: $(OBJ)

$(OBJ): $(SRC:%.c=%.o)
	$(CC) $^ -rdynamic $(OPT) $(FH8626_NATIVE_INC) -DGIT_REV=\"$(GIT_REV)\" -o $@

%.o: %.c Makefile
	$(CC) -c $< $(OPT) $(FH8626_NATIVE_INC) -DGIT_REV=\"$(GIT_REV)\" -o $@

clean:
	rm -rf $(SRC:%.c=%.o) $(OBJ)
