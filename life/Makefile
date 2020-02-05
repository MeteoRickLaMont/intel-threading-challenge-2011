CC=g++
CXX=g++
CXXFLAGS=-O3 -std=c++11 # -DSTATS
# CXXFLAGS=-g -std=c++11 -DSTATS
OBJS=greedy.o optimal.o nblock.o board.o
LIBS=-lpthread -lstdc++ -lrt
# LIBS=-lpthread -lasan -lstdc++ -lrt

all: greedy optimal nblock

greedy: greedy.o board.o
	$(CC) $(CXXFLAGS) -o $@ greedy.o board.o -ltbb $(LIBS)

optimal: optimal.o board.o
	$(CC) $(CXXFLAGS) -o $@ optimal.o board.o -L/usr/local/lib -ltbb $(LIBS)

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

jmfernandez: jmfernandez.o
	$(CC) $(CXXFLAGS) -o $@ jmfernandez.o -ltbb -ltbbmalloc $(LIBS)

check: greedy
	./greedy gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm $(OBJS)
