#include "http.h"
#include "tcp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * @Brief: Callback function executed by the underlying TCP layer when a response is received.
 * @Param: cb_handle Pointer to the embedded tcp_cb structure.
 * @Param: response The raw TCP response data buffer.
 * @Param: len The length of the response data.
 * @Return: 0 on success.
 */
static int http_tcp_callback(struct tcp_cb *cb_handle, const char *response, size_t len)
{
    struct http *self = CONTAINER_OF(cb_handle, struct http, tcp_handle);
    printf("[HTTP] Received TCP response (%zu bytes)\n", len);
    // Copy the response, ensuring null termination and boundary check.
    size_t copy_len = len < sizeof(self->response) - 1 ? len : sizeof(self->response) - 1;
    memcpy(self->response, response, copy_len);
    self->response[copy_len] = '\0';
    // Move HTTP state to complete, signaling that the response is ready.
    self->state = HTTP_STATE_COMPLETE;
    
    return 0;
}

/**
 * @Brief: Initializes and allocates a new HTTP client structure and its associated TCP client.
 * @Param: self Pointer to the http_t pointer to store the allocated structure.
 * @Param: host The hostname or IP address of the remote HTTP server.
 * @Param: port The port number as a string (e.g., "80" for HTTP).
 * @Return: 0 on success, -1 on failure (memory or TCP initialization error).
 */
int http_init(struct http **self, const char *host, const char *port)
{
    *self = (struct http *)calloc(1, sizeof(struct http));
    if (!*self) return -1;
    
    (*self)->host = strdup(host);
    (*self)->port = strdup(port);
    (*self)->state = HTTP_STATE_IDLE;
    
    // Initialize the underlying TCP context
    struct tcp *tcp;
    if (tcp_init(&tcp, host, port) != 0) 
    {
        printf("[HTTP] Failed to initialize TCP\n");
        free((*self)->host);
        free((*self)->port);
        free(*self);
        *self = NULL;
        return -1;
    }
    
    (*self)->tcp_ctx = tcp;
    
    // Set up the TCP callback - pass the embedded tcp_cb structure and function pointer
    (*self)->tcp_handle.cb_fn = http_tcp_callback;
    tcp_set_callback(tcp, &(*self)->tcp_handle, http_tcp_callback);
    
    printf("[HTTP] Initialized for %s:%s\n", host, port);
    return 0;
}

/**
 * @Brief: Sets the user-defined callback function and context for when the HTTP response is fully processed.
 * @Param: self Pointer to the initialized http_t structure.
 * @Param: cb_handle Pointer to the embedded http_cb structure in the parent (SSN1).
 * @Param: fn The callback function pointer.
 * @Return: void
 */
void http_set_callback(struct http *self, struct http_cb *cb_handle, http_cb_fn fn)
{
    if (!self) return;
    self->ssn1_handle = cb_handle;
    cb_handle->cb_fn = fn;
}

/**
 * @Brief: Constructs an HTTP POST request with sensor data encoded as JSON and queues it for transmission via TCP.
 * @Param: self Pointer to the initialized http_t structure.
 * @Param: device_id A unique identifier for the sensor.
 * @Param: timestamp The time of the reading.
 * @Param: temperature The measured temperature value.
 * @Param: threshold_flag Flag indicating if a warning threshold was breached (0 or 1).
 * @Return: 0 on successful queuing, -1 on failure (not IDLE, JSON/HTTP formatting error, or TCP queue failure).
 */
int http_send_temp_data(struct http *self, const char *device_id,
                         time_t timestamp, double temperature, int threshold_flag)
{
    if (!self || self->state != HTTP_STATE_IDLE) 
    {
        printf("[HTTP] Cannot send - not in IDLE state (current: %d)\n", self->state);
        return -1;
    }
    
    struct tcp *tcp = (struct tcp *)self->tcp_ctx;
    
    // Format timestamp
    char time_str[64];
    struct tm *tm_info = localtime(&timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Build JSON body
    char json_body[512];
    int json_len = snprintf(json_body, sizeof(json_body),
        "{\n"
        "  \"device\": \"%s\",\n"
        "  \"time\": \"%s\",\n"
        "  \"temperature\": \"%.2f°C\",\n"
        "  \"threshold_broken\": \"%d\"\n"
        "}",
        device_id, time_str, temperature, threshold_flag);
    
    if (json_len < 0 || json_len >= (int)sizeof(json_body)) 
    {
        printf("[HTTP] Failed to format JSON\n");
        return -1;
    }
    
    printf("[HTTP] JSON body:\n%s\n", json_body);
    
    // Build full HTTP request header + body
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
    
    if (req_len < 0 || req_len >= (int)sizeof(http_request)) 
    {
        printf("[HTTP] Failed to build HTTP request\n");
        return -1;
    }
    
    printf("[HTTP] Sending POST request (%d bytes)\n", req_len);
    
    // Queue the raw request data for the TCP client
    if (tcp_send_request(tcp, http_request, req_len) != 0) 
    {
        printf("[HTTP] Failed to queue TCP request\n");
        return -1;
    }
    
    self->state = HTTP_STATE_PROCESSING;
    return 0;
}

/**
 * @Brief: The main state machine worker for the HTTP client. It drives the underlying TCP state machine.
 * @Param: self Pointer to the initialized http_t structure.
 * @Return: 1 if a full request/response cycle completed, 0 if still processing, -1 on an error.
 */
int http_work(struct http *self)
{
    if (!self) return -1;
    
    struct tcp *tcp = (struct tcp *)self->tcp_ctx;
    
    switch (self->state) 
    {
        case HTTP_STATE_IDLE:
            return 0;
            
        case HTTP_STATE_PROCESSING:
            {
                int result = tcp_work(tcp); // Drive TCP state
                if (result < 0) 
                {
                    printf("[HTTP] TCP error\n");
                    self->state = HTTP_STATE_ERROR;
                    return -1;
                }
            }
            return 0;
            
        case HTTP_STATE_COMPLETE:
            // Response received and state set by http_tcp_callback
            if (self->ssn1_handle && self->ssn1_handle->cb_fn) 
            {
                self->ssn1_handle->cb_fn(self->ssn1_handle, self->response);
            }
            self->state = HTTP_STATE_IDLE;
            return 1;
            
        case HTTP_STATE_ERROR:
            self->state = HTTP_STATE_IDLE;
            return -1;
    }
    
    return 0;
}

/**
 * @Brief: Frees all resources associated with the HTTP structure, including the internal TCP client and dynamically allocated strings.
 * @Param: self Pointer to the http_t pointer to be disposed and set to NULL.
 * @Return: 0 on success, -1 if the pointer is invalid.
 */
int http_dispose(struct http **self)
{
    if (!self || !*self) return -1;
    // Dispose of the underlying TCP client
    if ((*self)->tcp_ctx) 
    {
        tcp_dispose((struct tcp **)&(*self)->tcp_ctx);
    }
    if ((*self)->host) free((*self)->host);
    if ((*self)->port) free((*self)->port);
    free(*self);
    *self = NULL;
    printf("[HTTP] Disposed\n");
    return 0;
}