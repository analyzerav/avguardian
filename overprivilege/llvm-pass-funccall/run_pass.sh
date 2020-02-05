#/bin/bash
f=$1
opt -load ./FuncCall.so -funccall $f -o /dev/null 
