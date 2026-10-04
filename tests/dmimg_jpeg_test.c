#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmimg.h"
#include <errno.h>
#include <string.h>

/*
 * The JPEG files in fixtures/ (made by fixtures/make_fixtures.py) decoded
 * through dmimg - which loads dmimg_jpeg by the extension - at every
 * scale. Each image is 37 x 23 of a smooth pattern; the decoded pixels are
 * checked against it with a tolerance for the compression.
 */

#ifndef DMIMG_JPEG_FIXTURES_DIR
#define DMIMG_JPEG_FIXTURES_DIR "fixtures"
#endif
#ifndef DMIMG_JPEG_TEST_DIR
#define DMIMG_JPEG_TEST_DIR "."
#endif
#define FIXTURE(name)   DMIMG_JPEG_FIXTURES_DIR "/" name
#define OUTPUT(name)    DMIMG_JPEG_TEST_DIR "/" name

#define W   37
#define H   23

static uint32_t g_pixels[W * H];
static uint8_t  g_covered[W * H];       /* Times each pixel was output */
static uint32_t g_w, g_h;               /* The size decoded at */
static uint32_t g_largest;

static int collect(void* ctx, const dmimg_block_t* b)
{
    (void)ctx;
    if (b->x + b->width > g_w || b->y + b->height > g_h)
        return -100;
    if (b->width * b->height > g_largest)
        g_largest = b->width * b->height;
    for (uint32_t y = 0; y < b->height; y++)
        for (uint32_t x = 0; x < b->width; x++)
        {
            g_pixels[(b->y + y) * g_w + b->x + x] = b->pixels[y * b->stride + x];
            g_covered[(b->y + y) * g_w + b->x + x]++;
        }
    return 0;
}

/* The pattern at the middle of the area a pixel at 1/2^scale stands for */
static void pattern(uint32_t x, uint32_t y, uint8_t scale, bool gray, int* rgb)
{
    double cx = (x + 0.5) * (1u << scale) - 0.5, cy = (y + 0.5) * (1u << scale) - 0.5;
    if (cx > W - 1) cx = W - 1;
    if (cy > H - 1) cy = H - 1;
    double r = cx * 6, g = cy * 10, b = 120;
    if (gray)
        r = g = b = (r * 299 + g * 587 + b * 114) / 1000;
    rgb[0] = (int)(r + 0.5);
    rgb[1] = (int)(g + 0.5);
    rgb[2] = (int)(b + 0.5);
}

/* Decode a file at a scale; how many pixels are further than `tolerance`
 * from the pattern, or -1 */
static int decode(const char* path, uint8_t scale, bool gray, int tolerance)
{
    dmimg_info_t info;
    int status = 0;
    dmimg_t image = dmimg_open_file(path, &info, &status);
    if (image == NULL)
    {
        Dmod_Printf("    cannot open %s: %d\n", path, status);
        return -1;
    }
    g_w = DMIMG_SCALED(info.width, scale);
    g_h = DMIMG_SCALED(info.height, scale);
    g_largest = 0;
    memset(g_covered, 0, sizeof(g_covered));
    int ret = (info.width == W && info.height == H && !info.alpha) ? dmimg_decode(image, scale, collect, NULL) : -2;
    dmimg_close(image);
    if (ret != 0)
    {
        Dmod_Printf("    %s at 1/%u: %d\n", path, 1u << scale, ret);
        return -1;
    }
    int wrong = 0;
    for (uint32_t y = 0; y < g_h; y++)
        for (uint32_t x = 0; x < g_w; x++)
        {
            int want[3];
            uint32_t c = g_pixels[y * g_w + x];
            int got[3] = { (int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF), (int)(c & 0xFF) };
            pattern(x, y, scale, gray, want);
            bool bad = g_covered[y * g_w + x] != 1 || (c >> 24) != 0xFF;
            if (x >= (W >> scale))                  /* Rounded up: the column next to it */
                bad = bad || c != g_pixels[y * g_w + x - 1];
            else if (y >= (H >> scale))             /* ... the row above */
                bad = bad || c != g_pixels[(y - 1) * g_w + x];
            else
                for (int k = 0; k < 3; k++)
                    bad = bad || got[k] - want[k] > tolerance || want[k] - got[k] > tolerance;
            if (bad && wrong++ == 0)
                Dmod_Printf("    %s at 1/%u: (%u, %u) is 0x%08X x%u, expected %d %d %d\n", path, 1u << scale, (unsigned)x,
                            (unsigned)y, (unsigned)c, (unsigned)g_covered[y * g_w + x], want[0], want[1], want[2]);
        }
    return wrong;
}

DMOD_TEST_STEP(dmimg_jpeg_decodes_baseline_images)
{
    DMOD_TEST_EXPECT_EQ(decode(FIXTURE("s420.jpg"), 0, false, 10), 0);
    DMOD_TEST_EXPECT_EQ(g_largest, 16u * 16u);              /* An MCU at a time */
    DMOD_TEST_EXPECT_EQ(decode(FIXTURE("s444.jpg"), 0, false, 8), 0);
    DMOD_TEST_EXPECT_EQ(g_largest, 8u * 8u);
    DMOD_TEST_EXPECT_EQ(decode(FIXTURE("gray.jpg"), 0, true, 8), 0);
}

DMOD_TEST_STEP(dmimg_jpeg_decodes_smaller)
{
    /* 37 x 23 -> 19 x 12, 10 x 6, 5 x 3: TJpgDec has 18 x 11, 9 x 5, 4 x 2 -
     * the last column and row repeat the ones next to them */
    for (uint8_t scale = 1; scale <= 3; scale++)
    {
        DMOD_TEST_EXPECT_EQ(decode(FIXTURE("s420.jpg"), scale, false, 6 * (1 << scale)), 0);
        DMOD_TEST_EXPECT_EQ(decode(FIXTURE("s444.jpg"), scale, false, 6 * (1 << scale)), 0);
    }
}

DMOD_TEST_STEP(dmimg_jpeg_offers_the_scales_it_has)
{
    dmimg_info_t info;
    dmimg_t image = dmimg_open_file(FIXTURE("tiny.jpg"), &info, NULL);       /* 5 x 5 */
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image == NULL)
        return;
    DMOD_TEST_EXPECT_EQ(info.scales, DMIMG_SCALE(0) | DMIMG_SCALE(1) | DMIMG_SCALE(2));
    DMOD_TEST_EXPECT_TRUE(strcmp(dmimg_decoder_name(image), "dmimg_jpeg") == 0);
    DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 3, collect, NULL), -EINVAL);
    dmimg_close(image);
}

static bool copy_head(const char* from, const char* to, size_t size)
{
    static uint8_t data[2048];
    void* f = Dmod_FileOpen(from, "rb");
    if (f == NULL)
        return false;
    size_t n = Dmod_FileRead(data, 1, sizeof(data), f);
    Dmod_FileClose(f);
    if (size > n)
        size = n;
    if ((f = Dmod_FileOpen(to, "wb")) == NULL)
        return false;
    bool ok = Dmod_FileWrite(data, 1, size, f) == size;
    Dmod_FileClose(f);
    return ok;
}

DMOD_TEST_STEP(dmimg_jpeg_reports_what_it_cannot_decode)
{
    int status = 0;
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(FIXTURE("progressive.jpg"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -ENOTSUP);

    /* Cut in the scan: opens, fails decoding */
    DMOD_TEST_EXPECT_TRUE(copy_head(FIXTURE("s420.jpg"), OUTPUT("cut.jpg"), 700));
    dmimg_t image = dmimg_open_file(OUTPUT("cut.jpg"), NULL, &status);
    DMOD_TEST_EXPECT_TRUE(image != NULL);
    if (image != NULL)
    {
        g_w = W;
        g_h = H;
        DMOD_TEST_EXPECT_EQ(dmimg_decode(image, 0, collect, NULL), -EBADMSG);
        dmimg_close(image);
    }

    /* Cut in the headers: does not open */
    DMOD_TEST_EXPECT_TRUE(copy_head(FIXTURE("s420.jpg"), OUTPUT("short.jpg"), 100));
    DMOD_TEST_EXPECT_TRUE(dmimg_open_file(OUTPUT("short.jpg"), NULL, &status) == NULL);
    DMOD_TEST_EXPECT_EQ(status, -EBADMSG);
}
