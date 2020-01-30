#include<stdio.h>
#include<stdint.h>

int main()
{
    int64_t i, j, load, start, end;

    printf("#!/bin/sh\n");
    printf("#PBS -N primes8x8\n");
    printf("#PBS -j oe\n");
    printf("#PBS -l walltime=1:00:00\n");
    printf("export OMP_NUM_THREADS=40\n");
    printf("cd ~/threading/primes\n");
    for (j = 0; j < 8; ++j) {
        load = (j + 1) * (1L << 17);
        start = 0;
        end = start + load;
        printf("./power %lu %lu 64 power%lu.out\n", start, end, j);
        printf("./stride %lu %lu 64 stride%lu.out\n", start, end, j);
        printf("echo\n");
    }
    /*
    for (i = 0; i < 8; ++i) {
        // load = 1000000 * (i + 1);
        load = (i + 1) * (1L << 20);
        for (j = 0; j < 8; ++j) {
            start = j * (1L << 28);
            end = start + load;
            printf("./power %lu %lu 64 power%lu%lu.out\n", start, end, i, j);
            printf("./stride %lu %lu 64 stride%lu%lu.out\n", start, end, i, j);
            printf("echo\n");
        }
    }
    */
    return 0;
}
