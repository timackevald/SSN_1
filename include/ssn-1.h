#ifndef __SSN1_H__
#define __SSN1_H__

#include <time.h>

#define LOG_24_HOUR 1440
#define N_READINGS 60

typedef int (*ssn1_callback_t)(void *ctx, const char *response);

typedef struct {
    void *http_ctx;
    ssn1_callback_t http_callback;

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

} ssn1_t;

int ssn1_init(ssn1_t **self);
int ssn1_work(ssn1_t *self);
int ssn1_dispose(ssn1_t **self);

#endif /* __SSN1_H__ */