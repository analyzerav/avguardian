# AVGuardian 

AVGuardian is a static anlaysis and runtime mitiation system to perform security vulnerability analysis for AV software systems. 

Currently it is prototyped on top of a popular open-source AV software code base [Baidu Apollo](https://github.com/ApolloAuto/apollo/). 

There are two major parts in the current code base. 

The first one is a publish-subscribe message overprivilege detection tool in `overprivilege`

The second one is a runtime overprivilege mitigation on top of ROS in `runtime`

The instructions of tool setup are detailed in README of each folder

In addition, please refer to `apollo_llvm_compile` for the LLVM's bitcode generation for Baidu Apollo's source code
