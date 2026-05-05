#include <windows.h>     // Windows CE definitions
#include "winsock2.h"    // Local header, same directory
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net_transport.h"

/*
 (C)Tsubasa Kato - Inspire Search Corporation - 2024
 Lynx-CE Ver. 0.01
 * Minimal text browser for Windows CE with a Lynx-like prompt:
 *   - Press 'g' to type a URL: e.g. http://example.com/
 *   - Press 'q' to quit
 *
 * This code uses no explicit '-lws2' or '-lcoredll'. We assume the CeGCC
 * environment links them automatically. If it doesn't, you'll have to add them.
 *
 * If you see "not a valid application" on the device, you likely have
 * an architecture or subsystem mismatch. See notes at the end.
 */


//------------------------------------------------------------------------------
// Strips out everything between '<' and '>'. Very naive HTML removal.
static void strip_html_tags(char *buffer)
{
    char *r = buffer;
    char *w = buffer;
    int in_tag = 0;

    while (*r)
    {
        if (*r == '<')
        {
            in_tag = 1;
        }
        else if (*r == '>')
        {
            in_tag = 0;
        }
        else if (!in_tag)
        {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

//------------------------------------------------------------------------------
// URL parser for http:// and https://
static int parse_url(const char *url, net_scheme_t *scheme, char *host, int hostLen,
                     char *path, int pathLen, unsigned short *port)
{
    const char *p = NULL;
    const char *slash = NULL;
    const char *colon = NULL;
    char hostport[256] = {0};

    if (strncmp(url, "https://", 8) == 0) {
        *scheme = NET_SCHEME_HTTPS;
        *port = 443;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        *scheme = NET_SCHEME_HTTP;
        *port = 80;
        p = url + 7;
    } else {
        return -1;
    }

    slash = strchr(p, '/');
    if (!slash) {
        strncpy(hostport, p, sizeof(hostport) - 1);
        strcpy(path, "/");
    } else {
        int hpLen = (int)(slash - p);
        if (hpLen <= 0 || hpLen >= (int)sizeof(hostport)) return -1;
        strncpy(hostport, p, hpLen);
        hostport[hpLen] = '\0';
        strncpy(path, slash, pathLen - 1);
        path[pathLen - 1] = '\0';
    }

    colon = strchr(hostport, ':');
    if (colon) {
        int hLen = (int)(colon - hostport);
        if (hLen <= 0 || hLen >= hostLen) return -1;
        strncpy(host, hostport, hLen);
        host[hLen] = '\0';
        *port = (unsigned short)atoi(colon + 1);
    } else {
        strncpy(host, hostport, hostLen - 1);
        host[hostLen - 1] = '\0';
    }

    return 0;
}

//------------------------------------------------------------------------------
// Sends a GET request to the given URL, reads the response, strips HTML, prints text.
static void fetch_url(const char *url, const net_tls_options_t *tls_opts)
{
    char host[256] = {0};
    char path[512] = {0};
    unsigned short port = 0;
    net_scheme_t scheme;

    if (parse_url(url, &scheme, host, sizeof(host), path, sizeof(path), &port) != 0)
    {
        printf("Error: Malformed or unsupported URL. Try http://example.com/ or https://example.com/\n");
        return;
    }

    net_tls_options_t effective_tls = *tls_opts;
    effective_tls.server_name = host;

    net_transport_t transport;
    if (net_transport_connect(&transport, host, port, scheme, &effective_tls) != 0)
    {
        printf("connect() failed.\n");
        return;
    }

    // Build a minimal HTTP GET request
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "User-Agent: CE-Lynx/1.0\r\n\r\n",
             path, host);

    // Send it
    if (net_transport_send(&transport, request, (int)strlen(request)) <= 0)
    {
        printf("send() failed.\n");
        net_transport_close(&transport);
        return;
    }

    // Read the response, strip headers, remove HTML tags
    int headers_done = 0;
    char buffer[1024];
    int received;

    while ((received = net_transport_recv(&transport, buffer, sizeof(buffer) - 1)) > 0)
    {
        buffer[received] = '\0';

        if (!headers_done)
        {
            // Look for "\r\n\r\n" to mark end of headers
            char *body = strstr(buffer, "\r\n\r\n");
            if (body)
            {
                headers_done = 1;
                body += 4;  // skip the \r\n\r\n
                strip_html_tags(body);
                printf("%s", body);
            }
            // If we haven't seen headers end yet, do nothing with partial data
        }
        else
        {
            // Already reading body
            strip_html_tags(buffer);
            printf("%s", buffer);
        }
    }

    printf("\n");  // extra newline after printing

    net_transport_close(&transport);
}

//------------------------------------------------------------------------------
// Main function: a simple loop with commands: 'g' to go, 'q' to quit.
int main(int argc, char **argv)
{
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
    {
        printf("WSAStartup failed.\n");
        return 1;
    }

    net_tls_options_t tls_opts;
    memset(&tls_opts, 0, sizeof(tls_opts));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tls-insecure") == 0) tls_opts.tls_insecure = 1;
        else if (strncmp(argv[i], "--ca-bundle=", 12) == 0) tls_opts.ca_bundle_path = argv[i] + 12;
    }

    printf("Minimal CE-Lynx Demo\n");
    printf("Press 'g' to enter a URL, or 'q' to quit.\n");

    while (1)
    {
        printf("\nCommand (g=Go, q=Quit): ");

        int c = getchar();
        // Clear out any trailing chars up to newline
        while (getchar() != '\n') { /* discard extra input */ }

        if (c == 'q' || c == 'Q')
        {
            printf("Bye!\n");
            break;
        }
        else if (c == 'g' || c == 'G')
        {
            // Prompt user for a URL
            char url[512];
            printf("Enter URL (e.g. http://example.com/): ");
            fflush(stdout);

            // Grab a line
            if (fgets(url, sizeof(url), stdin) == NULL)
            {
                printf("Error reading URL.\n");
                continue;
            }

            // Remove any trailing newline
            size_t len = strlen(url);
            if (len > 0 && url[len - 1] == '\n')
                url[len - 1] = '\0';

            // Attempt to fetch
            fetch_url(url, &tls_opts);
        }
        else
        {
            printf("Unknown command '%c'\n", c);
        }
    }

    WSACleanup();
    return 0;
}

