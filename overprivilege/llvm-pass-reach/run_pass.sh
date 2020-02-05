for func in $(cat funcorder.meta | cut -d',' -f2);
do
    file=$(cat funcdef.meta | grep "$func " | head -n1 | cut -d' ' -f2)
    echo $func > func.meta
    echo $func
    opt -load ./reaching-definitions.so -cd-reaching-definitions ../../llvm-pass-reach/$file -o /dev/null &> $func.df
done  
