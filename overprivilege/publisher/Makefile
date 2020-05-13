all: publisher.so

CXXFLAGS = -fPIC -std=c++11 $(shell llvm-config --cxxflags) -g -O0
CXX = g++

utils.o: utils.cpp utils.h

dataflow.o: dataflow.cpp dataflow.h

reaching-definitions.o: reaching-definitions.cpp reaching-definitions.h

%.so: %.o reaching-definitions.o dataflow.o utils.o
	$(CXX) -dylib -shared $(CXXFLAGS) $^ -o $@

clean:
	rm -f *.o *~ *.so

