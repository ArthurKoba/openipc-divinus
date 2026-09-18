#ifndef FH8626_SENSOR_GC1054_H
#define FH8626_SENSOR_GC1054_H

#include <stddef.h>
#include <stdint.h>

#define FH_SENSOR_CB_SIZE 0x68u

/*
 * Source-owned GC1054 instance. The stock ABI itself is process-global, just
 * like the retained vendor plug-in; Divinus owns exactly one active sensor
 * object and keeps the 0x68 callback wire for ISP context parity.
 */
struct fh_sensor_gc1054 {
    uint8_t *cb;
    int initialized;
};

int fh_sensor_gc1054_open(struct fh_sensor_gc1054 *s);
void fh_sensor_gc1054_close(struct fh_sensor_gc1054 *s);
void *fh_sensor_gc1054_cb(const struct fh_sensor_gc1054 *s, unsigned off);
const char *fh_sensor_gc1054_name(const struct fh_sensor_gc1054 *s);
int fh_sensor_gc1054_init(struct fh_sensor_gc1054 *s);
int fh_sensor_gc1054_set_fmt(struct fh_sensor_gc1054 *s, uint32_t fmt);
int fh_sensor_gc1054_set_intt(struct fh_sensor_gc1054 *s, uint32_t intt);
int fh_sensor_gc1054_set_gain(struct fh_sensor_gc1054 *s, uint32_t gain);
int fh_sensor_gc1054_get_gain(struct fh_sensor_gc1054 *s, uint32_t *gain);
int fh_sensor_gc1054_get_intt(struct fh_sensor_gc1054 *s, uint32_t *intt);
int fh_sensor_gc1054_set_vts_multiplier(struct fh_sensor_gc1054 *s,
    uint32_t multiplier);
int fh_sensor_gc1054_get_vi_attr(struct fh_sensor_gc1054 *s, void *attr);
int fh_sensor_gc1054_write_reg(struct fh_sensor_gc1054 *s, uint32_t reg,
    uint32_t value);
int fh_sensor_gc1054_read_reg(struct fh_sensor_gc1054 *s, uint32_t reg,
    uint32_t *value);
int fh_sensor_gc1054_set_mirror_flip(struct fh_sensor_gc1054 *s,
    uint32_t logical);
int fh_sensor_gc1054_get_mirror_flip(struct fh_sensor_gc1054 *s,
    uint32_t *logical);

/* Stock GC1054 leaves the optional AWB slots +0x58/+0x5c empty. */
void fh_sensor_gc1054_awb_gain(void *opaque, uint32_t gain[3]);
void fh_sensor_gc1054_awb_query(void *opaque, uint32_t gain[3]);

void fh_sensor_gc1054_dump(const struct fh_sensor_gc1054 *s);

#endif
