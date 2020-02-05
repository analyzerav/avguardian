all: cd.so

CXXFLAGS = -rdynamic -fPIC -fvisibility-inlines-hidden -std=c++11 -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wmissing-field-initializers -pedantic -Wno-long-long -Wnon-virtual-dtor -Wdelete-non-virtual-dtor -ffunction-sections -fdata-sections -O3 -DNDEBUG  -fno-exceptions -fno-rtti -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -std=c++11 -I /z/yuqi/llvm/include/ -I /z/yuqi/llvm_debug_build/include/ -g -O0

%.so: %.o
	$(CXX) -dylib -shared $^ -L/z/yuqi/llvm_debug_build/lib -o $@
clean:
	rm -f *.o *~ *.so
