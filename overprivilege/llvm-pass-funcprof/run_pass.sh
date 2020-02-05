#/bin/bash
rm *.prof
# bitcode_file, bitcode_dir
bc_dir=$2
for f in $1
do
 outf=$(echo "$f" | cut -d'_' -f2- | cut -d'.' -f1);
 outf1=$(echo "$outf.out")
 opt -load ./FuncProf.so -funcprof $f -o /dev/null &> $outf1
 opt -load ./FuncCount.so -funccount $f -o /dev/null &> func.cnt
 cat func.cnt | sort | uniq | wc -l > funccnt.tmp
 mv funccnt.tmp func.cnt
 ls *.prof | cut -d'.' -f1 > profile.meta
 opt -load ./FuncRef.so -funcref $f -o /dev/null &> $outf1
 cp $outf1 external.out
 while [ $(cat external.out | grep "NewFunc: " | wc -l) -gt 0 ]; do
  # extracting externally defined func
  cat external.out | grep "NewFunc: " | cut -d' ' -f2 | sort | uniq > newfunc.out
  rm profile.def
  # locate bitcode files for externally defined func
  for ff in $(cat newfunc.out); 
  do
     cat funcdef.meta | grep "$ff " | head -n1 >> profile.def
  done
  # determine what external funcs to extract target vars when analyzing each new bitcode file
  rm external.out
  for ff in $(cut -d' ' -f2 profile.def | sort | uniq);
  do
     # profile.meta: group of external func in a same new bitcode file
     cat profile.def | grep " $ff" | cut -d' ' -f1 > profile.meta  
     outf=$(echo $ff | cut -d'_' -f2- | cut -d'.' -f1);
     outf1=$(echo "$outf.comb")
     diff_str="xx"
     rm prev_comb
     # Extract more target vars based on current target set until converge
     while [ ! -z "$diff_str" ]; do
       echo "FuncComb pass"
       opt -load ./FuncComb.so -funccomb $bc_dir/$ff -o /dev/null &> $outf1
       if [ -e prev_comb ]; then diff_str=$(diff $outf1 prev_comb); fi
       cp $outf1 prev_comb
     done
     outf1=$(echo "$outf.out")
     opt -load ./FuncCount.so -funccount $bc_dir/$ff -o /dev/null &> func.cnt
     cat func.cnt | sort | uniq | wc -l > funccnt.tmp
     mv funccnt.tmp func.cnt
     opt -load ./FuncRef.so -funcref $bc_dir/$ff -o /dev/null &> $outf1
     cat $outf1 >> external.out
  done
 done
done
rm func.cnt external.out profile.def profile.meta newfunc.out prev_comb
echo "Determine dataflow order"
if [ -e funcdef.meta ] && [ -e funccall.meta ]; then 
   ls *.prof | cut -d'.' -f1 > func.meta
   ./Call > funcorder.meta
fi
echo "Update taint"
cut -d',' -f2 funcorder.meta > profile.meta
for func in $(cat funcorder.meta | cut -d',' -f2);
do
    file=$(cat funcdef.meta | grep "$func " | head -n1 | cut -d' ' -f2)
    echo $func > func.meta
    opt -load ./FuncTaint.so -functaint $bc_dir/$file -o /dev/null
done
rm profile.meta
