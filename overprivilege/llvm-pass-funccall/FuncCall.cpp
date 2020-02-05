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
  struct FuncCallPass : public FunctionPass {
    static char ID;
    FuncCallPass() : FunctionPass(ID) {}

    virtual bool runOnFunction(Function &F) {
      for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          Instruction &II = *I;
          if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              Function* fun = CI->getCalledFunction();
              if (fun)
                 errs() << F.getName() << " " << fun->getName() << "\n";
          }
          else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              Function* fun = CI->getCalledFunction();
              if (fun)
                 errs() << F.getName() << " " << fun->getName() << "\n";
          }
      }
      return false;
    }
  };
}

char FuncCallPass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FuncCall.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FuncCallPass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FuncCall.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FuncCallPass> X("funccall", "FunctionCall Pass");

