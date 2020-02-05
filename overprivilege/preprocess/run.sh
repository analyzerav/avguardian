if [ -e funcdef.meta ]; then rm funcdef.meta; fi; for f in $(find . -type f -name *.bc); do opt -load ./FuncDef.so -funcdef $f -o /dev/null &> tmp; for l in $(cat tmp); do echo "$l $f" >> funcdef.meta; done; done; rm tmp
#if [ -e funccall.meta ]; then rm funccall.meta; fi; for f in $(find . -type f -name *.bc); do opt -load ./FuncCall.so -funccall $f -o /dev/null &>> funccall.meta; done

