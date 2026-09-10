CC ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-pointer-sign -fsanitize=undefined -fno-sanitize-recover=all -g
FMT = ../../src/fmt
all: test_mp4_timing test_stream_send test_rtp_au test_rtsp_input
test_rtsp_input: test_rtsp_input.c ../../src/rtsp/tcp_input.h
	$(CC) $(CFLAGS) -fsanitize=address,undefined $< -o $@
test_mp4_timing: test_mp4_timing.c $(FMT)/mp4.c $(FMT)/mp4.h $(FMT)/bitbuf.c $(FMT)/moof.c $(FMT)/moov.c
	$(CC) $(CFLAGS) test_mp4_timing.c $(FMT)/bitbuf.c $(FMT)/moof.c $(FMT)/moov.c -o $@
test_stream_send: test_stream_send.c ../../src/stream_send.h
	$(CC) $(CFLAGS) $< -pthread -o $@
test_rtp_au: test_rtp_au.c ../../src/rtsp/rtp.c ../../src/rtsp/rtp_au.h
	$(CC) -std=gnu11 -ffunction-sections -fdata-sections -fsanitize=address,undefined -fno-sanitize-recover=all -g $< -Wl,--gc-sections -pthread -o $@
check: all
	./test_mp4_timing
	./test_stream_send
	./test_rtp_au
	./test_rtsp_input
.PHONY: all check
