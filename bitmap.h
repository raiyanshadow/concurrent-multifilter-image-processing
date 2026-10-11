#ifndef BITMAP_H_
#define BITMAP_H_

#include <stdint.h>
#include <stdio.h>

typedef struct pixel {
    unsigned char blue;
    unsigned char green;
    unsigned char red;
    unsigned char alpha;
} pixel;

typedef struct BMPHeader {
    uint8_t *data;
    int width;
    int height;
    int pixel_array_offset;
    int bits_per_pixel;
    int bytes_per_pixel;
} BMPHeader;

BMPHeader* read_bitmap_metadata(FILE *image);
pixel **read_pixel_array(FILE *image, BMPHeader* header);
int write_bitmap(const char* filename, BMPHeader* header, pixel* pixels);
void free_pixel_array(pixel** pixels, int height);
void free_bmp_header(BMPHeader *header);

#endif /* BITMAP_H_*/
