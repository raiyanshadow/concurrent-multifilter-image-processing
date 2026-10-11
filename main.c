#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "filters.h"
#include "bitmap.h"

#define NUM_FILTERS 10
#define NUM_FILTERS_NEED_INTENSITY 5
const char *filter_list[] = {"grayscale", "sharpen", "emboss", "gaussian_blur", "edge_detection", "glitch_effect", "invert",
                            "black_and_white", "pixelate", "vignette"};
const char *needs_strength[] = {"gaussian_blur", "black_and_white", "pixelate", "glitch_effect", "vignette"};

bool is_int(char *arg, int min, int max)
{
    char *endptr;
    errno = 0;
    long num = strtol(arg, &endptr, 10);

    if (arg == endptr) return false;
    if (errno == ERANGE) return false;
    if (num > max || num < min) return false;
    if (*endptr != '\0') return false;

    return true;

}

bool requires_strength(const char *arg)
{
    for (unsigned int i = 0; i < NUM_FILTERS_NEED_INTENSITY; i++)
    {
        if (strcmp(arg, needs_strength[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

bool parse_filter(char *arg, Filter* f)
{
    // Array of filter function pointers matching the filter_list order
    filter_func filter_functions[] = {
        grayscale_filter,            // GRAYSCALE
        sharpen_filter,              // SHARPEN
        emboss_filter,               // EMBOSS
        gaussian_blur_filter,        // GAUSSIAN_BLUR 
        edge_detection_filter,       // EDGE_DETECTION 
        glitch_filter,               // GLITCH_EFFECT
        invert_filter,               // INVERT
        black_and_white_filter,      // BLACK_AND_WHITE
        pixelate_filter,             // PIXELATE
        vignette_filter              // VIGNETTE
    };
    
    for (int i = 0; i < NUM_FILTERS; i++)
    {
        const char *filter = filter_list[i];
        size_t len = strlen(filter);

        if (strncmp(arg, filter, len) == 0 &&
            (arg[len] == '\0' || isdigit(arg[len])))
        {
            if (filter_functions[i] == NULL) {
                fprintf(stderr, "Filter %s is not yet implemented\n", filter);
                return false;
            }
            
            char *strength = arg + len;

            if (requires_strength(filter))
            {
                if (*strength == '\0')
                {
                    fprintf(stderr, "%s requires strength from 1 to 100, for example %s50\n", filter, filter);
                    return false;
                }

                if (!is_int(strength, 1, 100))
                {
                    fprintf(stderr, "Invalid strength for %s\n", filter);
                    return false;
                }
                f->strength = atoi(strength);
            }
            else
            {
                if (*strength != '\0')
                {
                    printf("%s does not take strength\n", filter);
                    return false;
                }
                f->strength = 0;
            }

            f->id = i;
            f->apply = filter_functions[i];

            return true;
        }
    }

    printf("Invalid filter: %s\n", arg);
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: main <input image> <number of filters> <filter 1> ... <filter n>\n");
        exit(1);
    } else if (!is_int(argv[2], 1, 100)) {
        fprintf(stderr, "Error: number of filters to apply must be a positive integer and less than 100 inclusive");
        exit(1);
    } 
    
    int nfilters = atoi(argv[2]);
    if (argc != nfilters + 3) {
        fprintf(stderr, "Expected %d filters, got %d", nfilters, argc - 3);
        exit(1);
    } 
    Filter* filters = malloc(sizeof(Filter) * nfilters);
    if (filters == NULL)
    {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < nfilters; i++)
    {
        if (!parse_filter(argv[i+3], &filters[i])) 
        {
            free(filters);
            exit(1);
        }
    }

    // expand gaussian_blur filters by strength
    int expanded_count = 0;
    for (int i = 0; i < nfilters; i++) {
        if (filters[i].id == 3) // gaussian_blur
            expanded_count += filters[i].strength;
        else
            expanded_count++;
    }
    printf("expanded_count: %d\n", expanded_count);

    Filter *expanded = malloc(sizeof(Filter) * expanded_count);
    if (!expanded) {
        perror("malloc");
        free(filters);
        exit(1);
    }
    int idx = 0;
    for (int i = 0; i < nfilters; i++) {
        if (filters[i].id == 3) {
            for (int j = 0; j < filters[i].strength; j++) {
                expanded[idx] = filters[i];
                expanded[idx].strength = 1;
                idx++;
            }
        } else {
            expanded[idx++] = filters[i];
        }
    }

    const char *dot = strrchr(argv[1], '.');
    int base_len = dot ? (int)(dot - argv[1]) : (int)strlen(argv[1]);

    // Allocate enough space for output filename
    int out_len = base_len + 1;
    for (int i = 0; i < nfilters; i++) {
        out_len += strlen(filter_list[filters[i].id]) + 10;
    }
    out_len += 5;
    char *out_filename = malloc(out_len);
    if (!out_filename) { 
        perror("malloc"); 
        exit(1);
    }

    strncpy(out_filename, argv[1], base_len);
    out_filename[base_len] = '\0';

    // Append filter names to filename
    for (int i = 0; i < nfilters; i++) {
        strcat(out_filename, "_");
        if (filters[i].id >= 0 && filters[i].id < 16) {
            strcat(out_filename, filter_list[filters[i].id]);
        } else {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "filter%d", filters[i].id);
            strcat(out_filename, id_buf);
        }
        if (filters[i].strength != 0) {
            char str_buf[8];
            snprintf(str_buf, sizeof(str_buf), "%d", filters[i].strength);
            strcat(out_filename, str_buf);
        }
    }

    strcat(out_filename, ".bmp");

    free(filters);

    FILE *image = fopen(argv[1], "rb");
    if (image == NULL) {
        // fprintf(stderr, "Cannot open file\n");
        perror("fopen");
        free(expanded);
        exit(1);
    }

    BMPHeader *header = read_bitmap_metadata(image);
    if (header == NULL)
    {
        free(expanded);
        fclose(image);
        exit(1);
    }

    printf("DEBUG: w=%d h=%d offset=%d bpp=%d bytes=%d\n",
       header->width,
       header->height,
       header->pixel_array_offset,
       header->bits_per_pixel,
       header->bytes_per_pixel);
    fflush(stdout);

    pixel **pixels = read_pixel_array(image, header);
    if (pixels == NULL)
    {
        free(expanded);
        free_bmp_header(header);
        fclose(image);
        exit(1);
    }

    fclose(image);
    
    process_filters(expanded, expanded_count, pixels, header, out_filename);
    free(out_filename);
    free(expanded);
    free_pixel_array(pixels, header->height);
    free_bmp_header(header);
}
