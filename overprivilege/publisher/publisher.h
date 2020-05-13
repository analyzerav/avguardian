#ifndef __PUBLISHER_H__
#define __PUBLISHER_H__

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndexYAML.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/Utils/Evaluator.h"

#include "llvm/Analysis/CallGraph.h"

#include "reaching-definitions.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <unordered_set>
#include <set>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "publisher"

class Publisher : public ModulePass {
    // Function def pointer
    std::map<std::string, Function*> func_def;
    std::map<std::string, std::set<std::string> > func_caller;
    std::map<std::string, std::set<std::string> > func_callee;
    std::vector<std::list<std::string> > ordered_callee;
    std::map<std::string, std::list<std::string> > recur_func;
    std::map<std::string, std::unordered_set<std::string> > field_copy;
    std::unordered_set<std::string> field_modify;
//    std::map<Function*, std::unordered_set<Value*> > field_use;
    std::map<std::string, bool> TargetFunc;
    std::unordered_set<std::string> topic;
    std::map<Function*, std::unordered_set<Value*> > pub_msg_val;
    std::map<Function*, std::unordered_set<Value*> > msg_val; 
    std::map<Function*, std::map<Value*, std::set<Value*> > > pub_alias_set;
    std::map<Function*, std::map<Value*, std::set<Value*> > > alias_set;
    std::map<Function*, std::map<Value*, std::set<Value*> > > pub_pts_set;
    std::map<Function*, std::map<Value*, std::set<Value*> > > pts_set;
  public:
    static char ID;
    Publisher() : ModulePass(ID) {}
    bool runOnFunction(Module &M, Function &F);
    bool runOnModule(Module &M);
    void initAnalyzeOrder(Module &M);
    void get_zero_callee_func(std::set<std::string>& proc_l);
    void traverse_call_chain(std::string s, std::list<std::string>& call_chain);
    void compute_caller_callee_order();
    std::unordered_set<Value*> getDefinitions(Function *F, Instruction *I, Value *val);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        // add dependencies here
//        AU.addRequired<CallGraph>();
//        AU.addRequired<DataLayout>();
       AU.addRequired<ReachingDefinitions>();
      // AU.setPreservesAll();
    }

};

#endif
