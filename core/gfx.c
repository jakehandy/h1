/* H1 - graphics primitives. See gfx.h. */
#include "gfx.h"

#ifdef H1_DEVICE
uint8_t g_fb[FB_W * FB_H] __attribute__((section(".pool")));
#else
uint8_t g_fb[FB_W * FB_H];
#endif

static int gm = G_MAX;
static int glevel = 255;      /* the ink level of the primitive being drawn, 0..255 */
static f32 gdim = 1.0f;
static f32 gox, goy;

void g_mode(int mode) { gm = mode; }
void g_dim(f32 k) { gdim = sat(k); }
void g_origin(f32 ox, f32 oy) { gox = ox; goy = oy; }

void g_clear(void)
{
    uint32_t *p = (uint32_t *)(void *)g_fb;
    uint32_t i;
    for (i = 0; i < FB_W * FB_H / 4u; i++)
        p[i] = 0;
}

void g_fade(int mul256)
{
    uint32_t i;
    for (i = 0; i < FB_W * FB_H; i++)
        g_fb[i] = (uint8_t)((g_fb[i] * mul256) >> 8);
}

static inline void set_level(f32 a)
{
    a *= gdim;
    glevel = a <= 0.0f ? 0 : (a >= 1.0f ? 255 : (int)(a * 255.0f + 0.5f));
}

/* one pixel, coverage 0..256 */
static inline void put(int x, int y, int cov)
{
    uint8_t *p;
    int v;
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H || cov <= 0)
        return;
    if (cov > 256)
        cov = 256;
    p = &g_fb[y * FB_W + x];
    if (gm == G_MAX) {
        v = (cov * glevel) >> 8;
        if (v > *p)
            *p = (uint8_t)v;
    } else if (gm == G_ADD) {
        v = *p + ((cov * glevel) >> 8);
        *p = (uint8_t)(v > 255 ? 255 : v);
    } else {
        v = glevel - *p;
        *p = (uint8_t)(*p + (v * cov) / 256);
    }
}

static inline int cov_of(f32 c) { return c <= 0.0f ? 0 : (c >= 1.0f ? 256 : (int)(c * 256.0f)); }

/* a horizontal run from xa to xb (edges, in pixel units), rows weighted by cy (0..256) */
static void hspan(int y, f32 xa, f32 xb, int cy)
{
    int x0, x1, x;
    if ((unsigned)y >= FB_H || xb <= xa || cy <= 0)
        return;
    if (xa < -0.5f) xa = -0.5f;
    if (xb > (f32)FB_W - 0.5f) xb = (f32)FB_W - 0.5f;
    if (xb <= xa)
        return;
    x0 = ifloor(xa + 0.5f);
    x1 = ifloor(xb + 0.5f);
    if (x0 == x1) {
        put(x0, y, (cov_of(xb - xa) * cy) >> 8);
        return;
    }
    put(x0, y, (cov_of((f32)x0 + 0.5f - xa) * cy) >> 8);
    for (x = x0 + 1; x < x1; x++)
        put(x, y, cy);
    put(x1, y, (cov_of(xb - ((f32)x1 - 0.5f)) * cy) >> 8);
}

void g_rect(f32 x, f32 y, f32 w, f32 h, f32 a)
{
    f32 ya, yb;
    int y0, y1, yy;
    if (w <= 0.0f || h <= 0.0f)
        return;
    set_level(a);
    x += gox; y += goy;
    ya = y - 0.5f; yb = y + h - 0.5f;          /* x,y name the top-left pixel's centre edge */
    x -= 0.5f;
    if (ya < -0.5f) ya = -0.5f;
    if (yb > (f32)FB_H - 0.5f) yb = (f32)FB_H - 0.5f;
    if (yb <= ya)
        return;
    y0 = ifloor(ya + 0.5f);
    y1 = ifloor(yb + 0.5f);
    if (y0 == y1) {
        hspan(y0, x, x + w, cov_of(yb - ya));
        return;
    }
    hspan(y0, x, x + w, cov_of((f32)y0 + 0.5f - ya));
    for (yy = y0 + 1; yy < y1; yy++)
        hspan(yy, x, x + w, 256);
    hspan(y1, x, x + w, cov_of(yb - ((f32)y1 - 0.5f)));
}

void g_disc(f32 cx, f32 cy, f32 r, f32 a)
{
    int y0, y1, y, x, xa, xb;
    f32 ro, ri2, ro2;
    if (r <= 0.0f)
        return;
    set_level(a);
    cx += gox; cy += goy;
    ro = r + 0.5f;
    ro2 = ro * ro;
    ri2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0.0f;
    y0 = imax(0, ifloor(cy - ro));
    y1 = imin(FB_H - 1, ifloor(cy + ro) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, dy2 = dy * dy, hw;
        if (dy2 >= ro2)
            continue;
        hw = sqrtfx(ro2 - dy2);
        xa = imax(0, ifloor(cx - hw));
        xb = imin(FB_W - 1, ifloor(cx + hw) + 1);
        for (x = xa; x <= xb; x++) {
            f32 dx = (f32)x - cx, d2 = dx * dx + dy2;
            if (d2 <= ri2)
                put(x, y, 256);
            else if (d2 < ro2)
                put(x, y, cov_of(ro - sqrtfx(d2)));
        }
    }
}

void g_ring(f32 cx, f32 cy, f32 r, f32 w, f32 a)
{
    int y0, y1, y, x;
    f32 h = w * 0.5f + 0.5f, ro, ri, ro2, ri2;
    if (r <= 0.0f || w <= 0.0f)
        return;
    if (r <= w * 0.5f) {
        g_disc(cx, cy, r + w * 0.5f, a);
        return;
    }
    set_level(a);
    cx += gox; cy += goy;
    ro = r + h;
    ri = r - h;
    ro2 = ro * ro;
    ri2 = ri > 0.0f ? ri * ri : 0.0f;
    y0 = imax(0, ifloor(cy - ro));
    y1 = imin(FB_H - 1, ifloor(cy + ro) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, dy2 = dy * dy, hwo, hwi;
        int xa, xb, xc, xd;
        if (dy2 >= ro2)
            continue;
        hwo = sqrtfx(ro2 - dy2);
        hwi = dy2 < ri2 ? sqrtfx(ri2 - dy2) : 0.0f;
        xa = imax(0, ifloor(cx - hwo));
        xb = imin(FB_W - 1, ifloor(cx - hwi) + 1);
        xc = imax(0, ifloor(cx + hwi));
        xd = imin(FB_W - 1, ifloor(cx + hwo) + 1);
        if (xc <= xb) {                        /* the two arcs meet: one run */
            xb = xd;
            xc = xd + 1;
        }
        for (x = xa; x <= xb; x++) {
            f32 dx = (f32)x - cx, d = sqrtfx(dx * dx + dy2);
            put(x, y, cov_of(h - absf(d - r)));
        }
        for (x = xc; x <= xd; x++) {
            f32 dx = (f32)x - cx, d = sqrtfx(dx * dx + dy2);
            put(x, y, cov_of(h - absf(d - r)));
        }
    }
}

void g_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, f32 w, f32 a)
{
    int y0, y1, x0, x1, x, y;
    f32 pad, irx2, iry2, h;
    if (rx <= 0.05f || ry <= 0.05f) {
        if (rx > 0.05f)
            g_line(cx - rx, cy, cx + rx, cy, w > 0.0f ? w : 1.5f, a);
        else if (ry > 0.05f)
            g_line(cx, cy - ry, cx, cy + ry, w > 0.0f ? w : 1.5f, a);
        return;
    }
    set_level(a);
    cx += gox; cy += goy;
    h = w > 0.0f ? w * 0.5f : 0.0f;
    pad = h + 1.0f;
    irx2 = 1.0f / (rx * rx);
    iry2 = 1.0f / (ry * ry);
    y0 = imax(0, ifloor(cy - ry - pad));
    y1 = imin(FB_H - 1, ifloor(cy + ry + pad) + 1);
    x0 = imax(0, ifloor(cx - rx - pad));
    x1 = imin(FB_W - 1, ifloor(cx + rx + pad) + 1);
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy;
        for (x = x0; x <= x1; x++) {
            f32 dx = (f32)x - cx;
            f32 f2 = dx * dx * irx2 + dy * dy * iry2, f, g2, d;
            if (w <= 0.0f && f2 < 0.5f) {      /* well inside a filled one */
                put(x, y, 256);
                continue;
            }
            if (f2 < 1e-6f)
                continue;
            f = sqrtfx(f2);
            g2 = dx * dx * irx2 * irx2 + dy * dy * iry2 * iry2;
            d = (f - 1.0f) * f * rsqrtf(g2);   /* signed distance to the edge, first order */
            if (w <= 0.0f)
                put(x, y, cov_of(0.5f - d));
            else
                put(x, y, cov_of(h + 0.5f - absf(d)));
        }
    }
}

void g_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a)
{
    f32 dx, dy, len2, inv, len, ux, uy, R, reach;
    int i, ia, ib, j, ja, jb;
    x0 += gox; x1 += gox; y0 += goy; y1 += goy;
    dx = x1 - x0;
    dy = y1 - y0;
    len2 = dx * dx + dy * dy;
    if (len2 < 1e-4f) {
        g_disc(x0 - gox, y0 - goy, w * 0.5f, a);
        return;
    }
    set_level(a);
    inv = rsqrtf(len2);
    len = len2 * inv;
    ux = dx * inv;
    uy = dy * inv;
    R = w * 0.5f + 0.5f;
    if (absf(dx) >= absf(dy)) {
        f32 slope;
        if (x0 > x1) {
            f32 t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            ux = -ux; uy = -uy;
        }
        slope = (y1 - y0) / (x1 - x0);
        reach = R / absf(ux) + 1.0f;
        ia = imax(0, ifloor(x0 - R));
        ib = imin(FB_W - 1, ifloor(x1 + R) + 1);
        for (i = ia; i <= ib; i++) {
            f32 xc = clampf((f32)i, x0, x1);
            f32 yc = y0 + (xc - x0) * slope;
            ja = imax(0, ifloor(yc - reach));
            jb = imin(FB_H - 1, ifloor(yc + reach) + 1);
            for (j = ja; j <= jb; j++) {
                f32 px = (f32)i - x0, py = (f32)j - y0, t = px * ux + py * uy, d;
                if (t < 0.0f)
                    d = sqrtfx(px * px + py * py);
                else if (t > len) {
                    px = (f32)i - x1; py = (f32)j - y1;
                    d = sqrtfx(px * px + py * py);
                } else
                    d = absf(px * uy - py * ux);
                put(i, j, cov_of(R - d));
            }
        }
    } else {
        f32 slope;
        if (y0 > y1) {
            f32 t = x0; x0 = x1; x1 = t;
            t = y0; y0 = y1; y1 = t;
            ux = -ux; uy = -uy;
        }
        slope = (x1 - x0) / (y1 - y0);
        reach = R / absf(uy) + 1.0f;
        ja = imax(0, ifloor(y0 - R));
        jb = imin(FB_H - 1, ifloor(y1 + R) + 1);
        for (j = ja; j <= jb; j++) {
            f32 yc = clampf((f32)j, y0, y1);
            f32 xc = x0 + (yc - y0) * slope;
            ia = imax(0, ifloor(xc - reach));
            ib = imin(FB_W - 1, ifloor(xc + reach) + 1);
            for (i = ia; i <= ib; i++) {
                f32 px = (f32)i - x0, py = (f32)j - y0, t = px * ux + py * uy, d;
                if (t < 0.0f)
                    d = sqrtfx(px * px + py * py);
                else if (t > len) {
                    px = (f32)i - x1; py = (f32)j - y1;
                    d = sqrtfx(px * px + py * py);
                } else
                    d = absf(px * uy - py * ux);
                put(i, j, cov_of(R - d));
            }
        }
    }
}

void g_poly(const f32 *xy, int n, int closed, f32 w, f32 a)
{
    int i;
    for (i = 0; i + 1 < n; i++)
        g_line(xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], w, a);
    if (closed && n > 2)
        g_line(xy[(n - 1) * 2], xy[(n - 1) * 2 + 1], xy[0], xy[1], w, a);
}

void g_fill(const f32 *xy, int n, f32 a)
{
    f32 ex[8], ey[8], ec[8], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, area = 0.0f, sgn;
    int i, x, y, x0, x1, y0, y1;
    if (n < 3 || n > 8)
        return;
    set_level(a);
    for (i = 0; i < n; i++) {
        f32 ax = xy[2 * i] + gox, ay = xy[2 * i + 1] + goy;
        int j = i + 1 < n ? i + 1 : 0;
        f32 bx = xy[2 * j] + gox, by = xy[2 * j + 1] + goy, dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, inv;
        area += ax * by - bx * ay;
        minx = minf(minx, ax); maxx = maxf(maxx, ax);
        miny = minf(miny, ay); maxy = maxf(maxy, ay);
        inv = l2 > 1e-8f ? rsqrtf(l2) : 0.0f;
        ex[i] = -dy * inv;                       /* the edge's normal, and its offset */
        ey[i] = dx * inv;
        ec[i] = -(ex[i] * ax + ey[i] * ay);
    }
    sgn = area >= 0.0f ? 1.0f : -1.0f;
    x0 = imax(0, ifloor(minx - 1.0f));
    x1 = imin(FB_W - 1, ifloor(maxx + 2.0f));
    y0 = imax(0, ifloor(miny - 1.0f));
    y1 = imin(FB_H - 1, ifloor(maxy + 2.0f));
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            f32 d = 1e9f;
            for (i = 0; i < n && d > -0.5f; i++) {
                f32 e = sgn * (ex[i] * (f32)x + ey[i] * (f32)y + ec[i]);
                if (e < d)
                    d = e;
            }
            if (d > -0.5f)
                put(x, y, cov_of(d + 0.5f));
        }
}

void g_vspan(int x, f32 y0, f32 y1, f32 a)
{
    int ya, yb, y;
    x += (int)gox;
    y0 += goy; y1 += goy;
    if ((unsigned)x >= FB_W || y1 <= y0)
        return;
    set_level(a);
    y0 -= 0.5f; y1 -= 0.5f;                      /* to edges */
    if (y0 < -0.5f) y0 = -0.5f;
    if (y1 > (f32)FB_H - 0.5f) y1 = (f32)FB_H - 0.5f;
    if (y1 <= y0)
        return;
    ya = ifloor(y0 + 0.5f);
    yb = ifloor(y1 + 0.5f);
    if (ya == yb) {
        put(x, ya, cov_of(y1 - y0));
        return;
    }
    put(x, ya, cov_of((f32)ya + 0.5f - y0));
    for (y = ya + 1; y < yb; y++)
        put(x, y, 256);
    put(x, yb, cov_of(y1 - ((f32)yb - 0.5f)));
}

void g_arc(f32 cx, f32 cy, f32 r, f32 t0, f32 t1, f32 w, f32 a)
{
    f32 span = absf(t1 - t0);
    int n = iclamp((int)(span * r * 0.9f) + 2, 2, 96), i;
    f32 px = cx + r * cos_turns(t0), py = cy + r * sin_turns(t0);
    for (i = 1; i <= n; i++) {
        f32 t = t0 + (t1 - t0) * (f32)i / (f32)n;
        f32 qx = cx + r * cos_turns(t), qy = cy + r * sin_turns(t);
        g_line(px, py, qx, qy, w, a);
        px = qx; py = qy;
    }
}

/* ---- the stroke font ------------------------------------------------------------------
 * Glyphs live on a grid 8 wide and 12 tall (half units), top-left origin; every corner is a
 * chamfer, so the letters stay crisp at any size and can be drawn with one line routine. */
#define P(x, y) (uint8_t)(((x) << 4) | (y))
#define UP 0xFF
#define END 0xFE

static const uint8_t F_A[] = {P(0,12),P(0,4),P(4,0),P(8,4),P(8,12),UP,P(0,8),P(8,8),END};
static const uint8_t F_B[] = {P(0,0),P(6,0),P(8,2),P(8,4),P(6,6),P(0,6),UP,P(6,6),P(8,8),P(8,10),P(6,12),P(0,12),P(0,0),END};
static const uint8_t F_C[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),END};
static const uint8_t F_D[] = {P(0,0),P(5,0),P(8,3),P(8,9),P(5,12),P(0,12),P(0,0),END};
static const uint8_t F_E[] = {P(8,0),P(0,0),P(0,12),P(8,12),UP,P(0,6),P(6,6),END};
static const uint8_t F_F[] = {P(8,0),P(0,0),P(0,12),UP,P(0,6),P(6,6),END};
static const uint8_t F_G[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),P(8,6),P(4,6),END};
static const uint8_t F_H[] = {P(0,0),P(0,12),UP,P(8,0),P(8,12),UP,P(0,6),P(8,6),END};
static const uint8_t F_I[] = {P(4,0),P(4,12),UP,P(1,0),P(7,0),UP,P(1,12),P(7,12),END};
static const uint8_t F_J[] = {P(8,0),P(8,10),P(6,12),P(2,12),P(0,10),P(0,8),END};
static const uint8_t F_K[] = {P(0,0),P(0,12),UP,P(8,0),P(0,7),UP,P(2,5),P(8,12),END};
static const uint8_t F_L[] = {P(0,0),P(0,12),P(8,12),END};
static const uint8_t F_M[] = {P(0,12),P(0,0),P(4,6),P(8,0),P(8,12),END};
static const uint8_t F_N[] = {P(0,12),P(0,0),P(8,12),P(8,0),END};
static const uint8_t F_O[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),END};
static const uint8_t F_P[] = {P(0,12),P(0,0),P(6,0),P(8,2),P(8,5),P(6,7),P(0,7),END};
static const uint8_t F_Q[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),UP,P(5,9),P(8,12),END};
static const uint8_t F_R[] = {P(0,12),P(0,0),P(6,0),P(8,2),P(8,5),P(6,7),P(0,7),UP,P(4,7),P(8,12),END};
static const uint8_t F_S[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,4),P(2,6),P(6,6),P(8,8),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_T[] = {P(0,0),P(8,0),UP,P(4,0),P(4,12),END};
static const uint8_t F_U[] = {P(0,0),P(0,10),P(2,12),P(6,12),P(8,10),P(8,0),END};
static const uint8_t F_V[] = {P(0,0),P(4,12),P(8,0),END};
static const uint8_t F_W[] = {P(0,0),P(1,12),P(4,5),P(7,12),P(8,0),END};
static const uint8_t F_X[] = {P(0,0),P(8,12),UP,P(8,0),P(0,12),END};
static const uint8_t F_Y[] = {P(0,0),P(4,6),P(8,0),UP,P(4,6),P(4,12),END};
static const uint8_t F_Z[] = {P(0,0),P(8,0),P(0,12),P(8,12),END};
static const uint8_t F_0[] = {P(2,0),P(6,0),P(8,2),P(8,10),P(6,12),P(2,12),P(0,10),P(0,2),P(2,0),UP,P(6,3),P(2,9),END};
static const uint8_t F_1[] = {P(2,2),P(4,0),P(4,12),UP,P(1,12),P(7,12),END};
static const uint8_t F_2[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,5),P(0,12),P(8,12),END};
static const uint8_t F_3[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,4),P(6,6),P(3,6),UP,P(6,6),P(8,8),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_4[] = {P(6,12),P(6,0),P(0,8),P(8,8),END};
static const uint8_t F_5[] = {P(8,0),P(0,0),P(0,5),P(6,5),P(8,7),P(8,10),P(6,12),P(2,12),P(0,10),END};
static const uint8_t F_6[] = {P(8,2),P(6,0),P(2,0),P(0,2),P(0,10),P(2,12),P(6,12),P(8,10),P(8,8),P(6,6),P(0,6),END};
static const uint8_t F_7[] = {P(0,0),P(8,0),P(3,12),END};
static const uint8_t F_8[] = {P(2,0),P(6,0),P(8,2),P(8,4),P(6,6),P(2,6),P(0,4),P(0,2),P(2,0),UP,P(2,6),P(0,8),P(0,10),P(2,12),P(6,12),P(8,10),P(8,8),P(6,6),END};
static const uint8_t F_9[] = {P(0,10),P(2,12),P(6,12),P(8,10),P(8,2),P(6,0),P(2,0),P(0,2),P(0,4),P(2,6),P(8,6),END};
static const uint8_t F_MINUS[] = {P(1,6),P(7,6),END};
static const uint8_t F_PLUS[] = {P(1,6),P(7,6),UP,P(4,3),P(4,9),END};
static const uint8_t F_DOT[] = {P(4,11),P(4,12),END};
static const uint8_t F_SLASH[] = {P(8,0),P(0,12),END};
static const uint8_t F_COLON[] = {P(4,3),P(4,4),UP,P(4,9),P(4,10),END};
static const uint8_t F_PCT[] = {P(8,0),P(0,12),UP,P(0,0),P(2,0),P(2,2),P(0,2),P(0,0),UP,P(6,10),P(8,10),P(8,12),P(6,12),P(6,10),END};
static const uint8_t F_BANG[] = {P(4,0),P(4,8),UP,P(4,11),P(4,12),END};
static const uint8_t F_QM[] = {P(0,2),P(2,0),P(6,0),P(8,2),P(8,4),P(4,7),P(4,8),UP,P(4,11),P(4,12),END};
static const uint8_t F_LT[] = {P(7,2),P(1,6),P(7,10),END};
static const uint8_t F_GT[] = {P(1,2),P(7,6),P(1,10),END};
static const uint8_t F_USC[] = {P(0,12),P(8,12),END};
static const uint8_t F_EQ[] = {P(1,4),P(7,4),UP,P(1,8),P(7,8),END};
static const uint8_t F_HASH[] = {P(3,1),P(2,11),UP,P(6,1),P(5,11),UP,P(0,4),P(8,4),UP,P(0,8),P(8,8),END};
static const uint8_t F_NONE[] = {END};

static const uint8_t *glyph(char c)
{
    static const uint8_t *const AZ[26] = {F_A, F_B, F_C, F_D, F_E, F_F, F_G, F_H, F_I, F_J, F_K, F_L, F_M,
                                          F_N, F_O, F_P, F_Q, F_R, F_S, F_T, F_U, F_V, F_W, F_X, F_Y, F_Z};
    static const uint8_t *const D09[10] = {F_0, F_1, F_2, F_3, F_4, F_5, F_6, F_7, F_8, F_9};
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return AZ[c - 'A'];
    if (c >= '0' && c <= '9') return D09[c - '0'];
    switch (c) {
    case '-': return F_MINUS;
    case '+': return F_PLUS;
    case '.': return F_DOT;
    case '/': return F_SLASH;
    case ':': return F_COLON;
    case '%': return F_PCT;
    case '!': return F_BANG;
    case '?': return F_QM;
    case '<': return F_LT;
    case '>': return F_GT;
    case '_': return F_USC;
    case '=': return F_EQ;
    case '#': return F_HASH;
    default: return F_NONE;
    }
}

#define GLYPH_W (8.0f / 12.0f)       /* of the cap height */
#define GLYPH_GAP (3.4f / 12.0f)

static f32 advance_of(char c, f32 size)
{
    if (c == ' ') return size * 0.5f;
    if (c == '.' || c == ':' || c == '!') return size * (5.0f / 12.0f + GLYPH_GAP * 0.6f);
    if (c == '1' || c == 'I') return size * (7.0f / 12.0f + GLYPH_GAP);
    return size * (GLYPH_W + GLYPH_GAP);
}

f32 g_text_w(const char *s, f32 size)
{
    f32 w = 0.0f;
    if (!*s)
        return 0.0f;
    for (; *s; s++)
        w += advance_of(*s, size);
    return w - size * GLYPH_GAP;
}

f32 g_text(f32 x, f32 y, const char *s, f32 size, f32 w, f32 a, int align)
{
    f32 u = size / 12.0f;
    if (align == 1) x -= g_text_w(s, size) * 0.5f;
    else if (align == 2) x -= g_text_w(s, size);
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        f32 ox = x, lx = 0.0f, ly = 0.0f;
        int pen = 0;
        if (*s == '.' || *s == ':' || *s == '!') ox -= u * 1.5f;
        if (*s == '1' || *s == 'I') ox -= u * 0.5f;
        for (; *g != END; g++) {
            f32 px, py;
            if (*g == UP) {
                pen = 0;
                continue;
            }
            px = ox + (f32)(*g >> 4) * u;
            py = y + (f32)(*g & 15) * u;
            if (pen)
                g_line(lx, ly, px, py, w, a);
            lx = px; ly = py;
            pen = 1;
        }
        x += advance_of(*s, size);
    }
    return x;
}
