#/bin/bash
f=$1
opt -load ./FuncDef.so -funcdef $f -o /dev/null 
