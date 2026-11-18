/* H1 - graphics: an 8-bit intensity framebuffer (0 = the bezel's black, 255 = full ink) and
 * anti-aliased vector primitives. One ink colour per instrument is applied by a palette when
 * the frame is presented, so the art is drawn once, in shades.
 *
 * Pixel centres are at integer coordinates. Everything clips to the screen, and shapes that run
 * off the edge are fine: the black around the panel is part of the picture. */
#pragma once
#include "mathx.h"

#define FB_W 240
#define FB_H 240

enum { G_MAX, G_ADD, G_PAINT };   /* MAX: line art (crossings don't blow out); PAINT: alpha over (a = 0 erases) */

extern uint8_t g_fb[FB_W * FB_H];

void g_clear(void);
void g_fade(int mul256);                                   /* every pixel * mul/256: trails */
void g_mode(int mode);
void g_dim(f32 k);                                         /* global multiplier on every later alpha (0..1) */
void g_origin(f32 ox, f32 oy);                             /* added to every coordinate */

void g_line(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a); /* round caps; w = stroke width */
void g_disc(f32 cx, f32 cy, f32 r, f32 a);
void g_ring(f32 cx, f32 cy, f32 r, f32 w, f32 a);
void g_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, f32 w, f32 a);   /* w <= 0: filled */
void g_rect(f32 x, f32 y, f32 w, f32 h, f32 a);            /* filled, x,y = top-left edge */
void g_poly(const f32 *xy, int n, int closed, f32 w, f32 a);
void g_fill(const f32 *xy, int n, f32 a);                  /* a filled convex polygon, either winding */
void g_vspan(int x, f32 y0, f32 y1, f32 a);                /* one column of pixels from y0 to y1 (edges), soft ends */
void g_arc(f32 cx, f32 cy, f32 r, f32 t0, f32 t1, f32 w, f32 a);   /* angles in turns, 0 = east, clockwise on screen */

/* stroke font: capitals, digits, a few signs. size = cap height in pixels. */
f32 g_text_w(const char *s, f32 size);
f32 g_text(f32 x, f32 y, const char *s, f32 size, f32 w, f32 a, int align);   /* y = top of caps; align 0 left, 1 centre, 2 right */
