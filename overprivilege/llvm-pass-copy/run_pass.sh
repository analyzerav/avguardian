#/bin/bash
f=$1
opt -load ./field-alias.so -cd-field-alias $f -o /dev/null
