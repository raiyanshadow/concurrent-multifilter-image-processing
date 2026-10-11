#include "filters.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Citation: We used gpt to get high level ideas of how filters work.


// grayscale one row: in -> out
void grayscale_row(pixel *in, pixel *out, int width) {
    for (int i = 0; i < width; i++) {
        int gray = (21 * in[i].red + 72 * in[i].green + 7 * in[i].blue + 50) / 100;
        if (gray > 255) gray = 255;
        if (gray < 0) gray = 0;
        out[i].red   = gray;
        out[i].green = gray;
        out[i].blue  = gray;
        out[i].alpha = in[i].alpha; // preserve alpha
    }
}

// Wrapper: apply grayscale filter to 3-row window safely
void grayscale_filter(Window* in_window, Window* out_window, int strength, int height, int abs_row) {
    (void)strength;
    (void)height;
    (void)abs_row;

    // Apply grayscale row-by-row from in -> out
    grayscale_row(in_window->rows[0], out_window->rows[0], in_window->width);
    grayscale_row(in_window->rows[1], out_window->rows[1], in_window->width);
    grayscale_row(in_window->rows[2], out_window->rows[2], in_window->width);
}

// glitch one row: in -> out
void glitch_row(pixel *in, pixel *out, int width, int strength, int abs_row) {
    // per-row seed for consistent but varied tears
    srand((unsigned int)(abs_row * 2654435761u));

    // large random horizontal tear offset per row
    int max_offset = (strength * 80) / 100;
    int tear = (rand() % (2 * max_offset + 1)) - max_offset;

    // per-channel offsets
    int r_offset = tear + (strength / 5);
    int g_offset = -tear / 2;
    int b_offset = tear - (strength / 8);

    for (int i = 0; i < width; i++) {
        // wraparound indexing for each channel
        int r_src = ((i + r_offset) % width + width) % width;
        int g_src = ((i + g_offset) % width + width) % width;
        int b_src = ((i + b_offset) % width + width) % width;

        out[i].red   = in[r_src].red;
        out[i].green = in[g_src].green;
        out[i].blue  = in[b_src].blue;
        out[i].alpha = in[i].alpha; // preserve alpha
    }
}

// Wrapper: apply glitch filter to 3-row window safely
void glitch_filter(Window* in_window, Window* out_window, int strength, int height, int abs_row) {
    (void)height;

    if (strength < 1) strength = 1;
    if (strength > 100) strength = 100;

    // Apply glitch row-by-row from in -> out
    for (int r = 0; r < 3; r++) {
        glitch_row(in_window->rows[r], out_window->rows[r], in_window->width, strength, abs_row - 1 + r);
    }
}

// sharpen_filter
void sharpen_filter(Window *in_window, Window *out_window, int strength, int height, int abs_row) {
    (void)strength;
    (void)height;
    (void)abs_row;

    // kernel to sharpen an image
    int kernel[3][3] = {
        {0, -1, 0},
        {-1, 5, -1},
        {0, -1, 0}
    };
    int width = in_window->width;

    // apply the filter to the middle row
    for (int pixel = 0; pixel < width; pixel++) {
        int r = 0;
        int g = 0;
        int b = 0;
        int left;
        int right;

        // Account for edges
        if (pixel == 0) left = 0; // repeat 1st column
        else left = pixel - 1;

        if (pixel == width - 1) right = pixel; // repeat last column 
        else right = pixel + 1;

        // Apply filter to each row
        for (int i = 0; i < 3; i++) {
            r += kernel[i][0] * in_window->rows[i][left].red;
            r += kernel[i][1] * in_window->rows[i][pixel].red;
            r += kernel[i][2] * in_window->rows[i][right].red;

            g += kernel[i][0] * in_window->rows[i][left].green;
            g += kernel[i][1] * in_window->rows[i][pixel].green;
            g += kernel[i][2] * in_window->rows[i][right].green;

            b += kernel[i][0] * in_window->rows[i][left].blue;
            b += kernel[i][1] * in_window->rows[i][pixel].blue;
            b += kernel[i][2] * in_window->rows[i][right].blue;
        }

        // Ensure rgb values are valid
        if (r < 0) r = 0;
        else if (r > 255) r = 255;

        if (g < 0) g = 0;
        else if (g > 255) g = 255;

        if (b < 0) b = 0;
        else if (b > 255) b = 255;

        out_window->rows[1][pixel].red   = r;
        out_window->rows[1][pixel].green = g;
        out_window->rows[1][pixel].blue  = b;
        out_window->rows[1][pixel].alpha = in_window->rows[1][pixel].alpha; // preserve alpha
    }
}

// emboss_filter
void emboss_filter(Window *in_window, Window *out_window, int strength, int height, int abs_row) {
    (void)strength;
    (void)height;
    (void)abs_row;

    // kernel to emboss an image
    int kernel[3][3] = {
        {-2, -1, 0},
        {-1, 1, 1},
        {0, 1, 2}
    };
    int width = in_window->width;

    // apply the filter to the middle row
    for (int pixel = 0; pixel < width; pixel++) {
        int r = 0;
        int g = 0;
        int b = 0;
        int left;
        int right;

        // Account for edges
        if (pixel == 0) left = 0; // repeat 1st column
        else left = pixel - 1;

        if (pixel == width - 1) right = pixel; // repeat last column 
        else right = pixel + 1;

        // Apply filter to each row
        for (int i = 0; i < 3; i++) {
            r += kernel[i][0] * in_window->rows[i][left].red;
            r += kernel[i][1] * in_window->rows[i][pixel].red;
            r += kernel[i][2] * in_window->rows[i][right].red;

            g += kernel[i][0] * in_window->rows[i][left].green;
            g += kernel[i][1] * in_window->rows[i][pixel].green;
            g += kernel[i][2] * in_window->rows[i][right].green;

            b += kernel[i][0] * in_window->rows[i][left].blue;
            b += kernel[i][1] * in_window->rows[i][pixel].blue;
            b += kernel[i][2] * in_window->rows[i][right].blue;
        }

        // Add bias
        r += 128;
        g += 128;
        b += 128;

        // Ensure rgb values are valid
        if (r < 0) r = 0;
        else if (r > 255) r = 255;

        if (g < 0) g = 0;
        else if (g > 255) g = 255;

        if (b < 0) b = 0;
        else if (b > 255) b = 255;

        out_window->rows[1][pixel].red   = r;
        out_window->rows[1][pixel].green = g;
        out_window->rows[1][pixel].blue  = b;
        out_window->rows[1][pixel].alpha = in_window->rows[1][pixel].alpha; // preserve alpha
    }
}

// vignette_filter
void vignette_filter(Window *in_window, Window *out_window, int strength, int height, int abs_row) {
    int width = in_window->width;

    // image center
    int center_y = height / 2;
    int center_x = width / 2;

    // gaussian variance
    double variance_y = ((strength / 100.0) * center_y) * ((strength / 100.0) * center_y);
    double variance_x = ((strength / 100.0) * center_x) * ((strength / 100.0) * center_x);

    // generate 1D gaussian kernels for x and y directions
    double *weight_x = malloc(sizeof(double) * width);
    if (!weight_x) {
        perror("malloc");
        exit(1);
    }

    // precompute x weights
    for (int col = 0; col < width; col++) {
        weight_x[col] = exp(-((double)((col - center_x) * (col - center_x)) / (2 * variance_x)));
    }
    
    // row indices for the 3-row window
    int rows[3] = {
        abs_row > 0 ? abs_row - 1 : abs_row,
        abs_row,
        abs_row < height - 1 ? abs_row + 1 : abs_row
    };

    // apply vignette to each row
    for (int r = 0; r < 3; r++) {
        double dy = rows[r] - center_y;
        double weight_y = exp(-((dy * dy) / (2 * variance_y)));

        for (int i = 0; i < width; i++) {
            double m = weight_y * weight_x[i];
            int rv = (int)(in_window->rows[r][i].red * m);
            int gv = (int)(in_window->rows[r][i].green * m);
            int bv = (int)(in_window->rows[r][i].blue * m);

            out_window->rows[r][i].red   = rv < 0 ? 0 : (rv > 255 ? 255 : rv);
            out_window->rows[r][i].green = gv < 0 ? 0 : (gv > 255 ? 255 : gv);
            out_window->rows[r][i].blue  = bv < 0 ? 0 : (bv > 255 ? 255 : bv);
            out_window->rows[r][i].alpha = in_window->rows[r][i].alpha; // preserve alpha
        }
    }

    free(weight_x);
}

// gaussian blur one row: in -> out
void gaussian_blur_row(Window* in, Window *out, int width) {
    pixel *r0 = in->rows[0];
    pixel *r1 = in->rows[1];
    pixel *r2 = in->rows[2];
    pixel *dst = out->rows[1];

    // left edge
    {
        int p = 0, l = 0, r = 1;

        int red =
            r0[l].red + 2*r0[p].red + r0[r].red +
            2*r1[l].red + 4*r1[p].red + 2*r1[r].red +
            r2[l].red + 2*r2[p].red + r2[r].red;

        int green =
            r0[l].green + 2*r0[p].green + r0[r].green +
            2*r1[l].green + 4*r1[p].green + 2*r1[r].green +
            r2[l].green + 2*r2[p].green + r2[r].green;

        int blue =
            r0[l].blue + 2*r0[p].blue + r0[r].blue +
            2*r1[l].blue + 4*r1[p].blue + 2*r1[r].blue +
            r2[l].blue + 2*r2[p].blue + r2[r].blue;

        dst[p].red   = red   >> 4; // divide by 16
        dst[p].green = green >> 4;
        dst[p].blue  = blue  >> 4;
        dst[p].alpha = r1[p].alpha;
    }

    // middle
    for (int p = 1; p < width - 1; p++) {
        int l = p - 1;
        int r = p + 1;

        int red =
            r0[l].red + 2*r0[p].red + r0[r].red +
            2*r1[l].red + 4*r1[p].red + 2*r1[r].red +
            r2[l].red + 2*r2[p].red + r2[r].red;

        int green =
            r0[l].green + 2*r0[p].green + r0[r].green +
            2*r1[l].green + 4*r1[p].green + 2*r1[r].green +
            r2[l].green + 2*r2[p].green + r2[r].green;

        int blue =
            r0[l].blue + 2*r0[p].blue + r0[r].blue +
            2*r1[l].blue + 4*r1[p].blue + 2*r1[r].blue +
            r2[l].blue + 2*r2[p].blue + r2[r].blue;

        dst[p].red   = red   >> 4;
        dst[p].green = green >> 4;
        dst[p].blue  = blue  >> 4;
        dst[p].alpha = r1[p].alpha;
    }

    // right edge
    if (width > 1) {
        int p = width - 1, l = p - 1, r = p;

        int red =
            r0[l].red + 2*r0[p].red + r0[r].red +
            2*r1[l].red + 4*r1[p].red + 2*r1[r].red +
            r2[l].red + 2*r2[p].red + r2[r].red;

        int green =
            r0[l].green + 2*r0[p].green + r0[r].green +
            2*r1[l].green + 4*r1[p].green + 2*r1[r].green +
            r2[l].green + 2*r2[p].green + r2[r].green;

        int blue =
            r0[l].blue + 2*r0[p].blue + r0[r].blue +
            2*r1[l].blue + 4*r1[p].blue + 2*r1[r].blue +
            r2[l].blue + 2*r2[p].blue + r2[r].blue;

        dst[p].red   = red   >> 4;
        dst[p].green = green >> 4;
        dst[p].blue  = blue  >> 4;
        dst[p].alpha = r1[p].alpha;
    }
}

// apply gaussian blur filter to 3-row window safely
void gaussian_blur_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row) {
    (void)strength;
    (void)height;
    (void)abs_row;

    gaussian_blur_row(in_window, out_window, in_window->width);
}

// edge_detection_filter
void edge_detection_filter(Window* in_window, Window *out_window, int strength, int height, int abs_row) {
    (void)strength;

    // edge detection kernel
    int kernel[3][3] = {
        {-1, -1, -1},
        {-1,  8, -1},
        {-1, -1, -1}
    };
    int width = in_window->width;

    // row indices for edge handling
    int row_idx[3] = {0, 1, 2};
    if (abs_row == 0)          row_idx[0] = 2;
    if (abs_row == height - 1) row_idx[2] = 0;

    // apply the filter to the middle row
    for (int pixel = 0; pixel < width; pixel++) {
        int r = 0;
        int left  = (pixel == 0) ? 0 : pixel - 1;
        int right = (pixel == width - 1) ? pixel : pixel + 1;

        for (int i = 0; i < 3; i++) {
            int gl = (21 * in_window->rows[row_idx[i]][left].red  +
                      72 * in_window->rows[row_idx[i]][left].green +
                       7 * in_window->rows[row_idx[i]][left].blue  + 50) / 100;
            int gm = (21 * in_window->rows[row_idx[i]][pixel].red  +
                      72 * in_window->rows[row_idx[i]][pixel].green +
                       7 * in_window->rows[row_idx[i]][pixel].blue  + 50) / 100;
            int gr = (21 * in_window->rows[row_idx[i]][right].red  +
                      72 * in_window->rows[row_idx[i]][right].green +
                       7 * in_window->rows[row_idx[i]][right].blue  + 50) / 100;

            r += kernel[i][0] * gl + kernel[i][1] * gm + kernel[i][2] * gr;
        }

        // Ensure rgb values are valid
        if (r < 0) r = 0;
        else if (r > 255) r = 255;

        out_window->rows[1][pixel].red   = r;
        out_window->rows[1][pixel].green = r;
        out_window->rows[1][pixel].blue  = r;
        out_window->rows[1][pixel].alpha = in_window->rows[1][pixel].alpha; // preserve alpha
    }
}

// pixelate one row: in -> out
void pixelate_row(Window* in_window, Window* out_window, int width, int strength) {
    // exponential block size: strength 1 -> 1px, strength 100 -> 64px
    int block_size = (int)pow(2, (strength * 6.0) / 100);
    if (block_size < 1) block_size = 1;

    int block_avg_r = 0, block_avg_g = 0, block_avg_b = 0;

    for (int pixel = 0; pixel < width; pixel++) {
        int block_start = (pixel / block_size) * block_size;

        // only recompute average at the start of each block
        if (pixel == block_start) {
            int block_end = block_start + block_size;
            if (block_end > width) block_end = width;

            block_avg_r = 0;
            block_avg_g = 0;
            block_avg_b = 0;
            int count = block_end - block_start;

            for (int j = block_start; j < block_end; j++) {
                block_avg_r += in_window->rows[1][j].red;
                block_avg_g += in_window->rows[1][j].green;
                block_avg_b += in_window->rows[1][j].blue;
            }

            block_avg_r /= count;
            block_avg_g /= count;
            block_avg_b /= count;
        }

        out_window->rows[1][pixel].red   = block_avg_r;
        out_window->rows[1][pixel].green = block_avg_g;
        out_window->rows[1][pixel].blue  = block_avg_b;
        out_window->rows[1][pixel].alpha = in_window->rows[1][pixel].alpha; // preserve alpha
    }
}

// apply pixelate filter to 3-row window safely
void pixelate_filter(Window* in_window, Window* out_window, int strength, int height, int abs_row) {
    (void)height;
    (void)abs_row;

    if (strength < 1) strength = 1;
    if (strength > 100) strength = 100;

    pixelate_row(in_window, out_window, in_window->width, strength);
}

// invert one row: in -> out
void invert_row(pixel *in, pixel *out, int width) {
    for (int i = 0; i < width; i++) {
        out[i].red   = 255 - in[i].red;
        out[i].green = 255 - in[i].green;
        out[i].blue  = 255 - in[i].blue;
        out[i].alpha = in[i].alpha; // preserve alpha
    }
}

// Wrapper: apply invert filter to 3-row window safely
void invert_filter(Window* in_window, Window* out_window, int strength, int height, int abs_row) {
    (void)strength;
    (void)height;
    (void)abs_row;

    invert_row(in_window->rows[1], out_window->rows[1], in_window->width);
}


// black and white one row: in -> out
void black_and_white_row(pixel *in, pixel *out, int width, int threshold) {
    for (int i = 0; i < width; i++) {
        int gray = (21 * in[i].red + 72 * in[i].green + 7 * in[i].blue + 50) / 100;

        int value;
        if (gray >= threshold) {
            value = 255;
        } else {
            value = 0;
        }

        out[i].red   = value;
        out[i].green = value;
        out[i].blue  = value;
        out[i].alpha = in[i].alpha; // preserve alpha
    }
}

// Wrapper: apply black and white filter to 3-row window safely
void black_and_white_filter(Window* in_window, Window* out_window, int strength, int height, int abs_row) {
    (void)height;
    (void)abs_row;

    // default threshold
    int threshold = 128;

    if (strength > 0) {
        if (strength < 1) strength = 1;
        if (strength > 100) strength = 100;
        threshold = (strength * 255) / 100;
    }

    black_and_white_row(in_window->rows[1], out_window->rows[1], in_window->width, threshold);
}
