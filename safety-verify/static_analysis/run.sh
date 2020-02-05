if [ "${1}" = ""  ]; then
    echo "Input an argumet as the target bitcode *.bc"
    exit
fi

cpp_name=$(ls ${1}/*.cpp)
bc_name=$(ls ${1}/*.bc)
if [ "$cpp_name" != "" ]; then
    name="${cpp_name%%.*}"
    bash build_test.sh $name
else
    name="${bc_name%%.*}"
fi

echo ${1} > config.tmp
opt -load ./traffic-rule-info.so -traffic-rule-info ${name}.bc -o /dev/null
