#include <windows.h>     // Windows CE definitions
#include "winsock2.h"    // Local header, same directory
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "image_pipeline.h"

#define HTTP_PORT 80

static BrowserConfig g_browser_config = {100, 0, 0, 0};

static void strip_html_tags(char *buffer)
{
    char *r = buffer;
    char *w = buffer;
    int in_tag = 0;

    while (*r)
    {
        if (*r == '<') in_tag = 1;
        else if (*r == '>') in_tag = 0;
        else if (!in_tag) *w++ = *r;
        r++;
    }
    *w = '\0';
}

static int parse_http_url(const char *url, char *host, char *path, int maxLen)
{
    if (strncmp(url, "http://", 7) != 0) return -1;

    {
        const char *p = url + 7;
        const char *slash = strchr(p, '/');
        if (!slash)
        {
            strncpy(host, p, maxLen);
            host[maxLen - 1] = '\0';
            strcpy(path, "/");
        }
        else
        {
            int hostLen = (int)(slash - p);
            if (hostLen <= 0 || hostLen >= maxLen) return -1;
            strncpy(host, p, hostLen);
            host[hostLen] = '\0';
            strncpy(path, slash, maxLen);
            path[maxLen - 1] = '\0';
        }
    }
    return 0;
}

static void process_image_refs(const char *base_url, const char *html)
{
    const char *p = html;
    while ((p = strstr(p, "<img")) != 0)
    {
        const char *src = strstr(p, "src=");
        if (!src) { p += 4; continue; }
        src += 4;

        while (*src == ' ' || *src == '\t') src++;

        {
            char quote = 0;
            char img_url[512];
            int i = 0;
            if (*src == '"' || *src == '\'') {
                quote = *src;
                src++;
            }

            while (*src && i < (int)sizeof(img_url) - 1)
            {
                if (quote) {
                    if (*src == quote) break;
                } else {
                    if (*src == ' ' || *src == '>' || *src == '\t') break;
                }
                img_url[i++] = *src++;
            }
            img_url[i] = '\0';

            if (strncmp(img_url, "http://", 7) == 0)
            {
                ImageBuffer src_img;
                ImageBuffer out_img;
                printf("\n[image] %s\n", img_url);

                if (image_pipeline_fetch_bmp(img_url, &src_img) == 0)
                {
                    printf("  decoded BMP: %dx%d\n", src_img.width, src_img.height);
                    if (g_browser_config.small_images_enabled)
                    {
                        if (image_pipeline_downscale_nearest(&src_img, &out_img, &g_browser_config) == 0)
                        {
                            printf("  downscaled: %dx%d (nearest-neighbor)\n", out_img.width, out_img.height);
                            image_pipeline_free(&out_img);
                        }
                        else
                        {
                            printf("  downscale skipped/failed\n");
                        }
                    }
                    else
                    {
                        printf("  downscale disabled (use --small-images)\n");
                    }
                    image_pipeline_free(&src_img);
                }
                else
                {
                    printf("  fetch/decode failed (currently supports HTTP 24-bit BMP only)\n");
                }
            }
            else
            {
                printf("\n[image] skipped non-absolute or non-http src from %s\n", base_url);
            }
        }
        p += 4;
    }
}

static void fetch_url(const char *url)
{
    char host[256] = {0};
    char path[512] = {0};

    if (parse_http_url(url, host, path, 256) != 0)
    {
        printf("Error: Malformed or unsupported URL. Try something like http://example.com/\n");
        return;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        printf("socket() failed.\n");
        return;
    }

    struct hostent *he = gethostbyname(host);
    if (!he)
    {
        printf("gethostbyname() failed for %s\n", host);
        closesocket(s);
        return;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(HTTP_PORT);
    server.sin_addr.s_addr = *((u_long*)he->h_addr);

    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0)
    {
        printf("connect() failed.\n");
        closesocket(s);
        return;
    }

    {
        char request[1024];
        snprintf(request, sizeof(request),
                "GET %s HTTP/1.0\r\n"
                "Host: %s\r\n"
                "Connection: close\r\n"
                "User-Agent: CE-Lynx/1.0\r\n\r\n",
                path, host);
        if (send(s, request, (int)strlen(request), 0) <= 0)
        {
            printf("send() failed.\n");
            closesocket(s);
            return;
        }
    }

    {
        int headers_done = 0;
        char buffer[1024];
        int received;
        char html_capture[4096];
        int html_used = 0;
        html_capture[0] = '\0';

        while ((received = recv(s, buffer, sizeof(buffer) - 1, 0)) > 0)
        {
            buffer[received] = '\0';

            if (!headers_done)
            {
                char *body = strstr(buffer, "\r\n\r\n");
                if (body)
                {
                    int remain;
                    headers_done = 1;
                    body += 4;
                    remain = (int)strlen(body);
                    if (html_used + remain < (int)sizeof(html_capture) - 1) {
                        strcpy(html_capture + html_used, body);
                        html_used += remain;
                    }
                    strip_html_tags(body);
                    printf("%s", body);
                }
            }
            else
            {
                int remain = (int)strlen(buffer);
                if (html_used + remain < (int)sizeof(html_capture) - 1) {
                    strcpy(html_capture + html_used, buffer);
                    html_used += remain;
                }
                strip_html_tags(buffer);
                printf("%s", buffer);
            }
        }

        printf("\n");
        if (html_used > 0)
        {
            process_image_refs(url, html_capture);
        }
    }

    closesocket(s);
}

static int parse_cli_flags(int argc, char **argv, BrowserConfig *cfg)
{
    int i;
    for (i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (strcmp(arg, "--small-images") == 0)
        {
            cfg->small_images_enabled = 1;
            if (cfg->image_scale_percent <= 0 || cfg->image_scale_percent > 100)
                cfg->image_scale_percent = 50;
        }
        else if (strncmp(arg, "--image-scale=", 14) == 0)
        {
            int val = atoi(arg + 14);
            if (val < 1 || val > 100) return -1;
            cfg->image_scale_percent = val;
            cfg->small_images_enabled = 1;
        }
        else if (strncmp(arg, "--max-img-width=", 16) == 0)
        {
            int val = atoi(arg + 16);
            if (val < 1) return -1;
            cfg->max_img_width = val;
        }
        else if (strncmp(arg, "--max-img-height=", 17) == 0)
        {
            int val = atoi(arg + 17);
            if (val < 1) return -1;
            cfg->max_img_height = val;
        }
        else
        {
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    WSADATA wsa;
    if (parse_cli_flags(argc, argv, &g_browser_config) != 0)
    {
        printf("Usage:\n");
        printf("  browser-lynx.exe [--small-images] [--image-scale=<1-100>] [--max-img-width=<n>] [--max-img-height=<n>]\n");
        return 1;
    }

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
    {
        printf("WSAStartup failed.\n");
        return 1;
    }

    printf("Minimal CE-Lynx Demo\n");
    printf("Image pipeline: %s, scale=%d%%, max=%dx%d\n",
        g_browser_config.small_images_enabled ? "ON" : "OFF",
        g_browser_config.image_scale_percent,
        g_browser_config.max_img_width,
        g_browser_config.max_img_height);
    printf("Press 'g' to enter a URL, or 'q' to quit.\n");

    while (1)
    {
        int c;
        printf("\nCommand (g=Go, q=Quit): ");
        c = getchar();
        while (getchar() != '\n') { }

        if (c == 'q' || c == 'Q')
        {
            printf("Bye!\n");
            break;
        }
        else if (c == 'g' || c == 'G')
        {
            char url[512];
            size_t len;
            printf("Enter URL (e.g. http://example.com/): ");
            fflush(stdout);

            if (fgets(url, sizeof(url), stdin) == NULL)
            {
                printf("Error reading URL.\n");
                continue;
            }

            len = strlen(url);
            if (len > 0 && url[len - 1] == '\n')
                url[len - 1] = '\0';

            fetch_url(url);
        }
        else
        {
            printf("Unknown command '%c'\n", c);
        }
    }

    WSACleanup();
    return 0;
}
