CC=g++
CXX=g++
CXXFLAGS=-O3 -std=c++11 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free # -DSTATS -DSINGLETHREAD -DDEBUG
# CXXFLAGS=-g -std=c++11 -DSTATS
OBJS=greedy.o optimal.o nblock.o board.o parser.o scanner.o
LIBS=-lpthread -lstdc++ -lrt
# LIBS=-lpthread -lasan -lstdc++ -lrt

all: greedy optimal nblock

greedy: greedy.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ greedy.o board.o parser.o scanner.o -ltbb -ltbbmalloc_proxy $(LIBS)

optimal: optimal.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ optimal.o board.o parser.o scanner.o -L/usr/local/lib -ltbb $(LIBS)

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

jmfernandez: jmfernandez.o
	$(CC) $(CXXFLAGS) -o $@ jmfernandez.o -ltbb -ltbbmalloc $(LIBS)

check: greedy
	./greedy gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm $(OBJS)

greedy.o: stopwatch.h board.h priority_queue.h parser.h scanner.h
optimal.o: stopwatch.h board.h priority_queue.h parser.h scanner.h
board.o: board.h
parser.o: board.h parser.h scanner.h
scanner.o: scanner.h
