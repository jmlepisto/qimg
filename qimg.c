/** @file qimg.c
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
 ** **Qimg - Quick Image Display**
 **
 ** Qimg provides a totally stripped and straightforward way of displaying
 ** images on a Linux system. No desktop environment or windowing system needed!
 **
 ** Qimg uses the Linux framebuffer interface to draw bitmap data on the screen.
 ** Images are drawn as raw pixels with no windowing context whatsoever.
 **
 ** Why? Mostly for fun but I've had a few occasions on some terminal-only
 ** systems where it would've been nice to view images and I found most of the
 ** existing solutions too complex and heavyweight for such a simple task.
 **
 ** ----------------------------------------------------------------------------
 **
 ** **Examples:**
 **
 ** General examples for using qimg.
 **
 ** **Basic usage:**
 **
 **     qimg input.jpg
 **
 ** Paints the given image at native size on the default framebuffer device
 ** once, exiting right after drawing is done.
 **
 **     qimg -c input.jpg
 **
 ** Paints the given image at native size on the default framebuffer device,
 ** hides the terminal cursor and keeps running until user exit
 ** via `SIGINT` or `SIGTERM`.
 ** This effectively prevents the terminal cursor from refreshing on top of
 ** the image.
 **
 **     qimg -r input.jpg
 **
 ** Paints the given image at native size on the default framebuffer device
 ** and keeps repainting the image on each cycle to prevent anything else from
 ** refreshing on top of the image. Keeps running until user exit
 ** via `SIGINT` or `SIGTERM`.
 **
 **     qimg -d /dev/fb1 input.jpg
 **
 ** Paints the given image at native size on the given framebuffer
 ** device `/dev/fb1`.
 **
 **     qimg -b 2 input.jpg
 **
 ** Paints the given image at native size on the given framebuffer
 ** device with index 2, resulting in `/dev/fb2`.
 **
 ** **Image positioning, background and resizing:**
 **
 ** To set image positioning, use:
 **
 **     -pos <pos>
 **
 ** Where `<pos>` can be one of the following:
 **
 **
 ** Position        | ```<pos>```
 ** --------        | -----------
 ** Center          | `c`
 ** Top left        | `tl`
 ** Top right       | `tr`
 ** Bottom right    | `br`
 ** Bottom left     | `bl`
 **
 ** To set background color, use:
 **
 **     -bg <color>
 **
 ** Where `<color>` can be one of the following:
 **
 ** | `<color>` |
 ** |-----------|
 ** | black     |
 ** | white     |
 ** | red       |
 ** | green     |
 ** | blue      |
 ** | disabled  |
 **
 ** `disabled` will leave the framebuffer as-is, only affecting the areas where
 ** the images is painted.
 **
 ** To resize the image, use:
 **
 **     -scale <style>
 **
 ** Where `<style>` can be one of the following:
 **
 ** | `<style>` |
 ** |-----------|
 ** | fit       |
 ** | stretch   |
 ** | fill      |
 ** | disabled  |
 **
 ** Please see #qimg_scale_ for scale style definitions.
 **
 ** **Slideshows:**
 **
 ** To show multiple images as a slideshow, simply feed `qimg` with multiple
 ** inputs:
 **
 **     qimg -c -delay 2 input1.jpg input2.jpg input3.jpg
 **
 ** This example will load and show three images with 2 second delay between
 ** them. As with the previous examples `-c` will hide the cursor.
 ** Setting image positioning, scaling or background colors will affect every
 ** image in the slideshow.
 **
 ** To loop the slideshow indefinitely, pass:
 **
 **     qimg -loop
 **
 **
 **/

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize.h>

#include <fcntl.h>
#include <glob.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <linux/fb.h>

#define FB_IDX_MAX_SIZE 4
#define FB_DEV_BASE "/dev/fb"
#define FB_CLASS_BASE "/sys/class/graphics/fb"
#define FB_CLASS_RESOLUTION "/virtual_size"
#define FB_GLOB "/sys/class/graphics/fb[0-9]"

/** Standard terminal control sequence for showing cursor */
#define CUR_SHOW "\e[?25h"
/** Standard terminal control sequence for hiding cursor */
#define CUR_HIDE "\e[?25l"

/** Maximum number of images to load in the buffer at once */
#define MAX_BUFFER_SIZE 5
#define MAX_IMAGES 256

/** Prints a formatted message to stderr */
#define log_msg(fmt_, ...)\
    fprintf(stderr, (fmt_ "\n"), ##__VA_ARGS__)

/**
 * Checks a condition and prints an error message if it is not met before
 * exiting
 */
#define assertf(A, fmt_, ...)\
    if (!(A)) {log_msg("[ERROR]: " fmt_, ##__VA_ARGS__); exit(EXIT_FAILURE);}\
    void f(void) /* To enforce semicolon and prevent warnings */

/** Generates lookup functions for converting string arguments to enums */
#define STRING_TO_ENUM_(e) e str2##e(const char* str) {                         \
    int j;                                                                      \
    for (j = 0;  j < sizeof (e##_conversion) / sizeof (e##_conversion[0]);  ++j)\
        if (!strcmp (str, e##_conversion[j].str))                               \
            return e##_conversion[j].en;                                        \
    assertf(false, "Unknown option %s for " #e, str);                           \
}

typedef enum { false, true } bool;

/** Represents resolution with horizontal and vertical pixel counts */
typedef struct qimg_point {
    int x;  /**< horizontal resolution */
    int y;  /**< vertical resolution */
} qimg_point;

/** Represents an RGBA color */
typedef struct qimg_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} qimg_color;

/** Represents an opened frambuffer instance */
typedef struct qimg_fb {
    qimg_point res;            /**< framebuffer resolution */
    unsigned int size;              /**< framebuffer size */
    int fbfd;                       /**< framebuffer file descriptor */
    char* fbdata;                   /**< framebuffer data pointer */
} qimg_fb;

/** Represents a loaded image */
typedef struct qimg_image {
    qimg_point res;            /**< resolution */
    int c;                          /**< channels */
    char _padding[4];               /**< guess what */
    uint8_t* pixels;                /**< image data pointer */
} qimg_image;

/** Represents a collection of loaded images */
typedef struct qimg_collection {
    int idx;
    int size;                           /**< number of images */
    char _padding[8];                   /**< yeah */
    qimg_image images[MAX_BUFFER_SIZE]; /**< image array */
} qimg_collection;

/** A dynamic collection of images used to load unlimited amount of inputs.
 * Loads one #qimg_collection of #MAX_BUFFER_SIZE images per time and updates
 * it whenever all current images have been read (`n_consumed == col.size`).
 */
typedef struct qimg_dyn_collection {
    char** input_paths;     /**< input path vector */
    int size;               /**< number of inputs */
    int idx;                /**< current element index */
    int n_consumed;         /**< number of elements condumed since last load */
    char _padding[4];       /**< magic */
    qimg_collection col;    /**< current collection */
} qimg_dyn_collection;

/** Image position */
typedef enum qimg_position {
    POS_CENTERED,
    POS_TOP_LEFT,
    POS_TOP_RIGHT,
    POS_BOTTOM_RIGHT,
    POS_BOTTOM_LEFT
} qimg_position;

/** Framebuffer background color */
typedef enum qimg_bg {
    BG_BLACK,
    BG_WHITE,
    BG_RED,
    BG_GREEN,
    BG_BLUE,
    BG_DISABLED
} qimg_bg;

/** Image scale types */
typedef enum qimg_scale {
    SCALE_DISABLED, /**< no scaling applied */
    SCALE_FIT,      /**< image scaled to fit the screen,
                    aspect ratio maintained */
    SCALE_STRETCH,  /**< image stretched to fill the whole screen */
    SCALE_FILL      /**< image scaled to fill the whole screen,
                    aspect ratio maintained */
} qimg_scale;

/* Lookup tables to find enums with string arguments */
const static struct {
    qimg_position en;
    const char *str;
} qimg_position_conversion [] = {
    {POS_CENTERED, "c"},
    {POS_TOP_LEFT, "tl"},
    {POS_TOP_RIGHT, "tr"},
    {POS_BOTTOM_LEFT, "bl"},
    {POS_BOTTOM_RIGHT, "br"}
};

const static struct {
    qimg_bg en;
    const char *str;
} qimg_bg_conversion [] = {
    {BG_BLACK, "black"},
    {BG_WHITE, "white"},
    {BG_RED, "red"},
    {BG_GREEN, "green"},
    {BG_BLUE, "blue"},
    {BG_DISABLED, "disabled"}
};

const static struct {
    qimg_scale en;
    const char *str;
} qimg_scale_conversion [] = {
    {SCALE_DISABLED, "disabled"},
    {SCALE_FIT, "fit"},
    {SCALE_STRETCH, "stretch"},
    {SCALE_FILL, "fill"}
};

STRING_TO_ENUM_(qimg_position)
STRING_TO_ENUM_(qimg_bg)
STRING_TO_ENUM_(qimg_scale)

static volatile bool run = true; /* used to go through cleanup on exit */
static qimg_scale scale = SCALE_DISABLED;
static clock_t begin_clk;


/*----------------------------------------------------------------------------*/


/**
 * @brief Gets color value of a pixel at given position.
 *
 * If the image has two or less channels, R, G and B values will be the
 * same in the returned #qimg_color_ struct.
 *
 * @param im    input image
 * @param x     pos x
 * @param y     pos y
 * @return color value of the given point
 */
qimg_color qimg_get_pixel(qimg_image* im, int x, int y);

/**
 * @brief Gets color values for background color enumeration
 * @param bg    background color value
 * @return color value
 */
qimg_color qimg_get_bg_color(qimg_bg bg);

/**
 * @brief Loads image at given path
 * @param input_path    input path
 * @return loaded image, exits if loading errors
 */
qimg_image qimg_load_image(char* input_path);

/**
 * @brief Loads multiple images to a collection
 * @param input_paths   input path vector
 * @param n_inputs      number of inputs
 * @param offset        offset in input_paths vector, in case some paths
 * should be omitted.
 * @return collection of images
 */
qimg_collection qimg_load_collection(char** input_paths, int n_inputs,
                                     int offset);

/**
 * @brief Initializes a dynamic collection and loads first images to it.
 *
 * A dynamic collection loads up to #MAX_BUFFER_SIZE images per time when
 * needed. #qimg_get_next should be used to fetch images from a dynamic
 * collection.
 *
 * @param input_paths   input path vector
 * @param n_inputs      number of inputs
 * @return
 */
qimg_dyn_collection qimg_init_dyn_collection(char** input_paths, int n_inputs);

/**
 * @brief Get next image from a dynamic collection
 * @param col   dynamic collection
 * @return image pointer
 */
qimg_image* qimg_get_next(qimg_dyn_collection* col);

/**
 * @brief Resizes an image
 * @param im        image to resize
 * @param dest_res  target resolution
 * @return true if resizing succeeded, false if not
 */
bool qimg_resize_image(qimg_image* im, qimg_point dest_res);

/**
 * @brief Calculates target dimensions for image when viewed on a viewport of
 * specified size on given scale style.
 *
 * See #qimg_scale for different scale type definitions.
 *
 * @param src       source image
 * @param vp        viewport
 * @param scale     scale style
 * @return target dimensions
 */
qimg_point qimg_get_scaled_dims(qimg_point src, qimg_point vp,
                                     qimg_scale scale);

/**
 * @brief Opens framebuffer with given index
 * @param idx   framebuffer index (/dev/fb<idx>)
 * @return framebuffer instance
 */
qimg_fb qimg_open_fb(int idx);

/**
 * @brief Opens framebuffer from given path
 * @param path  framebuffer path
 * @return framebuffer instance
 */
qimg_fb qimg_open_fb_from_path(const char* path);


/**
 * @brief Gets milliseconds since program start
 * @return milliseconds from start
 */
uint32_t qimg_get_millis(void);

/**
 * @brief Checks if given milliseoncds have elapsed since timestamps
 * @param start     beginning timestamp
 * @param millis    interval to check
 * @return true if the given interval has elapsed since beginnind
 */
bool qimg_have_millis_elapsed(uint32_t start, uint32_t millis);

/**
 * @brief Sleeps for given amount of time
 * @param ms    sleep time in milliseconds
 */
void qimg_sleep_ms(uint32_t ms);

/**
 * @brief Fills the framebuffer with black
 * @param fb    target framebuffer
 */
void qimg_clear_framebuffer(qimg_fb* fb);

/**
 * @brief Frees and unmaps the framebuffer instance
 * @param fb    target framebuffer
 */
void qimg_free_framebuffer(qimg_fb* fb);

/**
 * @brief Frees all images in a collection
 * @param col   target collection
 */
void qimg_free_collection(qimg_collection* col);

/**
 * @brief Frees an image from memory
 * @param im    target image
 */
void qimg_free_image(qimg_image* im);

/**
 * @brief Draws a dynamic collection of images on the framebuffer
 *
 * Images are loaded in batches and resized right before drawing if needed.
 *
 * @param col       image collection
 * @param fb        target framebuffer
 * @param pos       image positioning
 * @param bg        background style
 * @param repaint   keep repainting the image
 * @param delay_s   delay between images
 * @param loop      loop the slideshow images indefinitely
 */
void qimg_draw_images(qimg_dyn_collection* col, qimg_fb* fb, qimg_position pos,
                      qimg_bg bg, bool repaint, int delay_s, bool loop);

/**
 * @brief Draws an image on the framebuffer
 *
 * If delay_s <= 0, it is not applied. In this case the image is drawn
 * indefinitely if repaint is set to true.
 *
 * @param im        image
 * @param fb        target framebuffer
 * @param pos       image positioning
 * @param bg        background style
 * @param repaint   keep repainting the image
 * @param delay_s   time to keep the image on the framebuffer.
 */
void qimg_draw_image(qimg_image* im, qimg_fb* fb, qimg_position pos, qimg_bg bg,
                     bool repaint, int delay_s);

/**
 * @brief Translates framebuffer coordinates to image coordinates based on given
 * image positioning.
 * @param pos   image position
 * @param im    image pointer
 * @param fb    framebuffer pointer
 * @param x     framebuffer coordinate x
 * @param y     framebuffer coordinate y
 * @return translated coordinates
 */
qimg_point qimg_translate_coords(qimg_position pos, qimg_image* im, qimg_fb* fb,
                                 int x, int y);

/**
 * @brief Draws a data buffer to the framebuffer with optional repainting and
 * delays.
 *
 * Note that data buffer must be at least the same size as the framebuffer.
 *
 * If delay_s <= 0, it is not applied. In this case the image is drawn
 * indefinitely if repaint is set to true. If delay_s > 0 and repaint is set to
 * false, the function will simply wait delay_s seconds before returning.
 *
 * @param fb        framebuffer
 * @param buf       data buffer
 * @param delay_s   time to keep the image on the framebuffer
 * @param repaint   keep repainting the image
 */
void qimg_draw_buffer(qimg_fb* fb, char* buf, int delay_s, bool repaint);

/**
 * @brief Searches for default framebuffer index.
 * Exits if no framebuffers found.
 * @return lowest index found on the system
 */
int get_default_framebuffer_idx(void);

/**
 * @brief Sets terminal cursor visibility
 * @param visible   cursor visibility
 */
void set_cursor_visibility(bool visible);

/**
 * @brief Handles exit signals and stops processing to go through cleanups
 */
void interrupt_handler(int);

/**
 * @brief Prints usage help
 */
void print_help(void);


/*----------------------------------------------------------------------------*/


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

uint32_t qimg_get_millis(void) {
    return (uint32_t)((double)(clock() - begin_clk) / CLOCKS_PER_SEC) * 1000;
}

bool qimg_have_millis_elapsed(uint32_t start, uint32_t millis) {
    return (qimg_get_millis() - start) > millis;
}

void qimg_sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

qimg_fb qimg_open_fb(int idx) {
    /* Append device index to the framebuffer device path */
    char idx_buf[FB_IDX_MAX_SIZE];
    assertf(snprintf(idx_buf, FB_IDX_MAX_SIZE, "%d", idx) <
            FB_IDX_MAX_SIZE, "Framebuffer index overflow");
    char dev_path[FB_IDX_MAX_SIZE + sizeof(FB_DEV_BASE)] = FB_DEV_BASE;
    strncat(dev_path, idx_buf, FB_IDX_MAX_SIZE);

    return qimg_open_fb_from_path(dev_path);
}

qimg_fb qimg_open_fb_from_path(const char* path) {
    qimg_fb fb;
    fb.fbfd = open(path, O_RDWR);
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

void qimg_clear_framebuffer(qimg_fb* fb) {
    memset(fb->fbdata, 0, fb->size);
}

void qimg_draw_images(qimg_dyn_collection* dcol, qimg_fb* fb, qimg_position pos,
                      qimg_bg bg, bool repaint, int delay_s, bool loop) {
    int i = 0;
    while (i < dcol->size || (loop && dcol->size > 1)) {
        qimg_image* im = qimg_get_next(dcol);
        if (scale != SCALE_DISABLED) {
            qimg_point dest;
            dest = qimg_get_scaled_dims(im->res, fb->res, scale);
            qimg_resize_image(im, dest);
        }
        qimg_draw_image(im, fb, pos, bg, repaint, delay_s);
        ++i;
        if (!run) /* Draw routine exited via interrupt signal */
            break;
    }

}


qimg_point qimg_translate_coords(qimg_position pos, qimg_image* im, qimg_fb* fb,
                                 int x, int y) {
    qimg_point out;
    switch (pos) {
    case POS_TOP_LEFT:
        out.x = x;
        out.y = y;
        break;
    case POS_TOP_RIGHT:
        out.x = x - (fb->res.x - im->res.x);
        out.y = y;
        break;
    case POS_BOTTOM_RIGHT:
        out.x = x - (fb->res.x - im->res.x);
        out.y = y - (fb->res.y - im->res.y);
        break;
    case POS_BOTTOM_LEFT:
        out.x = x;
        out.y = y - (fb->res.y - im->res.y);
        break;
    case POS_CENTERED:
        out.x = x - ((fb->res.x / 2) - (im->res.x / 2));
        out.y = y - ((fb->res.y / 2) - (im->res.y / 2));
        break;
    }

    return out;
}

void qimg_draw_buffer(qimg_fb* fb, char* buf, int delay_s, bool repaint) {
    uint32_t delay_ms = delay_s * 1000;
    bool delay_set = (delay_s > 0);
    uint32_t start_ticks = qimg_get_millis();
    do {
        memcpy(fb->fbdata, buf, fb->size);

        /* Delay and repaint, check timer and draw again if needed */
        if (delay_set && repaint) {
            if (qimg_have_millis_elapsed(start_ticks, delay_ms))
                break;
        }
        /* Delay and no repaint, draw once and wait before break */
        else if (delay_set && !repaint) {
            uint32_t to_sleep = delay_ms - (qimg_get_millis() - start_ticks);
            if (to_sleep > 0)
                qimg_sleep_ms(to_sleep);
            break;
        }
        /* No delay or repaint, break immediately */
        else if (!delay_set && !repaint) {
            break;
        }
    } while (run);
}

void qimg_draw_image(qimg_image* im, qimg_fb* fb, qimg_position pos, qimg_bg bg,
                     bool repaint, int delay_s) {
    char* buf = malloc(fb->size);
    memcpy(buf, fb->fbdata, fb->size);
    qimg_color c;
    int offs, x, y;

    for (int x_ = 0; x_ < fb->res.x; ++x_) { /* This is embarassingly parallel */
        for (int y_ = 0; y_ < fb->res.y; ++y_) {
            /* Translate framebuffer coordinates to image coordinates */
            qimg_point t = qimg_translate_coords(pos, im, fb, x_, y_);
            x = t.x;
            y = t.y;

            if (x >= im->res.x || y >= im->res.y || x < 0 || y < 0) {
                if (bg == BG_DISABLED) {
                    continue; /* Keep the framebuffer as-is */
                } else {
                    c = qimg_get_bg_color(bg);
                }
            } else {
                c = qimg_get_pixel(im, x, y);
            }

            offs = (y_ * fb->res.x + x_) * 4;
            buf[offs + 0] = (char) c.b;
            buf[offs + 1] = (char) c.g;
            buf[offs + 2] = (char) c.r;
            buf[offs + 3] = (char) c.a;
        }
    }

    qimg_draw_buffer(fb, buf, delay_s, repaint);
    free(buf);
}

qimg_color qimg_get_pixel(qimg_image* im, int x, int y) {
    assertf(x < im->res.x && y < im->res.y, "Image coordinates out of bounds");
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

qimg_color qimg_get_bg_color(qimg_bg bg) {
    qimg_color bg_color;
    switch (bg) {
    case BG_BLACK:
        bg_color = (qimg_color){0, 0, 0, 0xff};
        break;
    case BG_WHITE:
        bg_color = (qimg_color){0xff, 0xff, 0xff, 0xff};
        break;
    case BG_RED:
        bg_color = (qimg_color){0xff, 0, 0, 0xff};
        break;
    case BG_GREEN:
        bg_color = (qimg_color){0, 0xff, 0, 0xff};
        break;
    case BG_BLUE:
        bg_color = (qimg_color){0, 0, 0xff, 0xff};
        break;
    default:
        bg_color = (qimg_color){0, 0, 0, 0xff};
        break;
    }
    return bg_color;
}

qimg_image qimg_load_image(char* input_path) {
    qimg_image im;
    im.pixels = stbi_load(input_path, &im.res.x, &im.res.y, &im.c, 0);
    assertf(im.pixels, "Loading image %s failed", input_path);
    return im;
}

qimg_collection qimg_load_collection(char** input_paths, int n_inputs, int offset) {
    qimg_collection col;
    for (int i = 0; i < n_inputs; ++i) {
        col.images[i] = qimg_load_image(input_paths[offset + i]);
    }
    col.size = n_inputs;
    col.idx = 0;
    return col;
}

qimg_dyn_collection qimg_init_dyn_collection(char** input_paths, int n_inputs) {
    qimg_dyn_collection dcol;
    dcol.input_paths = input_paths;
    dcol.size = n_inputs;
    dcol.n_consumed = 0;
    dcol.idx = 0;

    /* Load first batch */
    int n = (n_inputs < MAX_BUFFER_SIZE) ? n_inputs : MAX_BUFFER_SIZE;
    dcol.col = qimg_load_collection(input_paths, n, 0);
    return dcol;
}

qimg_image* qimg_get_next(qimg_dyn_collection* dcol) {
    if (dcol->n_consumed == dcol->col.size) {
        qimg_free_collection(&dcol->col);
        int left = dcol->size - dcol->idx;
        int n = (left < MAX_BUFFER_SIZE) ? left : MAX_BUFFER_SIZE;
        int offset = dcol->idx;
        dcol->col = qimg_load_collection(dcol->input_paths, n, offset);
        dcol->n_consumed = 0;
    }

    ++dcol->idx;
    ++dcol->n_consumed;

    if (dcol->idx == dcol->size) {
        dcol->idx = 0;
        dcol->n_consumed = dcol->size; /* trigger image loading */
    }

    return &dcol->col.images[dcol->col.idx++];
}

void qimg_free_image(qimg_image* im) {
    if (im->pixels)
        free(im->pixels);
}

void qimg_free_collection(qimg_collection* col) {
    for (int i = 0; i < col->size; ++i)
        qimg_free_image(&col->images[i]);
}

bool qimg_resize_image(qimg_image* im, qimg_point dest_res) {
    unsigned long s = dest_res.x * dest_res.y * im->c;
    uint8_t* out_buf = malloc(s);
    if (stbir_resize_uint8(im->pixels, im->res.x, im->res.y, 0, out_buf,
                           dest_res.x, dest_res.y, 0, im->c)) {
        /* Update data pointer */
        free(im->pixels);
        im->pixels = out_buf;

        /* Update resolution */
        im->res.x = dest_res.x;
        im->res.y = dest_res.y;
        return true;
    }
    return false;
}

qimg_point qimg_get_scaled_dims(qimg_point src, qimg_point vp,
                                     qimg_scale scale) {
    qimg_point r;
    float p;
    float px = (float) vp.x / (float) src.x;
    float py = (float) vp.y / (float) src.y;

    switch (scale) {
    case SCALE_DISABLED:
        r = src;
        break;
    case SCALE_STRETCH:
        r = vp;
        break;
    case SCALE_FILL:
        p = (px > py) ? px : py;
        r.x = src.x * p;
        r.y = src.y * p;
        break;
    case SCALE_FIT:
        p = (px > py) ? py : px;
        r.x = src.x * p;
        r.y = src.y * p;
        break;
    }

    return r;
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
           "-d <path>,      Use framebuffer device at given path.\n"
           "-b <i>,         Use framebuffer device with given index (/dev/fb<i>).\n"
           "                Default is to use one found with the lowest index.\n"
           "-c,             Hide terminal cursor.\n"
           "-r,             Keep repainting the image. If hiding the cursor\n"
           "                fails, this will certainly work for keeping the\n"
           "                image on top with the cost of CPU usage.\n"
           "\n"
           "Image layout:\n"
           "-pos <pos>,     Draw the image in given position. Possible values:\n"
           "                c   -   centered\n"
           "                tl  -   top left (default)\n"
           "                tr  -   top right\n"
           "                br  -   bottom right\n"
           "                bl  -   bottom left\n"
           "-bg <color>,    Fill background with color. Possible values:\n"
           "                black\n"
           "                white\n"
           "                red\n"
           "                green\n"
           "                blue\n"
           "                disabled (transparent, default)\n"
           "-scale <style>, Scale the image with given style. Possible values:\n"
           "                disabled    -   no scaling (default).\n"
           "                fit         -   fit the image to screen, preserving\n"
           "                                aspect ratio.\n"
           "                stretch     -   stretch the image to fill whole screen.\n"
           "                fill        -   fill the screen with the image,\n"
           "                                preserving aspect ratio.\n"
           "\n"
           "Slideshow and timing options:\n"
           "-delay <delay>, Slideshow interval in seconds (default 5s).\n"
           "                If used with a single image, the image is displayed\n"
           "                for <delay> seconds.\n"
           "-loop           Loop the slideshow indefinitely.\n"
           "\n"
           "Generic framebuffer operations:\n"
           "(Use one at a time, cannot be joined with other operations)\n"
           "-clear,         Clear the framebuffer\n"
           "\n");
}

void parse_arguments(int argc, char *argv[], int* fb_idx, char** input,
                     int* n_inputs, bool* refresh, bool* hide_cursor,
                     qimg_position* pos, qimg_bg* bg, int* slide_delay_s,
                     qimg_scale* scale, char** fb_path, bool* loop) {
    assertf(argc > 1, "Arguments missing");
    int opts = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-b") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                *fb_idx = atoi(argv[i]);
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
                *pos = str2qimg_position(argv[i]);
            }
        } else if (strcmp(argv[i], "-bg") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                *bg = str2qimg_bg(argv[i]);
            }
        } else if (strcmp(argv[i], "-delay") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                int dly = atoi(argv[i]);
                assertf(dly >= 0, "Delay must be positive");
                *slide_delay_s = dly;
            }
        } else if (strcmp(argv[i], "-scale") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                *scale = str2qimg_scale(argv[i]);
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            ++opts;
            if (argc > (++i)) {
                ++opts;
                *fb_path = argv[i];
            }
        } else if (strcmp(argv[i], "-loop") == 0) {
            ++opts;
            *loop = true;
        }


        /* These options only work one at a time, exiting after completion */
        else if (strcmp(argv[i], "-h") == 0) {
            ++opts;
            print_help();
            exit(EXIT_SUCCESS);
        } else if (strcmp(argv[i], "-clear") == 0) {
            ++opts;
            if (*fb_idx == -1)
                *fb_idx = get_default_framebuffer_idx();
            qimg_fb fb = qimg_open_fb(*fb_idx);
            qimg_clear_framebuffer(&fb);
            exit(EXIT_SUCCESS);
        }
    }
    /* We should still have some leftover arguments, these are our inputs */
    while (++opts < argc) {
        input[*n_inputs] = argv[opts];
        ++*n_inputs;
        assertf(*n_inputs <= MAX_IMAGES, "Too many input images (max %d)",
                MAX_IMAGES);
    }
}

int main(int argc, char *argv[]) {

    /* Record start ticks for timekeeping */
    begin_clk = clock();

    /* Setup starting values for params */
    int fb_idx = -1;
    int n_inputs = 0;
    int slide_dly_s = 0;
    char* input_paths[MAX_IMAGES];
    char* fb_path = NULL;
    bool repaint = false;
    bool hide_cursor = false;
    bool loop = false;
    qimg_position pos = POS_TOP_LEFT;
    qimg_bg bg = BG_DISABLED;

    parse_arguments(argc, argv, &fb_idx, input_paths, &n_inputs, &repaint,
                    &hide_cursor, &pos, &bg, &slide_dly_s, &scale, &fb_path,
                    &loop);

    assertf(n_inputs, "No input file");
    if (fb_idx == -1)
        fb_idx = get_default_framebuffer_idx();
    if (slide_dly_s == 0 && n_inputs > 1) /* Default interval for slideshows */
        slide_dly_s = 5;

    /* Open framebuffer */
    qimg_fb fb;
    if (fb_path)
        fb = qimg_open_fb_from_path(fb_path);
    else
        fb = qimg_open_fb(fb_idx);

    /* Initialize dynamic collection */
    qimg_dyn_collection dcol = qimg_init_dyn_collection(input_paths, n_inputs);

    /* Setup exit hooks on signals */
    signal(SIGINT, interrupt_handler);
    signal(SIGTERM, interrupt_handler);

    /* Fasten your seatbelts */
    if (hide_cursor) set_cursor_visibility(false);
    qimg_draw_images(&dcol, &fb, pos, bg, repaint, slide_dly_s, loop);

    /* if cursor is set to hidden and no repaint nor delay is set, the program
     * shall wait indefinitely for user interrupt */
    if (!repaint && hide_cursor && !slide_dly_s) pause();

    /* Cleanup */
    if (repaint || hide_cursor)
        qimg_clear_framebuffer(&fb);
    if (hide_cursor) set_cursor_visibility(true);
    qimg_free_collection(&dcol.col);
    qimg_free_framebuffer(&fb);

    return EXIT_SUCCESS;
}
