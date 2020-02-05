./FuncDep > funcdep.out
for l in $(cat funcdep.out | grep -v "order \-1" | sort -n -k3 | sed 's/ /,/g')   
do
    fun=$(echo $l | cut -d',' -f1)
    file=$(echo $l | cut -d',' -f5)
    echo $fun > func.meta
    opt -load ./reaching-definitions.so -cd-reaching-definitions $file -o /dev/null
done
