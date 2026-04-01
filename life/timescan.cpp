#include <cstdio>
#include <sys/time.h>		// For gettimeofday()
#include "scanner.h"

inline double elapsed(timeval &then)
{
    timeval now;
    gettimeofday(&now, 0);
    return now.tv_sec - then.tv_sec + (now.tv_usec - then.tv_usec)/1000000.;
}

void scan(const char *fname)
{
    int32_t x, y;
    Scanner scan(fname);
    do {
	y = scan.getNextPos();
	x = scan.getNextPos();
    } while (x != 0 || y != 0);
}

int main(int argc, char **argv)
{
    timeval begin;
    gettimeofday(&begin, 0);

    for (int i = 0; i < 10000; ++i)
	scan("10_DoDo_300x300-373546-15.txt");

    double msecs = elapsed(begin) * 1000;
    fprintf(stderr, "Elapsed time: %g milliseconds\n", msecs);
    return 0;
}
