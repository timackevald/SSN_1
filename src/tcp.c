#include "tcp.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

/**
 * @Brief: Sets a socket file descriptor to non-blocking mode.
 * @Param: sockfd The socket file descriptor to modify.
 * @Return: 0 on success, -1 on failure.
 */ 
static int tcp_set_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @Brief: Initializes and allocates a new TCP client structure.
 * @Param: self Pointer to the tcp_t pointer to store the allocated structure.
 * @Param: host The hostname or IP address of the remote server.
 * @Param: port The port number as a string.
 * @Return: 0 on success, -1 on failure (e.g., memory allocation error).
 */ 
int tcp_init(struct tcp **self, const char *host, const char *port)
{
    *self = (struct tcp *)calloc(1, sizeof(struct tcp));
    if (!*self) 
    {
        printf("[TCP] Failed to allocate memory\n");
        return -1;
    }
    
    (*self)->host = strdup(host);
    (*self)->port = strdup(port);
    (*self)->sockfd = -1;
    (*self)->state = TCP_STATE_IDLE;
    
    printf("[TCP] Initialized for %s:%s\n", host, port);
    return 0;
}

/**
 * @Brief: Sets the user-defined callback function and context to be executed upon successful response reception.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Param: cb_handle Pointer to the embedded tcp_cb structure in the parent (HTTP).
 * @Param: fn The callback function pointer.
 * @Return: void
 */ 
void tcp_set_callback(struct tcp *self, struct tcp_cb *cb_handle, tcp_cb_fn fn)
{
    if (!self) return;
    self->http_handle = cb_handle;
    cb_handle->cb_fn = fn;
}

/**
 * @Brief: Queues a data buffer to be sent when the TCP worker runs.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Param: data The buffer containing the data to send.
 * @Param: len The size of the data buffer in bytes.
 * @Return: 0 on success, -1 if unable to send (e.g., not in IDLE state or memory allocation failure).
 */ 
int tcp_send_request(struct tcp *self, const char *data, size_t len)
{
    if (!self || self->state != TCP_STATE_IDLE) 
    {
        printf("[TCP] Cannot send - not in IDLE state (current: %d)\n", self->state);
        return -1;
    }
    
    self->send_buffer = malloc(len);
    if (!self->send_buffer) 
    {
        printf("[TCP] Failed to allocate send buffer\n");
        return -1;
    }
    
    memcpy(self->send_buffer, data, len);
    self->send_len = len;
    self->sent_bytes = 0;
    self->recv_bytes = 0;
    memset(self->recv_buffer, 0, sizeof(self->recv_buffer));
    
    self->state = TCP_STATE_CONNECTING;
    printf("[TCP] Request queued, %zu bytes\n", len);
    
    return 0;
}

/**
 * @Brief: Resolves hostname and initiates a non-blocking connection attempt.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: 0 on success (connection started or finished), -1 on error (resolution or socket failure).
 */ 
static int tcp_start_connect(struct tcp *self)
{
    struct addrinfo hints, *res;
    int ret;
    
    printf("[TCP] Resolving %s:%s\n", self->host, self->port);
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    ret = getaddrinfo(self->host, self->port, &hints, &res);
    if (ret != 0) 
    {
        printf("[TCP] getaddrinfo failed: %s\n", gai_strerror(ret));
        return -1;
    }
    
    self->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (self->sockfd < 0) 
    {
        printf("[TCP] socket failed\n");
        freeaddrinfo(res);
        return -1;
    }
    
    if (tcp_set_nonblocking(self->sockfd) < 0) 
    {
        printf("[TCP] Failed to set non-blocking\n");
        close(self->sockfd);
        self->sockfd = -1;
        freeaddrinfo(res);
        return -1;
    }
    
    ret = connect(self->sockfd, res->ai_addr, res->ai_addrlen);
    
    freeaddrinfo(res);
    
    if (ret < 0 && errno != EINPROGRESS) 
    {
        printf("[TCP] connect failed: %s\n", strerror(errno));
        close(self->sockfd);
        self->sockfd = -1;
        return -1;
    }
    return 0;
}

/**
 * @Brief: Checks the status of a non-blocking connection using getsockopt.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: 0 on successful connection, -1 if the connection failed.
 */ 
static int tcp_check_connect(struct tcp *self)
{
    int error = 0;
    socklen_t len = sizeof(error);
    
    if (getsockopt(self->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) 
    {
        printf("[TCP] getsockopt failed\n");
        return -1;
    }
    
    if (error != 0) 
    {
        printf("[TCP] Connection failed: %s\n", strerror(error));
        return -1;
    }
    
    printf("[TCP] Connected!\n");
    return 0;
}

/**
 * @Brief: Performs non-blocking sending of queued data.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: 1 if all data has been sent, 0 if sending would block, -1 on a socket error.
 */ 
static int tcp_do_send(struct tcp *self)
{
    while (self->sent_bytes < self->send_len) 
    {
        ssize_t sent = send(self->sockfd,
                            self->send_buffer + self->sent_bytes,
                            self->send_len - self->sent_bytes,
                            MSG_DONTWAIT);
        
        if (sent < 0) 
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) 
            {
                return 0; // Would block, try again later
            }
            printf("[TCP] send failed: %s\n", strerror(errno));
            return -1;
        }
        
        self->sent_bytes += sent;
        printf("[TCP] Sent %zd bytes (total: %zu/%zu)\n", 
               sent, self->sent_bytes, self->send_len);
    }
    
    return 1; // All sent
}

/**
 * @Brief: Performs non-blocking receiving of data into the receive buffer.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: 1 if the connection was closed by the server (and all data received), 0 if data was received or would block, -1 on a socket error.
 */ 
static int tcp_do_recv(struct tcp *self)
{
    ssize_t received = recv(self->sockfd,
                            self->recv_buffer + self->recv_bytes,
                            sizeof(self->recv_buffer) - self->recv_bytes - 1,
                            MSG_DONTWAIT);
    
    if (received < 0) 
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // Would block, try again later
        }
        printf("[TCP] recv failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (received == 0) 
    {
        printf("[TCP] Connection closed by server\n");
        self->recv_buffer[self->recv_bytes] = '\0';
        return 1; // Done receiving
    }
    
    self->recv_bytes += received;
    printf("[TCP] Received %zd bytes (total: %zu)\n", received, self->recv_bytes);
    
    return 0; // Keep receiving
}

/**
 * @Brief: Cleans up socket resources and frees the send buffer.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: void
 */ 
static void tcp_cleanup(struct tcp *self)
{
    if (self->sockfd >= 0) 
    {
        close(self->sockfd);
        self->sockfd = -1;
    }
    
    if (self->send_buffer) 
    {
        free(self->send_buffer);
        self->send_buffer = NULL;
    }
    
    self->send_len = 0;
    self->sent_bytes = 0;
}

/**
 * @Brief: The main state machine worker function for the TCP client. It handles connection, sending, and receiving non-blockingly.
 * @Param: self Pointer to the initialized tcp_t structure.
 * @Return: 1 if a full request/response cycle completed, 0 if still processing, -1 on an error.
 */ 
int tcp_work(struct tcp *self)
{
    if (!self) return -1;
    
    switch (self->state) 
    {
        case TCP_STATE_IDLE:
            return 0;
            
        case TCP_STATE_CONNECTING:
            if (tcp_start_connect(self) != 0) 
            {
                self->state = TCP_STATE_ERROR;
                return -1;
            }
            self->state = TCP_STATE_CONNECTED;
            return 0;
            
        case TCP_STATE_CONNECTED:
            if (tcp_check_connect(self) != 0) 
            {
                self->state = TCP_STATE_ERROR;
                return -1;
            }
            self->state = TCP_STATE_SENDING;
            return 0;
            
        case TCP_STATE_SENDING:
            {
                int result = tcp_do_send(self);
                if (result < 0) 
                {
                    self->state = TCP_STATE_ERROR;
                    return -1;
                } 
                else if (result == 1) 
                {
                    self->state = TCP_STATE_RECEIVING;
                }
            }
            return 0;
            
        case TCP_STATE_RECEIVING:
            {
                int result = tcp_do_recv(self);
                if (result < 0) 
                {
                    self->state = TCP_STATE_ERROR;
                    return -1;
                } 
                else if (result == 1) 
                {
                    self->state = TCP_STATE_COMPLETE;
                }
            }
            return 0;
            
        case TCP_STATE_COMPLETE:
            if (self->http_handle && self->http_handle->cb_fn) 
            {
                self->http_handle->cb_fn(self->http_handle, self->recv_buffer, self->recv_bytes);
            }
            tcp_cleanup(self);
            self->state = TCP_STATE_IDLE;
            return 1;
            
        case TCP_STATE_ERROR:
            printf("[TCP] Error state, cleaning up\n");
            tcp_cleanup(self);
            self->state = TCP_STATE_IDLE;
            return -1;
    }
    
    return 0;
}

/**
 * @Brief: Frees all resources associated with the TCP structure and frees the structure itself.
 * @Param: self Pointer to the tcp_t pointer to be disposed and set to NULL.
 * @Return: 0 on success, -1 if the pointer is invalid.
 */ 
int tcp_dispose(struct tcp **self)
{
    if (!self || !*self) return -1;
    tcp_cleanup(*self);
    if ((*self)->host) free((*self)->host);
    if ((*self)->port) free((*self)->port);
    free(*self);
    *self = NULL;
    printf("[TCP] Disposed\n");
    return 0;
}