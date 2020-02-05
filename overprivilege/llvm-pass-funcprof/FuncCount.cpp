#include <vector>
#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <string>

using namespace llvm;

namespace {
  struct FuncCountPass : public FunctionPass {
    static char ID;
    FuncCountPass() : FunctionPass(ID) {}

    virtual bool runOnFunction(Function &F) {
      errs() << F.getName() << "\n";
      return false;
    }
  };
}

char FuncCountPass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FuncCount.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FuncCountPass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FuncCount.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FuncCountPass> X("funccount", "FunctionCount Pass");

