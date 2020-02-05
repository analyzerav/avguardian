cd /apollo
s1=$(ls -l bazel-apollo | cut -d'.' -f1 | cut -d'>' -f2 | cut -d' ' -f2)
s2=$(ls -l bazel-apollo | cut -d'.' -f2)
dir=$(echo "$s1.$s2")
n=$(echo $dir | grep -o '/' | wc -l);
build_dir=$(echo $dir | cut -d'/' -f1-$(($n-1)))
if [ ! -e /apollo/llvm_gen ]; then mkdir /apollo/llvm_gen; fi
clang -c -emit-llvm -std=c++11 -I $build_dir/external/civetweb/include $build_dir/external/civetweb/src/CivetServer.cpp -o /apollo/llvm_gen/CivetServer.bc
clang -c -emit-llvm -I $build_dir/external/civetweb/include $build_dir/external/civetweb/src/civetweb.c -o /apollo/llvm_gen/civetweb.bc
