#ifndef __SSN1_H__
#define __SSN1_H__

#include <time.h>
#include "http.h"

#define LOG_24_HOUR 1440
#define N_READINGS 60

typedef struct ssn1 ssn1_t;

struct ssn1
{
    // 1. SSN1 embeds the HTTP callback structure.
    // This is the member passed to http_set_callback().
    // The HTTP layer calls a function associated with this handle.
    // This allows the ssn1_http_callback to use container_of(http_handle, struct ssn1, http_handle)
    // to find the address of its parent ssn1_t structure.
    struct http_cb http_handle;
    struct http *http_ctx;
    // ---------------------------------------------------------------------------------------//
    double temp_read;
    double temp_average;
    double low_th_warning;
    double high_th_warning;
    int    th_flag;
    double log[LOG_24_HOUR];
    int    log_idx;
    time_t read_last;
    time_t read_cycle_start;
    double read_current_sum;
    int    read_count;
    int    sending;
};

int ssn1_init(struct ssn1 **self);
int ssn1_work(struct ssn1 *self);
int ssn1_dispose(struct ssn1 **self);

#endif /* __SSN1_H__ */