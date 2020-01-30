#include <stdio.h>
#include <unistd.h>

int main()
{
    int nthreads = sysconf(_SC_NPROCESSORS_CONF);
    printf("%d threads\n", nthreads);
}
