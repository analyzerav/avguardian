all: FunctionInfo.so

CXXFLAGS = -std=c++11 $(shell llvm-config --cxxflags) -g -O0
CXX = g++

dataflow.o: dataflow.cpp dataflow.h

reaching-definitions.o: reaching-definitions.cpp reaching-definitions.h

%.so: %.o dataflow.o reaching-definitions.o
	$(CXX) -shared $^ -o $@

clean:
	rm -f *.o *~ *.so
