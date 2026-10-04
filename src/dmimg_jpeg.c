#define DMOD_ENABLE_REGISTRATION    ON
#include "dmimg.h"
#include "tjpgd.h"
#include <errno.h>
#include <string.h>

/*
 * dmimg_jpeg - the JPEG decoder of dmimg, on TJpgDec (third_party/tjpgd,
 * ChaN): baseline JPEG - gray or YCbCr, any chroma subsampling - decoded
 * an MCU (8x8 ... 16x16 pixels) at a time, at 1/1, 1/2, 1/4 or 1/8 of its
 * size: the scaling happens in the IDCT, so a photo for a small screen
 * costs a fraction of decoding it whole. It keeps a work area of about
 * 3.5 KiB and one MCU - never the image. Progressive JPEG is not supported.
 *
 * TJpgDec rounds a scaled size down; dmimg rounds it up (DMIMG_SCALED). An
 * image whose size is not a multiple of 2^scale gets its last column / row
 * from the pixels next to it.
 */

#define POOL_SIZE       4096u           /* TJpgDec's work area: 3100 bytes + 320 for JD_FASTDECODE 1 */
#define MAX_MCU         16u             /* Pixels of an MCU's side */
#define MAX_BLOCK       ((MAX_MCU + 1u) * (MAX_MCU + 1u))

struct dmimg_decoder
{
    JDEC                    jd;
    const dmimg_input_t*    input;
    bool                    input_error;
    dmimg_output_fn         output;
    void*                   ctx;
    int                     stopped;            /* What the output returned to stop */
    uint32_t                floor_w, floor_h;   /* The scaled size TJpgDec outputs */
    uint32_t                ceil_w, ceil_h;     /* The scaled size of dmimg */
    uint32_t                pixels[MAX_BLOCK];
    uint32_t                pool[POOL_SIZE / sizeof(uint32_t)];  /* Word-aligned, as TJpgDec wants */
};

static const uint8_t g_soi[3] = { 0xFF, 0xD8, 0xFF };

/* ---- TJpgDec's callbacks (static: their addresses are handed out - a
 *      global function's address would be taken through the GOT, which the
 *      dmod loader does not relocate) ---- */

/* Read `size` bytes into `buffer`, or skip them when it is NULL */
static size_t on_input(JDEC* jd, uint8_t* buffer, size_t size)
{
    struct dmimg_decoder* d = jd->device;
    uint8_t skip[64];
    size_t done = 0;
    while (done < size)
    {
        size_t n = size - done;
        uint8_t* to = buffer;
        if (buffer == NULL)
        {
            to = skip;
            if (n > sizeof(skip))
                n = sizeof(skip);
        }
        else
            to += done;
        int32_t got = d->input->read(d->input->ctx, to, n);
        if (got < 0)
            d->input_error = true;
        if (got <= 0)
            break;
        done += (size_t)got;
    }
    return done;
}

static int on_output(JDEC* jd, void* bitmap, JRECT* rect)
{
    struct dmimg_decoder* d = jd->device;
    const uint8_t* rgb = bitmap;
    uint32_t w = (uint32_t)(rect->right - rect->left) + 1U, h = (uint32_t)(rect->bottom - rect->top) + 1U;

    /* The last column / row of the scaled image TJpgDec rounds away */
    uint32_t ow = w + ((rect->right + 1U == d->floor_w && d->ceil_w > d->floor_w) ? 1U : 0U);
    uint32_t oh = h + ((rect->bottom + 1U == d->floor_h && d->ceil_h > d->floor_h) ? 1U : 0U);
    if (ow * oh > MAX_BLOCK)
        return 0;                       /* Never: an MCU is at most 16x16 */
    for (uint32_t y = 0; y < oh; y++)
    {
        const uint8_t* row = rgb + (size_t)((y < h) ? y : h - 1U) * w * 3U;
        for (uint32_t x = 0; x < ow; x++)
        {
            const uint8_t* p = row + (size_t)((x < w) ? x : w - 1U) * 3U;
            d->pixels[y * ow + x] = 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }

    dmimg_block_t block;
    block.x = rect->left;
    block.y = rect->top;
    block.width = ow;
    block.height = oh;
    block.stride = ow;
    block.pixels = d->pixels;
    d->stopped = d->output(d->ctx, &block);
    return d->stopped == 0;
}

static int status_of(const struct dmimg_decoder* d, JRESULT r)
{
    switch (r)
    {
        case JDR_OK:    return 0;
        case JDR_INTR:  return (d->stopped != 0) ? d->stopped : -EINTR;
        case JDR_INP:   return d->input_error ? -EIO : -EBADMSG;      /* Read failed, or the image ends early */
        case JDR_MEM1:
        case JDR_MEM2:  return -ENOMEM;
        case JDR_PAR:   return -EINVAL;
        case JDR_FMT2:                                                  /* A variant TJpgDec does not have */
        case JDR_FMT3:  return -ENOTSUP;                                /* Progressive, arithmetic coding, ... */
        default:        return -EBADMSG;
    }
}

/* ---- DIF ---- */

dmod_dmimg_dif_api_declaration(1.0, dmimg_jpeg, bool, _probe, ( const uint8_t* head, size_t size ))
{
    return size >= sizeof(g_soi) && memcmp(head, g_soi, sizeof(g_soi)) == 0;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_jpeg, dmimg_decoder_t, _open,
                               ( const dmimg_input_t* input, dmimg_info_t* info, int* status ))
{
    struct dmimg_decoder* d = Dmod_Malloc(sizeof(*d));
    if (d == NULL)
    {
        *status = -ENOMEM;
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->input = input;

    /* The headers - up to the first scan */
    JRESULT r = jd_prepare(&d->jd, on_input, d->pool, sizeof(d->pool), d);
    if (r != JDR_OK || d->jd.width == 0 || d->jd.height == 0)
    {
        *status = (r != JDR_OK) ? status_of(d, r) : -EBADMSG;
        Dmod_Free(d);
        return NULL;
    }
    info->width = d->jd.width;
    info->height = d->jd.height;
    info->alpha = false;
    info->scales = 0;
    for (uint8_t n = 0; n <= 3U; n++)
    {
        /* TJpgDec outputs nothing for an image smaller than a pixel at a scale */
        if (((uint32_t)d->jd.width >> n) != 0 && ((uint32_t)d->jd.height >> n) != 0)
            info->scales |= (uint8_t)DMIMG_SCALE(n);
    }
    return d;
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_jpeg, int, _decode,
                               ( dmimg_decoder_t d, uint8_t scale, dmimg_output_fn output, void* ctx ))
{
    if (scale > 3U || output == NULL)
        return -EINVAL;
    d->output = output;
    d->ctx = ctx;
    d->floor_w = (uint32_t)d->jd.width >> scale;
    d->floor_h = (uint32_t)d->jd.height >> scale;
    d->ceil_w = DMIMG_SCALED((uint32_t)d->jd.width, scale);
    d->ceil_h = DMIMG_SCALED((uint32_t)d->jd.height, scale);
    if (d->floor_w == 0 || d->floor_h == 0)
        return -EINVAL;                 /* Not one of info.scales */
    return status_of(d, jd_decomp(&d->jd, on_output, scale));
}

dmod_dmimg_dif_api_declaration(1.0, dmimg_jpeg, void, _close, ( dmimg_decoder_t d ))
{
    Dmod_Free(d);
}

/* ---- Module ---- */

int dmod_init(const Dmod_Config_t* Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}
