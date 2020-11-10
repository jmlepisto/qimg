/**
 ** This file is part of the qimg project.
 ** Copyright 2020 Joni Lepistö <joni.m.lepisto@gmail.com>.
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **
 ** ----------------------------------------------------------------------------
 **
 ** Qimg - Quick Image Display
 **
 ** Qimg provides a totally stripped and straightforward way of displaying
 ** images on a Linux system. No desktop environment or windowing system needed!
 **
 ** Qimg uses the Linux framebuffer interface to draw bitmap data on the screen.
 ** Images are drawn as raw pixels with no windowing context whatsoever.
 **
 ** Why? Mostly for fun but I've had a few occasions on some terminal-only
 ** systems where it would've been nice to inspect images.
 **/


#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fcntl.h>
#include <glob.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define FB_IDX_MAX_SIZE 4
#define FB_DEV_BASE "/dev/fb"
#define FB_CLASS_BASE "/sys/class/graphics/fb"
#define FB_CLASS_RESOLUTION "/virtual_size"
#define FB_GLOB "/sys/class/graphics/fb[0-9]"

/* Standard terminal control sequences */
#define CUR_SHOW "\e[?25h"
#define CUR_HIDE "\e[?25l"

#define log_error(M) fprintf(stderr, "[ERROR]: %s\n", M)
#define assertf(A, M) if (!(A)) {log_error(M); exit(EXIT_FAILURE);} void f(void)

typedef enum { false, true } bool;

typedef struct res_ {
    int x;
    int y;
} qimg_resolution;

typedef struct col_ {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} qimg_color;

typedef struct fb_ {
    qimg_resolution res;/* framebuffer resolution */
    unsigned int size;  /* framebuffer size */
    int fbfd;           /* framebuffer file descriptor */
    char* fbdata;       /* framebuffer data pointer */
} qimg_fb;

typedef struct im_ {
    qimg_resolution res;/* resolution */
    int c;              /* channels */
    char _padding[4];   /* guess what */
    uint8_t* pixels;    /* image data pointer */
} qimq_image;

typedef enum pos_ {
    POS_CENTERED,
    POS_TOP_LEFT,
    POS_TOP_RIGHT,
    POS_BOTTOM_RIGHT,
    POS_BOTTOM_LEFT
} qimg_position;

static volatile bool run = true; /* used to go through cleanup on exit */

qimg_color qimg_get_pixel_color(qimq_image* im, int x, int y);
qimq_image qimg_load_image(char* input_path);
qimg_fb qimg_open_framebuffer(int idx);
void qimg_free_framebuffer(qimg_fb* fb);
void qimg_free_image(qimq_image* im);
void qimg_draw_image(qimq_image* im, qimg_fb* fb, qimg_position pos, bool repaint);

int get_default_framebuffer_idx(void);
void set_cursor_visibility(bool blink);
void interrupt_handler(int);
void print_help(void);

int get_default_framebuffer_idx() {
    /* Glob search for framebuffer devices */
    glob_t globbuf;
    glob(FB_GLOB, 0, NULL, &globbuf);

    /* At least one result matched */
    assertf(globbuf.gl_pathc > 0, "No framebuffers found");

    /* Get framebuffer index */
    strsep(&globbuf.gl_pathv[0], "fb"); /* Discard the first split */
    return atoi(strsep(&globbuf.gl_pathv[0], "fb"));
}

void qimg_free_framebuffer(qimg_fb* fb) {
    munmap(fb->fbdata, fb->size);
    close(fb->fbfd);
}

qimg_fb qimg_open_framebuffer(int idx) {
    /* Append device index to the framebuffer device path */
    char idx_buf[FB_IDX_MAX_SIZE];
    assertf(snprintf(idx_buf, FB_IDX_MAX_SIZE, "%d", idx) <
            FB_IDX_MAX_SIZE, "Framebuffer index overflow");
    char dev[FB_IDX_MAX_SIZE + sizeof(FB_DEV_BASE)] = FB_DEV_BASE;
    strncat(dev, idx_buf, FB_IDX_MAX_SIZE);

    /* Open framebuffer */
    qimg_fb fb;
    fb.fbfd = open(dev, O_RDWR);
    assertf(fb.fbfd >= 0, "Framebuffer device fopen() failed");

    /* Get framebuffer information */
    struct fb_var_screeninfo vinfo;
    ioctl(fb.fbfd, FBIOGET_VSCREENINFO, &vinfo);

    fb.res.x = (int) vinfo.xres;
    fb.res.y = (int) vinfo.yres;
    unsigned int fb_bpp = vinfo.bits_per_pixel;
    unsigned int fb_bytes = fb_bpp / 8;

    /* Calculate data size and map framebuffer to memory */
    fb.size = fb.res.x * fb.res.y * fb_bytes;
    fb.fbdata = mmap(0, fb.size,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fb.fbfd, (off_t) 0);

    return fb;
}

void qimg_draw_image(qimq_image* im, qimg_fb* fb, qimg_position pos,
                     bool repaint) {
    char* buf = malloc(fb->size);
    qimg_color c;
    int offs;
    int x, y;
    for (int x_ = 0; x_ < fb->res.x; ++x_) { /* This is embarassingly parallel */
        for (int y_ = 0; y_ < fb->res.y; ++y_) {
            switch (pos) {
            case POS_TOP_LEFT:
                x = x_;
                y = y_;
                break;
            case POS_TOP_RIGHT:
                x = x_ - (fb->res.x - im->res.x);
                y = y_;
                break;
            case POS_BOTTOM_RIGHT:
                x = x_ - (fb->res.x - im->res.x);
                y = y_ - (fb->res.y - im->res.y);
                break;
            case POS_BOTTOM_LEFT:
                x = x_;
                y = y_ - (fb->res.y - im->res.y);
                break;
            case POS_CENTERED:
                x = x_ - ((fb->res.x / 2) - (im->res.x / 2));
                y = y_ - ((fb->res.y / 2) - (im->res.y / 2));
                break;
            }

            if (x > im->res.x || y > im->res.y || x < 0 || y < 0)
                continue;

            c = qimg_get_pixel_color(im, x, y);
            offs = (y_ * fb->res.x + x_) * 4;
            buf[offs + 0] = (char) c.b;
            buf[offs + 1] = (char) c.g;
            buf[offs + 2] = (char) c.r;
            buf[offs + 3] = (char) c.a;
        }
    }

    do {
        memcpy(fb->fbdata, buf, fb->size);
    } while (repaint && run);
}

qimg_color qimg_get_pixel_color(qimq_image* im, int x, int y) {
    assertf(x <= im->res.x && y <= im->res.y, "Image coordinates out of bounds");
    uint8_t* offset = im->pixels + (y * im->res.x + x) * im->c;
    qimg_color color;

    if (im->c < 3) {
        color.r = offset[0];
        color.g = offset[0];
        color.b = offset[0];
        color.a = im->c >= 2 ? offset[1] : 0xff;
    } else {
        color.r = offset[0];
        color.g = offset[1];
        color.b = offset[2];
        color.a = im->c >= 4 ? offset[3] : 0xff;
    }
    return color;
}

qimq_image qimg_load_image(char* input_path) {
    qimq_image im;
    im.pixels = stbi_load(input_path, &im.res.x, &im.res.y, &im.c, 0);
    assertf(im.pixels, "Image loading failed");
    return im;
}

void qimg_free_image(qimq_image* im) {
    if (im->pixels)
    free(im->pixels);
}

void set_cursor_visibility(bool visible) {
    if (visible) printf(CUR_SHOW);
    else printf(CUR_HIDE);
    fflush(stdout);
}

void interrupt_handler(int dummy) {
    run = false;
}

void print_help() {
    printf("QIMG - Quick Image Display\n"
           "\n"
           "Usage: qimg [OPTION]... INPUT...\n"
           "\n"
           "General options:\n"
           "-h,             Print this help.\n"
           "-b <index>,     Use framebuffer device with given index.\n"
           "                Default is to use one found with the lowest index.\n"
           "-c,             Hide terminal cursor.\n"
           "-r,             Keep repainting the image. If hiding the cursor\n"
           "                fails, this will certainly work for keeping the\n"
           "                image on top.\n"
           "\n"
           "Image layout:\n"
           "-pos <position> Draw the image in given position. Possible values:\n"
           "                0   -   centered\n"
           "                1   -   top left (default)\n"
           "                2   -   top right\n"
           "                3   -   bottom right\n"
           "                4   -   bottom left\n"
           "-bg <color>     Fill background with color. Possible values:\n"
           "                0   -   black (default)\n"
           "                1   -   white\n"
           "                2   -   red\n"
           "                3   -   green\n"
           "                4   -   blue\n");
}

void parse_arguments(int argc, char *argv[], int* fb_idx, char** input,
                     bool* refresh, bool* hide_cursor, qimg_position* pos) {
    assertf(argc > 1, "Arguments missing");
    int opts = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-b") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                int temp = atoi(argv[i]);
                *fb_idx = temp;
            }
        } else if (strcmp(argv[i], "-r") == 0) {
            ++opts;
            *refresh = true;
        } else if (strcmp(argv[i], "-c") == 0) {
            ++opts;
            *hide_cursor = true;
        } else if (strcmp(argv[i], "-pos") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                int temp = atoi(argv[i]);
                *pos = (qimg_position) temp;
            }
        }


        else if (strcmp(argv[i], "-h") == 0) {
            print_help();
            exit(0);
        }
    }

    if (++opts < argc)
        *input = argv[argc-1];
}

int main(int argc, char *argv[]) {
    int fb_idx = -1;
    char* input_path = NULL;
    bool repaint = false;
    bool hide_cursor = false;
    qimg_position pos = POS_TOP_LEFT;
    parse_arguments(argc, argv, &fb_idx, &input_path, &repaint, &hide_cursor,
                    &pos);

    assertf(input_path, "No input file");
    if (fb_idx == -1)
        fb_idx = get_default_framebuffer_idx();

    qimg_fb fb = qimg_open_framebuffer(fb_idx);
    qimq_image im = qimg_load_image(input_path);

    /* Setup exit hooks on signals */
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);

    /* Fasten your seatbelts */
    if (hide_cursor) set_cursor_visibility(false);
    qimg_draw_image(&im, &fb, pos, repaint);

    /* Pause to keep terminal cursor hidden */
    /* Pause will return when a signal is caught AND handled */
    if (!repaint && hide_cursor) pause();

    /* Cleanup */
    if (hide_cursor) set_cursor_visibility(true);
    qimg_free_image(&im);
    qimg_free_framebuffer(&fb);

	return 0;
}
