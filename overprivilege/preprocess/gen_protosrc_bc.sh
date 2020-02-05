# Make sure/apollo/llvm_gen/protosrc has been created
cd /apollo
s1=$(ls -l bazel-apollo | cut -d'.' -f1 | cut -d'>' -f2 | cut -d' ' -f2)
s2=$(ls -l bazel-apollo | cut -d'.' -f2)
dir=$(echo "$s1.$s2")
n=$(echo $dir | grep -o '/' | wc -l);
build_dir=$(echo $dir | cut -d'/' -f1-$(($n-1)))
cd $build_dir/external/com_google_protobuf/src/
find . -type f -name *.cc > /apollo/protosrc_bitcode.meta
if [ ! -e /apollo/llvm_gen ]; then mkdir /apollo/llvm_gen; fi
if [ ! -e /apollo/llvm_gen/protosrc ]; then mkdir /apollo/llvm_gen/protosrc; fi
for f in $(cat /apollo/protosrc_bitcode.meta);
do
    a=$(python /apollo/bc_name.py $f)
    if [ -e /apollo/llvm_gen/protosrc/$a ]; then continue; fi
    echo "Generating bc for $f"
    clang -c -emit-llvm -std=c++11 $build_dir/external/com_google_protobuf/src/$f -o /apollo/llvm_gen/protosrc/$a
done
