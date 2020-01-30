CC=g++
CXX=g++
CXXFLAGS=-O3
OBJS=nblock.o
LIBS=-lpthread

all: nblock

nblock: nblock.o
	$(CC) $(CXXFLAGS) -o $@ nblock.o $(LIBS)

check: nblock
	./nblock gridin.txt pathout.txt
	@cat pathout.txt

clean:
	rm $(OBJS)
