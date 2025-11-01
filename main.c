#include "ssn-1.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("### SSN-1: Smart Sensor Node 1 ### \n"
               "- A temperature monitoring program for industrial use\n"
               "\n"
               "The Smart Sensor reads the ambient temperature every 1 second for 1 minute, returning the average of those readings.\n"
               "The calculated average is logged by the device (rolling 24 hours, oldest then gets deleted) and is then sent off to the designated server via TCP/HTTP.\n"
               "\n"
               "The user sets a low and high threshold warning for the system as shown below\n"
               "Usage: %s <low threshold warning> <high threshold warning>\n"
               "Example: ./ssn-1 3.14 4.20\n", argv[0]);
        return -1;
    }

    char *end;
    double low_temp_th = strtod(argv[1], &end);
    if (*end != '\0') 
    {
        printf("Invalid format for %s\n", argv[1]);
        return -1;
    }
    double high_temp_th = strtod(argv[2], &end);
    if (*end != '\0')
    {
        printf("Invalid format for %s\n", argv[2]);
        return -1;
    }

    struct ssn1 *self;
    if (ssn1_init(&self) != 0)
    {
        printf("Failed to initiate sensor struct.\n");
        return -1;
    }

    self->low_th_warning  = low_temp_th;
    self->high_th_warning = high_temp_th;

    printf("Low warning: %f\n"
           "High warning: %f\n", self->low_th_warning, self->high_th_warning);

    /* MAIN PROGRAM LOOP*/
    while (1)
    {
        int rv = ssn1_work(self);

        if (rv == 1 && self->th_flag == 1)
        {
            printf("[WARNING] Threshold breached!\n");
        }
        else 
        {
            usleep(10000); // Avoid busy wait
        }

    }
    return 0;
}