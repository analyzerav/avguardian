all: DefUse.so

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) -std=c++11 -g -O0

%.so: %.o
	$(CXX) -dylib -shared $^ -o $@
clean:
	rm -f *.o *~ *.so
