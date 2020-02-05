# Overprivilege detection tool

This tool consists of several LLVM passes defined as follows. It also consists of some preprocessing step

## Instructions

Generating LLVM's bitcode files of Apollo

1. Start Apollo container and build Apollo code base following the instructions in https://github.com/ApolloAuto/apollo

2. Once build succeeds, run preprocess/gen_*.sh (inside Apollo container) to generate bitcode files for Apollo and third-party libraries

3. Run specific llvm passes using run_pass.sh

## Directory layout of LLVM passes

1. llvm-pass-funcdef/

FuncDef: LLVM pass to identify bitcode file location for each function definition, output funcdef.meta

2. llvm-pass-funccall/

FuncCall:: LLVM pass to identify caller-callee relationship, output funccall.meta

3. llvm-pass-funcprof/

LLVM passes for identifying target message variables in Baidu Apollo

FuncProf: extract target message related variables to construct tainted variable set for each function, target message is defined in the input config file msg.meta

FuncRef: check ret_var of callees as tainted variable or not, update caller's tainted variable set, do this recursively until the tainted variable set is complete

FuncCount: get all defined func name in a bitcode file

FuncComb: reoslve external func def and extract target message related variables from them

CallOrder.cpp: generate the topological order and identify entry points of inter-procedural dataflow analysis, output funcorder.meta

FuncTaint: update taint source of return vars from call instructions

4. llvm-pass-reaching/

LLVM pass for generating function data-flow profile summary from reaching definition analysis

ReachingDefinitions: read funcorder.meta and each *.prof to perform reaching definition analysis of tainted vars, output per-function profile as *.df

5. llvm-pass-copy/

LLVM pass for extracting copy-from relationship among message fields to detect publisher-side overprivilege 

FieldAlias: read each *.df to generate copy-from publish-overprivileged fields

6. llvm-pass-defuse/

LLVM pass for generating function data-flow profile summary form define-use analysis to detect subscriber-side overprivilege

UseDef: read funcorder.meta and each *.prof to generate used fields, output per-function profile as *.uf
