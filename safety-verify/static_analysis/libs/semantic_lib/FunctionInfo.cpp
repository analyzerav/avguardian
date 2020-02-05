#include "llvm/Pass.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/DebugInfo.h"

#include <ostream>
#include <fstream>
#include <iostream>
#include <sstream> 
#include <set> 
#include <list>

#include <sys/stat.h>
#include <cxxabi.h>

#include "reaching-definitions.h"

using namespace llvm;


class FunctionInfo : public ModulePass {

public:
  static char ID;
  FunctionInfo() : ModulePass(ID) {}

  std::map<std::string, std::set<std::string>> TargetBB;
  std::map<std::string, std::set<std::string>> TargetFuncVar;
  std::map<std::string, bool> TargetType;

  std::map<std::string, std::map<std::string, Value*> > func_val_map;
  std::map<std::string, std::map<Value*, std::string> > func_instr_map;
  std::map<std::string, std::map<std::string, std::vector<std::string> > > func_reaching_def;

  std::string demangle(const char* name){
        int status = -1;
        std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
        return (status == 0) ? res.get() : std::string(name);
  }

  std::string valueToStr(const Value* value) {
        std::string instStr;
        llvm::raw_string_ostream rso(instStr);
        value->print(rso);
        return instStr;
  }

  std::string getValDefVar(const Value* def) {
       const int VAR_NAME_START_IDX = 2;
       std::string str = valueToStr(def);
       if (isa<ConstantInt>(def) || isa<ConstantFP>(def) || isa<Argument>(def)) {
           return str.substr(str.find_last_of(" ")+1);
       }
       else if (isa<BranchInst>(def)) {
           return "";
       }
       else if (isa<Instruction>(def)) {
           int varNameEndIdx = str.find(' ', VAR_NAME_START_IDX);
           str = str.substr(VAR_NAME_START_IDX, varNameEndIdx-VAR_NAME_START_IDX);
       }
       return str;
  }

  bool std_function(const char* name){
        std::string func_name = demangle(name);
        return (func_name.find("std::") != std::string::npos ||
                func_name.find("cxx") != std::string::npos ||
                func_name.find("gnu") != std::string::npos ||
                func_name.find("__") != std::string::npos);
  }

 std::string getPredicateName(CmpInst::Predicate Pred) {
   switch (Pred) {
   default:                   return "unknown";
   case FCmpInst::FCMP_FALSE: return "false";
   case FCmpInst::FCMP_OEQ:   return "oeq";
   case FCmpInst::FCMP_OGT:   return "ogt";
   case FCmpInst::FCMP_OGE:   return "oge";
   case FCmpInst::FCMP_OLT:   return "olt";
   case FCmpInst::FCMP_OLE:   return "ole";
   case FCmpInst::FCMP_ONE:   return "one";
   case FCmpInst::FCMP_ORD:   return "ord";
   case FCmpInst::FCMP_UNO:   return "uno";
   case FCmpInst::FCMP_UEQ:   return "ueq";
   case FCmpInst::FCMP_UGT:   return "ugt";
   case FCmpInst::FCMP_UGE:   return "uge";
   case FCmpInst::FCMP_ULT:   return "ult";
   case FCmpInst::FCMP_ULE:   return "ule";
   case FCmpInst::FCMP_UNE:   return "une";
   case FCmpInst::FCMP_TRUE:  return "true";
   case ICmpInst::ICMP_EQ:    return "eq";
   case ICmpInst::ICMP_NE:    return "ne";
   case ICmpInst::ICMP_SGT:   return "sgt";
   case ICmpInst::ICMP_SGE:   return "sge";
   case ICmpInst::ICMP_SLT:   return "slt";
   case ICmpInst::ICMP_SLE:   return "sle";
   case ICmpInst::ICMP_UGT:   return "ugt";
   case ICmpInst::ICMP_UGE:   return "uge";
   case ICmpInst::ICMP_ULT:   return "ult";
   case ICmpInst::ICMP_ULE:   return "ule";
   }
  }

  std::string getOpType(Value* I, std::string operand) {
        if (isa<LoadInst>(I) || isa<CastInst>(I)) {
            Instruction* II = dyn_cast<Instruction>(I);
            return std::string(II->getOpcodeName());
        }
        else if (GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(I)) {
            std::stringstream ss;
            ss << "field";
            if (gepInst->getNumOperands() > 1)
                if (ConstantInt* CI = dyn_cast<ConstantInt>(gepInst->getOperand(1)))
                    if (CI->getZExtValue() != 0)
                        ss << "+" << CI->getZExtValue();
            if (gepInst->getNumOperands() > 2)
                if (ConstantInt* CI = dyn_cast<ConstantInt>(gepInst->getOperand(2)))
                    ss << ":" << CI->getZExtValue();
            return ss.str();
        }
        else if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
            CallSite CS(I);
            Function *CalledFunc = CS.getCalledFunction();
            if (CalledFunc) {
                std::string func_name = CalledFunc->getName().str();
                std::stringstream ss;
                int j;
                Instruction* II = dyn_cast<Instruction>(I);
                for (j = 0; j < II->getNumOperands(); j++) 
                     if (getValDefVar(II->getOperand(j)) == operand)
                         break;
                ss << demangle(func_name.c_str()) << "-" << j;
                return ss.str();
            }
         }
         else if (Instruction* II = dyn_cast<Instruction>(I)) {
            std::stringstream ss;
            int j;
            for (j = 0; j < II->getNumOperands(); j++)
                 if (getValDefVar(II->getOperand(j)) == operand)
                     break;
            if (CmpInst* CI = dyn_cast<CmpInst>(I))
                ss << II->getOpcodeName() << "-" << getPredicateName(CI->getPredicate()) << "(" << j << ")";
            else
                ss << II->getOpcodeName() << "(" << j << ")";
            return ss.str();
         }
         return "";
  }

  void printSrcVar(std::string str, std::string var_chain, std::map<std::string, std::list<Value*>>& var_taint) {
       if (var_taint.find(str) != var_taint.end()) {
           for (auto val : var_taint[str]) {
                std::string next = getValDefVar(val);
                var_chain += ("->" + getOpType(val, str) + "," + next);
                printSrcVar(next, var_chain, var_taint);
           }
       }
       else {
           errs() << var_chain << "\n";
       }  
  }

  void traceSrcVar(BasicBlock *bb, std::string func_name) {
        errs() << "\n\n" << bb->getName() << "\n";
        std::list<Instruction*> curr_instr;
        std::map<std::string, std::list<Value*>> var_taint;
 //       std::list<Instruction*> instr_stack;
        if (const BranchInst *i = dyn_cast<BranchInst>(bb->getTerminator())) {
            if (i->isConditional()) 
                curr_instr.push_back(bb->getTerminator());
            while (curr_instr.size() > 0) { 
                Instruction* I = curr_instr.back();
                errs() << *I << "\t" << "src_file: " << srcDir(I) << "/" << srcFile(I) << ":" << lineNum(I);
                /*if (CmpInst* CI = dyn_cast<CmpInst>(I)) {
                    errs() << "\t" << "CmpInst: " << CI->getPredicate();
                    for (int j = 0; j < CI->getNumOperands(); j++) {
                         Value* val = CI->getOperand(j);
                         std::string var = getValDefVar(val);
                         errs() << " " << var;
                    }
                }
                else if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
                   CallSite CS(I);
                   Function *CalledFunc = CS.getCalledFunction();
                   if (CalledFunc) {
                       std::string func_name = CalledFunc->getName().str();
                       errs() << "\t" << "FuncCall: " << demangle(func_name.c_str());
                   }
                }*/
                curr_instr.pop_back();
    //            instr_stack.push_back(I);
                std::string instr = func_instr_map[func_name][I];
                for (int j = 0; j < I->getNumOperands(); j++) {
                     if (isa<InvokeInst>(I) && j >= I->getNumOperands() - 3) 
                         continue;
                     Value* val = I->getOperand(j);
                     for (int jj = 0; jj < func_reaching_def[func_name][instr].size(); jj++) {
                          Value* def_val = func_val_map[func_name][func_reaching_def[func_name][instr][jj]];
                          if (valueToDefinitionVar(def_val) == val) {
                              //if (def_val != val)
                              //    errs() << "\t" << "diff_def " << func_instr_map[func_name][def_val] << "," << func_instr_map[func_name][val] << "," << curr_instr.size();
                              //else
                              //    errs() << "\t" << "same_def " << func_instr_map[func_name][def_val] << "," << func_instr_map[func_name][val] << "," << curr_instr.size();
                             if (isa<Instruction>(def_val)) 
                                 if (dyn_cast<Instruction>(def_val) != I)
                                     if (TargetFuncVar[func_name].find(getValDefVar(def_val)) == TargetFuncVar[func_name].end()) 
                                         curr_instr.push_back(dyn_cast<Instruction>(def_val));
                             else if (isa<Argument>(def_val))
                                 errs() << "Arg: " << valueToStr(def_val) << "\n";
                             //errs() << "," << curr_instr.size();
                          }
                     }
                     std::string str = getValDefVar(val);
                     //errs() << "\t" << "curr " << str;
                     if (var_taint.find(str) == var_taint.end())
                         var_taint[str] = std::list<Value*>();
                     var_taint[str].push_back(I);
                     if (TargetFuncVar[func_name].find(str) != TargetFuncVar[func_name].end()) {
                         errs() << "\t" << "Operand: "; 
                         printSrcVar(str, str, var_taint);
 /*                        errs() << " " << str;
                         while (!instr_stack.empty()) {
                            errs() << "->"; // << getValDefVar(instr_stack.back());
                            Instruction* taint = instr_stack.back();
                            if (isa<CallInst>(taint) || isa<InvokeInst>(taint)) {
                                CallSite CS(taint);
                                Function *CalledFunc = CS.getCalledFunction();
                                if (CalledFunc) {
                                    std::string func_name = CalledFunc->getName().str();
                                    errs() << demangle(func_name.c_str());
                                    
                                } 
                            }
                            instr_stack.pop_back();
                         }
   */                  errs() << "Terminate at " << valueToStr(val) << "\n";  }
                       // Constant instruction type can inlcude func signatures
                       else if (!isa<GetElementPtrInst>(I) && (isa<ConstantInt>(val) || isa<ConstantFP>(val))) {
                         errs() << "\t" << "Operand: ";
                         std::stringstream ss;
                         if (ConstantInt* CI = dyn_cast<ConstantInt>(val))
                             ss << CI->getZExtValue();
                         else if (ConstantFP* CF = dyn_cast<ConstantFP>(val))
                             ss << CF->getValueAPF().convertToDouble(); 
                         printSrcVar(str, ss.str(), var_taint);
                       }
                }
                errs() << "\n";
/*
                // Checking if source-level vars or args are hit
                for (User::op_iterator it = I->op_begin(); it != I->op_end(); ++it) {
                     if (Instruction *vi = dyn_cast<Instruction>(*it))
                         if (TargetFuncVar[func_name].find(getValDefVar(vi)) == TargetFuncVar[func_name].end()) 
                             curr_instr.push_back(vi);
                     else if (isa<Argument>(*it))
                         errs() << "Arg: " << valueToStr(*it) << "\n";
                }
*/              
            }
        }
  }

  void getSrcInfo(BasicBlock *bb) {
        for (BasicBlock::iterator i_iter = bb->begin(); i_iter != bb->end(); ++i_iter) {
             Instruction &II = *i_iter;
             errs() << II;
             errs() << " src_line: " << lineNum(&II) << " src_file: " << srcDir(&II) << "/" << srcFile(&II) << "\n";
             for (Value::use_iterator it = II.use_begin(); it != II.use_end(); ++it) 
                  if (Instruction *vi = dyn_cast<Instruction>(*it)) 
                      errs() << "DefUse: " << *vi << "\n";
             for (User::op_iterator it = II.op_begin(); it != II.op_end(); ++it) 
                  if (Instruction *vi = dyn_cast<Instruction>(*it))
                      errs() << "UseDef: " << *vi << "\n";
        }
  }

  void runOnFunction(Function &F) {
        std::string func_name = F.getName().str();
        if (TargetBB.find(func_name) == TargetBB.end())
            return;
        errs() << "Doing runOnFunction " << func_name << "\n";
        // Extract source-level vars from args and entry BB
        for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
            std::string var = getValDefVar(&(*arg));
            if (var[1] >= '0' && var[1] <= '9')
                continue;
             TargetFuncVar[func_name].insert(var);
        }
        BasicBlock &mainEntryBB = F.getEntryBlock();
        for (BasicBlock::iterator i_iter = mainEntryBB.begin(); i_iter != mainEntryBB.end(); ++i_iter) {
            Instruction &II = *i_iter;
            if (isa<AllocaInst>(&II)) {
                std::string var = getValDefVar(&II);
                if (var[1] >= '0' && var[1] <= '9')
                    continue;
               
                std::string type_str = typeToString(II.getType());
                //errs() << "Found var " << var << " " << type_str;
                if (type_str.find('"') != std::string::npos) {
                    type_str = type_str.substr(type_str.find('"')+1);
                    type_str = type_str.substr(0, type_str.find('"'));
                }
                //errs() << " " << type_str << "\n";
                if (TargetType.find(type_str) == TargetType.end())
                    continue;
                TargetFuncVar[func_name].insert(var);
            }
        }
        /*for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
             Argument &AA = *arg;
             for (Value::use_iterator it = AA.use_begin(); it != AA.use_end(); ++it) {
                  if (Instruction *vi = dyn_cast<Instruction>(*it)) {
                      std::string var = getValDefVar(vi);
                      if (TargetFuncVar[func_name].find(var) != TargetFuncVar[func_name].end())
                          errs() << "Redef src_var: " << getValDefVar(&AA) << "->" << var << "\n";
                  }
             }
        }
        for (BasicBlock::iterator i_iter = mainEntryBB.begin(); i_iter != mainEntryBB.end(); ++i_iter) {
             Instruction &II = *i_iter;
             for (Value::use_iterator it = II.use_begin(); it != II.use_end(); ++it) {
                  if (Instruction *vi = dyn_cast<Instruction>(*it)) {
                      std::string var = getValDefVar(vi);
                      if (TargetFuncVar[func_name].find(var) != TargetFuncVar[func_name].end())
                          errs() << "Redef src_var: " << getValDefVar(&II) << "->" << var << "\n";
                  }
            }
        }*/
        errs() << func_name << " src_var: ";
        for (auto var : TargetFuncVar[func_name])
            errs() << " " << var;
        errs() << "\n";
        for (Function::iterator bb_iter = F.begin(); bb_iter != F.end(); ++bb_iter) {
             BasicBlock *bb = &(*bb_iter);
             if (TargetBB[func_name].find(bb->getName().str()) != TargetBB[func_name].end()) 
                 traceSrcVar(bb, func_name);
        }
  }

  void initFunc(Function& F) {
    std::string func_name = F.getName().str();
    func_val_map[func_name] = std::map<std::string, Value*>();
    func_instr_map[func_name] = std::map<Value*, std::string>();
    int arg_idx = 0;
    for (Function::arg_iterator arg = F.arg_begin(); arg != F.arg_end(); ++arg, arg_idx++) {
         func_val_map[func_name]["arg,"+std::to_string(arg_idx)] = &*arg;
         func_instr_map[func_name][&*arg] = "arg," + std::to_string(arg_idx);
    }
    for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
         std::string curr_bb = basicBlock->getName();
         int instr_idx = 0;
         for (BasicBlock::iterator instruction = basicBlock->begin(); instruction != basicBlock->end(); ++instruction, ++instr_idx) { 
              func_val_map[func_name][curr_bb+","+std::to_string(instr_idx)] = &*instruction;
              func_instr_map[func_name][&*instruction] = curr_bb + "," + std::to_string(instr_idx);
         }
    }
  }

  virtual bool runOnModule(Module &M) {
        //record the function-bb name
        std::ifstream infile("func_bb.meta");
           if (infile.is_open()) {
              std::string func_bb;
              while (std::getline(infile, func_bb)) {
                 std::string func = func_bb.substr(0, func_bb.find("-"));
                 if (TargetBB.find(func) == TargetBB.end())
                     TargetBB[func] = std::set<std::string>();
                 TargetBB[func].insert(func_bb.substr(func_bb.find("-")+1));
              }
              infile.close();
        }
        // Init target type
        TargetType["class.apollo::planning::PathObstacle"] = true;
        TargetType["class.apollo::planning::ReferenceLineInfo"] = true;

        ReachingDefinitions &RD = getAnalysis<ReachingDefinitions>();
        for (Function &F : M) {
          std::string func_name = F.getName().str();
          initFunc(F);
          if (RD.func_reaching_def.find(func_name) != RD.func_reaching_def.end()) {      
              func_reaching_def[func_name]= std::map<std::string, std::vector<std::string> >(); //std::map<Value*, std::vector<Value*> >();
              for (std::map<Value*, std::vector<Value*> >::iterator it = RD.func_reaching_def[func_name].begin(); it != RD.func_reaching_def[func_name].end(); it++) {
                   std::string curr_bb = RD.func_bb_name_map[func_name][RD.func_instr_bb_map[func_name][it->first]];
                   int curr_idx = RD.func_instr_id_map[func_name][it->first];
                   std::string curr_instr = curr_bb + "," + std::to_string(curr_idx);
                   func_reaching_def[func_name][curr_instr] = std::vector<std::string>();  
                   //func_reaching_def[func_name][it->first] = std::vector<Value*>();
                   for (int i = 0; i < it->second.size(); i++) {
                        std::string def_bb = RD.func_bb_name_map[func_name][RD.func_instr_bb_map[func_name][(it->second)[i]]];
                        int def_idx = RD.func_instr_id_map[func_name][(it->second)[i]];
                        std::string def_instr = def_bb + "," + std::to_string(def_idx);
                        func_reaching_def[func_name][curr_instr].push_back(def_instr); 
                        //func_reaching_def[func_name][it->first].push_back((it->second)[i]);
                   }
              }/*
              func_instr_bb_map[func_name] = std::map<Value*, BasicBlock*>();
              for (std::map<Value*, BasicBlock*>::iterator it = RD.func_instr_bb_map[func_name].begin(); it != RD.func_instr_bb_map[func_name].end(); it++)
                   func_instr_bb_map[func_name][it->first] = it->second;
              func_bb_name_map[func_name] = std::map<BasicBlock*, std::string>();
              for (std::map<BasicBlock*, std::string>::iterator it = RD.func_bb_name_map[func_name].begin(); it != RD.func_bb_name_map[func_name].end(); it++)
                   func_bb_name_map[func_name][it->first] = it->second;
              func_instr_id_map[func_name] = std::map<Value*, int>();
              for (std::map<Value*, int>::iterator it = RD.func_instr_id_map[func_name].begin(); it != RD.func_instr_id_map[func_name].end(); it++)
                   func_instr_id_map[func_name][it->first] = it->second; */
          }
          runOnFunction(F);
          errs() << "Done FunctionInfo " << func_name << "\n";
        }

        return false;
  }

  std::string srcDir(Instruction* I){
        if (MDNode *N = I->getMetadata("dbg")) {  // Here I is an LLVM instruction
                DILocation Loc(N);                      // DILocation is in DebugInfo.h
                return Loc.getDirectory().str();
        }
        return "NIL";
  }

  std::string srcFile(Instruction* I){
        if (MDNode *N = I->getMetadata("dbg")) {  // Here I is an LLVM instruction
                DILocation Loc(N);                      // DILocation is in DebugInfo.h
                return Loc.getFilename().str();
        }
        return "NIL";
  }

  unsigned lineNum(Instruction* I){
	if (MDNode *N = I->getMetadata("dbg")) {  // Here I is an LLVM instruction
		DILocation Loc(N);                      // DILocation is in DebugInfo.h
		unsigned Line = Loc.getLineNumber();
		return Line;
	}
        return 0;
  }

  // We don't modify the program, so we preserve all analyses
  //virtual 
  void getAnalysisUsage(AnalysisUsage &AU) const {
        AU.addRequired<ReachingDefinitions>();
        AU.setPreservesAll();
  }
};

// LLVM uses the address of this static member to identify the pass, so the
// initialization value is unimportant.
char FunctionInfo::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FunctionInfo.so loop.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FunctionInfo());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FunctionInfo.so -function-info loop.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FunctionInfo> X("function-info", "Function Information");
