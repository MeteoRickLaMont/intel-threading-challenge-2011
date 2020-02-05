#ifndef BOARD_H
#define BOARD_H

#include <limits.h>		// For CHAR_BIT
#include <string>
#include <vector>
#include "stopwatch.h"
#ifdef STATS
extern Stopwatch tLoad;
#endif

//
// Internal representation of Game of Life grid. Store bitmaps for every
// cell, living or dead, that's in bounds. Representation is LSB first,
// the leftmost cell is 1. To shift right on-screen shift the bitmap left.
// Note that major x coordinates are always factors of BMPSIZE (32 or 64).
//
// Sort cells by increasing y values. Subsort by increasing x (left to
// right).
//
typedef int32_t tPos;
typedef uint64_t tBmp;

const int BMPSIZE = ((int)sizeof(tBmp) * CHAR_BIT);
const tBmp XBMPMASK = BMPSIZE - 1;
const tBmp XCELLMASK = ~XBMPMASK;

struct BoardStats {
    BoardStats()
#ifdef STATS
    : npositions(0),
            tPush("    Push Queue"),
             tPop("    Pop Queue"),
         tNextGen("    Next Gen"),
      tLegalMoves("    Legal Moves"),
          tOutput("    Output")
#endif
    {}

#ifdef STATS
    long npositions;
    Stopwatch tPush;
    Stopwatch tPop;
    Stopwatch tNextGen;
    Stopwatch tLegalMoves;
    Stopwatch tOutput;
#endif
};

//
// Like a chess program, represent a board position. It consists of the
// locations of living cells, the intelligent cell, and the history of
// moves that led to this position.
//
class Position {
public:
    Position();
    Position(const Position &rhs);

    void load(const char *const fname);
    void output(BoardStats *t, FILE *fp, const int dir);
    static void output(BoardStats *t, FILE *fp, const char *s);
    void print();

    Position *nextgen(BoardStats *t, const int dir);
    std::vector<int> legalMoves(BoardStats *t) const;
    std::string getMoves() const { return fMoves; }
    uint32_t length() const { return fMoves.length(); }
    uint32_t distance(const int dir) const;

private:
    std::vector<tBmp> fCells;		// Game of Life board

    //
    // Specific to maze of life extension
    //
    const char *f;			// File contents while parsing
    tPos fIntelligentX, fIntelligentY;  // Current position of smart cell
    std::string fMoves;			// How it arrived here

    inline void ADD2(tBmp &b0, tBmp &b1, const tBmp a0, const tBmp a1) const;
    inline void ADD3(tBmp &b0, tBmp &b1,
	const tBmp a0, const tBmp a1, const tBmp a2) const;
    inline tBmp ROL(const tBmp a0, const tBmp a1) const;
    inline tBmp ROR(const tBmp a0, const tBmp a1) const;

    // inline void scanCoords(const char *&f, tPos &y, tPos &x) const;
    tPos getNextPos();
    void printFilledColumn(tPos x, tPos y, tBmp b) const;
};

#endif
