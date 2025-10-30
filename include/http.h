#ifndef __HTTP_H_
#define __HTTP_H_

#include <stdint.h>
#include <time.h>

typedef enum 
{
    HTTP_STATE_IDLE,
    HTTP_STATE_PROCESSING,
    HTTP_STATE_COMPLETE,
    HTTP_STATE_ERROR
} http_state_t;

typedef int (*http_callback_t)(void *ctx, const char *response);

typedef struct 
{
    char *host;
    char *port;
    http_state_t state;
    void *tcp_ctx;
    char response[4096];
    http_callback_t callback;
    void *callback_ctx;
} http_t;

int http_init(http_t **self, const char *host, const char *port);
void http_set_callback(http_t *self, http_callback_t callback, void *ctx);
int http_send_temp_data(http_t *self, const char *device_id, 
                        time_t timestamp, double temperature, int threshold_flag);
int http_work(http_t *self);
int http_dispose(http_t **self);

#endif /* __HTTP_H_ */