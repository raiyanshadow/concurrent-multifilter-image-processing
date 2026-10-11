#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "bitmap.h"


//Citation: The code in this file was inspired by the bitmap lab code.


/*
 * Read BMP metadata needed by the rest of the program.
 *
 * This program only supports:
 *   - 24-bit BMP  (3 bytes per pixel)
 *   - 32-bit BMP  (4 bytes per pixel)
 *
 * We normalize both formats into the same in-memory pixel struct:
 *   pixel { blue, green, red, alpha }
 *
 * For 24-bit BMPs, alpha is set to 255 when reading.
 */
BMPHeader* read_bitmap_metadata(FILE *image) {
    if (image == NULL) {
        fprintf(stderr, "read_bitmap_metadata: image file is NULL\n");
        return NULL;
    }

    BMPHeader *header = malloc(sizeof(BMPHeader));
    if (!header) { perror("malloc"); return NULL; }

    header->data = NULL;
    header->width = 0;
    header->height = 0;
    header->pixel_array_offset = 0;
    header->bits_per_pixel = 0;
    header->bytes_per_pixel = 0;

    /* Check BMP signature ("BM") */
    unsigned char signature[2];
    if (fseek(image, 0, SEEK_SET) != 0) {
        perror("fseek");
        free(header);
        return NULL;
    }
    if (fread(signature, 1, 2, image) != 2) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading BMP signature\n");
        }
        free(header);
        return NULL;
    }
    if (signature[0] != 'B' || signature[1] != 'M') {
        fprintf(stderr, "Unsupported file format: not a BMP file\n");
        free(header);
        return NULL;
    }

    /* Read pixel array offset */
    if (fseek(image, 10, SEEK_SET) != 0) {
        perror("fseek");
        free(header);
        return NULL;
    }
    if (fread(&header->pixel_array_offset, sizeof(int), 1, image) != 1) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading pixel array offset\n");
        }
        free(header);
        return NULL;
    }

    /* Read width and height */
    if (fseek(image, 18, SEEK_SET) != 0) {
        perror("fseek");
        free(header);
        return NULL;
    }
    if (fread(&header->width, sizeof(int), 1, image) != 1) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading BMP width\n");
        }
        free(header);
        return NULL;
    }
    if (fread(&header->height, sizeof(int), 1, image) != 1) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading BMP height\n");
        }
        free(header);
        return NULL;
    }

    /* Read bit depth */
    if (fseek(image, 28, SEEK_SET) != 0) {
        perror("fseek");
        free(header);
        return NULL;
    }
    unsigned short bpp;
    if (fread(&bpp, sizeof(unsigned short), 1, image) != 1) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading BMP bit depth\n");
        }
        free(header);
        return NULL;
    }
    header->bits_per_pixel = (int)bpp;
    header->bytes_per_pixel = bpp / 8;

    /* Reject unsupported formats early so the rest of the
     * pipeline only ever sees 24-bit or 32-bit BMP data */
    if (header->bits_per_pixel != 24 && header->bits_per_pixel != 32) {
        fprintf(stderr,
                "Unsupported BMP bit depth: %d. Only 24-bit and 32-bit BMP files are supported.\n",
                header->bits_per_pixel);
        free(header);
        return NULL;
    }
    if (header->bytes_per_pixel != 3 && header->bytes_per_pixel != 4) {
        fprintf(stderr,
                "Unsupported BMP pixel size: %d bytes per pixel.\n",
                header->bytes_per_pixel);
        free(header);
        return NULL;
    }
    if (header->pixel_array_offset <= 0) {
        fprintf(stderr, "Invalid BMP pixel array offset\n");
        free(header);
        return NULL;
    }

    // Store header data
    header->data = malloc(header->pixel_array_offset);
    if (!header->data) { perror("malloc"); free(header); return NULL; }
    if (fseek(image, 0, SEEK_SET) != 0) {
        perror("fseek");
        free(header->data);
        free(header);
        return NULL;
    }
    if (fread(header->data, 1, header->pixel_array_offset, image)
                != (size_t)header->pixel_array_offset) {
        if (ferror(image)) {
            perror("fread");
        } else {
            fprintf(stderr, "Unexpected EOF while reading BMP header bytes\n");
        }
        free(header->data);
        free(header);
        return NULL;
    }

    return header;
}


/*
 * Read the BMP pixel array and normalize each pixel into our in-memory pixel
 * struct.
 *
 * For 24-bit BMP:
 *   B, G, R are read from file and alpha is set to 255.
 *
 * For 32-bit BMP:
 *   B, G, R, A are read directly from file.
 */
pixel **read_pixel_array(FILE *image, BMPHeader* header) {
    if (image == NULL || header == NULL) {
        fprintf(stderr, "read_pixel_array: image/header is NULL\n");
        return NULL;
    }

    int w = header->width, h = header->height;
    int bpp = header->bytes_per_pixel;
    int row_bytes = (w * bpp + 3) & ~3; // padded to multiple of 4

    pixel **pixels = malloc(sizeof(pixel*) * h);
    if (!pixels) { perror("malloc"); return NULL; }

    if (fseek(image, header->pixel_array_offset, SEEK_SET) != 0) {
        perror("fseek");
        free(pixels);
        return NULL;
    }

    for (int i = 0; i < h; i++) {
        pixels[i] = malloc(sizeof(pixel) * w);
        if (!pixels[i]) { perror("malloc"); free_pixel_array(pixels, i); return NULL; }

        unsigned char *row = malloc(row_bytes);
        if (!row) {
            perror("malloc");
            free_pixel_array(pixels, i + 1);
            return NULL;
        }

        if (fread(row, 1, row_bytes, image) != (size_t)row_bytes) {
            if (ferror(image)) {
                perror("fread");
            } else {
                fprintf(stderr, "Unexpected EOF while reading BMP row %d\n", i);
            }
            free(row);
            free_pixel_array(pixels, i + 1);
            return NULL;
        }

        for (int j = 0; j < w; j++) {
            pixels[i][j].blue  = row[j*bpp + 0];
            pixels[i][j].green = row[j*bpp + 1];
            pixels[i][j].red   = row[j*bpp + 2];
            
            if (bpp == 4) {
                pixels[i][j].alpha = row[j * bpp + 3];
            } else {
                pixels[i][j].alpha = 255;
            }
        }
        free(row);
    }
    return pixels;
}


/*
 * Write a BMP back out using the preserved original header bytes and the
 * normalized pixel array.
 *
 * Since the header is preserved, the output format stays consistent with the
 * original file (24-bit remains 24-bit, 32-bit remains 32-bit).
 */
int write_bitmap(const char* filename, BMPHeader* header, pixel* pixels) {
    if (filename == NULL || header == NULL || pixels == NULL) {
        fprintf(stderr, "write_bitmap: invalid argument\n");
        return -1;
    }

    FILE *output = fopen(filename, "wb");
    if (!output) { perror("fopen"); return -1; }

    int w = header->width, h = header->height;
    int bpp = header->bytes_per_pixel;
    int row_bytes = (w * bpp + 3) & ~3;

    // Write header
    if (fwrite(header->data, 1, header->pixel_array_offset, output) != (size_t)header->pixel_array_offset) {
        perror("fwrite");
        if (fclose(output) != 0) {
            perror("fclose");
            return -1;
        }
        return -1;
    }
    
    unsigned char *row = malloc(row_bytes); // automatically zero-padded
    if (!row) {
        perror("malloc"); 
        if (fclose(output) != 0) {
            perror("fclose");
            return -1;
        }
        return -1;
    }

    // Write pixel data row by row with required BMP padding
    for (int i = 0; i < h; i++) {
        pixel *p = pixels + i * w;
        for (int j = 0; j < w; j++) {
            row[j*bpp + 0] = p[j].blue;
            row[j*bpp + 1] = p[j].green;
            row[j*bpp + 2] = p[j].red;
            if (bpp == 4) row[j * bpp + 3] = p[j].alpha;
        }
        
        int used = w * bpp;
        if (row_bytes > used) {
            memset(row + used, 0, row_bytes - used);
        }
        
        if (fwrite(row, 1, row_bytes, output) != (size_t)row_bytes) {
            perror("fwrite");
            free(row);
            if (fclose(output) != 0) {
                perror("fclose");
                return -1;
            }
            return -1;
        }
    }
    free(row);
    if (fclose(output) != 0) {
        perror("fclose");
        return -1;
    }
    return 0;
}

void free_pixel_array(pixel** pixels, int height) {
    if (pixels == NULL) return;

    for (int i = 0; i < height; i++) {free(pixels[i]);}
    free(pixels);
}

void free_bmp_header(BMPHeader *header) {
    if (header == NULL) return;

    free(header->data);
    free(header);
}
