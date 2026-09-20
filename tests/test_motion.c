#include "frontend/motion.h"
#include "acutest.h"
#include <stdlib.h>
#include <string.h>

static uint8_t pattern(int x, int y)
{
    unsigned n = (unsigned)(x + 300) * 374761393u + (unsigned)(y + 300) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (uint8_t)((n >> 24) & 0xc0);
}

static void scene(uint8_t *pixels, int dx, int dy)
{
    for (int y = 0; y < MOTION_H; ++y)
        for (int x = 0; x < MOTION_W; ++x)
            pixels[y * MOTION_W + x] = pattern(x - dx, y - dy);
}

static void test_translation_and_endpoints(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS], out[MOTION_PIXELS];
    TEST_ASSERT(p != NULL);
    const int shifts[][2] = {{12,8}, {0,-18}, {-7,0}, {0,31}, {24,-16}};
    for (unsigned k = 0; k < sizeof(shifts) / sizeof(shifts[0]); ++k) {
        int dx = shifts[k][0], dy = shifts[k][1];
        TEST_CASE_("translation %d,%d", dx, dy);
        scene(a, 0, 0); scene(b, dx, dy);
        motion_estimate(p, a, b);
        TEST_CHECK(p->translated && !p->cut);
        TEST_CHECK(p->dx == dx && p->dy == dy);
        motion_synthesize(p, 0, out); TEST_CHECK(!memcmp(out, a, sizeof(a)));
        motion_synthesize(p, 256, out); TEST_CHECK(!memcmp(out, b, sizeof(b)));
        motion_synthesize(p, 400, out); TEST_CHECK(!memcmp(out, b, sizeof(b)));
        if (!(dx & 1) && !(dy & 1)) {
            motion_synthesize(p, 128, out);
            unsigned errors = 0;
            for (int y = 20; y < 80; ++y)
                for (int x = 30; x < 130; ++x)
                    errors += out[y * MOTION_W + x] != pattern(x - dx/2, y - dy/2);
            TEST_CHECK(errors == 0);
        }
    }
    free(p);
}

static void test_cuts_and_static(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS], out[MOTION_PIXELS];
    TEST_ASSERT(p != NULL);
    memset(a, 255, sizeof(a)); memset(b, 0, sizeof(b));
    motion_estimate(p, a, b);
    TEST_CHECK(p->cut);
    motion_synthesize(p, 128, out); TEST_CHECK(!memcmp(out, b, sizeof(b)));
    scene(a, 0, 0);
    motion_estimate(p, a, a);
    TEST_CHECK(!p->cut && !p->moving_blocks);
    motion_synthesize(p, 73, out); TEST_CHECK(!memcmp(out, a, sizeof(a)));
    memset(a, 230, sizeof(a)); memcpy(b, a, sizeof(a));
    for (int y = 10; y < 20; ++y)
        for (int x = 10; x < 150; ++x) b[y * 160 + x] = pattern(x, y);
    motion_estimate(p, a, b);
    TEST_CHECK(!p->translated && !p->moving_blocks); /* newly appearing text */
    free(p);
}

static void test_presenter_timing(void)
{
    motion_presenter_t *p = malloc(sizeof(*p));
    motion_pair_t *pair = calloc(1, sizeof(*pair));
    TEST_ASSERT(p && pair);
    motion_presenter_reset(p);
    TEST_CHECK(motion_presenter_phase(p, 0) == 256);
    pair->moving_blocks = 1;
    motion_presenter_accept(p, pair, 1000000);
    motion_presenter_accept(p, pair, 1100000);
    TEST_CHECK(motion_presenter_phase(p, 1200000) == 0);
    TEST_CHECK(p->duration_us == 100000);
    TEST_CHECK(motion_presenter_phase(p, 1250000) == 128);
    TEST_CHECK(motion_presenter_phase(p, 1300000) == 256);
    TEST_CHECK(motion_presenter_phase(p, 1000000) == 0);
    motion_presenter_accept(p, pair, 9000000);
    TEST_CHECK(p->duration_us == 0); /* no multi-second input latency */
    pair->cut = true;
    motion_presenter_accept(p, pair, 9100000);
    TEST_CHECK(motion_presenter_phase(p, 9100000) == 256);
    motion_presenter_reset(p);
    TEST_CHECK(!p->valid);
    free(pair); free(p);
}

static void test_border_and_small_motion(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS];
    struct { uint8_t before[16], pixels[MOTION_PIXELS], after[16]; } out;
    TEST_ASSERT(p != NULL);
    memset(&out, 0xa5, sizeof(out));
    memset(a, 255, sizeof(a)); memcpy(b, a, sizeof(a));
    for (int y = 40; y < 56; ++y)
        for (int x = 40; x < 56; ++x) {
            a[y * MOTION_W + x] = pattern(x, y);
            b[(y + 2) * MOTION_W + x + 4] = pattern(x, y);
        }
    motion_estimate(p, a, b);
    TEST_CHECK(p->moving_blocks > 0);
    motion_synthesize(p, 128, out.pixels);
    TEST_CHECK(memcmp(out.pixels, a, sizeof(a)) != 0);
    TEST_CHECK(memcmp(out.pixels, b, sizeof(b)) != 0);
    /* Local matching and fractional edge sampling must never leave the LCD. */
    for (unsigned phase = 1; phase < 256; ++phase) motion_synthesize(p, phase, out.pixels);
    for (int i = 0; i < 16; ++i) {
        TEST_CHECK(out.before[i] == 0xa5); TEST_CHECK(out.after[i] == 0xa5);
    }
    free(p);
}

static void test_scaled_subpixel_and_hud(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS];
    uint8_t *out = malloc(MOTION_PIXELS * 9);
    TEST_ASSERT(p && out);
    scene(a, 0, 0); scene(b, 1, 0);
    motion_estimate(p, a, b);
    TEST_CHECK(p->translated && p->dx == 1 && !p->dy);
    motion_synthesize_scaled(p, 85, 3, out); /* one physical pixel, not a 3px blur */
    unsigned errors = 0;
    for (int y = 10; y < 290; ++y)
        for (int x = 10; x < 470; ++x)
            errors += abs((int)out[y * 480 + x] - a[(y / 3) * 160 + (x - 1) / 3]) > 1;
    TEST_CHECK(errors == 0);
    motion_synthesize_scaled(p, 256, 3, out);
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 480; ++x)
            TEST_CHECK(out[y * 480 + x] == b[(y / 3) * 160 + x / 3]);

    scene(a, 0, 0); scene(b, 12, 0);
    for (int y = 8; y < 16; ++y)
        for (int x = 128; x < 136; ++x)
            a[y * 160 + x] = b[y * 160 + x] = (x & 1) ? 255 : 0;
    motion_estimate(p, a, b);
    TEST_CHECK(p->translated && p->dx == 12);
    motion_synthesize_scaled(p, 128, 3, out);
    unsigned hud_errors = 0, displaced_hud = 0;
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 480; ++x) {
            if (y >= 24 && y < 48 && x >= 384 && x < 408)
                hud_errors += out[y * 480 + x] != b[(y / 3) * 160 + x / 3];
            else displaced_hud += out[y * 480 + x] > 192;
        }
    TEST_CHECK(hud_errors == 0);
    TEST_CHECK(displaced_hud == 0);
    free(p); free(out);
}

static void check_argb_fast_matches_indexed(int dx, int dy)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS];
    uint8_t *indexed = malloc(MOTION_PIXELS * 9);
    uint32_t *argb = malloc(MOTION_PIXELS * 9 * sizeof(*argb));
    uint32_t *pitched = malloc(300 * 512 * sizeof(*pitched));
    uint32_t palette[256];
    TEST_ASSERT(p && indexed && argb && pitched);
    for (unsigned i = 0; i < 256; ++i)
        palette[i] = 0xff000000u | (i << 16) | ((255u - i) << 8) | ((i * 37u) & 0xffu);

    scene(a, 0, 0);
    scene(b, dx, dy);
    motion_estimate(p, a, b);
    TEST_ASSERT_(p->translated && p->dx == dx && p->dy == dy,
                 "translation=%d displacement=%d,%d", p->translated, p->dx, p->dy);
    for (unsigned phase = 17; phase < 256; phase += 34) {
        TEST_CHECK(motion_synthesize_scaled_argb_fast(p, phase, palette, argb));
        memset(pitched, 0xa5, 300 * 512 * sizeof(*pitched));
        TEST_CHECK(motion_synthesize_scaled_argb_fast_pitch(p, phase, palette, pitched, 512));
        motion_synthesize_scaled(p, phase, 3, indexed);
        unsigned errors = 0;
        for (int y = 0; y < 300; ++y) {
            for (int x = 0; x < 480; ++x) {
                uint32_t expected = palette[indexed[y * 480 + x]];
                errors += argb[y * 480 + x] != expected;
                errors += pitched[y * 512 + x] != expected;
            }
            for (int x = 480; x < 512; ++x)
                errors += pitched[y * 512 + x] != 0xa5a5a5a5u;
        }
        TEST_CHECK_(errors == 0, "dx=%d dy=%d phase=%u errors=%u", dx, dy, phase, errors);
    }

    free(pitched); free(argb); free(indexed); free(p);
}

static void test_scaled_argb_fast_path_matches_indexed(void)
{
    check_argb_fast_matches_indexed(0, 12);
    check_argb_fast_matches_indexed(-19, 0);

    motion_pair_t *p = calloc(1, sizeof(*p));
    uint32_t palette[256] = {0}, argb[MOTION_PIXELS * 9];
    TEST_ASSERT(p != NULL);
    p->moving_blocks = 1;
    TEST_CHECK(!motion_synthesize_scaled_argb_fast(p, 128, palette, argb));
    p->translated = true;
    p->dx = 3;
    p->dy = 2;
    TEST_CHECK(!motion_synthesize_scaled_argb_fast(p, 128, palette, argb));
    p->dy = 0;
    p->cut = true;
    TEST_CHECK(!motion_synthesize_scaled_argb_fast(p, 128, palette, argb));
    free(p);
}

static void test_history_bounds_and_cuts(void)
{
    motion_presenter_t *p = calloc(1, sizeof(*p));
    motion_pair_t *pair = calloc(1, sizeof(*pair));
    TEST_ASSERT(p && pair);
    pair->moving_blocks = 1;
    for (unsigned i = 0; i < 100; ++i) {
        pair->after[0] = (uint8_t)i;
        motion_presenter_accept(p, pair, 1000000 + i * 16667);
        TEST_CHECK(p->count <= MOTION_HISTORY);
    }
    TEST_CHECK(motion_presenter_phase(p, 4000000) == 256);
    TEST_CHECK(p->pair.after[0] == 99);
    pair->cut = true;
    pair->after[0] = 237;
    motion_presenter_accept(p, pair, 5000000);
    TEST_CHECK(!p->count);
    TEST_CHECK(motion_presenter_phase(p, 5000000) == 256);
    TEST_CHECK(p->pair.after[0] == 237);
    free(pair); free(p);
}

static uint8_t band_pixel(int x, int y)
{
    if ((y >= 16 && y < 80) || (x >= 40 && x < 70)) return pattern(x, y);
    return 231;
}

static void test_local_motion_requires_round_trip(void)
{
    motion_pair_t *p = calloc(1, sizeof(*p));
    uint8_t *out = malloc(MOTION_PIXELS * 9);
    TEST_ASSERT(p && out);
    scene(p->before, 0, 0); scene(p->after, 4, 0);
    p->moving_blocks = MOTION_BLOCKS;
    p->x1 = MOTION_W; p->y1 = MOTION_H;
    for (int i = 0; i < MOTION_BLOCKS; ++i) {
        p->forward[i] = (motion_vector_t){4, 0, 1};
        p->backward[i] = (motion_vector_t){-4, 0, 1};
    }
    for (int scale = 1; scale <= 3; scale += 2) {
        motion_synthesize_scaled(p, 128, scale, out);
        unsigned errors = 0;
        for (int y = 10 * scale; y < 90 * scale; ++y)
            for (int x = 16 * scale; x < 144 * scale; ++x)
                errors += out[y * MOTION_W * scale + x] != pattern(x / scale - 2, y / scale);
        TEST_CHECK(errors == 0); /* consistent local flow still interpolates */
    }

    /* Occlusion/repeated texture: the reverse match is a different object.
     * Never blend the unrelated patches into a fictitious phone/icon. */
    for (int i = 0; i < MOTION_BLOCKS; ++i)
        p->backward[i] = (motion_vector_t){7, 0, 1};
    for (int scale = 1; scale <= 3; scale += 2)
        for (unsigned phase = 64; phase <= 192; phase += 128) {
            motion_synthesize_scaled(p, phase, scale, out);
            const uint8_t *expected = phase < 128 ? p->before : p->after;
            unsigned errors = 0;
            for (int y = 0; y < MOTION_H * scale; ++y)
                for (int x = 0; x < MOTION_W * scale; ++x)
                    errors += out[y * MOTION_W * scale + x] != expected[(y / scale) * MOTION_W + x / scale];
            TEST_CHECK(errors == 0);
        }
    free(out); free(p);
}

static void test_periodic_row_moves_with_scroll(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS];
    uint8_t *out = malloc(MOTION_PIXELS * 9);
    TEST_ASSERT(p && out);
    scene(a, 0, 0); scene(b, 8, 0);
    for (int x = 0; x < MOTION_W; ++x)
        a[40 * MOTION_W + x] = b[40 * MOTION_W + x] = (x & 7) < 4 ? 0 : 192;
    motion_estimate(p, a, b);
    TEST_ASSERT(p->translated && p->dx == 8 && p->dy == 0);
    for (int scale = 1; scale <= 3; scale += 2) {
        motion_synthesize_scaled(p, 128, scale, out);
        unsigned errors = 0;
        for (int y = 40 * scale; y < 41 * scale; ++y)
            for (int x = 16 * scale; x < 144 * scale; ++x)
                errors += out[y * MOTION_W * scale + x] != a[40 * MOTION_W + x / scale - 4];
        TEST_CHECK(errors == 0);
    }
    free(out); free(p);
}

static void test_independent_horizontal_bands(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS], out[MOTION_PIXELS];
    TEST_ASSERT(p != NULL);
    memset(a, 231, sizeof(a)); memset(b, 231, sizeof(b));
    for (int y = 0; y < MOTION_H; ++y) {
        int dx = y < 11 ? -14 : (y >= 16 && y < 80 ? -18 : 0);
        if ((y >= 11 && y < 16) || (y >= 80 && y < 90)) continue;
        for (int x = 0; x < MOTION_W; ++x) {
            a[y * MOTION_W + x] = band_pixel(x, y);
            b[y * MOTION_W + x] = band_pixel(x - dx, y);
        }
    }
    motion_estimate(p, a, b);
    TEST_ASSERT(p->translated && p->dy == 0);
    TEST_CHECK(p->row_dx[3] == -14);
    TEST_CHECK(p->row_dx[40] == -18);
    TEST_CHECK(p->stationary_rows[95]);
    motion_synthesize(p, 128, out);
    unsigned errors = 0;
    for (int y = 0; y < MOTION_H; ++y) {
        if ((y >= 11 && y < 16) || (y >= 80 && y < 90)) continue;
        int dx = y < 11 ? -14 : (y < 80 ? -18 : 0);
        for (int x = 25; x < 135; ++x)
            errors += out[y * MOTION_W + x] != band_pixel(x - dx / 2, y);
    }
    TEST_CHECK(errors == 0);
    free(p);
}

static void test_scroll_with_unaligned_and_blinking_hud(void)
{
    motion_pair_t *p = malloc(sizeof(*p));
    uint8_t a[MOTION_PIXELS], b[MOTION_PIXELS];
    uint8_t *out = malloc(MOTION_PIXELS * 9);
    TEST_ASSERT(p && out);
    for (int y = 0; y < MOTION_H; ++y)
        for (int x = 0; x < MOTION_W; ++x) {
            a[y * MOTION_W + x] = pattern(x / 4, y / 4);
            b[y * MOTION_W + x] = pattern((x + 19) / 4, y / 4);
        }
    /* Glyphs span tile boundaries; the clock also changes during scrolling. */
    for (int y = 0; y < MOTION_H; ++y) {
        if (y >= 13 && y < 87) continue;
        for (int x = 0; x < MOTION_W; ++x)
            a[y * MOTION_W + x] = b[y * MOTION_W + x] =
                (x >= 35 && x < 55 && y % 5) ? pattern(x, y) : 231;
    }
    b[91 * MOTION_W + 73] ^= 0xc0;
    motion_estimate(p, a, b);
    TEST_ASSERT_(p->translated && p->dx == -19 && p->dy == 0,
                 "translation=%d displacement=%d,%d", p->translated, p->dx, p->dy);
    for (unsigned scale = 1; scale <= 3; scale += 2)
        for (unsigned phase = 17; phase < 256; phase += 34) {
            motion_synthesize_scaled(p, phase, scale, out);
            unsigned errors = 0;
            for (int y = 0; y < MOTION_H * (int)scale; ++y) {
                if (y >= 13 * (int)scale && y < 87 * (int)scale) continue;
                for (int x = 0; x < MOTION_W * (int)scale; ++x)
                    errors += out[y * MOTION_W * scale + x] !=
                        b[(y / scale) * MOTION_W + x / scale];
            }
            TEST_CHECK_(errors == 0, "scale=%u phase=%u HUD errors=%u", scale, phase, errors);
        }
    free(out); free(p);
}

TEST_LIST = {
    {"translation_and_exact_endpoints", test_translation_and_endpoints},
    {"cuts_and_static_frames", test_cuts_and_static},
    {"presentation_timing", test_presenter_timing},
    {"small_motion_and_bounds", test_border_and_small_motion},
    {"scaled_subpixel_and_stationary_hud", test_scaled_subpixel_and_hud},
    {"scaled_argb_fast_path_matches_indexed", test_scaled_argb_fast_path_matches_indexed},
    {"bounded_history_and_scene_cuts", test_history_bounds_and_cuts},
    {"scroll_with_unaligned_and_blinking_hud", test_scroll_with_unaligned_and_blinking_hud},
    {"independent_horizontal_bands", test_independent_horizontal_bands},
    {"local_motion_requires_round_trip", test_local_motion_requires_round_trip},
    {"periodic_row_moves_with_scroll", test_periodic_row_moves_with_scroll},
    {NULL, NULL}
};
