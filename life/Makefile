CC=g++
CXX=g++
CXXFLAGS=-O3 -std=c++11 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free # -DDONTLEAK -DSTATS -DDEBUG
OBJS=greedy.o beam.o nblock.o jmfernandez.o board.o parser.o scanner.o
LIBS=-lpthread -lstdc++ -lrt

all: greedy beam

greedy: greedy.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ greedy.o board.o parser.o scanner.o -ltbb -ltbbmalloc_proxy $(LIBS)

beam: beam.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ beam.o board.o parser.o scanner.o -ltbb -ltbbmalloc_proxy $(LIBS)

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

jmfernandez: jmfernandez.o
	$(CC) $(CXXFLAGS) -o $@ jmfernandez.o -ltbb -ltbbmalloc $(LIBS)

check: greedy
	./greedy gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm -f $(OBJS)

greedy.o: stopwatch.h board.h parser.h scanner.h
beam.o: stopwatch.h board.h parser.h scanner.h
board.o: stopwatch.h board.h
parser.o: stopwatch.h board.h parser.h scanner.h
scanner.o: scanner.h
