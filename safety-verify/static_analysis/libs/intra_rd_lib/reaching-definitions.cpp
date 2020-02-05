#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

#include "reaching-definitions.h"

using namespace llvm;

char ReachingDefinitions::ID = 1;
static RegisterPass<ReachingDefinitions> X("cd-reaching-definitions",
    "Reaching Definitions", false, true);

