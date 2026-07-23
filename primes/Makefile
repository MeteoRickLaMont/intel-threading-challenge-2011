CXX=g++
# Pinned 2011-vintage TBB, built from third_party/ on first use — see
# third_party/README.md and scripts/build-tbb.sh.
TBB_ROOT=$(abspath ../third_party/tbb)
TBB_LIB=$(TBB_ROOT)/lib/libtbb.so.2
CXXFLAGS+=-O3 -std=c++11 -DNDEBUG -msse2 -I$(TBB_ROOT)/include
LDFLAGS=-L$(TBB_ROOT)/lib -Wl,-rpath,$(TBB_ROOT)/lib
# CXXFLAGS+=-g
OBJS=main.o primes.o powers.o searches.o output.o
LIBS=-lpthread

all: primesums jmfernandez

# A plain `make` on a fresh clone builds TBB itself first.
$(TBB_LIB):
	../scripts/build-tbb.sh

primesums: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

jmfernandez: $(TBB_LIB) jmfernandez.o
	$(CXX) $(CXXFLAGS) -o $@ jmfernandez.o $(LDFLAGS) -ltbb -ltbbmalloc

check: primesums
	./primesums 1 29 6 sumsout.txt
	@cat sumsout.txt

clean:
	rm -f $(OBJS)

main.o: primes.h powers.h searches.h output.h common.h
primes.o: primes.h common.h
powers.o: primes.h powers.h common.h
searches.o: primes.h powers.h searches.h output.h common.h
output.o: powers.h output.h common.h
