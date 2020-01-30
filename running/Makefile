CXX=g++
CXXFLAGS+=-O3 -mtune=core2 -DNDEBUG
OBJS=main.o
LIBS=-lpthread

running: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS)
