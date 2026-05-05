#ifndef NET_TRANSPORT_H
#define NET_TRANSPORT_H

#include "winsock2.h"

#define NET_TRANSPORT_ERR -1

typedef enum {
    NET_SCHEME_HTTP = 0,
    NET_SCHEME_HTTPS = 1
} net_scheme_t;

typedef struct {
    int tls_insecure;
    const char *ca_bundle_path;
    const char *server_name;
} net_tls_options_t;

typedef struct {
    SOCKET socket_fd;
    int use_tls;

#ifdef USE_POLARSSL
    void *tls_ctx;
    void *tls_ssl;
#endif
} net_transport_t;

int net_transport_connect(net_transport_t *transport,
                          const char *host,
                          unsigned short port,
                          net_scheme_t scheme,
                          const net_tls_options_t *tls_opts);

int net_transport_send(net_transport_t *transport, const void *data, int len);
int net_transport_recv(net_transport_t *transport, void *buffer, int len);
void net_transport_close(net_transport_t *transport);

#endif
