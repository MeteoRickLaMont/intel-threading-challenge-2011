#include <stdio.h>
#include <libcpuid.h>

int main(void)
{
    if (!cpuid_present()) {
        printf("Sorry, your CPU doesn't support CPUID!\n");
        return -1;
    }

    struct cpu_raw_data_t raw; 
    struct cpu_id_t data;     

    if (cpuid_get_raw_data(&raw) < 0) { 
        printf("Sorry, cannot get the CPUID raw data.\n");
        printf("Error: %s\n", cpuid_error());
        return -2;
    }

    if (cpu_identify(&raw, &data) < 0) {    
        printf("Sorrry, CPU identification failed.\n");
        printf("Error: %s\n", cpuid_error());
        return -3;
    }

    printf("Processor has %d physical cores\n", data.num_cores);
    return 0;
}
