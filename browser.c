#include <windows.h>     // Windows CE definitions
#include "winsock2.h"    // Local header, same directory
#include <stdio.h>
#include <string.h>

/*
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

#define HTTP_PORT 80

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
// A very naive parser for URLs of the form http://host/path
// Puts the host part in 'host[]' and path part in 'path[]'.
static int parse_http_url(const char *url, char *host, char *path, int maxLen)
{
    // Check "http://"
    if (strncmp(url, "http://", 7) != 0)
    {
        // We only handle plain "http://" in this sample
        return -1;
    }

    // Move pointer past "http://"
    const char *p = url + 7;

    // Find first slash that starts the path
    const char *slash = strchr(p, '/');
    if (!slash)
    {
        // No slash => only a hostname
        strncpy(host, p, maxLen);
        host[maxLen - 1] = '\0';
        strcpy(path, "/");
    }
    else
    {
        // Copy the host portion
        int hostLen = (int)(slash - p);
        if (hostLen <= 0 || hostLen >= maxLen)
            return -1;

        strncpy(host, p, hostLen);
        host[hostLen] = '\0';

        // Copy remainder as the path
        strncpy(path, slash, maxLen);
        path[maxLen - 1] = '\0';
    }

    return 0;
}

//------------------------------------------------------------------------------
// Sends a GET request to the given URL, reads the response, strips HTML, prints text.
static void fetch_url(const char *url)
{
    char host[256] = {0};
    char path[512] = {0};

    if (parse_http_url(url, host, path, 256) != 0)
    {
        printf("Error: Malformed or unsupported URL. Try something like http://example.com/\n");
        return;
    }

    // Create a TCP socket
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        printf("socket() failed.\n");
        return;
    }

    // Resolve the hostname
    struct hostent *he = gethostbyname(host);
    if (!he)
    {
        printf("gethostbyname() failed for %s\n", host);
        closesocket(s);
        return;
    }

    // Build the server address
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(HTTP_PORT);
    server.sin_addr.s_addr = *((u_long*)he->h_addr);

    // Connect
    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0)
    {
        printf("connect() failed.\n");
        closesocket(s);
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
    if (send(s, request, (int)strlen(request), 0) <= 0)
    {
        printf("send() failed.\n");
        closesocket(s);
        return;
    }

    // Read the response, strip headers, remove HTML tags
    int headers_done = 0;
    char buffer[1024];
    int received;

    while ((received = recv(s, buffer, sizeof(buffer) - 1, 0)) > 0)
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

    closesocket(s);
}

//------------------------------------------------------------------------------
// Main function: a simple loop with commands: 'g' to go, 'q' to quit.
int main(void)
{
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
    {
        printf("WSAStartup failed.\n");
        return 1;
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

