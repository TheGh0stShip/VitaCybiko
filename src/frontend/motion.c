#include "motion.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#define MOTION_INLINE static inline __attribute__((always_inline))
#else
#define MOTION_INLINE static inline
#endif

MOTION_INLINE int minimum(int a, int b) { return a < b ? a : b; }
MOTION_INLINE int maximum(int a, int b) { return a > b ? a : b; }

static void shrink(const uint8_t *src, uint8_t dst[40 * 25])
{
    for (int y = 0; y < 25; ++y)
        for (int x = 0; x < 40; ++x) {
            unsigned sum = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i)
                    sum += src[(y * 4 + j) * MOTION_W + x * 4 + i];
            dst[y * 40 + x] = (uint8_t)((sum + 8) / 16);
        }
}

/* Normalize by overlap, rejecting comparisons with too little common image.
 * The coarse image is box filtered: odd-pixel text motion must not alias out
 * of the large-displacement search. */
static unsigned translation_cost(const uint8_t *a, const uint8_t *b,
                                 int w, int h, int dx, int dy, int step)
{
    int x0 = maximum(0, dx), x1 = minimum(w, w + dx);
    int y0 = maximum(0, dy), y1 = minimum(h, h + dy);
    if ((x1 - x0) * (y1 - y0) < w * h / 2) return UINT_MAX;
    unsigned sum = 0, n = 0;
    for (int y = y0; y < y1; y += step)
        for (int x = x0; x < x1; x += step) {
            sum += (unsigned)abs((int)a[(y - dy) * w + x - dx] - b[y * w + x]);
            ++n;
        }
    return n ? sum * 256u / n : UINT_MAX;
}

static void estimate_translation(motion_pair_t *p)
{
    uint8_t a[1000], b[1000];
    shrink(p->before, a); shrink(p->after, b);
    int alo = 255, ahi = 0, blo = 255, bhi = 0;
    for (int i = 0; i < MOTION_PIXELS; ++i) {
        alo = minimum(alo, p->before[i]); ahi = maximum(ahi, p->before[i]);
        blo = minimum(blo, p->after[i]); bhi = maximum(bhi, p->after[i]);
    }
    if (ahi - alo < 32 || bhi - blo < 32) return;
    unsigned zero = translation_cost(a, b, 40, 25, 0, 0, 1);
    unsigned best = zero;
    int bx = 0, by = 0;
    for (int dy = -12; dy <= 12; ++dy)
        for (int dx = -12; dx <= 12; ++dx) {
            unsigned score = translation_cost(a, b, 40, 25, dx, dy, 1);
            if (score < best || (score == best && abs(dx) + abs(dy) < abs(bx) + abs(by))) {
                best = score; bx = dx; by = dy;
            }
        }
    int cx = bx * 4, cy = by * 4;
    zero = translation_cost(p->before, p->after, MOTION_W, MOTION_H, 0, 0, 1);
    best = zero;
    for (int dy = cy - 3; dy <= cy + 3; ++dy)
        for (int dx = cx - 3; dx <= cx + 3; ++dx) {
            unsigned score = translation_cost(p->before, p->after,
                MOTION_W, MOTION_H, dx, dy, 1);
            if (score < best || (score == best && abs(dx) + abs(dy) < abs(bx) + abs(by))) {
                best = score; bx = dx; by = dy;
            }
        }
    /* A dominant scroll must agree across the image, not just find a nearby
     * repeated glyph. Uncertain/non-rigid motion uses local matching. */
    if ((bx || by) && best < 12u * 256u && best < zero / 2u) {
        p->translated = true; p->dx = bx; p->dy = by;
    }
}

static unsigned block_cost(const uint8_t *a, const uint8_t *b,
                           int x, int y, int dx, int dy, int step,
                           unsigned limit)
{
    int w = minimum(MOTION_BLOCK, MOTION_W - x);
    int h = minimum(MOTION_BLOCK, MOTION_H - y);
    if (x + dx < 0 || x + dx + w > MOTION_W ||
        y + dy < 0 || y + dy + h > MOTION_H) return UINT_MAX;
    unsigned sum = 0;
    for (int j = 0; j < h; j += step) {
        for (int i = 0; i < w; i += step)
            sum += (unsigned)abs((int)a[(y + j) * MOTION_W + x + i] -
                b[(y + dy + j) * MOTION_W + x + dx + i]);
        if (sum > limit) break;
    }
    return sum;
}

static void estimate_blocks(const uint8_t *a, const uint8_t *b,
                            motion_vector_t *vectors)
{
    for (int ty = 0; ty < MOTION_ROWS; ++ty)
        for (int tx = 0; tx < MOTION_COLS; ++tx) {
            int index = ty * MOTION_COLS + tx, x = tx * 8, y = ty * 8;
            motion_vector_t *v = &vectors[index];
            unsigned zero = block_cost(a, b, x, y, 0, 0, 1, UINT_MAX);
            if (!zero) { v->valid = 1; continue; }
            int lo = 255, hi = 0;
            for (int j = y; j < minimum(y + 8, MOTION_H); ++j)
                for (int i = x; i < x + 8; ++i) {
                    lo = minimum(lo, a[j * MOTION_W + i]);
                    hi = maximum(hi, a[j * MOTION_W + i]);
                }
            /* A flat patch matching another flat patch is not evidence of
             * motion. Otherwise newly drawn text makes the blank background
             * appear to move and creates hundreds of bogus vectors. */
            if (hi - lo < 32) continue;
            unsigned best = block_cost(a, b, x, y, 0, 0, 2, UINT_MAX);
            int bx = 0, by = 0;
            for (int dy = -12; dy <= 12; dy += 2)
                for (int dx = -12; dx <= 12; dx += 2) {
                    unsigned cost = block_cost(a, b, x, y, dx, dy, 2, best);
                    if (cost < best) { best = cost; bx = dx; by = dy; }
                }
            int cx = bx, cy = by;
            best = zero;
            for (int dy = cy - 1; dy <= cy + 1; ++dy)
                for (int dx = cx - 1; dx <= cx + 1; ++dx) {
                    unsigned cost = block_cost(a, b, x, y, dx, dy, 1, best);
                    if (cost < best) { best = cost; bx = dx; by = dy; }
                }
            int area = minimum(8, MOTION_H - y) * 8;
            if ((bx || by) && best < (unsigned)area * 12 && best < zero / 2) {
                v->x = (int8_t)bx; v->y = (int8_t)by; v->valid = 1;
            }
        }
}

static bool separator_row(const motion_pair_t *p, int y)
{
    int base = y * MOTION_W;
    uint8_t background = p->before[base];
    for (int x = 0; x < MOTION_W; ++x)
        if (p->before[base + x] != background || p->after[base + x] != background)
            return false;
    return true;
}

static void local_consistency(const motion_vector_t *flow,
                              const motion_vector_t *reverse,
                              unsigned *moving, unsigned *consistent)
{
    for (int i = 0; i < MOTION_BLOCKS; ++i) {
        motion_vector_t v = flow[i];
        if (!v.valid || (!v.x && !v.y)) continue;
        ++*moving;
        int x = i % MOTION_COLS * 8 + 4 + v.x;
        int y = minimum(i / MOTION_COLS * 8 + 4, MOTION_H - 1) + v.y;
        if (x < 0 || y < 0 || x >= MOTION_W || y >= MOTION_H) continue;
        motion_vector_t b = reverse[y / 8 * MOTION_COLS + x / 8];
        *consistent += b.valid && abs(v.x + b.x) <= 1 && abs(v.y + b.y) <= 1;
    }
}

static void estimate_horizontal_bands(motion_pair_t *p)
{
    /* Uniform separator rows divide independently moving title, carousel,
     * caption and status regions without knowing any CyOS screen layout. */
    for (int y = 0; y < MOTION_H;) {
        if (separator_row(p, y)) { p->stationary_rows[y++] = 1; continue; }
        int first = y++;
        while (y < MOTION_H && !separator_row(p, y)) ++y;
        unsigned zero = translation_cost(p->before + first * MOTION_W,
            p->after + first * MOTION_W, MOTION_W, y - first, 0, 0, 1);
        unsigned best = zero;
        int dx = 0;
        /* Fine search around dominant motion; unrelated/ambiguous changes
         * keep their native endpoint instead of generating a false warp. */
        if (zero)
            for (int candidate = p->dx - 8; candidate <= p->dx + 8; ++candidate) {
                unsigned cost = translation_cost(p->before + first * MOTION_W,
                    p->after + first * MOTION_W, MOTION_W, y - first, candidate, 0, 1);
                if (cost < best || (cost == best && abs(candidate) < abs(dx))) {
                    best = cost; dx = candidate;
                }
            }
        bool moving = dx && best < 12u * 256u && best < zero / 2;
        for (int row = first; row < y; ++row) {
            unsigned still_cost = translation_cost(p->before + row * MOTION_W,
                p->after + row * MOTION_W, MOTION_W, 1, 0, 0, 1);
            unsigned motion_cost = translation_cost(p->before + row * MOTION_W,
                p->after + row * MOTION_W, MOTION_W, 1, dx, 0, 1);
            /* A periodic row can be identical at both endpoints while
             * crossing several display pixels between them. When the
             * band's translation matches it exactly, keep it with the band
             * instead of freezing a horizontal strip through a moving icon. */
            bool row_moves = moving && (!motion_cost || motion_cost < still_cost);
            p->stationary_rows[row] = !row_moves;
            p->row_dx[row] = row_moves ? dx : 0;
        }
    }
}

void motion_estimate(motion_pair_t *p, const uint8_t *before, const uint8_t *after)
{
    memset(p, 0, sizeof(*p));
    memcpy(p->before, before, MOTION_PIXELS);
    memcpy(p->after, after, MOTION_PIXELS);
    unsigned changed = 0, error = 0;
    p->x0 = MOTION_W; p->y0 = MOTION_H;
    for (int i = 0; i < MOTION_PIXELS; ++i) {
        unsigned d = (unsigned)abs((int)before[i] - after[i]);
        changed += d != 0; error += d;
        if (d) {
            p->x0 = minimum(p->x0, i % MOTION_W);
            p->x1 = maximum(p->x1, i % MOTION_W + 1);
            p->y0 = minimum(p->y0, i / MOTION_W);
            p->y1 = maximum(p->y1, i / MOTION_W + 1);
        }
    }
    if (!changed) return;
    estimate_translation(p);
    /* Hard cuts/fades involving most of the screen have no useful optical
     * correspondence. A verified scrolling translation overrides this. */
    if (!p->translated && changed > MOTION_PIXELS * 3 / 4 &&
        error > MOTION_PIXELS * 48u) { p->cut = true; return; }
    for (int ty = 0; ty < MOTION_ROWS; ++ty)
        for (int tx = 0; tx < MOTION_COLS; ++tx) {
            int x = tx * 8, y = ty * 8, lo = 255, hi = 0;
            bool identical = true;
            for (int j = y; j < minimum(y + 8, MOTION_H); ++j)
                for (int i = x; i < x + 8; ++i) {
                    int k = j * MOTION_W + i;
                    identical &= before[k] == after[k];
                    lo = minimum(lo, before[k]); hi = maximum(hi, before[k]);
                }
            /* Unchanged text/HUD remains byte-exact during a dominant scroll.
             * Flat background is NOT pinned: moving edges may cross it. */
            p->stationary[ty * MOTION_COLS + tx] = identical && hi - lo > 32;
            /* Repeated strokes can be identical at both endpoints while
             * still belonging to the moving title/icon. Do not pin a tile
             * that the verified translation also explains exactly. */
            if (p->translated && p->stationary[ty * MOTION_COLS + tx] &&
                (block_cost(before, after, x, y, p->dx, p->dy, 1, UINT_MAX) == 0 ||
                 block_cost(after, before, x, y, -p->dx, -p->dy, 1, UINT_MAX) == 0))
                p->stationary[ty * MOTION_COLS + tx] = 0;
        }
    if (p->translated) {
        if (!p->dy) estimate_horizontal_bands(p);
        p->moving_blocks = MOTION_BLOCKS;
        return;
    }
    estimate_blocks(before, after, p->forward);
    estimate_blocks(after, before, p->backward);
    unsigned moving = 0, consistent = 0;
    local_consistency(p->forward, p->backward, &moving, &consistent);
    local_consistency(p->backward, p->forward, &moving, &consistent);
    /* Independent glyph matches are not a coherent deformation field. If
     * most moving patches cannot be tracked both ways, mixing good patches
     * with native fallback patches produces a torn mosaic. Keep that source
     * transition exact instead. Dominant/banded scrolling is unaffected. */
    if (moving && consistent * 4 < moving * 3) {
        p->local_rejected = true;
        return;
    }
    for (int i = 0; i < MOTION_BLOCKS; ++i)
        if ((p->forward[i].valid && (p->forward[i].x || p->forward[i].y)) ||
            (p->backward[i].valid && (p->backward[i].x || p->backward[i].y)))
            ++p->moving_blocks;
}

/* Coordinates are Q8. Bilinear filtering makes one guest-pixel motion have
 * intermediate positions on the 3x Vita LCD; it is not an unwarped ghost. */
MOTION_INLINE int sample(const uint8_t *image, int x, int y, int scale)
{
    if (x < 0 || y < 0 || x > (MOTION_W * scale - 1) * 256 ||
        y > (MOTION_H * scale - 1) * 256) return -1;
    int ix = x / 256, iy = y / 256, fx = x & 255, fy = y & 255;
    int nx = minimum((ix + 1) / scale, MOTION_W - 1);
    int ny = minimum((iy + 1) / scale, MOTION_H - 1);
    ix /= scale; iy /= scale;
    int top = image[iy * MOTION_W + ix] * (256 - fx) + image[iy * MOTION_W + nx] * fx;
    int bottom = image[ny * MOTION_W + ix] * (256 - fx) + image[ny * MOTION_W + nx] * fx;
    return (top * (256 - fy) + bottom * fy + 32768) / 65536;
}

MOTION_INLINE bool crosses_stationary(const motion_pair_t *p, int x, int y, int scale)
{
    if (x < 0 || y < 0 || x > (MOTION_W * scale - 1) * 256 ||
        y > (MOTION_H * scale - 1) * 256) return false;
    int x0 = x / 256 / scale, y0 = y / 256 / scale;
    int x1 = (x & 255) ? minimum((x / 256 + 1) / scale, MOTION_W - 1) : x0;
    int y1 = (y & 255) ? minimum((y / 256 + 1) / scale, MOTION_H - 1) : y0;
    return p->stationary[(y0 / 8) * MOTION_COLS + x0 / 8] ||
           p->stationary[(y0 / 8) * MOTION_COLS + x1 / 8] ||
           p->stationary[(y1 / 8) * MOTION_COLS + x0 / 8] ||
           p->stationary[(y1 / 8) * MOTION_COLS + x1 / 8];
}

MOTION_INLINE int warped_local(const uint8_t *image, const motion_vector_t *vectors,
                        const motion_vector_t *reverse,
                        int x, int y, unsigned phase, int scale)
{
    int sx = x * 256, sy = y * 256;
    motion_vector_t v = vectors[(y / (8 * scale)) * MOTION_COLS + x / (8 * scale)];
    if (!v.valid) return -1;
    /* Invert the source flow only when it converges. Previously the final
     * iteration could change vectors without recalculating the coordinates,
     * or accept an invalid source block. Both sampled unrelated icon pieces. */
    for (int iteration = 0; iteration < 4; ++iteration) {
        sx = x * 256 - v.x * (int)phase * scale;
        sy = y * 256 - v.y * (int)phase * scale;
        if (sx < 0 || sy < 0 || sx >= MOTION_W * 256 * scale || sy >= MOTION_H * 256 * scale)
            return -1;
        motion_vector_t next = vectors[(sy / (2048 * scale)) * MOTION_COLS + sx / (2048 * scale)];
        if (!next.valid) return -1;
        if (next.x == v.x && next.y == v.y) {
            int ex = sx + v.x * 256 * scale, ey = sy + v.y * 256 * scale;
            if (ex < 0 || ey < 0 || ex >= MOTION_W * 256 * scale ||
                ey >= MOTION_H * 256 * scale) return -1;
            motion_vector_t back = reverse[(ey / (2048 * scale)) * MOTION_COLS + ex / (2048 * scale)];
            /* Occluded patches and repeated patterns can each look like a
             * good one-way match. Require the reverse flow to return within
             * one source pixel before using either patch for synthesis. */
            if (!back.valid || abs(v.x + back.x) > 1 || abs(v.y + back.y) > 1)
                return -1;
            return sample(image, sx, sy, scale);
        }
        v = next;
    }
    return -1;
}

MOTION_INLINE void copy_scaled(const uint8_t *image, int scale, uint8_t *out)
{
    if (scale == 1) { memcpy(out, image, MOTION_PIXELS); return; }
    for (unsigned y = 0; y < MOTION_H * scale; ++y)
        for (unsigned x = 0; x < MOTION_W * scale; ++x)
            out[y * MOTION_W * scale + x] = image[(y / scale) * MOTION_W + x / scale];
}

typedef struct { int lo, hi, fraction; bool valid; } axis_sample_t;

MOTION_INLINE axis_sample_t axis_sample(int q, int size)
{
    axis_sample_t s = {0};
    s.valid = q >= 0 && q <= (size * 3 - 1) * 256;
    if (s.valid) {
        s.lo = q / 256 / 3;
        s.hi = minimum((q / 256 + 1) / 3, size - 1);
        s.fraction = q & 255;
    }
    return s;
}

MOTION_INLINE int mix_axis(int a, int b, int fraction)
{
    return (a * (256 - fraction) + b * fraction + 128) / 256;
}

MOTION_INLINE uint8_t combine(int a, int b, unsigned phase, int fallback)
{
    if (a < 0) return (uint8_t)(b < 0 ? fallback : b);
    if (b < 0) return (uint8_t)a;
    return (uint8_t)mix_axis(a, b, (int)phase);
}

/* Axis-aligned scrolls are the dominant CyOS animation. Evaluate one value
 * per repeated row/column triplet rather than running a general optical-flow
 * sampler 144,000 times. No per-pixel division, allocation or float math. */
static void vertical_scaled(const motion_pair_t *p, unsigned phase, uint8_t *out)
{
    int da = p->dy * (int)phase * 3, db = p->dy * (int)(256 - phase) * 3;
    for (int y = 0; y < 300; ++y) {
        axis_sample_t a = axis_sample(y * 256 - da, 100);
        axis_sample_t b = axis_sample(y * 256 + db, 100);
        int sy = y / 3, tile_row = sy / 8 * MOTION_COLS;
        for (int x = 0; x < 160; ++x) {
            int index = sy * 160 + x, col = x / 8, va = -1, vb = -1;
            uint8_t value;
            if (p->stationary[tile_row + col]) value = p->after[index];
            else {
                if (a.valid && !p->stationary[a.lo / 8 * MOTION_COLS + col] &&
                    (!a.fraction || !p->stationary[a.hi / 8 * MOTION_COLS + col]))
                    va = mix_axis(p->before[a.lo * 160 + x], p->before[a.hi * 160 + x], a.fraction);
                if (b.valid && !p->stationary[b.lo / 8 * MOTION_COLS + col] &&
                    (!b.fraction || !p->stationary[b.hi / 8 * MOTION_COLS + col]))
                    vb = mix_axis(p->after[b.lo * 160 + x], p->after[b.hi * 160 + x], b.fraction);
                value = combine(va, vb, phase, phase < 128 ? p->before[index] : p->after[index]);
            }
            out[y * 480 + x * 3] = value;
            out[y * 480 + x * 3 + 1] = value;
            out[y * 480 + x * 3 + 2] = value;
        }
    }
}

static void horizontal_scaled(const motion_pair_t *p, unsigned phase,
                              uint8_t out[static MOTION_PIXELS * 9])
{
    axis_sample_t a[480], b[480];
    int previous_dx = INT_MAX;
    for (int y = 0; y < 100; ++y) {
        int dx = p->row_dx[y];
        bool tile_mask = dx == p->dx;
        if (!p->stationary_rows[y] && dx != previous_dx) {
            int da = dx * (int)phase * 3, db = dx * (int)(256 - phase) * 3;
            for (int x = 0; x < 480; ++x) {
                a[x] = axis_sample(x * 256 - da, 160);
                b[x] = axis_sample(x * 256 + db, 160);
            }
            previous_dx = dx;
        }
        int row = y * 160, tile_row = y / 8 * MOTION_COLS;
        uint8_t *line = out + y * 3 * 480;
        for (int x = 0; x < 480; ++x) {
            int index = row + x / 3, va = -1, vb = -1;
            if (p->stationary_rows[y] || (tile_mask && p->stationary[tile_row + x / 24]))
                line[x] = p->after[index];
            else {
                if (a[x].valid && (!tile_mask || (!p->stationary[tile_row + a[x].lo / 8] &&
                    (!a[x].fraction || !p->stationary[tile_row + a[x].hi / 8]))))
                    va = mix_axis(p->before[row + a[x].lo], p->before[row + a[x].hi], a[x].fraction);
                if (b[x].valid && (!tile_mask || (!p->stationary[tile_row + b[x].lo / 8] &&
                    (!b[x].fraction || !p->stationary[tile_row + b[x].hi / 8]))))
                    vb = mix_axis(p->after[row + b[x].lo], p->after[row + b[x].hi], b[x].fraction);
                line[x] = combine(va, vb, phase, phase < 128 ? p->before[index] : p->after[index]);
            }
            line[x + 480] = line[x];
            line[x + 960] = line[x];
        }
    }
}

MOTION_INLINE void synthesize_scaled(const motion_pair_t *p, unsigned phase,
                                     int scale, uint8_t *out)
{
    if (phase >= 256 || p->cut) { copy_scaled(p->after, scale, out); return; }
    if (!phase || !p->moving_blocks) { copy_scaled(p->before, scale, out); return; }
    if (scale == 3 && p->translated && !p->dx) { vertical_scaled(p, phase, out); return; }
    if (scale == 3 && p->translated && !p->dy) { horizontal_scaled(p, phase, out); return; }
    int x0 = 0, y0 = 0, x1 = MOTION_W * scale, y1 = MOTION_H * scale;
    if (!p->translated) {
        copy_scaled(p->before, scale, out);
        x0 = maximum(0, p->x0 - 2) * scale;
        y0 = maximum(0, p->y0 - 2) * scale;
        x1 = minimum(MOTION_W, p->x1 + 2) * scale;
        y1 = minimum(MOTION_H, p->y1 + 2) * scale;
    }
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            int k = y * MOTION_W * scale + x;
            int source = (y / scale) * MOTION_W + x / scale;
            int tile = (y / (8 * scale)) * MOTION_COLS + x / (8 * scale);
            int dx = p->translated && !p->dy ? p->row_dx[y / scale] : p->dx;
            bool tile_mask = dx == p->dx;
            if (p->stationary_rows[y / scale] || (tile_mask && p->stationary[tile])) {
                out[k] = p->after[source]; continue;
            }
            int a, b;
            if (p->translated) {
                int ax = x * 256 - dx * (int)phase * scale;
                int ay = y * 256 - p->dy * (int)phase * scale;
                int bx = x * 256 + dx * (int)(256 - phase) * scale;
                int by = y * 256 + p->dy * (int)(256 - phase) * scale;
                /* A fixed HUD must not also appear displaced elsewhere. */
                a = tile_mask && crosses_stationary(p, ax, ay, scale) ? -1 : sample(p->before, ax, ay, scale);
                b = tile_mask && crosses_stationary(p, bx, by, scale) ? -1 : sample(p->after, bx, by, scale);
            } else {
                a = warped_local(p->before, p->forward, p->backward, x, y, phase, scale);
                b = warped_local(p->after, p->backward, p->forward, x, y, 256 - phase, scale);
            }
            if (a < 0 && b < 0) out[k] = phase < 128 ? p->before[source] : p->after[source];
            else if (a < 0) out[k] = (uint8_t)b;
            else if (b < 0) out[k] = (uint8_t)a;
            else out[k] = (uint8_t)((a * (256 - phase) + b * phase + 128) / 256);
        }
}

void motion_synthesize_scaled(const motion_pair_t *p, unsigned phase,
                              unsigned scale, uint8_t *out)
{
    if (!p || !out) return;
    /* Keep the scale a compile-time constant inside the hot loop. Vita's
     * Cortex-A9 must not call software integer division for every sample. */
    if (scale == 1) synthesize_scaled(p, phase, 1, out);
    else if (scale == 3) synthesize_scaled(p, phase, 3, out);
}

void motion_synthesize(const motion_pair_t *p, unsigned phase, uint8_t *out)
{
    if (!p || !out) return;
    synthesize_scaled(p, phase, 1, out);
}

void motion_presenter_reset(motion_presenter_t *p) { memset(p, 0, sizeof(*p)); }

void motion_presenter_accept(motion_presenter_t *p, const motion_pair_t *pair,
                             uint64_t now_us)
{
    /* Future source frames are required to interpolate rather than predict.
     * A bounded 200 ms LCD-only lookahead covers Classic's 6-10 frame scroll
     * cadence. Guest execution, input sampling and audio are never delayed.
     * A hard cut or long idle gap snaps immediately and discards old motion. */
    if (!p->valid || pair->cut || now_us <= p->last_source_us ||
        now_us - p->last_source_us > 500000) {
        p->pair = *pair;
        p->received_us = now_us;
        p->duration_us = 0;
        p->head = p->count = 0;
        p->selected_end_us = now_us;
    } else {
        if (p->count == MOTION_HISTORY) {
            p->head = (p->head + 1) % MOTION_HISTORY;
            --p->count;
        }
        motion_slot_t *slot = &p->history[(p->head + p->count) % MOTION_HISTORY];
        slot->pair = *pair;
        slot->start_us = p->last_source_us;
        slot->end_us = now_us;
        ++p->count;
    }
    p->last_source_us = now_us;
    p->valid = true;
}

unsigned motion_presenter_phase(motion_presenter_t *p, uint64_t now_us)
{
    uint64_t playhead = now_us > MOTION_DELAY_US ? now_us - MOTION_DELAY_US : 0;
    while (p->count > 1 && p->history[p->head].end_us <= playhead) {
        p->head = (p->head + 1) % MOTION_HISTORY;
        --p->count;
    }
    if (p->count) {
        motion_slot_t *slot = &p->history[p->head];
        if (p->selected_end_us != slot->end_us) {
            p->pair = slot->pair;
            p->received_us = slot->start_us;
            p->duration_us = slot->end_us - slot->start_us;
            p->selected_end_us = slot->end_us;
        }
    }
    if (!p->valid || !p->duration_us) return 256;
    if (playhead <= p->received_us) return 0;
    uint64_t elapsed = playhead - p->received_us;
    if (elapsed >= p->duration_us) return 256;
    return (unsigned)(elapsed * 256 / p->duration_us);
}
