# Safety Rule Verfification  

This tool consists of a static anlaysis tool and theorem prover based on Z3 for proving the correction implementation of safety rules for AV software systems. 

Currently it is prootyped on top of a popular open-source AV software code base [Baidu Apollo](https://github.com/ApolloAuto/apollo/). 

There are two major parts in the current code base. 

The first one consists of LLVM-based control and data dependence analysis passes in `static_analysis`

The second one is a theorem prover based on Z3 in `constraint_solver`

The instructions of tool setup are detailed in README of each folder

In addition, please refer to `apollo_llvm_compile` for the LLVM's bitcode generation (with DEBUG option) for Baidu Apollo's source code
