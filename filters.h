#ifndef FILTERS_H
#define FILTERS_H

#define NUM_FILTERS 10

#include "bitmap.h"

typedef struct {
    int width;
    pixel* rows[3];
} Window;

typedef void (*filter_func)(Window* in_window, Window* out_window, int strength, int height, int abs_row);

typedef struct {
    int id;
    int strength;     // strength value (0 if not used)
    filter_func apply;
} Filter;

enum {
    GRAYSCALE,
    SHARPEN,
    EMBOSS,
    GAUSSIAN_BLUR,
    EDGE_DETECTION,
    GLITCH_EFFECT,
    INVERT,
    BLACK_AND_WHITE,
    PIXELATE,
    VIGNETTE
};

void process_filters(Filter *filters, int count, pixel** pixels, BMPHeader* header, char* out_filename);
void grayscale_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void glitch_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void sharpen_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void emboss_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void vignette_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void gaussian_blur_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void edge_detection_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void pixelate_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void invert_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
void black_and_white_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row);
#endif
