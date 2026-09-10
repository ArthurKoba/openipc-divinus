#ifndef _RTSP_RTCP_H
#define _RTSP_RTCP_H

#include <stdlib.h>
#include <stdio.h>
#include "rtp.h"
#include "rfc.h"
#include "rtsp.h"
#include "common.h"

/******************************************************************************
 *              DECLARATIONS
 ******************************************************************************/

static inline int __rtcp_send_sr(struct connection_item_t *con, int track_id);


/******************************************************************************
 *              INLINE FUNCTIONS
 ******************************************************************************/
static inline int __rtcp_send_sr(struct connection_item_t *con, int track_id)
{
    struct timeval tv;
    unsigned int ts_h, ts_l;
    int send_bytes;
    struct sockaddr_in to_addr;
    transport_t *t;

    ASSERT(track_id >= 0 &&
        track_id < (int)(sizeof(con->trans) / sizeof(con->trans[0])),
        return FAILURE);
    t = &con->trans[track_id];

    ASSERT(gettimeofday(&tv,NULL) == 0, return FAILURE);

    ts_h = (unsigned int)tv.tv_sec + 2208988800U;
    ts_l = (((double)tv.tv_usec) / 1e6) * 4294967296.0;

    rtcp_t rtcp = { common: {version: 2, length: htons(8), p:0, count: 0, pt:RTCP_SR},
        r: { sr: { ssrc: htonl(con->ssrc),
            ntp_sec: htonl(ts_h),
            ntp_frac: htonl(ts_l),
            rtp_ts: htonl(t->rtp_timestamp),
            psent: htonl(t->rtcp_packet_cnt),
            osent: htonl(t->rtcp_octet)}}};

    if (t->is_tcp) {
        unsigned char head[4];
        head[0] = '$';
        head[1] = t->channel_rtcp;
        head[2] = 0;
        head[3] = 36;

        struct iovec iov[2] = {
            {.iov_base = head, .iov_len = sizeof(head)},
            {.iov_base = &rtcp, .iov_len = 36}
        };
        pthread_mutex_lock(&con->write_mutex);
        int send_rc = stream_send_deadline(con->client_fd, iov, 2, 100);
        if (send_rc < 0) {
            /* Retire the connection, not an incomplete interleaved packet.
             * Socket close/reuse remains owned by the RTSP connection thread. */
            shutdown(con->client_fd, SHUT_RDWR);
        }
        pthread_mutex_unlock(&con->write_mutex);
        send_bytes = send_rc == 0 ? 36 : -1;
        if (send_rc < 0) return SUCCESS; /* do not starve other subscribers */

        ASSERT(send_bytes == 36, ({
            ERR("send (interleaved):%d:%s\n", send_bytes, strerror(errno));
            return FAILURE;}));
    } else {
        to_addr = con->addr;
        to_addr.sin_port = htons(t->client_port_rtcp);

        ASSERT((send_bytes = send(t->server_rtcp_fd,
            &(rtcp), 36, 0)) == 36, ({
                    ERR("send:%d:%s\n", send_bytes, strerror(errno));
                    return FAILURE;}));
    }

    t->rtcp_packet_cnt = 0;
    t->rtcp_octet = 0;
    t->rtcp_tick = t->rtcp_tick_org;

    return SUCCESS;
}

#endif
