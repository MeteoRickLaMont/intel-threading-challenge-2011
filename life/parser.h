#ifndef PARSER_H
#define PARSER_H

#include <cstddef>      // For size_t
#include "scanner.h"

class Position;

class Parser {
public:
    explicit Parser(const char *const fname);

    size_t getTotalCells() const { return m_ydim * m_xdim; }
    Position *loadInitial();

private:
    Scanner scan;
    int32_t m_ydim, m_xdim;
};

#endif
