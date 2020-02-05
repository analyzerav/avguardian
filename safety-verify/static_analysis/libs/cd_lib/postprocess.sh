cat $1 | grep "CD Rsult \|Sink Function: \|Boolean expr: " > $2 
cat $1 | grep "INVOKE\|CALL" | cut -d'-' -f2,3 | sort | uniq > caller.meta 
cat $1 | grep "INVOKE\|CALL" | cut -d'-' -f4,5 | sort | uniq > callee.meta
cat caller.meta callee.meta | sort | uniq > func.map

