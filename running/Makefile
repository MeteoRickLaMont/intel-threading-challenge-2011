CXX=g++
CXXFLAGS+=-O3 -mtune=core2 -DNDEBUG
OBJS=main.o
LIBS=-lpthread

running: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

check: running
	./running 365607bc731db54c82afa48a829ce0b4 7f8b22d034a08b997d84e9f12c799ebe 55f825ec17b9471020afad2615eaa31c
	@echo "431787735 cycles is correct result"

clean:
	rm -f $(OBJS)
