#include "tcp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

static int tcp_set_nonblocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int tcp_init(tcp_t **self, const char *host, const char *port)
{
    *self = (tcp_t*)calloc(1, sizeof(tcp_t));
    if (!*self) {
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

void tcp_set_callback(tcp_t *self, tcp_callback_t callback, void *ctx)
{
    if (!self) return;
    self->callback = callback;
    self->callback_ctx = ctx;
}

int tcp_send_request(tcp_t *self, const char *data, size_t len)
{
    if (!self || self->state != TCP_STATE_IDLE) {
        printf("[TCP] Cannot send - not in IDLE state (current: %d)\n", self->state);
        return -1;
    }
    
    self->send_buffer = malloc(len);
    if (!self->send_buffer) {
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

static int tcp_start_connect(tcp_t *self)
{
    struct addrinfo hints, *res;
    int ret;
    
    printf("[TCP] Resolving %s:%s\n", self->host, self->port);
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    ret = getaddrinfo(self->host, self->port, &hints, &res);
    if (ret != 0) {
        printf("[TCP] getaddrinfo failed: %s\n", gai_strerror(ret));
        return -1;
    }
    
    self->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (self->sockfd < 0) {
        printf("[TCP] socket failed\n");
        freeaddrinfo(res);
        return -1;
    }
    
    if (tcp_set_nonblocking(self->sockfd) < 0) {
        printf("[TCP] Failed to set non-blocking\n");
        close(self->sockfd);
        self->sockfd = -1;
        freeaddrinfo(res);
        return -1;
    }
    
    printf("[TCP] Starting non-blocking connect...\n");
    ret = connect(self->sockfd, res->ai_addr, res->ai_addrlen);
    
    freeaddrinfo(res);
    
    if (ret < 0 && errno != EINPROGRESS) {
        printf("[TCP] connect failed: %s\n", strerror(errno));
        close(self->sockfd);
        self->sockfd = -1;
        return -1;
    }
    
    printf("[TCP] Connect in progress...\n");
    return 0;
}

static int tcp_check_connect(tcp_t *self)
{
    int error = 0;
    socklen_t len = sizeof(error);
    
    if (getsockopt(self->sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        printf("[TCP] getsockopt failed\n");
        return -1;
    }
    
    if (error != 0) {
        printf("[TCP] Connection failed: %s\n", strerror(error));
        return -1;
    }
    
    printf("[TCP] Connected!\n");
    return 0;
}

static int tcp_do_send(tcp_t *self)
{
    while (self->sent_bytes < self->send_len) {
        ssize_t sent = send(self->sockfd,
                           self->send_buffer + self->sent_bytes,
                           self->send_len - self->sent_bytes,
                           MSG_DONTWAIT);
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
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

static int tcp_do_recv(tcp_t *self)
{
    ssize_t received = recv(self->sockfd,
                           self->recv_buffer + self->recv_bytes,
                           sizeof(self->recv_buffer) - self->recv_bytes - 1,
                           MSG_DONTWAIT);
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0; // Would block, try again later
        }
        printf("[TCP] recv failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (received == 0) {
        printf("[TCP] Connection closed by server\n");
        self->recv_buffer[self->recv_bytes] = '\0';
        return 1; // Done
    }
    
    self->recv_bytes += received;
    printf("[TCP] Received %zd bytes (total: %zu)\n", received, self->recv_bytes);
    
    return 0; // Keep receiving
}

static void tcp_cleanup(tcp_t *self)
{
    if (self->sockfd >= 0) {
        close(self->sockfd);
        self->sockfd = -1;
    }
    
    if (self->send_buffer) {
        free(self->send_buffer);
        self->send_buffer = NULL;
    }
    
    self->send_len = 0;
    self->sent_bytes = 0;
}

int tcp_work(tcp_t *self)
{
    if (!self) return -1;
    
    switch (self->state) {
        case TCP_STATE_IDLE:
            return 0;
            
        case TCP_STATE_CONNECTING:
            if (tcp_start_connect(self) != 0) {
                self->state = TCP_STATE_ERROR;
                return -1;
            }
            self->state = TCP_STATE_CONNECTED;
            return 0;
            
        case TCP_STATE_CONNECTED:
            if (tcp_check_connect(self) != 0) {
                self->state = TCP_STATE_ERROR;
                return -1;
            }
            self->state = TCP_STATE_SENDING;
            return 0;
            
        case TCP_STATE_SENDING:
            {
                int result = tcp_do_send(self);
                if (result < 0) {
                    self->state = TCP_STATE_ERROR;
                    return -1;
                } else if (result == 1) {
                    self->state = TCP_STATE_RECEIVING;
                }
            }
            return 0;
            
        case TCP_STATE_RECEIVING:
            {
                int result = tcp_do_recv(self);
                if (result < 0) {
                    self->state = TCP_STATE_ERROR;
                    return -1;
                } else if (result == 1) {
                    self->state = TCP_STATE_COMPLETE;
                }
            }
            return 0;
            
        case TCP_STATE_COMPLETE:
            if (self->callback) {
                self->callback(self->callback_ctx, self->recv_buffer, self->recv_bytes);
            }
            tcp_cleanup(self);
            self->state = TCP_STATE_IDLE;
            printf("[TCP] Back to IDLE\n");
            return 1;
            
        case TCP_STATE_ERROR:
            printf("[TCP] Error state, cleaning up\n");
            tcp_cleanup(self);
            self->state = TCP_STATE_IDLE;
            return -1;
    }
    
    return 0;
}

int tcp_dispose(tcp_t **self)
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