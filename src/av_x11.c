/*
 * av-x11: Standalone X11 image viewer (C99)
 * Dependencies: libX11, stb_image.h
 * Target: CentOS 6.x compatible (gcc -std=c99)
 *
 * Usage: av-x11 image.png [image2.png]
 *   Single image  → full window
 *   Two images    → side-by-side (50/50)
 *
 * Keys: q/Esc=quit, f=fit, +/-=zoom, hjkl=pan, Space=1:1, s=sync, Tab=swap panel
 * Mouse: drag=pan, scroll=zoom
 */

#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

/* ── Data structures ──────────────────────────────────────────────────────── */

typedef struct {
    unsigned char *pixels;  /* RGBA8 */
    int w, h;
    char path[512];
    int loaded;
} AvImage;

typedef struct {
    double zoom;
    double pan_x, pan_y;  /* image-space offset from center */
    int fit;              /* flag: fit to panel on next render */
} AvViewport;

typedef struct {
    AvImage     img[2];
    AvViewport  vp[2];
    int         num_images;
    int         sync;          /* viewport sync toggle */
    int         active_panel;  /* 0 or 1 */
    int         dirty;
    int         running;

    /* X11 */
    Display    *dpy;
    Window      win;
    GC          gc;
    XImage     *ximg;
    unsigned char *framebuf;   /* BGRA or matching Visual */
    int         win_w, win_h;
    int         bpp;           /* bytes per pixel (4) */

    /* pixel byte order from Visual masks */
    int         r_shift, g_shift, b_shift;

    /* drag state */
    int         dragging;
    int         drag_x, drag_y;
    int         drag_panel;

    /* mouse position for status bar */
    int         mouse_x, mouse_y;

    Atom        wm_delete;
} AvState;

/* ── Forward declarations ─────────────────────────────────────────────────── */

static void av_init_display(AvState *s, int w, int h);
static int  av_load_image(const char *path, AvImage *img);
static void av_render(AvState *s);
static void av_render_panel(AvState *s, int idx, int x0, int y0, int pw, int ph);
static void av_event_loop(AvState *s);
static void av_fit_viewport(AvViewport *vp, int img_w, int img_h, int pw, int ph);
static void av_resize_framebuf(AvState *s);
static void av_draw_statusbar(AvState *s);
static int  av_panel_at(AvState *s, int mx);
static void av_cleanup(AvState *s);

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static int bit_offset(unsigned long mask) {
    int shift = 0;
    if (mask == 0) return 0;
    while ((mask & 1) == 0) { mask >>= 1; shift++; }
    return shift;
}

static inline unsigned char clamp8(int v) {
    return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* ── Display init ─────────────────────────────────────────────────────────── */

static void av_init_display(AvState *s, int w, int h) {
    s->dpy = XOpenDisplay(NULL);
    if (!s->dpy) {
        fprintf(stderr, "[av-x11] Cannot open display\n");
        exit(1);
    }

    int screen = DefaultScreen(s->dpy);
    Visual *vis = DefaultVisual(s->dpy, screen);

    s->r_shift = bit_offset(vis->red_mask);
    s->g_shift = bit_offset(vis->green_mask);
    s->b_shift = bit_offset(vis->blue_mask);
    s->bpp = 4;

    s->win = XCreateSimpleWindow(s->dpy, RootWindow(s->dpy, screen),
                                  0, 0, (unsigned)w, (unsigned)h,
                                  0, 0, BlackPixel(s->dpy, screen));

    XStoreName(s->dpy, s->win, "av-x11");

    XSelectInput(s->dpy, s->win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);

    s->wm_delete = XInternAtom(s->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(s->dpy, s->win, &s->wm_delete, 1);

    s->gc = XCreateGC(s->dpy, s->win, 0, NULL);

    XMapWindow(s->dpy, s->win);
    XFlush(s->dpy);

    s->win_w = w;
    s->win_h = h;
    s->ximg = NULL;
    s->framebuf = NULL;
    av_resize_framebuf(s);
}

/* ── Framebuffer management ───────────────────────────────────────────────── */

static void av_resize_framebuf(AvState *s) {
    if (s->ximg) {
        /* XDestroyImage frees the data pointer, so NULL it first if we manage it */
        s->ximg->data = NULL;
        XDestroyImage(s->ximg);
        s->ximg = NULL;
    }
    free(s->framebuf);

    /* Clamp window dimensions before sizing the framebuffer. Window geometry is
       WM/attacker controlled (ConfigureNotify copies it verbatim); without this
       a huge window makes win_w*win_h*bpp overflow 32-bit int -> tiny/negative
       calloc -> out-of-bounds writes. Clamping here keeps win_w/win_h consistent
       with XCreateImage/XPutImage/hit-testing, and bounds area well under INT_MAX
       (16384*16384*4 ~= 1.07e9). Ordinary window sizes are unaffected. */
    if (s->win_w < 1)     s->win_w = 1;
    if (s->win_h < 1)     s->win_h = 1;
    if (s->win_w > 16384) s->win_w = 16384;
    if (s->win_h > 16384) s->win_h = 16384;

    size_t size = (size_t)s->win_w * (size_t)s->win_h * (size_t)s->bpp;
    s->framebuf = (unsigned char *)calloc(1, size);
    if (!s->framebuf) {
        fprintf(stderr, "[av-x11] framebuf alloc failed\n");
        exit(1);
    }

    int screen = DefaultScreen(s->dpy);
    s->ximg = XCreateImage(s->dpy, DefaultVisual(s->dpy, screen),
                            (unsigned)DefaultDepth(s->dpy, screen),
                            ZPixmap, 0, (char *)s->framebuf,
                            (unsigned)s->win_w, (unsigned)s->win_h,
                            32, s->win_w * s->bpp);
    if (!s->ximg) {
        fprintf(stderr, "[av-x11] XCreateImage failed\n");
        exit(1);
    }
}

/* ── Image loading ────────────────────────────────────────────────────────── */

static int av_load_image(const char *path, AvImage *img) {
    if (img->pixels) {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
    }

    int comp;
    img->pixels = stbi_load(path, &img->w, &img->h, &comp, 4);  /* force RGBA */
    if (!img->pixels) {
        fprintf(stderr, "[av-x11] Failed to load: %s (%s)\n", path, stbi_failure_reason());
        img->loaded = 0;
        return 0;
    }

    strncpy(img->path, path, sizeof(img->path) - 1);
    img->path[sizeof(img->path) - 1] = '\0';
    img->loaded = 1;
    return 1;
}

/* ── Viewport fit ─────────────────────────────────────────────────────────── */

static void av_fit_viewport(AvViewport *vp, int img_w, int img_h, int pw, int ph) {
    if (img_w <= 0 || img_h <= 0 || pw <= 0 || ph <= 0) return;

    double sx = (double)pw / (double)img_w;
    double sy = (double)ph / (double)img_h;
    vp->zoom = (sx < sy) ? sx : sy;
    vp->pan_x = 0.0;
    vp->pan_y = 0.0;
    vp->fit = 0;
}

/* ── Panel at mouse X ─────────────────────────────────────────────────────── */

static int av_panel_at(AvState *s, int mx) {
    if (s->num_images < 2) return 0;
    return (mx >= s->win_w / 2) ? 1 : 0;
}

/* ── Render one panel ─────────────────────────────────────────────────────── */

static void av_render_panel(AvState *s, int idx, int x0, int y0, int pw, int ph) {
    AvImage *img = &s->img[idx];
    AvViewport *vp = &s->vp[idx];

    if (!img->loaded) {
        /* Fill dark background */
        int r_shift = s->r_shift, g_shift = s->g_shift, b_shift = s->b_shift;
        unsigned int bg = ((unsigned int)30 << r_shift) |
                          ((unsigned int)30 << g_shift) |
                          ((unsigned int)30 << b_shift) |
                          (0xFFu << 24);
        for (int y = y0; y < y0 + ph && y < s->win_h; y++) {
            unsigned int *row = (unsigned int *)(s->framebuf + (size_t)y * s->win_w * s->bpp);
            for (int x = x0; x < x0 + pw && x < s->win_w; x++) {
                row[x] = bg;
            }
        }
        return;
    }

    if (vp->fit) {
        av_fit_viewport(vp, img->w, img->h, pw, ph);
    }

    double zoom = vp->zoom;
    /* Scaled image dimensions */
    double scaled_w = img->w * zoom;
    double scaled_h = img->h * zoom;
    /* Top-left corner in panel coordinates */
    double ox = (pw - scaled_w) * 0.5 + vp->pan_x;
    double oy = (ph - scaled_h) * 0.5 + vp->pan_y;

    int r_shift = s->r_shift, g_shift = s->g_shift, b_shift = s->b_shift;
    unsigned int bg_pix = ((unsigned int)25 << r_shift) |
                          ((unsigned int)25 << g_shift) |
                          ((unsigned int)25 << b_shift) |
                          (0xFFu << 24);

    int use_bilinear = (zoom < 1.0);

    for (int sy = 0; sy < ph && (y0 + sy) < s->win_h; sy++) {
        int wy = y0 + sy;
        if (wy < 0) continue;
        unsigned int *row = (unsigned int *)(s->framebuf + (size_t)wy * s->win_w * s->bpp);

        for (int sx = 0; sx < pw && (x0 + sx) < s->win_w; sx++) {
            int wx = x0 + sx;
            if (wx < 0) continue;

            /* Map screen pixel to image coordinate */
            double img_xf = (sx - ox) / zoom;
            double img_yf = (sy - oy) / zoom;

            if (img_xf < 0 || img_yf < 0 || img_xf >= img->w || img_yf >= img->h) {
                /* Checkerboard background for out-of-bounds */
                int cx = (sx / 16) & 1, cy = (sy / 16) & 1;
                int g = (cx ^ cy) ? 40 : 30;
                row[wx] = ((unsigned int)g << r_shift) |
                          ((unsigned int)g << g_shift) |
                          ((unsigned int)g << b_shift) |
                          (0xFFu << 24);
                continue;
            }

            unsigned char r, g, b, a;

            if (use_bilinear) {
                /* Bilinear interpolation */
                int ix = (int)img_xf;
                int iy = (int)img_yf;
                double fx = img_xf - ix;
                double fy = img_yf - iy;
                int ix1 = (ix + 1 < img->w) ? ix + 1 : ix;
                int iy1 = (iy + 1 < img->h) ? iy + 1 : iy;

                unsigned char *p00 = img->pixels + (iy  * img->w + ix ) * 4;
                unsigned char *p10 = img->pixels + (iy  * img->w + ix1) * 4;
                unsigned char *p01 = img->pixels + (iy1 * img->w + ix ) * 4;
                unsigned char *p11 = img->pixels + (iy1 * img->w + ix1) * 4;

                double w00 = (1.0 - fx) * (1.0 - fy);
                double w10 = fx * (1.0 - fy);
                double w01 = (1.0 - fx) * fy;
                double w11 = fx * fy;

                r = clamp8((int)(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11 + 0.5));
                g = clamp8((int)(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11 + 0.5));
                b = clamp8((int)(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11 + 0.5));
                a = clamp8((int)(p00[3]*w00 + p10[3]*w10 + p01[3]*w01 + p11[3]*w11 + 0.5));
            } else {
                /* Nearest neighbor */
                int ix = (int)img_xf;
                int iy = (int)img_yf;
                if (ix >= img->w) ix = img->w - 1;
                if (iy >= img->h) iy = img->h - 1;
                unsigned char *p = img->pixels + (iy * img->w + ix) * 4;
                r = p[0]; g = p[1]; b = p[2]; a = p[3];
            }

            /* Alpha blend over dark background */
            if (a < 255) {
                double af = a / 255.0;
                r = clamp8((int)(r * af + 25 * (1.0 - af)));
                g = clamp8((int)(g * af + 25 * (1.0 - af)));
                b = clamp8((int)(b * af + 25 * (1.0 - af)));
            }

            row[wx] = ((unsigned int)r << r_shift) |
                      ((unsigned int)g << g_shift) |
                      ((unsigned int)b << b_shift) |
                      (0xFFu << 24);
        }
    }

    (void)bg_pix;
}

/* ── Status bar ───────────────────────────────────────────────────────────── */

static void av_draw_statusbar(AvState *s) {
    char buf[512];
    int panel = av_panel_at(s, s->mouse_x);
    AvImage *img = &s->img[panel];
    AvViewport *vp = &s->vp[panel];

    if (!img->loaded) {
        snprintf(buf, sizeof(buf), "  [no image]");
    } else {
        /* Map mouse to image coords */
        int pw, x0;
        if (s->num_images == 2) {
            pw = s->win_w / 2;
            x0 = (panel == 0) ? 0 : pw;
        } else {
            pw = s->win_w;
            x0 = 0;
        }
        int ph = s->win_h;
        double scaled_w = img->w * vp->zoom;
        double scaled_h = img->h * vp->zoom;
        double ox = (pw - scaled_w) * 0.5 + vp->pan_x;
        double oy = (ph - scaled_h) * 0.5 + vp->pan_y;

        double img_xf = (s->mouse_x - x0 - ox) / vp->zoom;
        double img_yf = (s->mouse_y - oy) / vp->zoom;
        int ix = (int)img_xf;
        int iy = (int)img_yf;

        /* Extract filename from path */
        const char *fname = strrchr(img->path, '/');
        fname = fname ? fname + 1 : img->path;

        if (ix >= 0 && iy >= 0 && ix < img->w && iy < img->h) {
            unsigned char *p = img->pixels + (iy * img->w + ix) * 4;
            snprintf(buf, sizeof(buf), "  %s  %dx%d  zoom:%.0f%%  [%d,%d]  RGBA=(%d,%d,%d,%d)%s",
                     fname, img->w, img->h, vp->zoom * 100.0,
                     ix, iy, p[0], p[1], p[2], p[3],
                     s->sync ? "  [SYNC]" : "");
        } else {
            snprintf(buf, sizeof(buf), "  %s  %dx%d  zoom:%.0f%%%s",
                     fname, img->w, img->h, vp->zoom * 100.0,
                     s->sync ? "  [SYNC]" : "");
        }
    }

    /* Draw status bar background */
    int bar_h = 20;
    int bar_y = s->win_h - bar_h;
    unsigned int bar_bg = ((unsigned int)40 << s->r_shift) |
                          ((unsigned int)40 << s->g_shift) |
                          ((unsigned int)45 << s->b_shift) |
                          (0xFFu << 24);
    for (int y = bar_y; y < s->win_h; y++) {
        unsigned int *row = (unsigned int *)(s->framebuf + (size_t)y * s->win_w * s->bpp);
        for (int x = 0; x < s->win_w; x++) {
            row[x] = bar_bg;
        }
    }

    /* XPutImage first, then draw text on top */
    XPutImage(s->dpy, s->win, s->gc, s->ximg, 0, 0, 0, 0,
              (unsigned)s->win_w, (unsigned)s->win_h);

    XSetForeground(s->dpy, s->gc, WhitePixel(s->dpy, DefaultScreen(s->dpy)));
    XDrawString(s->dpy, s->win, s->gc, 4, s->win_h - 5,
                buf, (int)strlen(buf));
}

/* ── Full render ──────────────────────────────────────────────────────────── */

static void av_render(AvState *s) {
    if (s->num_images == 2) {
        int pw = s->win_w / 2;
        int ph = s->win_h;
        av_render_panel(s, 0, 0, 0, pw, ph);
        av_render_panel(s, 1, pw, 0, s->win_w - pw, ph);

        /* 1px divider */
        int div_x = pw;
        if (div_x >= 0 && div_x < s->win_w) {
            unsigned int div_col = ((unsigned int)80 << s->r_shift) |
                                   ((unsigned int)80 << s->g_shift) |
                                   ((unsigned int)80 << s->b_shift) |
                                   (0xFFu << 24);
            for (int y = 0; y < s->win_h; y++) {
                unsigned int *row = (unsigned int *)(s->framebuf + (size_t)y * s->win_w * s->bpp);
                row[div_x] = div_col;
            }
        }
    } else {
        av_render_panel(s, 0, 0, 0, s->win_w, s->win_h);
    }

    av_draw_statusbar(s);
    s->dirty = 0;
}

/* ── Event loop ───────────────────────────────────────────────────────────── */

static void av_event_loop(AvState *s) {
    XEvent ev;
    s->running = 1;
    s->dirty = 1;

    while (s->running) {
        if (s->dirty) {
            av_render(s);
        }

        /* Block on next event if not dirty, else check pending */
        if (XPending(s->dpy) == 0 && !s->dirty) {
            /* Block until event */
            XNextEvent(s->dpy, &ev);
        } else if (XPending(s->dpy) > 0) {
            XNextEvent(s->dpy, &ev);
        } else {
            continue;
        }

        switch (ev.type) {
        case Expose:
            s->dirty = 1;
            break;

        case ConfigureNotify:
            if (ev.xconfigure.width != s->win_w || ev.xconfigure.height != s->win_h) {
                s->win_w = ev.xconfigure.width;
                s->win_h = ev.xconfigure.height;
                av_resize_framebuf(s);
                s->dirty = 1;
            }
            break;

        case KeyPress: {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            int panel = s->active_panel;
            AvViewport *vp = &s->vp[panel];
            double pan_step = 50.0;

            switch (ks) {
            case XK_q: case XK_Escape:
                s->running = 0;
                break;

            case XK_f:
                s->vp[panel].fit = 1;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].fit = 1;
                s->dirty = 1;
                break;

            case XK_plus: case XK_equal:
                vp->zoom *= 1.25;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].zoom = vp->zoom;
                s->dirty = 1;
                break;

            case XK_minus:
                vp->zoom /= 1.25;
                if (vp->zoom < 0.01) vp->zoom = 0.01;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].zoom = vp->zoom;
                s->dirty = 1;
                break;

            case XK_h:
                vp->pan_x += pan_step;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].pan_x = vp->pan_x;
                s->dirty = 1;
                break;

            case XK_l:
                vp->pan_x -= pan_step;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].pan_x = vp->pan_x;
                s->dirty = 1;
                break;

            case XK_k:
                vp->pan_y += pan_step;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].pan_y = vp->pan_y;
                s->dirty = 1;
                break;

            case XK_j:
                vp->pan_y -= pan_step;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].pan_y = vp->pan_y;
                s->dirty = 1;
                break;

            case XK_space:
                vp->zoom = 1.0;
                vp->pan_x = 0.0;
                vp->pan_y = 0.0;
                if (s->sync && s->num_images == 2) {
                    s->vp[1 - panel].zoom = 1.0;
                    s->vp[1 - panel].pan_x = 0.0;
                    s->vp[1 - panel].pan_y = 0.0;
                }
                s->dirty = 1;
                break;

            case XK_s:
                s->sync = !s->sync;
                if (s->sync && s->num_images == 2) {
                    /* Copy active viewport to the other */
                    s->vp[1 - panel] = *vp;
                }
                s->dirty = 1;
                break;

            case XK_Tab:
                if (s->num_images == 2) {
                    s->active_panel = 1 - s->active_panel;
                    s->dirty = 1;
                }
                break;

            default:
                break;
            }
            break;
        }

        case ButtonPress: {
            int panel = av_panel_at(s, ev.xbutton.x);
            if (ev.xbutton.button == Button1) {
                s->dragging = 1;
                s->drag_x = ev.xbutton.x;
                s->drag_y = ev.xbutton.y;
                s->drag_panel = panel;
            } else if (ev.xbutton.button == Button4) {
                /* Scroll up → zoom in */
                AvViewport *vp = &s->vp[panel];
                vp->zoom *= 1.1;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].zoom = vp->zoom;
                s->dirty = 1;
            } else if (ev.xbutton.button == Button5) {
                /* Scroll down → zoom out */
                AvViewport *vp = &s->vp[panel];
                vp->zoom /= 1.1;
                if (vp->zoom < 0.01) vp->zoom = 0.01;
                if (s->sync && s->num_images == 2)
                    s->vp[1 - panel].zoom = vp->zoom;
                s->dirty = 1;
            }
            break;
        }

        case ButtonRelease:
            if (ev.xbutton.button == Button1) {
                s->dragging = 0;
            }
            break;

        case MotionNotify: {
            s->mouse_x = ev.xmotion.x;
            s->mouse_y = ev.xmotion.y;

            if (s->dragging) {
                int dx = ev.xmotion.x - s->drag_x;
                int dy = ev.xmotion.y - s->drag_y;
                int panel = s->drag_panel;
                s->vp[panel].pan_x += dx;
                s->vp[panel].pan_y += dy;
                s->vp[panel].fit = 0;
                if (s->sync && s->num_images == 2) {
                    s->vp[1 - panel].pan_x = s->vp[panel].pan_x;
                    s->vp[1 - panel].pan_y = s->vp[panel].pan_y;
                }
                s->drag_x = ev.xmotion.x;
                s->drag_y = ev.xmotion.y;
            }
            s->dirty = 1;
            break;
        }

        case ClientMessage:
            if ((Atom)ev.xclient.data.l[0] == s->wm_delete) {
                s->running = 0;
            }
            break;

        default:
            break;
        }
    }
}

/* ── Cleanup ──────────────────────────────────────────────────────────────── */

static void av_cleanup(AvState *s) {
    for (int i = 0; i < 2; i++) {
        if (s->img[i].pixels) {
            stbi_image_free(s->img[i].pixels);
            s->img[i].pixels = NULL;
        }
    }
    if (s->ximg) {
        s->ximg->data = NULL;  /* we manage framebuf separately */
        XDestroyImage(s->ximg);
    }
    free(s->framebuf);
    if (s->gc)  XFreeGC(s->dpy, s->gc);
    if (s->win) XDestroyWindow(s->dpy, s->win);
    if (s->dpy) XCloseDisplay(s->dpy);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: av-x11 <image> [image2]\n");
        fprintf(stderr, "Keys: q/Esc=quit  f=fit  +/-=zoom  hjkl=pan  Space=1:1  s=sync  Tab=panel\n");
        return 1;
    }

    AvState s;
    memset(&s, 0, sizeof(s));
    s.vp[0].zoom = 1.0;
    s.vp[1].zoom = 1.0;
    s.vp[0].fit = 1;
    s.vp[1].fit = 1;

    /* Load images */
    if (!av_load_image(argv[1], &s.img[0])) {
        return 1;
    }
    s.num_images = 1;

    if (argc >= 3) {
        if (av_load_image(argv[2], &s.img[1])) {
            s.num_images = 2;
        }
    }

    /* Determine initial window size */
    int init_w = 1024, init_h = 768;
    if (s.num_images == 1 && s.img[0].loaded) {
        init_w = s.img[0].w;
        init_h = s.img[0].h;
        if (init_w > 1920) { init_w = 1920; }
        if (init_h > 1080) { init_h = 1080; }
        if (init_w < 400)  { init_w = 400; }
        if (init_h < 300)  { init_h = 300; }
    } else if (s.num_images == 2) {
        init_w = 1400;
        init_h = 800;
    }

    av_init_display(&s, init_w, init_h);

    printf("[av-x11] %d image(s) loaded\n", s.num_images);
    for (int i = 0; i < s.num_images; i++) {
        printf("  [%d] %s (%dx%d)\n", i, s.img[i].path, s.img[i].w, s.img[i].h);
    }

    av_event_loop(&s);
    av_cleanup(&s);

    return 0;
}
