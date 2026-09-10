#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/rtsp/list.h"
#include "../../src/rtsp/rtp.h"
static unsigned packets, markers, fu_ends;
static int capture_packet(struct nal_rtp_t *p)
{
    packets++;
    markers += !!p->packet.header.m;
    if ((p->packet.payload[0] & 31) == 28 && (p->packet.payload[1] & 64)) fu_ends++;
    assert(p->rtpsize <= (int)sizeof(p->packet));
    return 0;
}
#define DIVINUS_RTP_TEST_SINK capture_packet
#include "../../src/rtsp/rtp.c"
int main(void)
{
    unsigned char au[]={0,0,0,1,0x67,1,2,3,0,0,1,0x65,4,5,6,0,0,0,1,0x41,7,8,9};
    unsigned char slice[4000]; memset(slice,0x55,sizeof(slice)); slice[0]=0x41;
    hal_vidpack packs[2]={0}; hal_vidstream stream={0}; struct rtp_au_tail tail;
    packs[0].data=au; packs[0].length=sizeof(au); stream.pack=packs; stream.count=1;
    assert(!rtp_au_find_tail(&stream,&tail)); assert(tail.pack==0 && tail.nal==au+19);
    unsigned char *nal=au; size_t n=0; unsigned nal_count=0;
    while(nal_split(au,&nal,&n,sizeof(au))==0){
        assert(!__transfer_nal_h26x(NULL,nal,n,0,nal==tail.nal)); nal_count++;
    }
    assert(nal_count==3 && packets==3 && markers==1);
    packets=markers=fu_ends=0;
    assert(!__transfer_nal_h26x(NULL,slice,sizeof(slice),0,0));
    assert(packets==3 && markers==0 && fu_ends==1);
    assert(!__transfer_nal_h26x(NULL,slice,sizeof(slice),0,1));
    assert(packets==6 && markers==1 && fu_ends==2);
    packs[1].data=slice; packs[1].length=sizeof(slice); stream.count=2;
    assert(!rtp_au_find_tail(&stream,&tail) && tail.pack==1 && tail.nal==slice);
    packs[1].offset=sizeof(slice)+1; assert(rtp_au_find_tail(&stream,&tail)<0);
    assert(__transfer_nal_h26x(NULL,NULL,0,0,1)<0);
    packets=markers=0; slice[0]=2; slice[1]=1;
    assert(!__transfer_nal_h26x(NULL,slice,sizeof(slice),1,0)); assert(markers==0);
    assert(!__transfer_nal_h26x(NULL,slice,sizeof(slice),1,1)); assert(markers==1);
    puts("RTP actual packetizer: mixed start codes, multi-NAL/multi-pack, H264/H265 FU markers PASS");
    return 0;
}
