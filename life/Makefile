CC=g++
CXX=g++
# jmfernandez was written in 2011 against that vintage's TBB API
# (tbb::task_scheduler_init & co, removed in later TBB/oneTBB) and is pinned
# to a matching 2011-vintage TBB, built from third_party/ on first use — see
# third_party/README.md and scripts/build-tbb.sh.
TBB_ROOT=$(abspath ../third_party/tbb)
TBB_LIB=$(TBB_ROOT)/lib/libtbb.so.2

# greedy and beam were written in the 2020s against modern oneTBB. Linking
# them against the 2011 TBB above causes beam to livelock (its task
# scheduler predates the work-stealing fixes oneTBB shipped since); fetched
# at build time by scripts/fetch-onetbb.sh.
ONETBB_ROOT=$(abspath ../third_party/onetbb)
ONETBB_LIB=$(ONETBB_ROOT)/lib/libtbb.so.12

CXXFLAGS=-O3 -std=c++11 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free # -DDONTLEAK -DSTATS -DDEBUG
TBB_LDFLAGS=-L$(TBB_ROOT)/lib -Wl,-rpath,$(TBB_ROOT)/lib
ONETBB_LDFLAGS=-L$(ONETBB_ROOT)/lib -Wl,-rpath,$(ONETBB_ROOT)/lib
OBJS=greedy.o beam.o nblock.o jmfernandez.o board.o parser.o scanner.o
LIBS=-lpthread -lstdc++ -lrt

all: greedy beam

# A plain `make` on a fresh clone builds/fetches TBB first.
$(TBB_LIB):
	../scripts/build-tbb.sh

$(ONETBB_LIB):
	../scripts/fetch-onetbb.sh

greedy: $(ONETBB_LIB) greedy.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ greedy.o board.o parser.o scanner.o $(ONETBB_LDFLAGS) -ltbb -ltbbmalloc_proxy $(LIBS)

beam: $(ONETBB_LIB) beam.o board.o parser.o scanner.o
	$(CC) $(CXXFLAGS) -o $@ beam.o board.o parser.o scanner.o $(ONETBB_LDFLAGS) -ltbb -ltbbmalloc_proxy $(LIBS)

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

jmfernandez: $(TBB_LIB) jmfernandez.o
	$(CC) $(CXXFLAGS) -o $@ jmfernandez.o $(TBB_LDFLAGS) -ltbb -ltbbmalloc $(LIBS)

check: greedy
	./greedy gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm -f $(OBJS)

# board.h pulls in tbb/concurrent_priority_queue.h, so anything that
# includes it needs oneTBB's headers fetched first.
greedy.o beam.o board.o parser.o: CXXFLAGS += -I$(ONETBB_ROOT)/include
greedy.o: stopwatch.h board.h parser.h scanner.h $(ONETBB_LIB)
beam.o: stopwatch.h board.h parser.h scanner.h $(ONETBB_LIB)
board.o: stopwatch.h board.h $(ONETBB_LIB)
parser.o: stopwatch.h board.h parser.h scanner.h $(ONETBB_LIB)
scanner.o: scanner.h

# jmfernandez.cpp uses the 2011 TBB API (task_scheduler_init & co), removed
# from later TBB/oneTBB — it needs the 2011 headers, not oneTBB's.
jmfernandez.o: CXXFLAGS += -I$(TBB_ROOT)/include
jmfernandez.o: $(TBB_LIB)
