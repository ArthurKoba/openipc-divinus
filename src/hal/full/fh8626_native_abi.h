#pragma once

#include <stdint.h>

/*
 * FH8626V100 direct-kernel ABI recovered from the canonical media owner.
 * These values are evidence-backed for the current 1280x720 H.264 channel-0 path.
 * Do not substitute FH8852/V201 layouts for unresolved FH8626 contracts.
 */
#define FH8626_VMM_ALLOC          0xC0686D0AUL
#define FH8626_ISP_6905           0x80046905UL
#define FH8626_ISP_6919           0x00006919UL
#define FH8626_ISP_6920           0x40016920UL
#define FH8626_ISP_6921           0x40016921UL
#define FH8626_ISP_692F           0x4004692FUL
#define FH8626_ISP_6932           0x00006932UL
#define FH8626_ISP_START          0x0000690AUL
#define FH8626_MEDIA_BIND         0xC0084D00UL
#define FH8626_MEDIA_STREAM_6     0xC1704D06UL
#define FH8626_VPU_MEM_QUERY      0xC0046942UL
#define FH8626_VPU_SYS_MEM_INIT   0xC00C6940UL
#define FH8626_VPU_SET_VI_ATTR    0xC0F46946UL
#define FH8626_VPU_CHN_MEM_QUERY  0xC0106943UL
#define FH8626_VPU_SET_CHN_MEM    0xC0186944UL
#define FH8626_VPU_SET_CHN_CFG    0xC00C6948UL
#define FH8626_VPU_OPEN_CHN       0xC004694FUL
#define FH8626_VPU_ENABLE         0xC004694DUL
#define FH8626_PAE_SYS_QUERY      0xC0045002UL
#define FH8626_PAE_SYS_INIT       0xC00C5000UL
#define FH8626_PAE_ENC_MEM_SIZE   0xC0145003UL
#define FH8626_PAE_ENC_MEM_INIT   0xC01C5004UL
#define FH8626_PAE_SET_CONFIG     0xC02C5006UL
#define FH8626_PAE_ENC_START      0xC0045008UL
#define FH8626_PAE_GET_STATUS     0xC030503FUL
#define FH8626_PAE_STREAM_STEP    0xC0045011UL

#define FH8626_NATIVE_WIDTH 1280u
#define FH8626_NATIVE_HEIGHT 720u
#define FH8626_NATIVE_FPS 25u
#define FH8626_NATIVE_CHANNEL 0u
#define FH8626_MEDIA_STREAM_DESC_WORDS 92u
#define FH8626_MEDIA_STREAM_KIND 4u

struct fh8626_mem3 {
    uint32_t phys;
    uint32_t virt;
    uint32_t size;
};

struct fh8626_vpu_query {
    uint32_t chn;
    uint32_t width;
    uint32_t height;
    uint32_t size;
};

struct fh8626_vpu_mem {
    uint32_t chn;
    uint32_t phys;
    uint32_t virt;
    uint32_t size;
    uint32_t width;
    uint32_t height;
};

struct fh8626_channel_cfg {
    uint32_t chn;
    uint32_t width;
    uint32_t height;
};

struct fh8626_pae_mem_query {
    uint32_t chn;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t refmode;
};

struct fh8626_pae_mem {
    uint32_t chn;
    uint32_t phys;
    uint32_t virt;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t refmode;
};

struct fh8626_pae_cfg {
    uint32_t chn;
    uint32_t width;
    uint32_t height;
    uint32_t field0c;
    uint32_t profile;
    uint32_t qp;
    uint32_t fps;
    uint32_t mode;
    uint32_t field20;
    uint32_t field24;
    uint32_t field28;
};
