all: field-alias.so

#We'll want to force clang usage rather than g++
CXX=clang

CXXFLAGS = -rdynamic -std=c++11 $(shell llvm-config --cxxflags) -g -O0

dataflow.o: dataflow.cpp dataflow.h

field-alias.o: field-alias.cpp field-alias.h

%.so: %.o dataflow.o field-alias.o
	$(CXX) -dylib -flat_namespace -shared -fcolor-diagnostics $^ -o $@

clean:
	rm -f *.o *~ *.so

.PHONY: clean all
