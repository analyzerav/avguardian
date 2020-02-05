# Make sure /apollo/llvm_gen/curlpp has been created
cd /apollo
s1=$(ls -l bazel-apollo | cut -d'.' -f1 | cut -d'>' -f2 | cut -d' ' -f2)
s2=$(ls -l bazel-apollo | cut -d'.' -f2)
dir=$(echo "$s1.$s2")
n=$(echo $dir | grep -o '/' | wc -l);
build_dir=$(echo $dir | cut -d'/' -f1-$(($n-1)))
cd $build_dir/external/curlpp/src/
find . -type f -name *.cpp > /apollo/curlpp_bitcode.meta
if [ ! -e /apollo/llvm_gen ]; then mkdir /apollo/llvm_gen; fi
if [ ! -e /apollo/llvm_gen/curlpp ]; then mkdir /apollo/llvm_gen/curlpp; fi
for f in $(cat /apollo/curlpp_bitcode.meta);
do
    a=$(python /apollo/bc_name.py $f)
    if [ -e /apollo/llvm_gen/curlpp/$a ]; then continue; fi
    echo "Generating bc for $f"
    clang -c -emit-llvm -std=c++11 -I $build_dir/external/curlpp/include/ $build_dir/external/curlpp/src/$f -o /apollo/llvm_gen/curlpp/$a
done
