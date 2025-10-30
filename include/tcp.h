#ifndef __TCP_H_
#define __TCP_H_

#include <stdint.h>
#include <stddef.h>

typedef enum 
{
    TCP_STATE_IDLE,
    TCP_STATE_CONNECTING,
    TCP_STATE_CONNECTED,
    TCP_STATE_SENDING,
    TCP_STATE_RECEIVING,
    TCP_STATE_COMPLETE,
    TCP_STATE_ERROR
} tcp_state_t;

typedef int (*tcp_callback_t)(void *ctx, const char *response, size_t len);

typedef struct 
{
    char *host;
    char *port;
    int sockfd;
    tcp_state_t state;
    char *send_buffer;
    size_t send_len;
    size_t sent_bytes;
    char recv_buffer[4096];
    size_t recv_bytes;
    tcp_callback_t callback;
    void *callback_ctx;
} tcp_t;

int tcp_init(tcp_t **self, const char *host, const char *port);
void tcp_set_callback(tcp_t *self, tcp_callback_t callback, void *ctx);
int tcp_send_request(tcp_t *self, const char *data, size_t len);
int tcp_work(tcp_t *self);
int tcp_dispose(tcp_t **self);

#endif /* __TCP_H_ */