all: reaching-definitions.so

#We'll want to force clang usage rather than g++
CXX=clang

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) -g -O0

dataflow.o: dataflow.cpp dataflow.h

ExtAPI.o: ExtAPI.cpp ExtAPI.h

%.so: %.o dataflow.o ExtAPI.o
	$(CXX) -dylib -flat_namespace -shared -fcolor-diagnostics $^ -o $@

clean:
	rm -f *.o *~ *.so

.PHONY: clean all
