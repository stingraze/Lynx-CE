#include "net_transport.h"

#include <stdio.h>
#include <string.h>

#ifdef USE_POLARSSL
#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/x509_crt.h"
#include "polarssl/havege.h"
#endif

static int tcp_connect_socket(const char *host, unsigned short port)
{
    struct hostent *he = gethostbyname(host);
    if (!he) {
        return INVALID_SOCKET;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = *((u_long*)he->h_addr);

    if (connect(s, (struct sockaddr*)&server, sizeof(server)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    return s;
}

int net_transport_connect(net_transport_t *transport,
                          const char *host,
                          unsigned short port,
                          net_scheme_t scheme,
                          const net_tls_options_t *tls_opts)
{
    memset(transport, 0, sizeof(*transport));
    transport->socket_fd = tcp_connect_socket(host, port);
    if (transport->socket_fd == INVALID_SOCKET) {
        return NET_TRANSPORT_ERR;
    }

    if (scheme == NET_SCHEME_HTTP) {
        transport->use_tls = 0;
        return 0;
    }

#ifndef USE_POLARSSL
    (void)tls_opts;
    printf("TLS requested, but this build has no PolarSSL/MbedTLS backend.\n");
    closesocket(transport->socket_fd);
    transport->socket_fd = INVALID_SOCKET;
    return NET_TRANSPORT_ERR;
#else
    /* Placeholder TLS backend hook:
       For WinCE builds with PolarSSL/MbedTLS, initialize contexts here and set:
       - verification REQUIRED by default
       - verification OPTIONAL only when tls_opts && tls_opts->tls_insecure
       - CA chain loading from tls_opts->ca_bundle_path
       - hostname verification using tls_opts->server_name
    */
    (void)tls_opts;
    transport->use_tls = 1;
    return NET_TRANSPORT_ERR;
#endif
}

int net_transport_send(net_transport_t *transport, const void *data, int len)
{
#ifdef USE_POLARSSL
    if (transport->use_tls) {
        return NET_TRANSPORT_ERR;
    }
#endif
    return send(transport->socket_fd, (const char*)data, len, 0);
}

int net_transport_recv(net_transport_t *transport, void *buffer, int len)
{
#ifdef USE_POLARSSL
    if (transport->use_tls) {
        return NET_TRANSPORT_ERR;
    }
#endif
    return recv(transport->socket_fd, (char*)buffer, len, 0);
}

void net_transport_close(net_transport_t *transport)
{
    if (!transport) return;
    if (transport->socket_fd != INVALID_SOCKET) {
        closesocket(transport->socket_fd);
        transport->socket_fd = INVALID_SOCKET;
    }
}
