# AVGuardian 

AVGuaridan is a static anlaysis and formal verification framework for AV software systems. 

Currently it is prootyped on top of a popular open-source AV software code base [Baidu Apollo](https://github.com/ApolloAuto/apollo/). 

There are two major parts in the current code base. 

The first one is a publish-subscribe message overprivilege detection tool in `overprivilege`

The second one is a safety driving rule compliance verification framework in `safety-verify`

The instructions of tool setup are detailed in README of each folder

In addition, please refer to `apollo_llvm_compile` for the LLVM's bitcode generation (with DEBUG option) for Baidu Apollo's source code
