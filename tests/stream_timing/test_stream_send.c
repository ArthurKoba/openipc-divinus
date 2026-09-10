#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "../../src/stream_send.h"

static void *slow_reader(void *p) {
    int fd=*(int*)p;
    char byte;
    for(int i=0;i<250;++i) {
        (void)recv(fd,&byte,1,MSG_DONTWAIT);
        usleep(1000);
    }
    return NULL;
}
int main(void) {
    int fd[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    int size=1024;
    assert(setsockopt(fd[0],SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))==0);
    char *data=calloc(1,1<<20);
    assert(data);
    struct iovec iov[2]={{.iov_base=(void*)"head",.iov_len=4},
                         {.iov_base=data,.iov_len=1<<20}};
    pthread_t thread;
    assert(pthread_create(&thread,NULL,slow_reader,&fd[1])==0);
    int64_t start=stream_now_ms();
    assert(stream_send_deadline(fd[0],iov,2,100)==-1);
    int64_t elapsed=stream_now_ms()-start;
    assert(elapsed>=90 && elapsed<500);
    pthread_join(thread,NULL);
    close(fd[1]);
    iov[0]=(struct iovec){.iov_base=data,.iov_len=5};
    assert(stream_send_deadline(fd[0],iov,1,100)==-1); /* no SIGPIPE */
    close(fd[0]);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    iov[0]=(struct iovec){.iov_base=(void*)"hello",.iov_len=5};
    assert(stream_send_deadline(fd[0],iov,1,100)==0);
    char result[5]; assert(read(fd[1],result,5)==5);
    close(fd[0]); close(fd[1]); free(data);
    puts("stream send: slow reader total deadline, peer close, normal send PASS");
    return 0;
}
