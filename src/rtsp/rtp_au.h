#ifndef DIVINUS_RTP_AU_H
#define DIVINUS_RTP_AU_H
#include "../hal/types.h"
#include "../fmt/nal.h"

struct rtp_au_tail { unsigned pack; unsigned char *nal; };
static inline int rtp_pack_annexb(const unsigned char *data, size_t len)
{
    return len >= 3 && data[0] == 0 && data[1] == 0 &&
        (data[2] == 1 || (len >= 4 && data[2] == 0 && data[3] == 1));
}

/* One hal_vidstream is one access unit. Find its final NAL, across packs,
 * before sending: neither a slice boundary nor a FU end is necessarily EOF. */
static inline int rtp_au_find_tail(const hal_vidstream *stream, struct rtp_au_tail *tail)
{
    if (!stream || !stream->pack || !stream->count || !tail) return -1;
    tail->nal = NULL;
    for (unsigned i = 0; i < stream->count; ++i) {
        const hal_vidpack *p = &stream->pack[i];
        if (!p->data || p->offset > p->length) return -1;
        unsigned char *data = p->data + p->offset;
        size_t len = p->length - p->offset;
        if (!len) continue;
        if (rtp_pack_annexb(data, len)) {
            unsigned char *nal = data;
            size_t size = 0;
            while (nal_split(data, &nal, &size, len) == 0) {
                tail->pack = i;
                tail->nal = nal;
            }
        } else {
            tail->pack = i;
            tail->nal = data;
        }
    }
    return tail->nal ? 0 : -1;
}
#endif
