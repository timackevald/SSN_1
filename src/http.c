#include "http.h"
#include "tcp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int http_tcp_callback(void *ctx, const char *response, size_t len)
{
    http_t *self = (http_t*)ctx;
    printf("[HTTP] Received TCP response (%zu bytes)\n", len);
    size_t copy_len = len < sizeof(self->response) - 1 ? len : sizeof(self->response) - 1;
    memcpy(self->response, response, copy_len);
    self->response[copy_len] = '\0';
    self->state = HTTP_STATE_COMPLETE;
    
    return 0;
}

int http_init(http_t **self, const char *host, const char *port)
{
    *self = (http_t*)calloc(1, sizeof(http_t));
    if (!*self) return -1;
    
    (*self)->host = strdup(host);
    (*self)->port = strdup(port);
    (*self)->state = HTTP_STATE_IDLE;
    
    tcp_t *tcp;
    if (tcp_init(&tcp, host, port) != 0) {
        printf("[HTTP] Failed to initialize TCP\n");
        free((*self)->host);
        free((*self)->port);
        free(*self);
        *self = NULL;
        return -1;
    }
    
    (*self)->tcp_ctx = tcp;
    tcp_set_callback(tcp, http_tcp_callback, *self);
    
    printf("[HTTP] Initialized for %s:%s\n", host, port);
    return 0;
}

void http_set_callback(http_t *self, http_callback_t callback, void *ctx)
{
    if (!self) return;
    self->callback = callback;
    self->callback_ctx = ctx;
}

int http_send_temp_data(http_t *self, const char *device_id,
                        time_t timestamp, double temperature, int threshold_flag)
{
    if (!self || self->state != HTTP_STATE_IDLE) {
        printf("[HTTP] Cannot send - not in IDLE state (current: %d)\n", self->state);
        return -1;
    }
    
    tcp_t *tcp = (tcp_t*)self->tcp_ctx;
    
    char time_str[64];
    struct tm *tm_info = localtime(&timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char json_body[512];
    int json_len = snprintf(json_body, sizeof(json_body),
        "{\n"
        "  \"device\": \"%s\",\n"
        "  \"time\": \"%s\",\n"
        "  \"temperature\": \"%.2f°C\",\n"
        "  \"threshold_broken\": \"%d\"\n"
        "}",
        device_id, time_str, temperature, threshold_flag);
    
    if (json_len < 0 || json_len >= (int)sizeof(json_body)) {
        printf("[HTTP] Failed to format JSON\n");
        return -1;
    }
    
    printf("[HTTP] JSON body:\n%s\n", json_body);
    
    char http_request[2048];
    int req_len = snprintf(http_request, sizeof(http_request),
        "POST /post HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        self->host, json_len, json_body);
    
    if (req_len < 0 || req_len >= (int)sizeof(http_request)) {
        printf("[HTTP] Failed to build HTTP request\n");
        return -1;
    }
    
    printf("[HTTP] Sending POST request (%d bytes)\n", req_len);
    
    if (tcp_send_request(tcp, http_request, req_len) != 0) {
        printf("[HTTP] Failed to queue TCP request\n");
        return -1;
    }
    
    self->state = HTTP_STATE_PROCESSING;
    return 0;
}

int http_work(http_t *self)
{
    if (!self) return -1;
    
    tcp_t *tcp = (tcp_t*)self->tcp_ctx;
    
    switch (self->state) {
        case HTTP_STATE_IDLE:
            return 0;
            
        case HTTP_STATE_PROCESSING:
            {
                int result = tcp_work(tcp);
                if (result < 0) {
                    printf("[HTTP] TCP error\n");
                    self->state = HTTP_STATE_ERROR;
                    return -1;
                }
            }
            return 0;
            
        case HTTP_STATE_COMPLETE:
            if (self->callback) {
                self->callback(self->callback_ctx, self->response);
            }
            self->state = HTTP_STATE_IDLE;
            printf("[HTTP] Back to IDLE\n");
            return 1;
            
        case HTTP_STATE_ERROR:
            self->state = HTTP_STATE_IDLE;
            return -1;
    }
    
    return 0;
}

int http_dispose(http_t **self)
{
    if (!self || !*self) return -1;
    
    if ((*self)->tcp_ctx) {
        tcp_dispose((tcp_t**)&(*self)->tcp_ctx);
    }
    
    if ((*self)->host) free((*self)->host);
    if ((*self)->port) free((*self)->port);
    
    free(*self);
    *self = NULL;
    
    printf("[HTTP] Disposed\n");
    return 0;
}