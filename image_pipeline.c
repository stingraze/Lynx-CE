#include "image_pipeline.h"

#include <windows.h>
#include "winsock2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTP_PORT 80

#pragma pack(push, 1)
typedef struct {
    unsigned short bfType;
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
} BMPFileHeader;

typedef struct {
    unsigned int biSize;
    int biWidth;
    int biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int biCompression;
    unsigned int biSizeImage;
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

static int parse_http_url(const char *url, char *host, char *path, int max_len)
{
    if (strncmp(url, "http://", 7) != 0)
        return -1;

    {
        const char *p = url + 7;
        const char *slash = strchr(p, '/');
        if (!slash) {
            strncpy(host, p, max_len);
            host[max_len - 1] = '\0';
            strcpy(path, "/");
        } else {
            int host_len = (int)(slash - p);
            if (host_len <= 0 || host_len >= max_len)
                return -1;
            strncpy(host, p, host_len);
            host[host_len] = '\0';
            strncpy(path, slash, max_len);
            path[max_len - 1] = '\0';
        }
    }
    return 0;
}

static int http_get_to_memory(const char *url, unsigned char **out_buf, int *out_len)
{
    char host[256] = {0};
    char path[512] = {0};
    SOCKET s;
    struct hostent *he;
    struct sockaddr_in server;
    char request[1024];
    int cap = 4096;
    int used = 0;
    int received;
    char chunk[1024];
    unsigned char *buf;

    if (parse_http_url(url, host, path, 256) != 0)
        return -1;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return -1;

    he = gethostbyname(host);
    if (!he) {
        closesocket(s);
        return -1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(HTTP_PORT);
    server.sin_addr.s_addr = *((u_long*)he->h_addr);

    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0) {
        closesocket(s);
        return -1;
    }

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: CE-Lynx/1.0\r\n\r\n",
             path, host);

    if (send(s, request, (int)strlen(request), 0) <= 0) {
        closesocket(s);
        return -1;
    }

    buf = (unsigned char*)malloc(cap);
    if (!buf) {
        closesocket(s);
        return -1;
    }

    while ((received = recv(s, chunk, sizeof(chunk), 0)) > 0) {
        if (used + received > cap) {
            int new_cap = cap * 2;
            while (new_cap < used + received)
                new_cap *= 2;
            {
                unsigned char *nb = (unsigned char*)realloc(buf, new_cap);
                if (!nb) {
                    free(buf);
                    closesocket(s);
                    return -1;
                }
                buf = nb;
                cap = new_cap;
            }
        }
        memcpy(buf + used, chunk, received);
        used += received;
    }

    closesocket(s);

    if (used <= 0) {
        free(buf);
        return -1;
    }

    *out_buf = buf;
    *out_len = used;
    return 0;
}

static int decode_bmp_from_http_payload(const unsigned char *payload, int payload_len, ImageBuffer *out_img)
{
    const char *header_end;
    const unsigned char *bmp_data;
    int bmp_len;
    BMPFileHeader fh;
    BMPInfoHeader ih;
    int row_stride;
    int y;

    header_end = strstr((const char*)payload, "\r\n\r\n");
    if (!header_end)
        return -1;

    bmp_data = (const unsigned char*)(header_end + 4);
    bmp_len = payload_len - (int)(bmp_data - payload);
    if (bmp_len < (int)(sizeof(BMPFileHeader) + sizeof(BMPInfoHeader)))
        return -1;

    memcpy(&fh, bmp_data, sizeof(fh));
    memcpy(&ih, bmp_data + sizeof(fh), sizeof(ih));

    if (fh.bfType != 0x4D42 || ih.biBitCount != 24 || ih.biCompression != 0)
        return -1;

    if (ih.biWidth <= 0 || ih.biHeight <= 0)
        return -1;

    out_img->width = ih.biWidth;
    out_img->height = ih.biHeight;
    out_img->bytes_per_pixel = 3;
    out_img->pixels = (unsigned char*)malloc(out_img->width * out_img->height * 3);
    if (!out_img->pixels)
        return -1;

    row_stride = ((out_img->width * 3 + 3) / 4) * 4;
    if ((int)fh.bfOffBits + row_stride * out_img->height > bmp_len) {
        free(out_img->pixels);
        out_img->pixels = 0;
        return -1;
    }

    for (y = 0; y < out_img->height; ++y) {
        const unsigned char *src_row = bmp_data + fh.bfOffBits + (out_img->height - 1 - y) * row_stride;
        unsigned char *dst_row = out_img->pixels + y * out_img->width * 3;
        memcpy(dst_row, src_row, out_img->width * 3);
    }

    return 0;
}

int image_pipeline_fetch_bmp(const char *img_url, ImageBuffer *out_img)
{
    unsigned char *payload = 0;
    int payload_len = 0;
    memset(out_img, 0, sizeof(*out_img));

    if (http_get_to_memory(img_url, &payload, &payload_len) != 0)
        return -1;

    if (decode_bmp_from_http_payload(payload, payload_len, out_img) != 0) {
        free(payload);
        return -1;
    }

    free(payload);
    return 0;
}

int image_pipeline_downscale_nearest(const ImageBuffer *src, ImageBuffer *dst, const BrowserConfig *cfg)
{
    int target_w = src->width;
    int target_h = src->height;
    int x, y;

    memset(dst, 0, sizeof(*dst));

    if (!cfg->small_images_enabled)
        return -1;

    if (cfg->image_scale_percent > 0 && cfg->image_scale_percent < 100) {
        target_w = (src->width * cfg->image_scale_percent) / 100;
        target_h = (src->height * cfg->image_scale_percent) / 100;
    }

    if (cfg->max_img_width > 0 && target_w > cfg->max_img_width) {
        target_h = (target_h * cfg->max_img_width) / target_w;
        target_w = cfg->max_img_width;
    }
    if (cfg->max_img_height > 0 && target_h > cfg->max_img_height) {
        target_w = (target_w * cfg->max_img_height) / target_h;
        target_h = cfg->max_img_height;
    }

    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;

    dst->width = target_w;
    dst->height = target_h;
    dst->bytes_per_pixel = src->bytes_per_pixel;
    dst->pixels = (unsigned char*)malloc(target_w * target_h * src->bytes_per_pixel);
    if (!dst->pixels)
        return -1;

    for (y = 0; y < target_h; ++y) {
        int sy = (y * src->height) / target_h;
        for (x = 0; x < target_w; ++x) {
            int sx = (x * src->width) / target_w;
            const unsigned char *sp = src->pixels + (sy * src->width + sx) * src->bytes_per_pixel;
            unsigned char *dp = dst->pixels + (y * target_w + x) * src->bytes_per_pixel;
            memcpy(dp, sp, src->bytes_per_pixel);
        }
    }

    return 0;
}

void image_pipeline_free(ImageBuffer *img)
{
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = 0;
    }
}
