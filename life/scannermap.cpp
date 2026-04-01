#include <stdio.h>		// For perror
#include <stdlib.h>		// For exit()
#include <fcntl.h>		// For O_RDONLY
#include <unistd.h>		// For close
#include <sys/stat.h>		// For fstat()
#include <sys/mman.h>		// For mmap()
#include "scanner.h"

Scanner::Scanner(const char *const fname) {
    fd = open(fname, O_RDONLY);
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

Scanner::~Scanner() {
    if (map)
	munmap(const_cast<char *>(map), size);
    map = f = nullptr;
    close(fd);
}

//
// Reference: "Fastware" by Andrei Alexandrescu
// NDC London, 16-20 January 2017
// https://www.youtube.com/watch?v=o4-CwDo2zpg
//
// Alexandrescu shows an optimized atoui() function but it assumes
// we know the beginning and end of the string. The function below
// more closely resembles his digits10() function. Besides converting
// the string to int, it also skips leading delimiters (if any) and
// determines the length of the string to be converted.
//
// Assumptions:
// - Numbers are all positive
// - Representable as a signed 32 bit integer (10 digits or fewer)
// - File ends with a newline or other delimiter
//
int32_t Scanner::getNextPos() {
    uint32_t s0, s1, s2, s3;

    //
    // From the input description, the file will consist entirely
    // of decimal digits, spaces and newlines. We need to distinguish
    // digits from delimiters.
    //
    // Use the "wraparoo" trick (unsigned subtraction) to match decimal 
    // digits because it uses only one subtraction and one compare.
    // If it turns out to be a digit then we would have to do the
    // subtraction anyway, so reuse the result of the subtraction.
    //
    // Anything that's not a decimal digit is considered a delimiter.
    // Don't unroll the loop because we expect only one delimiter
    // between any pair of numbers (maybe two on Windows for '\r\n').
    //
    do
	s0 = static_cast<uint32_t>(*f++) - '0';
    while (s0 >= 10);

    //
    // Begin each iteration with s0 set to digit just read.
    // 'result' contains partial sum from previous iteration(s).
    //
    // The law of small numbers says that most numbers in a program
    // are small. Most are between -1000 and 1000. Indeed, every
    // number in the 10 benchmark files are between 0 and 300. These
    // small numbers will all be processed in the first iteration of
    // the following loop. The only overhead will be the addition of 0
    // in "result + ...".
    //
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

	//
	// Four or more additional digits in number.
	// If exactly four return it now.
	// Otherwise, prepare for next iteration.
	//
	result += s0 * 1000 + s1 * 100 + s2 * 10 + s3;
	s0 = static_cast<uint32_t>(*f++) - '0';
	if (s0 >= 10)
	    return result;

	// Skip ahead by 4 orders of magnitude
	result *= 10000;
    }
}
