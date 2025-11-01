#ifndef __HTTP_H_
#define __HTTP_H_

#include <stdint.h>
#include <time.h>
#include <stddef.h>
#include "tcp.h"

// Container of macro: 
// This macro computes the address of the structure (type) that contains the member (member),
// given a pointer to the member (ptr).
// This is the core of the type-safe, embedded callback pattern.
#define CONTAINER_OF(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

struct http_cb;
typedef int (*http_cb_fn)(struct http_cb *self, const char *msg);
struct http_cb
{   
    http_cb_fn cb_fn;
};

typedef enum 
{
    HTTP_STATE_IDLE,
    HTTP_STATE_PROCESSING,
    HTTP_STATE_COMPLETE,
    HTTP_STATE_ERROR
} http_state_t;

typedef struct http http_t;

struct http
{
    char response[4096];
    char *host;
    char *port;
    http_state_t state;   
    // The HTTP struct now embeds the TCP callback structure.
    // This is the member whose address is passed to tcp_set_callback.
    struct tcp_cb tcp_handle;
    struct tcp *tcp_ctx;
    // The HTTP struct stores a pointer to the SSN1's embedded callback structure.
    // This is the handle the HTTP layer will use to call back the SSN1 layer.     
    struct http_cb *ssn1_handle;
};

int http_init(struct http **self, const char *host, const char *port);
void http_set_callback(struct http *self, struct http_cb *cb_handle, http_cb_fn fn);
int http_send_temp_data(struct http *self, const char *device_id, time_t timestamp, double temperature, int threshold_flag);
int http_work(struct http *self);
int http_dispose(struct http **self);

#endif /* __HTTP_H_ */