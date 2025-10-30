#include "ssn-1.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

/* PRIVATE FUNCTIONS */
/**
 * @Brief: Simulates reading temperature from a physical sensor.
 * @Param: self Pointer to the ssn1_t structure.
 * @Return: A double representing the simulated temperature reading.
 */ 
double ssn1_sensor(ssn1_t *self);

/**
 * @Brief: Callback function executed by the HTTP client upon successful receipt of a server response.
 * @Param: ctx A user-defined context pointer (expected to be ssn1_t*).
 * @Param: response The received server response string.
 * @Return: 0 on success.
 */ 
static int ssn1_http_callback(void *ctx, const char *response)
{
    ssn1_t *self = (ssn1_t*)ctx;
    
    printf("\n");
    printf("========================================\n");
    printf("  SERVER RESPONSE\n");
    printf("========================================\n");
    printf("%s\n", response);
    printf("========================================\n");
    printf("\n");
    
    self->sending = 0;  // Done sending
    
    return 0;
}

/**
 * @Brief: Initializes and allocates the SSN1 structure, sets initial state, and initializes the HTTP client.
 * @Param: self Pointer to the ssn1_t pointer where the allocated structure will be stored.
 * @Return: 0 on success, -1 on failure (memory or HTTP client initialization).
 */
int ssn1_init(ssn1_t **self)
{
    *self = (ssn1_t*)calloc(1, sizeof(ssn1_t));
    if (!*self) return -1;

    (*self)->read_cycle_start = time(NULL);
    (*self)->read_last        = (*self)->read_cycle_start;
    (*self)->sending          = 0;
    // Initialize HTTP client with callback
    http_t *http;
    if (http_init(&http, "httpbin.org", "80") != 0) 
    {
        printf("Failed to initialize HTTP client\n");
        free(*self);
        *self = NULL;
        return -1;
    }
    (*self)->http_ctx = http;
    // Set callback from HTTP to SSN1
    http_set_callback(http, ssn1_http_callback, *self);

    return 0;
}

/**
 * @Brief: The main state machine worker for the sensor node. It handles HTTP transmission and time-based sensor reading/averaging.
 * @Param: self Pointer to the ssn1_t structure.
 * @Return: 0: Nothing ready. 1: Averaging cycle complete and transmission initiated. 2: New reading taken.
 */
int ssn1_work(ssn1_t *self)
{
    http_t *http = (http_t*)self->http_ctx;
    
    // If we're in the middle of sending, drive the HTTP state machine
    if (self->sending) 
    {
        int result = http_work(http);
        if (result == 1) 
        {
            // HTTP transaction complete (callback was called)
            printf("[SSN1] HTTP transaction complete\n");
        } 
        else if (result < 0) 
        {
            printf("[SSN1] HTTP transaction failed\n");
            self->sending = 0;
        }
        return 0;
    }
    
    time_t now = time(NULL);
    time_t time_since_reading = now - self->read_last;

    // Check if it is time to average sum (reading count reached N_READINGS)
    if (self->read_count >= N_READINGS)
    {
        self->temp_average = self->read_current_sum / N_READINGS;
        printf("\n[SSN1] Average temp over 1 minute: %.2f°C\n", self->temp_average);
        // Log result and advance idx or flip-over to 0 (circular buffer)
        self->log[self->log_idx] = self->temp_average;
        self->log_idx = (self->log_idx + 1) % LOG_24_HOUR;
        
        // Check warning thresholds
        if (self->temp_average < self->low_th_warning 
             || self->temp_average > self->high_th_warning)
        {
            self->th_flag = 1;
            printf("[SSN1] WARNING: Temperature threshold breached!\n");
        }
        else
        {
            self->th_flag = 0;
        }
        
        // Initiate HTTP send with the new data
        if (http_send_temp_data(http, "SSN1-UUID-12345", 
                               self->read_last, self->temp_average, self->th_flag) == 0) 
        {
            self->sending = 1;
        } 
        else 
        {
            printf("[SSN1] Failed to initiate HTTP send\n");
        }
        
        // Reset and start timer for the next cycle
        self->read_current_sum = 0.0;
        self->read_count       = 0;
        self->read_cycle_start = now;
        self->read_last        = now;
        
        // Signal reading cycle complete to caller
        return 1;
    }

    // Check if it is time to read (at least 1 second passed)
    if (time_since_reading >= 1)
    {
        double read = ssn1_sensor(self);
        self->temp_read = read;
        self->read_current_sum += read;
        self->read_count++;
        
        // Reset timer
        self->read_last = now;
        printf("Reading #%d: %.2f°C\n", self->read_count, self->temp_read);
        
        // Signal reading taken
        return 2;
    }
    
    // Signal nothing to do
    return 0;
}

/**
 * @Brief: Cleans up all resources, including the internal HTTP client, and frees the SSN1 structure.
 * @Param: self Pointer to the ssn1_t pointer to be disposed and set to NULL.
 * @Return: 0 on success, -1 if the pointer is invalid.
 */
int ssn1_dispose(ssn1_t **self)
{
    if (!self || !*self) return -1;
    printf("[SSN1] Disposing sensor...\n");
    // Cleanup HTTP (which will cleanup TCP)
    if ((*self)->http_ctx) 
    {
        http_dispose((http_t**)&(*self)->http_ctx);
    }
    // Free the struct
    free(*self);
    *self = NULL;
    printf("[SSN1] Sensor disposed\n");
    return 0;
}

/* SIMULATING FUNCTION */
/**
 * @Brief: Simulates reading temperature from a physical sensor.
 * @Param: self Pointer to the ssn1_t structure.
 * @Return: A random double value within a range slightly over the high/low warning thresholds.
 */
double ssn1_sensor(ssn1_t *self)
{
    double low  = self->low_th_warning;
    double high = self->high_th_warning;
    double norm_rand = (double)rand() / (double)RAND_MAX;
    double val_in_range = low + (norm_rand * (high - low + 1));
    return val_in_range;
}