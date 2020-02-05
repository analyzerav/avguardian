
#ifndef __FIELD_ALIAS__
#define __FIELD_ALIAS__

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/IncludeFile.h"
#include "llvm/Support/InstIterator.h"

#include "dataflow.h"
#include <list>

namespace llvm {

//////////////////////////////////////////////////////////////////////////////////////////////
//Dataflow analysis
class FieldAliasDataFlow : public DataFlow {

  protected:
    BitVector applyMeet(std::vector<BitVector> meetInputs) {
      BitVector meetResult;

      //Meet op = union of inputs
      if (!meetInputs.empty()) {
        for (int i = 0; i < meetInputs.size(); i++) {
          if (i == 0)
            meetResult = meetInputs[i];
          else
            meetResult |= meetInputs[i];
        }
      }

      return meetResult;
    }

    TransferResult applyTransfer(const BitVector& value, DenseMap<Value*, int> domainEntryToValueIdx, BasicBlock* block) {
      TransferResult transfer;

      //First, calculate the set of downwards exposed definition generations and the set of killed definitions in this block
      int domainSize = domainEntryToValueIdx.size();
      BitVector genSet(domainSize);
      BitVector killSet(domainSize);
      for (BasicBlock::iterator instruction = block->begin(); instruction != block->end(); ++instruction) {
        DenseMap<Value*, int>::const_iterator currDefIter = domainEntryToValueIdx.find(&*instruction);
        if (currDefIter != domainEntryToValueIdx.end()) {
          //Kill prior definitions for the same variable (including those in this block's gen set)
          for (DenseMap<Value*, int>::const_iterator prevDefIter = domainEntryToValueIdx.begin();
               prevDefIter != domainEntryToValueIdx.end();
               ++prevDefIter) {
            std::string prevDefStr = valueToDefinitionVarStr(prevDefIter->first);
            std::string currDefStr = valueToDefinitionVarStr(currDefIter->first);
            if (prevDefStr == currDefStr) {
              killSet.set(prevDefIter->second);
              genSet.reset(prevDefIter->second);
            }
          }

          //Add this new definition to gen set (note that we might later remove it if another def in this block kills it)
          genSet.set((*currDefIter).second);
        }
      }

      //Then, apply transfer function: Y = GenSet \union (X - KillSet)
      transfer.baseValue = killSet;
      transfer.baseValue.flip();
      transfer.baseValue &= value;
      transfer.baseValue |= genSet;

      return transfer;
    }
};
//////////////////////////////////////////////////////////////////////////////////////////////

class FieldAlias : public ModulePass {
public:
  static char ID;

  FieldAlias();

  //List of reaching definitions at an instruction
  std::map<Value*, std::vector<Value*> > reaching_def_instr;

  // Redef indicator for var in func body: func_redef[func][var]
  std::map<std::string, std::map<std::string, bool> > func_redef;

  // Index of args redefined in func body
  std::map<std::string, std::set<std::string> > func_arg_rd;

  // List of alias set in func body: func_alias_set[func]
  std::map<std::string, std::vector<std::set<std::string> > > func_alias_set;

  // List of ret_val & arg alias
  std::map<std::string, std::set<std::string> > func_ret_alias;

  // List of arg & arg alias
  std::map<std::string, std::vector<std::pair<std::string, std::string> > > func_arg_alias;

  // Field in func body
  std::map<std::string, std::map<std::string, std::pair<std::string, int> > > func_field;

  std::map<std::string, std::set<std::string> > func_caller;
  std::map<std::string, std::set<std::string> > func_callee;
  std::vector<std::list<std::string> > ordered_callee;
  std::map<std::string, std::list<std::string> > recur_func;

  // Function def pointer
  std::map<std::string, Function*> func_def;

  // Function taint set
  std::map<std::string, std::map<std::string, std::set<std::string> > > func_taint_change;
  std::map<std::string, std::map<std::string, std::set<std::string> > > func_taint_copy;

  void initAnalyzeOrder(Module &M);
  void get_zero_callee_func(std::set<std::string>& proc_l);
  void traverse_call_chain(std::string s, std::list<std::string>& call_chain);
  void compute_caller_callee_order();

  // Add alias
  void updateAliasSet(std::string s1, std::string s2, std::vector<std::set<std::string> >& alias);

  void updateArgAlias(Function& F);

  std::string traceFuncRefArg(Function& F, std::string var_str);

  //Find reaching defintions of operands
  void getOperandDefVals(Function& F, Instruction* inst, std::map<Value*, std::vector<Value*> >& reaching_def_instr, std::vector<std::set<std::string> >& alias, Instruction* prev_inst);

  virtual bool runOnModule(Module &M);
  virtual void getAnalysisUsage(AnalysisUsage& AU) const;

private:
  void runOnFunction(Function& F, Module &M);
};

// void initializeFieldAliasPass(PassRegistry&);

}

FORCE_DEFINING_FILE_TO_BE_LINKED(FieldAlias);

#endif
