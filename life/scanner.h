#ifndef SCANNER_H
#define SCANNER_H

#include <cstdint>              // For int32_t

class Scanner {
public:
    explicit Scanner(const char *const fname);
    ~Scanner();
    int32_t operator++();

private:
    int m_fd;                           // File descriptor
    int m_size;                         // Number of bytes in file
    char *m_map;                        // Memory mapped file
    const char *m_f;                    // Current position in file
};

#endif
