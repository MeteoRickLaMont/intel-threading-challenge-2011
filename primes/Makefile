CXX=g++
CXXFLAGS+=-O3 -std=c++11 -DNDEBUG -msse2
# CXXFLAGS+=-g
OBJS=main.o primes.o powers.o searches.o output.o
LIBS=-lpthread

all: primesums jmfernandez

primesums: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

jmfernandez: jmfernandez.o
	$(CXX) $(CXXFLAGS) -o $@ jmfernandez.o -ltbb -ltbbmalloc

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
