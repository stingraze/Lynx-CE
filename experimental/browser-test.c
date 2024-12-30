#include <windows.h>    // For Windows CE definitions
#include "winsock2.h"   // Local winsock2.h in the same directory
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*
 (C)Tsubasa Kato - Inspire Search Corporation - 2024/12/30
 Coded with the help of ChatGPT - o1.
 * A more advanced text-based "browser" for Windows CE (or Win32) demonstrating:
 *   - Link parsing: <a href="...">some text</a>
 *   - Naive form parsing: <form method="..." action="..."> + <input name="..." type="text">
 *   - Navigation: g=Go URL, l=List links, pick link, f=Fill form, q=Quit
 *
 * This is extremely naive. Real HTML is much more complex.
 * Also, we only do plain HTTP, no HTTPS. We do not handle chunked
 * encoding, redirection, cookies, etc.
 *
 * Compile with CeGCC (ARM example):
 *   arm-mingw32ce-gcc -Wall -I. -o browser.exe browser.c
 * If you get link errors, try adding -lws2 or -lcoredll.
 * If you get "not a valid application" on the device, it's likely
 * a CPU or subsystem mismatch.
 */

#define HTTP_PORT 80

// A small struct to store discovered links
typedef struct {
    char text[128];
    char url[512];
} Link;

// A small struct to store an extremely naive form
typedef struct {
    char method[16];        // e.g. "GET" or "POST"
    char action[512];       // e.g. "http://example.com/search"
    char inputName[64];     // e.g. "q"
    int  found;             // Did we find a form or not?
} FormInfo;

// We store discovered links in an array
static Link gLinks[100];
static int  gNumLinks = 0;

// We store a single naive form
static FormInfo gForm;

//------------------------------------------------------------------------------
// Strips out naive HTML tags: everything between < and > is removed.
// We do not preserve <a> or <form> etc. for display, but we do parse them separately.
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
// Convert a string to lowercase (in-place).
static void strlower(char *s)
{
    while (*s) {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

//------------------------------------------------------------------------------
// Parse out <a href="..."> LinkText </a> and store in gLinks[].
// Also parse out <form ...> and <input ...> (very naive).
// Then we can strip out everything else for display.
static void parse_links_and_form(char *html)
{
    gNumLinks = 0;
    memset(&gForm, 0, sizeof(gForm));

    // A naive approach: we scan for <a href="..."> or <form ...> or <input ...>
    // We'll do this in a loop. This is not a real HTML parser. It's extremely naive.
    char *p = html;

    while (1)
    {
        char *tagStart = strstr(p, "<");
        if (!tagStart) break;
        char *tagEnd = strstr(tagStart, ">");
        if (!tagEnd) break;

        // We'll examine the substring [tagStart, tagEnd]
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
        strlower(tagBuf);  // make it easier to parse

        // Check if it's <a ...
        if (strncmp(tagBuf, "<a ", 3) == 0) {
            // find href="
            char *hrefPos = strstr(tagBuf, "href=");
            if (hrefPos) {
                // Usually it's href="someURL"
                char *quote1 = strchr(hrefPos, '\"');
                if (quote1) {
                    char *quote2 = strchr(quote1+1, '\"');
                    if (quote2) {
                        // We found the URL between quote1+1 and quote2
                        int urlLen = (int)(quote2 - (quote1+1));
                        if (urlLen > 0 && urlLen < (int)sizeof(gLinks[gNumLinks].url)-1) {
                            strncpy(gLinks[gNumLinks].url, quote1+1, urlLen);
                            gLinks[gNumLinks].url[urlLen] = '\0';
                        }
                    }
                }
            }
            // next, we find the link text by searching after the > up to </a>
            // but we only do a naive approach: skip the entire tag, then read until we see "</a>"
            // So let's do that:
            char *linkTextStart = tagEnd + 1;
            char *endTag = strcasestr(linkTextStart, "</a>");
            if (endTag) {
                int txtLen = (int)(endTag - linkTextStart);
                if (txtLen < 0) txtLen = 0;
                if (txtLen > (int)sizeof(gLinks[gNumLinks].text)-1) {
                    txtLen = sizeof(gLinks[gNumLinks].text)-1;
                }
                strncpy(gLinks[gNumLinks].text, linkTextStart, txtLen);
                gLinks[gNumLinks].text[txtLen] = '\0';
                // strip newlines from link text
                // also strip leading/trailing spaces
                // or we can do something naive:
                // We'll just do it minimal:
                for (int i = 0; i < (int)strlen(gLinks[gNumLinks].text); i++) {
                    if (gLinks[gNumLinks].text[i] == '\r' ||
                        gLinks[gNumLinks].text[i] == '\n')
                    {
                        gLinks[gNumLinks].text[i] = ' ';
                    }
                }
                // We advanced one link
                gNumLinks++;
                if (gNumLinks >= 100) gNumLinks = 99;  // arbitrary limit
            }
        }
        // Check if it's <form ...
        else if (strncmp(tagBuf, "<form", 5) == 0) {
            // find method= and action=
            // e.g. <form method="get" action="http://example.com/search">
            gForm.found = 1;
            // default to GET if not found
            strcpy(gForm.method, "get");
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
        // Check if it's <input ...
        // We'll only parse name= if type=text or omitted
        else if (strncmp(tagBuf, "<input", 6) == 0 && gForm.found) {
            // find name=
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
            // For full correctness, we'd also check type="text", but let's skip.
        }

        // Move p past this tag
        p = tagEnd + 1;
    }
}

//------------------------------------------------------------------------------
// Minimal function to parse a URL like "http://example.com/path" into host+path.
static int parse_http_url(const char *url, char *host, char *path, int maxLen)
{
    if (strncmp(url, "http://", 7) != 0) {
        return -1; // only handle http://
    }
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    if (!slash) {
        // no slash => host only
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

//------------------------------------------------------------------------------
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

    // Build request
    // If postData != NULL, we do a POST, else GET.
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

    // We'll read the entire response into a large buffer. This is naive, but workable for a demo.
    static char bigBuf[65536]; // 64 KB max, watch out
    int totalReceived = 0;
    while (1) {
        if (totalReceived >= (int)sizeof(bigBuf)-1) break;
        int r = recv(s, bigBuf + totalReceived, (int)(sizeof(bigBuf)-1 - totalReceived), 0);
        if (r <= 0) break;
        totalReceived += r;
    }
    bigBuf[totalReceived] = '\0';

    closesocket(s);

    // We want to skip headers. Look for \r\n\r\n
    char *body = strstr(bigBuf, "\r\n\r\n");
    if (!body) {
        printf("No HTTP body found.\n");
        return;
    }
    body += 4;

    // 1) Parse links and form info from the raw HTML
    parse_links_and_form(body);

    // 2) For display, we do a copy, strip tags
    //    (But we just reuse "body" in place, or better copy it so we don't lose link parse data)
    static char displayBuf[65536];
    strncpy(displayBuf, body, sizeof(displayBuf)-1);
    displayBuf[sizeof(displayBuf)-1] = '\0';

    strip_html_tags(displayBuf);

    // 3) Print the text
    printf("----- Page Text -----\n%s\n----- End -----\n", displayBuf);
}

//------------------------------------------------------------------------------
// Global variable storing the "current URL" so user can follow links easily.
static char gCurrentURL[512] = "http://example.com/";

//------------------------------------------------------------------------------
// A naive function to resolve relative links (e.g. "about.html") into an absolute "http://host/about.html" 
// If link starts with "http://" we keep it. Otherwise, we just append to current host. 
static void make_absolute_url(const char *baseURL, const char *link, char *out, int outSize)
{
    if (strncmp(link, "http://", 7) == 0) {
        // Already absolute
        strncpy(out, link, outSize);
        out[outSize-1] = '\0';
    } else {
        // We'll parse base, extract host, then append link
        // Real code handles directories, etc. We'll do naive approach: base always is "http://host"
        char baseHost[256], basePath[512];
        if (parse_http_url(baseURL, baseHost, basePath, 256) == 0) {
            // e.g. "http://example.com/..."
            snprintf(out, outSize, "http://%s%s", baseHost, link);
        } else {
            // fallback
            strncpy(out, link, outSize);
            out[outSize-1] = '\0';
        }
    }
}

//------------------------------------------------------------------------------
// Let's do the main interactive loop
int main(void)
{
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed.\n");
        return 1;
    }

    printf("Welcome to CE-Lynx Advanced Demo\n");
    printf("Commands:\n");
    printf("  g = Go to a new URL\n");
    printf("  l = List discovered links on current page, pick one to follow\n");
    printf("  f = If there's a form, fill text input & submit\n");
    printf("  q = Quit\n");

    // Start by fetching gCurrentURL
    fetch_page(gCurrentURL, NULL);

    while (1) {
        printf("\nCurrent URL: %s\n", gCurrentURL);
        printf("Command (g/l/f/q) > ");
        fflush(stdout);

        int c = getchar();
        // discard extra input until newline
        while (getchar() != '\n') { /* discard */ }

        if (c == 'q' || c == 'Q') {
            printf("Bye!\n");
            break;
        }
        else if (c == 'g' || c == 'G') {
            // Prompt for a new URL
            printf("Enter URL (http://...): ");
            fflush(stdout);
            char url[512];
            if (!fgets(url, sizeof(url), stdin)) {
                continue;
            }
            url[strcspn(url, "\r\n")] = '\0'; // remove newline
            if (url[0] == '\0') continue;

            strncpy(gCurrentURL, url, sizeof(gCurrentURL));
            gCurrentURL[sizeof(gCurrentURL)-1] = '\0';

            fetch_page(gCurrentURL, NULL);
        }
        else if (c == 'l' || c == 'L') {
            // List links, pick one
            if (gNumLinks == 0) {
                printf("No links found on this page.\n");
            } else {
                // display them
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
                    // Make absolute
                    char absURL[512];
                    make_absolute_url(gCurrentURL, gLinks[choice-1].url, absURL, sizeof(absURL));
                    strncpy(gCurrentURL, absURL, sizeof(gCurrentURL));
                    fetch_page(gCurrentURL, NULL);
                }
            }
        }
        else if (c == 'f' || c == 'F') {
            // Check if there's a found form (gForm.found)
            if (!gForm.found) {
                printf("No form found on this page.\n");
            } else {
                // We have gForm.method, gForm.action, gForm.inputName
                // Prompt user for text input
                printf("Form method=%s, action=%s\n", gForm.method, gForm.action);
                printf("Input name=%s\n", gForm.inputName);
                printf("Enter text for input: ");
                fflush(stdout);
                char userText[256];
                if (!fgets(userText, sizeof(userText), stdin)) continue;
                userText[strcspn(userText, "\r\n")] = '\0';

                // If GET: we build ?inputName=urlEncoded(userText)
                // If POST: we build post body inputName=urlEncoded(userText)
                // We'll do minimal URL encoding (replace ' ' with '+', skip others).
                char enc[512] = {0};
                int idx = 0;
                for (int i = 0; i < (int)strlen(userText) && idx < (int)sizeof(enc)-4; i++) {
                    char ch = userText[i];
                    if (ch == ' ') {
                        enc[idx++] = '+';
                    } else if (isalnum((unsigned char)ch)) {
                        enc[idx++] = ch;
                    } else {
                        // do %XX?
                        idx += snprintf(enc+idx, sizeof(enc)-idx, "%%%02X", (unsigned char)ch);
                    }
                }

                char fullURL[512];
                make_absolute_url(gCurrentURL, gForm.action, fullURL, sizeof(fullURL));

                if (strcmp(gForm.method, "post") == 0) {
                    // e.g. inputName=enc
                    char postData[512];
                    snprintf(postData, sizeof(postData), "%s=%s", gForm.inputName, enc);
                    // fetch with POST
                    strncpy(gCurrentURL, fullURL, sizeof(gCurrentURL));
                    gCurrentURL[sizeof(gCurrentURL)-1] = '\0';
                    fetch_page(gCurrentURL, postData);
                } else {
                    // assume GET
                    // e.g. fullURL + ?inputName=enc
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
