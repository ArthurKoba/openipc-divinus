#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "../../src/rtsp/tcp_input.h"
int main(void) {
    struct rtsp_tcp_input s = {0};
    char out[8192]; unsigned n;
    int fd[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, fd));
    const char request[] = "OPTIONS rtsp://test RTSP/1.0\r\nCSeq: 1\r\n\r\n";
    const unsigned char rtcp[] = {'$',1,0,4,0x80,201,0,0};
    for (unsigned i=0;i<sizeof(rtcp);i++) {
        assert(write(fd[0],rtcp+i,1)==1);
        assert(rtsp_tcp_next(fd[1],&s,out,&n)==0);
    }
    for (unsigned i=0;i<sizeof(request)-1;i++) {
        assert(write(fd[0],request+i,1)==1);
        int rc=rtsp_tcp_next(fd[1],&s,out,&n);
        assert(rc==(i==sizeof(request)-2));
    }
    assert(n==sizeof(request)-1 && !memcmp(out,request,n));
    assert(write(fd[0],request,sizeof(request)-1)==sizeof(request)-1);
    assert(write(fd[0],request,sizeof(request)-1)==sizeof(request)-1);
    assert(rtsp_tcp_next(fd[1],&s,out,&n)==1);
    assert(rtsp_tcp_next(fd[1],&s,out,&n)==1);
    s.used=4; memcpy(s.data,"$\1\377\377",4);
    assert(rtsp_tcp_extract(&s,out,&n)==0 && s.skip==65535);
    while(s.skip) { s.used=s.skip>8192?8192:s.skip; memset(s.data,0,s.used);
        assert(rtsp_tcp_extract(&s,out,&n)==0); }
    close(fd[0]); assert(rtsp_tcp_next(fd[1],&s,out,&n)==-2); close(fd[1]);
    puts("RTSP fragmented RTCP/requests, pipelining, large discard, EOF: PASS");
}
