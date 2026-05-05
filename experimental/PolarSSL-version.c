#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/x509_crt.h"
#include "polarssl/havege.h"

static void print_usage(const char *prog)
{
    printf("Usage: %s [--tls-insecure] [--ca-bundle=<path>] <url>\n", prog);
}

int main(int argc, char *argv[])
{
    const char *url = NULL;
    const char *ca_bundle = NULL;
    int tls_insecure = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tls-insecure") == 0) tls_insecure = 1;
        else if (strncmp(argv[i], "--ca-bundle=", 12) == 0) ca_bundle = argv[i] + 12;
        else url = argv[i];
    }

    if (!url) {
        print_usage(argv[0]);
        return -1;
    }

    const char *p = NULL;
    char host[256] = {0};
    char path[256] = "/";
    int port = 80;
    int is_https = 0;

    if (!strncmp(url, "https://", 8)) { is_https = 1; p = url + 8; port = 443; }
    else if (!strncmp(url, "http://", 7)) { p = url + 7; }
    else { p = url; }

    const char *slash = strchr(p, '/');
    if (slash) {
        int len = (int)(slash - p);
        if (len >= (int)sizeof(host)) len = sizeof(host) - 1;
        strncpy(host, p, len);
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
    }

    net_context server_fd;
    int ret;
    char port_str[16];
    sprintf(port_str, "%d", port);
    if ((ret = net_connect(&server_fd, host, port_str)) != 0) {
        printf("Failed to connect: -0x%04x\n", -ret);
        return -1;
    }

    if (!is_https) {
        char request[512];
        snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
        net_send(&server_fd, (unsigned char*)request, strlen(request));
        char buf[512];
        while ((ret = net_recv(&server_fd, (unsigned char*)buf, sizeof(buf)-1)) > 0) {
            buf[ret] = 0; printf("%s", buf);
        }
        net_close(&server_fd);
        return 0;
    }

    ssl_context ssl;
    havege_state hs;
    x509_crt cacert;

    ssl_init(&ssl);
    havege_init(&hs);
    x509_crt_init(&cacert);

    if (ca_bundle && *ca_bundle) {
        ret = x509_crt_parse_file(&cacert, ca_bundle);
        if (ret < 0) {
            printf("Failed to load CA bundle: %s (ret=-0x%04x)\n", ca_bundle, -ret);
        }
    }

    ssl_set_endpoint(&ssl, SSL_IS_CLIENT);
    ssl_set_rng(&ssl, havege_rand, &hs);
    ssl_set_bio(&ssl, net_recv, &server_fd, net_send, &server_fd);
    ssl_set_ciphersuites(&ssl, ssl_list_ciphersuites());
    ssl_set_ca_chain(&ssl, &cacert, NULL, host);
    ssl_set_authmode(&ssl, tls_insecure ? SSL_VERIFY_OPTIONAL : SSL_VERIFY_REQUIRED);

    while ((ret = ssl_handshake(&ssl)) != 0) {
        if (ret != POLARSSL_ERR_NET_WANT_READ && ret != POLARSSL_ERR_NET_WANT_WRITE) {
            printf("ssl_handshake() failed: -0x%04x\n", -ret);
            net_close(&server_fd);
            return -1;
        }
    }

    if (!tls_insecure) {
        int flags = ssl_get_verify_result(&ssl);
        if (flags != 0) {
            printf("Certificate verification failed (flags=0x%08x)\n", flags);
            ssl_close_notify(&ssl);
            net_close(&server_fd);
            return -1;
        }
    }

    char request[512];
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, host);
    if ((ret = ssl_write(&ssl, (unsigned char*)request, strlen(request))) <= 0) {
        printf("ssl_write() failed: -0x%04x\n", -ret);
    } else {
        char buf[512];
        while ((ret = ssl_read(&ssl, (unsigned char*)buf, sizeof(buf)-1)) > 0) {
            buf[ret] = 0; printf("%s", buf);
        }
    }

    ssl_close_notify(&ssl);
    net_close(&server_fd);
    ssl_free(&ssl);
    x509_crt_free(&cacert);
    havege_free(&hs);
    return 0;
}
