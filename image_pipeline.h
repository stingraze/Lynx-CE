#ifndef IMAGE_PIPELINE_H
#define IMAGE_PIPELINE_H

#include <stddef.h>

typedef struct BrowserConfig {
    int image_scale_percent;   /* 1..100 */
    int max_img_width;         /* 0 means unset */
    int max_img_height;        /* 0 means unset */
    int small_images_enabled;  /* boolean */
} BrowserConfig;

typedef struct ImageBuffer {
    int width;
    int height;
    int bytes_per_pixel;
    unsigned char *pixels;
} ImageBuffer;

int image_pipeline_fetch_bmp(const char *img_url, ImageBuffer *out_img);
int image_pipeline_downscale_nearest(const ImageBuffer *src,
                                     ImageBuffer *dst,
                                     const BrowserConfig *cfg);
void image_pipeline_free(ImageBuffer *img);

#endif
