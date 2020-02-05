all: FuncCount.so FuncProf.so FuncRef.so FuncComb.so FuncTaint.so

CXXFLAGS = -rdynamic $(shell llvm-config --cxxflags) -std=c++11 -g -O0

%.so: %.o
	$(CXX) -dylib -shared $^ -o $@
clean:
	rm -f *.o *~ *.so
