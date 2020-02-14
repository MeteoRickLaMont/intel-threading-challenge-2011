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
using tPos = int32_t;
using tBmp = uint64_t;

struct BoardStats {
    BoardStats()
#ifdef STATS
    : npositions(0),
            tPush("    Push Queue"),
             tPop("    Pop Queue"),
         tNextGen("    Next Gen"),
      tLegalMoves("    Legal Moves"),
          tOutput("    Output"),
           tPart2("    threadsearch")
#endif
    {}

#ifdef STATS
    long npositions;
    Stopwatch tPush;
    Stopwatch tPop;
    Stopwatch tNextGen;
    Stopwatch tLegalMoves;
    Stopwatch tOutput;
    Stopwatch tPart2;
#endif
};

//
// Like a chess program, represent a board position. It consists of the
// locations of living cells, the intelligent cell, and the history of
// moves that led to this position.
//
class Position {
public:
    constexpr static int BMPSIZE = ((int)sizeof(tBmp) * CHAR_BIT);
    constexpr static tBmp XBMPMASK = BMPSIZE - 1;
    constexpr static tBmp XCELLMASK = ~XBMPMASK;

    Position();
    Position(const Position &rhs);
    Position(tPos yintel, tPos xintel, std::vector<tBmp> &&cells) :
	fIntelligentY(yintel - 1),
	fIntelligentX(xintel - 1),
	fCells(cells)
    {}

    static void setDimensions(tPos ydim, tPos xdim) {
	gBoardHeight = ydim;
	gMaxY = ydim - 1;
	gMaxX = xdim - 1;
	gBoardWidth = gMaxX / BMPSIZE + 1;
	gRightx = gMaxX & XCELLMASK;
	gRightmask = ~((tBmp)0) >> (BMPSIZE - (xdim & XBMPMASK));
    }

    static void setGoal(tPos ygoal, tPos xgoal) {
	gGoalY = ygoal - 1;
	gGoalX = xgoal - 1;
    }

    static size_t getBoardHeight() { return gBoardHeight; }
    static size_t getBoardWidth() { return gBoardWidth; }

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
    tPos fIntelligentX, fIntelligentY;  // Current position of smart cell
    std::string fMoves;			// How it arrived here

    //
    // These parameters are common to all positions.
    //
    static tPos gMaxX, gMaxY, gRightx;	// Dimensions in cells
    static size_t gBoardHeight, gBoardWidth;
    static tBmp gRightmask;		// To clip cells to right edge
    static tPos gGoalX, gGoalY;


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
