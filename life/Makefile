CC=g++
CXX=g++
# Pinned 2011-vintage TBB, built from third_party/ on first use — see
# third_party/README.md and scripts/build-tbb.sh.
TBB_ROOT=$(abspath ../third_party/tbb)
TBB_LIB=$(TBB_ROOT)/lib/libtbb.so.2
CXXFLAGS=-O3 -std=c++11 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -I$(TBB_ROOT)/include # -DDONTLEAK -DSTATS -DDEBUG
LDFLAGS=-L$(TBB_ROOT)/lib -Wl,-rpath,$(TBB_ROOT)/lib
OBJS=greedy.o beam.o nblock.o jmfernandez.o board.o parser.o scanner.o
LIBS=-lpthread -lstdc++ -lrt

all: greedy beam

# A plain `make` on a fresh clone builds TBB itself first.
$(TBB_LIB):
	../scripts/build-tbb.sh

greedy: $(TBB_LIB) greedy.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ greedy.o board.o parser.o scanner.o $(LDFLAGS) -ltbb -ltbbmalloc_proxy $(LIBS)

beam: $(TBB_LIB) beam.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ beam.o board.o parser.o scanner.o $(LDFLAGS) -ltbb -ltbbmalloc_proxy $(LIBS)

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

jmfernandez: $(TBB_LIB) jmfernandez.o
	$(CC) $(CXXFLAGS) -o $@ jmfernandez.o $(LDFLAGS) -ltbb -ltbbmalloc $(LIBS)

check: greedy
	./greedy gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm -f $(OBJS)

# board.h pulls in tbb/concurrent_priority_queue.h, so anything that
# includes it needs TBB's headers built first.
greedy.o: stopwatch.h board.h parser.h scanner.h $(TBB_LIB)
beam.o: stopwatch.h board.h parser.h scanner.h $(TBB_LIB)
board.o: stopwatch.h board.h $(TBB_LIB)
parser.o: stopwatch.h board.h parser.h scanner.h $(TBB_LIB)
scanner.o: scanner.h
jmfernandez.o: $(TBB_LIB)
