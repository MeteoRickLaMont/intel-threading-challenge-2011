#ifndef SCANNER_H
#define SCANNER_H

#include <stdio.h>		// For perror
#include <stdlib.h>		// For exit()
#include <fcntl.h>		// For O_RDONLY
#include <sys/stat.h>		// For fstat()
#include <sys/mman.h>		// For mmap()
#include <cstdint>		// For int32_t

class Scanner {
public:
    explicit Scanner(const char *const fname) {
	int fd = open(fname, O_RDONLY);
	if (fd < 0) {
	    perror(fname);
	    exit(-1);
	}

	// Get the size of the file.
	struct stat s;
	int status = fstat(fd, &s);
	size = s.st_size;

	f = map = (char *)mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!map) {
	    perror(fname);
	    exit(-1);
	}
    }

    ~Scanner() {
	if (map)
	    munmap(const_cast<char *>(map), size);
	map = f = nullptr;
    }

    int32_t getNextPos() {
	uint32_t s0, s1, s2, s3;

	// Anything that's not a decimal digit is a delimiter.
	// Skip leading delimiters.
	do
	    s0 = static_cast<uint32_t>(*f++) - '0';
	while (s0 >= 10);

	// Enter loop with s0 set to digit just read
	int32_t result = 0;
	for (;;) {
	    s1 = static_cast<uint32_t>(*f++) - '0';
	    if (s1 >= 10)
		return result + s0;
	    s2 = static_cast<uint32_t>(*f++) - '0';
	    if (s2 >= 10)
		return result + s0 * 10 + s1;
	    s3 = static_cast<uint32_t>(*f++) - '0';
	    if (s3 >= 10)
		return result + s0 * 100 + s1 * 10 + s2;

	    result += s0 * 1000 + s1 * 100 + s2 * 10 + s3;
	    s0 = static_cast<uint32_t>(*f++) - '0';
	    if (s0 >= 10)
		return result;
	    result *= 10000;
	}
    }

private:
    const char *map;			// Memory mapped file
    const char *f;			// Current position in file
    int size;				// Number of bytes in file
};

#endif
