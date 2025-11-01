#ifndef __TCP_H_
#define __TCP_H_

#include <stddef.h>
#include <stdint.h>

struct http; // Forward declaration of the HTTP context for the container_of macro. 

struct tcp_cb;
typedef int (*tcp_cb_fn)(struct tcp_cb *self, const char *data, size_t len);

struct tcp_cb
{
    tcp_cb_fn cb_fn;
};

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

typedef struct tcp tcp_t;

struct tcp
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
    // Stores the pointer to the HTTP layer's embedded callback structure.    
    struct tcp_cb *http_handle; 
};

int tcp_init(struct tcp **self, const char *host, const char *port);
void tcp_set_callback(struct tcp *self, struct tcp_cb *cb_handle, tcp_cb_fn fn);
int tcp_send_request(struct tcp *self, const char *data, size_t len);
int tcp_work(struct tcp *self);
int tcp_dispose(struct tcp **self);

#endif /* __TCP_H_ */