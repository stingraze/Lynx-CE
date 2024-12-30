#include <windows.h>    // For Windows CE definitions
#include "winsock2.h"   // Local winsock2.h in the same directory
#include <stdio.h>
#include <string.h>

// Define HTTP port
#define HTTP_PORT 80

// Structure to store discovered links
typedef struct {
    char text[128];
    char url[512];
} Link;

// Structure to store a naive form
typedef struct {
    char method[16];        // e.g., "GET" or "POST"
    char action[512];       // e.g., "http://example.com/search"
    char inputName[64];     // e.g., "q"
    int  found;             // Flag to indicate if a form was found
} FormInfo;

// Global variables to store links and form information
static Link gLinks[100];
static int  gNumLinks = 0;
static FormInfo gForm;

// Minimal 'tolower' replacement for ASCII, without <ctype.h>
static char my_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        c = c + ('a' - 'A');
    }
    return c;
}

// Custom case-insensitive substring search (replaces strcasestr)
static char* my_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;  // If needle is empty, match at start

    for (; *haystack; haystack++) {
        // Compare first characters (case-insensitive)
        if (my_lower(*haystack) == my_lower(*needle)) {
            const char *h = haystack + 1;
            const char *n = needle + 1;
            while (*n && *h && (my_lower(*h) == my_lower(*n))) {
                h++;
                n++;
            }
            if (!*n) {
                // Reached end of needle => match found
                return (char*)haystack;
            }
        }
    }
    return NULL;
}

// Strips out naive HTML tags: everything between < and > is removed
static void strip_html_tags(char *buffer)
{
    char *r = buffer;
    char *w = buffer;
    int in_tag = 0;

    while (*r) {
        if (*r == '<') {
            in_tag = 1;
        } else if (*r == '>') {
            in_tag = 0;
        } else if (!in_tag) {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

// Convert entire string to lower-case using my_lower, in place
static void strlower(char *s)
{
    while (*s) {
        *s = my_lower(*s);
        s++;
    }
}

// Parse out <a href="...">LinkText</a> and store in gLinks[]
// Also parse out <form ...> and <input ...> (very naive)
static void parse_links_and_form(char *html)
{
    gNumLinks = 0;
    memset(&gForm, 0, sizeof(gForm));

    // Scan for <a href="..."> or <form ...> or <input ...>
    char *p = html;

    while (1)
    {
        char *tagStart = strstr(p, "<");
        if (!tagStart) break;
        char *tagEnd = strstr(tagStart, ">");
        if (!tagEnd) break;

        int tagLen = (int)(tagEnd - tagStart + 1);
        if (tagLen < 3) {
            p = tagEnd + 1;
            continue;
        }

        // Extract the tag into a temporary buffer
        char tagBuf[1024];
        int copyLen = (tagLen >= 1023) ? 1023 : tagLen;
        strncpy(tagBuf, tagStart, copyLen);
        tagBuf[copyLen] = '\0';
        strlower(tagBuf);  // Easier to parse in lower-case

        // <a ...
        if (strncmp(tagBuf, "<a ", 3) == 0) {
            // Find href="
            char *hrefPos = strstr(tagBuf, "href=");
            if (hrefPos) {
                // Usually it's href="someURL"
                char *quote1 = strchr(hrefPos, '\"');
                if (quote1) {
                    char *quote2 = strchr(quote1+1, '\"');
                    if (quote2) {
                        int urlLen = (int)(quote2 - (quote1+1));
                        if (urlLen > 0 && urlLen < (int)sizeof(gLinks[gNumLinks].url)-1) {
                            strncpy(gLinks[gNumLinks].url, quote1+1, urlLen);
                            gLinks[gNumLinks].url[urlLen] = '\0';
                        }
                    }
                }
            }
            // Find link text after the tagEnd, up to </a>
            char *linkTextStart = tagEnd + 1;
            char *endTag = my_strcasestr(linkTextStart, "</a>");
            if (endTag) {
                int txtLen = (int)(endTag - linkTextStart);
                if (txtLen < 0) txtLen = 0;
                if (txtLen > (int)sizeof(gLinks[gNumLinks].text)-1) {
                    txtLen = sizeof(gLinks[gNumLinks].text)-1;
                }
                strncpy(gLinks[gNumLinks].text, linkTextStart, txtLen);
                gLinks[gNumLinks].text[txtLen] = '\0';

                // Replace \r or \n with spaces
                for (int i = 0; i < (int)strlen(gLinks[gNumLinks].text); i++) {
                    if (gLinks[gNumLinks].text[i] == '\r' || gLinks[gNumLinks].text[i] == '\n') {
                        gLinks[gNumLinks].text[i] = ' ';
                    }
                }
                gNumLinks++;
                if (gNumLinks >= 100) gNumLinks = 99; // Arbitrary limit
            }
        }
        // <form ...
        else if (strncmp(tagBuf, "<form", 5) == 0) {
            gForm.found = 1;
            // Default to "get"
            strcpy(gForm.method, "get");

            // Parse method=
            char *methodPos = strstr(tagBuf, "method=");
            if (methodPos) {
                char *q1 = strchr(methodPos, '\"');
                if (q1) {
                    char *q2 = strchr(q1+1, '\"');
                    if (q2) {
                        int len = (int)(q2 - (q1+1));
                        if (len > 0 && len < (int)sizeof(gForm.method)) {
                            strncpy(gForm.method, q1+1, len);
                            gForm.method[len] = '\0';
                        }
                        strlower(gForm.method);
                    }
                }
            }
            // Parse action=
            char *actionPos = strstr(tagBuf, "action=");
            if (actionPos) {
                char *q1 = strchr(actionPos, '\"');
                if (q1) {
                    char *q2 = strchr(q1+1, '\"');
                    if (q2) {
                        int len = (int)(q2 - (q1+1));
                        if (len > 0 && len < (int)sizeof(gForm.action)) {
                            strncpy(gForm.action, q1+1, len);
                            gForm.action[len] = '\0';
                        }
                    }
                }
            }
        }
        // <input ...
        else if (strncmp(tagBuf, "<input", 6) == 0 && gForm.found) {
            // Find name=
            char *namePos = strstr(tagBuf, "name=");
            if (namePos) {
                char *q1 = strchr(namePos, '\"');
                if (q1) {
                    char *q2 = strchr(q1+1, '\"');
                    if (q2) {
                        int len = (int)(q2 - (q1+1));
                        if (len > 0 && len < (int)sizeof(gForm.inputName)) {
                            strncpy(gForm.inputName, q1+1, len);
                            gForm.inputName[len] = '\0';
                        }
                    }
                }
            }
        }

        // Move p past this tag
        p = tagEnd + 1;
    }
}

// Minimal function to parse "http://host/path" into host+path.
static int parse_http_url(const char *url, char *host, char *path, int maxLen)
{
    if (strncmp(url, "http://", 7) != 0) {
        return -1; // Only handle http://
    }
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    if (!slash) {
        // No slash => host only
        strncpy(host, p, maxLen);
        host[maxLen-1] = '\0';
        strcpy(path, "/");
    } else {
        int hlen = (int)(slash - p);
        if (hlen <= 0 || hlen >= maxLen) return -1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';

        strncpy(path, slash, maxLen);
        path[maxLen-1] = '\0';
    }
    return 0;
}

// Core fetch function: do HTTP GET or POST, read response, parse links & forms, strip HTML for display.
static void fetch_page(const char *url, const char *postData)
{
    char host[256] = {0};
    char path[512] = {0};

    if (parse_http_url(url, host, path, 256) != 0) {
        printf("Malformed or unsupported URL (only http://...).\n");
        return;
    }

    // Create socket
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        printf("socket() failed.\n");
        return;
    }

    // Resolve host
    struct hostent *he = gethostbyname(host);
    if (!he) {
        printf("gethostbyname() failed for %s\n", host);
        closesocket(s);
        return;
    }

    // Build address
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(HTTP_PORT);
    server.sin_addr.s_addr = *((u_long*)he->h_addr);

    // Connect
    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0) {
        printf("connect() failed.\n");
        closesocket(s);
        return;
    }

    // Build request (POST if postData != NULL, else GET)
    char request[2048];
    if (postData) {
        // POST
        snprintf(request, sizeof(request),
            "POST %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: CE-Lynx/1.0\r\n"
            "Connection: close\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            path, host, (int)strlen(postData), postData
        );
    } else {
        // GET
        snprintf(request, sizeof(request),
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: CE-Lynx/1.0\r\n"
            "Connection: close\r\n\r\n",
            path, host);
    }

    // Send
    if (send(s, request, (int)strlen(request), 0) <= 0) {
        printf("send() failed.\n");
        closesocket(s);
        return;
    }

    // Read the entire response into a large buffer. Very naive.
    static char bigBuf[65536]; // 64 KB max
    int totalReceived = 0;
    while (1) {
        if (totalReceived >= (int)sizeof(bigBuf)-1) break;
        int r = recv(s, bigBuf + totalReceived, (int)(sizeof(bigBuf)-1 - totalReceived), 0);
        if (r <= 0) break;
        totalReceived += r;
    }
    bigBuf[totalReceived] = '\0';
    closesocket(s);

    // Skip headers
    char *body = strstr(bigBuf, "\r\n\r\n");
    if (!body) {
        printf("No HTTP body found.\n");
        return;
    }
    body += 4;

    // 1) Parse links and form info
    parse_links_and_form(body);

    // 2) Make a copy for display, removing HTML tags
    static char displayBuf[65536];
    strncpy(displayBuf, body, sizeof(displayBuf)-1);
    displayBuf[sizeof(displayBuf)-1] = '\0';

    strip_html_tags(displayBuf);

    // 3) Print
    printf("----- Page Text -----\n%s\n----- End -----\n", displayBuf);
}

// Global variable storing the "current URL" so user can follow links easily.
static char gCurrentURL[512] = "";  // Start with empty URL

// Make absolute URL if link is relative. If link starts with "http://" we keep it.
static void make_absolute_url(const char *baseURL, const char *link, char *out, int outSize)
{
    if (strncmp(link, "http://", 7) == 0) {
        // Already absolute
        strncpy(out, link, outSize);
        out[outSize-1] = '\0';
    } else {
        // Parse base, extract host, then append link
        char baseHost[256], basePath[512];
        if (parse_http_url(baseURL, baseHost, basePath, 256) == 0) {
            snprintf(out, outSize, "http://%s%s", baseHost, link);
        } else {
            // Fallback
            strncpy(out, link, outSize);
            out[outSize-1] = '\0';
        }
    }
}

// Interactive loop
int main(void)
{
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    printf("Welcome to CE-Lynx Advanced Demo (No Automatic Navigation)\n");
    printf("Commands:\n");
    printf("  g = Go to a new URL\n");
    printf("  l = List discovered links on current page, pick one to follow\n");
    printf("  f = If there's a form, fill text input & submit\n");
    printf("  q = Quit\n");

    printf("\nPress 'g' to enter a URL or 'q' to quit.\n");

    while (1) {
        printf("\nCurrent URL: %s\n", gCurrentURL[0] ? gCurrentURL : "None");
        printf("Command (g/l/f/q) > ");
        fflush(stdout);

        int c = getchar();
        // Discard the rest of the line
        while (getchar() != '\n') { /* discard */ }

        if (c == 'q' || c == 'Q') {
            printf("Bye!\n");
            break;
        }
        else if (c == 'g' || c == 'G') {
            // Prompt for new URL
            printf("Enter URL (http://...): ");
            fflush(stdout);
            char url[512];
            if (!fgets(url, sizeof(url), stdin)) {
                printf("Error reading URL.\n");
                continue;
            }
            url[strcspn(url, "\r\n")] = '\0'; // Remove trailing newline
            if (url[0] == '\0') {
                printf("Empty URL entered.\n");
                continue;
            }

            strncpy(gCurrentURL, url, sizeof(gCurrentURL));
            gCurrentURL[sizeof(gCurrentURL)-1] = '\0';

            fetch_page(gCurrentURL, NULL);
        }
        else if (c == 'l' || c == 'L') {
            // List links
            if (gNumLinks == 0) {
                printf("No links found on this page.\n");
            } else {
                for (int i = 0; i < gNumLinks; i++) {
                    printf("[%d] %s => %s\n", i+1, gLinks[i].text, gLinks[i].url);
                }
                printf("Enter link number to follow: ");
                fflush(stdout);

                char buf[32];
                if (!fgets(buf, sizeof(buf), stdin)) continue;
                int choice = atoi(buf);
                if (choice < 1 || choice > gNumLinks) {
                    printf("Invalid link index.\n");
                } else {
                    char absURL[512];
                    make_absolute_url(gCurrentURL, gLinks[choice-1].url, absURL, sizeof(absURL));
                    strncpy(gCurrentURL, absURL, sizeof(gCurrentURL));
                    gCurrentURL[sizeof(gCurrentURL)-1] = '\0';
                    fetch_page(gCurrentURL, NULL);
                }
            }
        }
        else if (c == 'f' || c == 'F') {
            // If we found a form
            if (!gForm.found) {
                printf("No form found on this page.\n");
            } else {
                printf("Form method=%s, action=%s\n", gForm.method, gForm.action);
                printf("Input name=%s\n", gForm.inputName);
                printf("Enter text for input: ");
                fflush(stdout);

                char userText[256];
                if (!fgets(userText, sizeof(userText), stdin)) {
                    printf("Error reading input.\n");
                    continue;
                }
                userText[strcspn(userText, "\r\n")] = '\0'; // Remove trailing newline

                // Naive URL-encode: replace ' ' with '+', non-alnum with %XX
                char enc[512] = {0};
                int idx = 0;
                for (int i = 0; i < (int)strlen(userText) && idx < (int)sizeof(enc)-4; i++) {
                    char ch = userText[i];
                    if (ch == ' ') {
                        enc[idx++] = '+';
                    }
                    else if ((ch >= '0' && ch <= '9') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= 'a' && ch <= 'z'))
                    {
                        enc[idx++] = ch;
                    } else {
                        // Do %XX
                        idx += snprintf(enc+idx, sizeof(enc)-idx, "%%%02X", (unsigned char)ch);
                    }
                }

                // Make absolute action URL
                char fullURL[512];
                make_absolute_url(gCurrentURL, gForm.action, fullURL, sizeof(fullURL));

                // Determine if GET or POST
                if (strcmp(gForm.method, "post") == 0) {
                    // POST: inputName=enc
                    char postData[512];
                    snprintf(postData, sizeof(postData), "%s=%s", gForm.inputName, enc);
                    strncpy(gCurrentURL, fullURL, sizeof(gCurrentURL));
                    gCurrentURL[sizeof(gCurrentURL)-1] = '\0';
                    fetch_page(gCurrentURL, postData);
                } else {
                    // Assume GET: append ?inputName=enc
                    char getURL[512];
                    snprintf(getURL, sizeof(getURL), "%s?%s=%s", fullURL, gForm.inputName, enc);
                    strncpy(gCurrentURL, getURL, sizeof(gCurrentURL));
                    gCurrentURL[sizeof(gCurrentURL)-1] = '\0';
                    fetch_page(gCurrentURL, NULL);
                }
            }
        }
        else {
            printf("Unknown command.\n");
        }
    }

    WSACleanup();
    return 0;
}

