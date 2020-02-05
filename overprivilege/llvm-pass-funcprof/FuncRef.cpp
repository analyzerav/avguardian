#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <stdio.h>

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/ValueMap.h"
#include "llvm/Support/CFG.h"

#include "llvm/IR/Instruction.h"
#include "llvm/Support/Casting.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <list>
#include <tuple>

using namespace llvm;

namespace {
  struct FuncRefPass : public FunctionPass {
    static char ID;
    FuncRefPass() : FunctionPass(ID) {}

    // Function count
    int func_cnt = -1;
    // Target message types
    std::map<std::string, std::vector<bool> > targetMsg;
    // Target var for message fields <type, msg, field>
    std::map< std::string, std::map< std::string, std::tuple<std::string, std::string, int> > > func_var_field;
    // Target variables identified from input args or local variables in a function
    std::map< std::string, std::map<std::string, std::string> > func_var;
    // Function call instructions with args info (located by offset), including each func and arg (whether or not target var is involved)
    std::map< std::string, std::map< int, std::vector<std::string> > > func_call_param;
    // Function args, only including those identified as target var
    std::map< std::string, std::map<std::string, std::string> > func_arg;
    // Function calls taking target vars as args (by its index)
    std::map< std::string, std::map< int, std::map<int, bool> > > func_call;
    // Function ret vars info
    std::map< std::string, std::map<std::string, std::string> > func_ret;
    // Function callee info <func, inst_idx, callee_name>
    std::map< std::string, std::map<int, std::string> > func_callee;
    // Function pass pointer
    std::map<std::string, Function*> func_pass;
    // Function updated
    std::map<std::string, bool> func_updated;
    // External defined function args as target vars
    std::map< std::string, std::map<int, bool> > ext_func_arg;
    // Tainted source
    std::map<std::string, std::map< std::string, std::list<std::string> > > func_taint;
    // Function arg index
    std::map< std::string, std::map<std::string, int> > func_arg_idx;

    void initFuncProf(std::string func) {
      if (func_arg.find(func) == func_arg.end())
          func_arg[func] = std::map<std::string, std::string>();
      if (func_var.find(func) == func_var.end())
          func_var[func] = std::map<std::string, std::string>();
      if (func_call_param.find(func) == func_call_param.end())
          func_call_param[func] = std::map< int, std::vector<std::string> >();
      if (func_call.find(func) == func_call.end())
          func_call[func] = std::map<int, std::map<int, bool> >();
      if (func_var_field.find(func) == func_var_field.end())
          func_var_field[func] = std::map< std::string, std::tuple<std::string, std::string, int> >();
      if (func_callee.find(func) == func_callee.end())
          func_callee[func] = std::map<int, std::string>();
      if (func_taint.find(func) == func_taint.end())
          func_taint[func] = std::map< std::string, std::list<std::string> >();
      if (func_arg_idx.find(func) == func_arg_idx.end())
          func_arg_idx[func] = std::map<std::string, int>();
    }

    int getFuncCnt(const char* file_name) {
      std::ifstream msgfile(file_name);
      std::string line;
      if (msgfile.is_open()) {
          std::getline(msgfile, line);
          func_cnt = atoi(line.c_str());
          msgfile.close();
          return 0;
      }
      return -1;
    }

    int loadTargetMessage(const char* file_name) {
      std::ifstream msgfile(file_name);
      std::string line, msg;
      if (msgfile.is_open()) {
        while (std::getline(msgfile, line)) {
          msg = line.substr(0, line.find(" "));
          targetMsg[msg] = std::vector<bool>(atoi(line.substr(line.find(" ")+1).c_str()), false);
        }
        msgfile.close();
      }
      else {
        return -1;
      }
      // Read existing func prof to get args, vars, callees, var_field
      std::ifstream myfile("profile.meta");
      std::string func;
      if (myfile.is_open()) {
        while (std::getline(myfile, func)) {
           initFuncProf(func); 
           loadFuncProf(func);
        }
        myfile.close();
        return 0;
      }
      return -1;
    }

  std::string typeToString(Type* t) {
    std::string type_str;
    raw_string_ostream rso(type_str);
    t->print(rso);
    return rso.str();
  }

  std::string valueToStr(const Value* value) {
    std::string instStr;
    raw_string_ostream rso(instStr);
    value->print(rso);
    if (!isa<Instruction>(value))
        return instStr;
    std::string str = rso.str();
    str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
    return str;
  }

  std::string instrToString(Instruction* I) {
    std::string inst_str;
    raw_string_ostream rso(inst_str);
    I->print(rso);
    std::string str = rso.str();
    str.erase(std::remove(str.begin(), str.end(), ' '), str.end());
    return str;
  }

  std::string argToString(Argument* arg) {
    std::string inst_str;
    raw_string_ostream rso(inst_str);
    arg->print(rso);
    std::string str = rso.str();
    return str;
  }

  std::string typeSanitizer(std::string str) {
    if (str.find('"') != std::string::npos) {
        str = str.substr(str.find('"')+1);
        str = str.substr(0, str.find('"'));
    }
    return str;
  }

  void clearFuncProf(std::string func) {
    std::string file_name = func + ".prof";
    std::ofstream myfile;
    myfile.open(file_name.c_str(), std::fstream::out);
    myfile.close();
  }

  void setFuncProf(std::string func, std::string line) { 
    std::string file_name = func + ".prof";
    std::ofstream myfile;
    myfile.open(file_name.c_str(), std::fstream::out | std::fstream::app);
    myfile << line << "\n";
    myfile.close();
  }

  std::string getFuncPointer(Instruction* inst, int num_arg) {
    int j = 0;
    std::string var_str = "";
    for (User::op_iterator i = inst->op_begin(), e = inst->op_end(); i != e; ++i) {
         // Function pointer case
         if ((isa<InvokeInst>(*inst) || isa<CallInst>(*inst)) && j >= num_arg) {
             if (Instruction *vi = dyn_cast<Instruction>(*i)) {
                 std::string str = instrToString(vi);
                 if (str.find("=") != std::string::npos) 
                     var_str = str.substr(0, str.find("="));
             }
             break;
         }
         j++;
    }
    return var_str;
  }

  // Return the name of first operand
  std::string getOperandVar(Instruction* inst) {
    if (inst->getNumOperands() > 0) {
        Value* val = inst->getOperand(0);
        if (val->hasName())
            return "%"+val->getName().str();
         std::string str = valueToStr(val);
         if (isa<Instruction>(val)) {
             if (val->hasName())
                 return "%"+val->getName().str();
             else if (str.find("=") != std::string::npos)
                 return str.substr(0, str.find("="));
         }
    }
    return "";
/*
    if (inst->getNumOperands() > 0)
        if (inst->getOperand(0)->hasName())
            return ("%"+inst->getOperand(0)->getName().str());
    std::string var_str = "";
    for (User::op_iterator i = inst->op_begin(), e = inst->op_end(); i != e; ++i) {
         if (Instruction *vi = dyn_cast<Instruction>(*i)) {
             Value* returnval = cast<Value>(vi);
             if (returnval->hasName()) {
                var_str = "%"+returnval->getName().str();
             }
             else {
                 std::string str = instrToString(vi);
                 if (str.find("=") != std::string::npos)
                     var_str = str.substr(0, str.find("="));
             }
             break;
         }
    }
    return var_str;
*/  }

  std::string getUnamedVar(Instruction* inst, int idx) {
    int j = 0;
    for (User::op_iterator i = inst->op_begin(), e = inst->op_end(); i != e; ++i) {
         if (Instruction *vi = dyn_cast<Instruction>(*i)) {
             if (j == idx) {
                 std::string str = instrToString(vi);
                 if (str.find("=") != std::string::npos) 
                     return str.substr(0, str.find("="));
             }
         }
         j++;
    } 
    return "";
  }

  // Return var by arg idx
  std::string getFuncArgByIdx(Function& F, int idx) {
    std::string func = F.getName().str();
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
          Argument& A = *arg;
          std::string arg_str = argToString(&A);
          std::string var_str = arg_str.substr(arg_str.find_last_of(" ")+1);
          std::string type_str = typeToString(A.getType());
          if (idx == 0) 
              return var_str;
          idx--;
    }
    return "";
  }

  // Add target var in func_var and func_arg
  void setFuncArg(Function& F, int idx) {
    std::string func = F.getName().str();
    int i = idx;
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
          Argument& A = *arg;
          std::string arg_str = argToString(&A);
          std::string var_str = arg_str.substr(arg_str.find_last_of(" ")+1);
          std::string type_str = typeToString(A.getType());
          // Target variable ref/pointer found
          if (i == 0) {// && arg_str.find("* "+var_str) != std::string::npos) {
              func_var[func][var_str] = func_arg[func][var_str] = type_str;
              func_arg_idx[func][var_str] = idx;
              errs() << "Argument: " << arg_str << ", var: " << var_str << ", type: " << type_str << "\n";
          }
          i--;
    }
  }

    bool getVarType(Function& F) {
       std::string ret_var_str, ret_type_str;
       std::string type_str, var_str;
       std::string func = F.getName().str();
       bool found = false; 
       int callee = 0;
       for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
           Instruction &II = *I;
           ret_type_str = typeToString(II.getType());
           ret_var_str = "";
           // Infer return type
           std::string str = instrToString(&II);
           if (str.find("=") != std::string::npos)
              ret_var_str = str.substr(0, str.find("="));
           // Find target variable
           if (GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(&II)) {
              ret_type_str = typeToString(cast<PointerType>(gepInst->getType()->getScalarType())->getElementType());
              type_str = typeToString(gepInst->getPointerOperandType());
              var_str = getOperandVar(&II);
              // Also considering member access of a target variable
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                 int offset = -1;
                 errs() << "Field: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", struct: " << typeSanitizer(type_str);
                 if (gepInst->getNumOperands() > 2) {
                     if (ConstantInt* CI = dyn_cast<ConstantInt>(gepInst->getOperand(2))) {
                         offset = CI->getZExtValue();
                         errs() << ", offset: " << offset;
                     }
                 }
                 func_var[func][ret_var_str] = ret_type_str;
                 func_var_field[func][ret_var_str] = std::make_tuple(ret_type_str, typeSanitizer(type_str), offset);
                 if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                     func_taint[func][ret_var_str] = std::list<std::string>();
                 if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                     func_taint[func][ret_var_str].push_back(var_str);
                 errs() << ", taint: " << var_str << "\n";
                 found = true;
              }
           }
           else if (LoadInst* LI = dyn_cast<LoadInst>(&II)) {
              ret_type_str = typeToString(LI->getType());
              var_str = getOperandVar(&II);
              type_str = typeToString(LI->getOperand(0)->getType());
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                     func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Pointer: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                  found = true;
              }
           }
           else if (BitCastInst* BI = dyn_cast<BitCastInst>(&II)) {
              ret_type_str = typeToString(BI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Cast: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                  found = true;
              }
           }
           else if (ZExtInst* ZI = dyn_cast<ZExtInst>(&II)) {
              ret_type_str = typeToString(ZI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Zext: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                  found = true;
              }
           }
           else if (SExtInst* SI = dyn_cast<SExtInst>(&II)) {
              ret_type_str = typeToString(SI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Sext: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                  found = true;
              }
           }
           else if (TruncInst* TI = dyn_cast<TruncInst>(&II)) {
              ret_type_str = typeToString(TI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(ret_var_str) == func_var[func].end() && func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Trunc: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                  found = true;
              }
           }
           else if (ReturnInst* RI = dyn_cast<ReturnInst>(&II)) {
              var_str = getOperandVar(&II);
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  if (func_ret[func].find(var_str) == func_ret[func].end())
                      func_ret[func][var_str] = func_var[func][var_str];
                  errs() << "Ret: " << II << ", var: " << var_str << ", type: " << func_var[func][var_str] << "\n";
                  found = true;
              }
           }
           else if (isa<PHINode>(II)) {
              PHINode* PI = cast<PHINode>(&II);
              ret_type_str = typeToString(PI->getType());
              for (int incomingIdx = 0; incomingIdx < PI->getNumIncomingValues(); incomingIdx++) {
                   Value* val = PI->getIncomingValue(incomingIdx);
                   var_str = "";
                   std::string str = valueToStr(val);
                   if (isa<Instruction>(val)) {
                       if (val->hasName())
                           var_str = "%"+val->getName().str();
                       else if (str.find("=") != std::string::npos)
                            var_str = str.substr(0, str.find("="));
                   }
                   else {
                       var_str = str.substr(str.find_last_of(" ")+1);
                   }
                   if (var_str != "") {
                       if (targetMsg.find(typeSanitizer(ret_type_str)) != targetMsg.end() || func_var[func].find(var_str) != func_var[func].end()) {
                           if (func_var[func].find(ret_var_str) == func_var[func].end())
                               func_var[func][ret_var_str] = ret_type_str;
                           if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                               func_taint[func][ret_var_str] = std::list<std::string>();
                           func_taint[func][ret_var_str].push_back(var_str);
                           found = true;
                           errs() << "PHI: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
                        }
                  }
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
           }
           else if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              bool new_callee = false;
              std::string callee_func = getCallee(&II);
//              errs() << "Debug: getCallee " << callee << "," << func << II  << "\n";
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   if (func_var[func].find(var_str) != func_var[func].end()) {
                       if (func_call[func].find(callee) == func_call[func].end())
                           func_call[func][callee] = std::map<int, bool>();
                       // TODO: only consider ref/pointer arg
                       if (func_call[func][callee].find(i) == func_call[func][callee].end()) {
                           errs() << "Parameter: " << II << ", var: " << var_str << ", type: " << type_str << ", idx: " << i << "\n";
                           found = true;
                           // new callee included (callee def found)
                           if (func_var.find(callee_func) == func_var.end() && func_pass.find(callee_func) != func_pass.end()) {
                               initFuncProf(callee_func);
                               setFuncArg(*func_pass[callee_func], i);
//                               errs() << "Debug: getCallee inside " << callee << II  << "\n";
                               func_callee[func][callee] = callee_func;
                               new_callee = true;
                           }
                           // new arg in existing callee included (callee def found)
                           else if (func_var.find(callee_func) != func_var.end() && func_pass.find(callee_func) != func_pass.end()) {
                               std::string arg_str = getFuncArgByIdx(*func_pass[callee_func], i);
//                               errs() << "Debug: getCallee inside inside " <<  callee << "," << func << "," << arg_str << II   << "\n";
                               if (arg_str != "" && func_arg[callee_func].find(arg_str) == func_arg[callee_func].end()) { 
                                   setFuncArg(*func_pass[callee_func], i); 
                                   new_callee = true;
                               }
                               func_callee[func][callee] = callee_func;
                           }
                           else if (func_pass.find(callee_func) == func_pass.end()) {
                               errs() << "NewFunc: " << callee_func << "\n";
                               if (ext_func_arg.find(callee_func) == ext_func_arg.end())
                                   ext_func_arg[callee_func] = std::map<int, bool>();
                               func_callee[func][callee] = callee_func;
                               ext_func_arg[callee_func][i] = true;
                           }
                       }
                       func_call[func][callee][i] = true;
                    }
              }
              if (new_callee) {
                 if (getVarType(*func_pass[callee_func]))
                     errs() << "****************** FOUND VAR TYPE FOR FUNCTION: " << callee_func << " ******************\n\n";
              }
           }
           else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              bool new_callee = false;
              std::string callee_func = getCallee(&II);
//              errs() << "Debug: getCallee " << callee << "," << func << II  << "\n";
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   if (func_var[func].find(var_str) != func_var[func].end()) {
//                       errs() << "Debug: getCallee found " << callee << "," << func << " " << var_str  << "\n";
                       if (func_call[func].find(callee) == func_call[func].end())
                           func_call[func][callee] = std::map<int, bool>();
                       // TODO: only consider ref/pointer arg
                       if (func_call[func][callee].find(i) == func_call[func][callee].end()) {
                           errs() << "Parameter: " << II << ", var: " << var_str << ", type: " << type_str << ", idx: " << i << "\n";
                           found = true;
                           // new callee included (callee def found)
                           if (func_var.find(callee_func) == func_var.end() && func_pass.find(callee_func) != func_pass.end()) {
                               initFuncProf(callee_func);
                               setFuncArg(*func_pass[callee_func], i);
//                               errs() << "Debug: getCallee inside " << callee << II  << "\n";
                               func_callee[func][callee] = callee_func;
                               new_callee = true;
                           }
                           // new arg in existing callee included (callee def found)
                           else if (func_var.find(callee_func) != func_var.end() && func_pass.find(callee_func) != func_pass.end()) {
                               std::string arg_str = getFuncArgByIdx(*func_pass[callee_func], i);
                               errs() << "Debug: getCallee inside inside " <<  callee << "," << func << "," << arg_str << II   << "\n";
                               if (arg_str != "" && func_arg[callee_func].find(arg_str) == func_arg[callee_func].end()) {
                                   setFuncArg(*func_pass[callee_func], i);
                                   new_callee = true;
                               }
                               func_callee[func][callee] = callee_func;
                           }
                           else if (func_pass.find(callee_func) == func_pass.end()) {
                               errs() << "NewFunc: " << callee_func << "\n";
                               if (ext_func_arg.find(callee_func) == ext_func_arg.end())
                                   ext_func_arg[callee_func] = std::map<int, bool>();
                               func_callee[func][callee] = callee_func;
                               ext_func_arg[callee_func][i] = true;
                           }
                       }
                       func_call[func][callee][i] = true;
                    }
              }
              if (new_callee) {
                 if (getVarType(*func_pass[callee_func]))
                     errs() << "****************** FOUND VAR TYPE FOR FUNCTION: " << callee_func << " ******************\n\n";
              }
           }
           callee++;
           if (targetMsg.find(typeSanitizer(ret_type_str)) != targetMsg.end()) {
              if (func_var[func].find(ret_var_str) == func_var[func].end())
                  func_var[func][ret_var_str] = ret_type_str;
              found = true;
              errs() << "LHS: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << "\n";
           }
       }
       if (found) 
           func_updated[func] = true;
       getCallFunc(F);
       return found;
  }
/*
  std::string traceTaintSrc(std::string func, std::string targetVar) {
    std::string curr_var = targetVar;
    std::string idx = "";
    while (func_taint[func].find(curr_var) != func_taint[func].end()) {
        if (idx == "")
            idx = std::to_string(std::get<2>(func_var_field[func][curr_var]));
        else
            idx = std::to_string(std::get<2>(func_var_field[func][curr_var])) + "," + idx;
        if (func_taint[func][curr_var].size() > 1)
            errs() << "Debug: getFieldIdx " << curr_var << " " << func_taint[func][curr_var].size() << "\n";
        curr_var = func_taint[func][curr_var].front();
        if (func_var_field[func].find(curr_var) == func_var_field[func].end())
            break;
    }
    if (idx != "")
       return (curr_var + ":" + idx);
    else
       return curr_var;
  }
*/
  void getCallFunc(Function& F) {
    std::string ret_var_str, ret_type_str;
    std::string type_str, var_str;
    std::string func = F.getName().str();
    bool found = false;
    int inst_idx = 0;
//    errs() << "****************** START FUNC CALL FOR FUNCTION: " << F.getName() << " ******************\n\n";
    // Analyze function instructions to extract new target vars from return vars of callee
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          Instruction &II = *I;
          //errs() << "Type analysis: " << *I << "\n";
          ret_type_str = typeToString(II.getType());
          ret_var_str = "";
          // Infer return type
          std::string str = instrToString(&II);
          if (str.find("=") != std::string::npos) 
              ret_var_str = str.substr(0, str.find("="));
          if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              std::string callee = getCallee(&II);
              // If callee is defined in current bc and not yet analyzed, create func_arg[callee] and analyze it by calling getVarType
              // If it is defined externally, add it in ext_func_arg
/*              if (func_call[func].find(inst_idx) != func_call[func].end() && func_pass.find(callee) == func_pass.end()) {
                  errs() << "NewFunc: " << callee << "\n";
                  if (ext_func_arg.find(callee) == ext_func_arg.end())
                      ext_func_arg[callee] = std::map<int, bool>();
              }
*/
              if (func_call[func].find(inst_idx) != func_call[func].end() && (func_arg.find(callee) == func_arg.end() || func_arg[callee].size() == 0)) {
                  if (func_pass.find(callee) == func_pass.end()) {
                      errs() << "NewFunc: " << callee << "\n";
                      if (ext_func_arg.find(callee) == ext_func_arg.end())
                          ext_func_arg[callee] = std::map<int, bool>();
                      for (std::map<int, bool>::iterator it = func_call[func][inst_idx].begin(); it != func_call[func][inst_idx].end(); it++)
                           ext_func_arg[callee][it->first] = true;
                  }
                  else {
                      initFuncProf(callee);
                      for (std::map<int, bool>::iterator it = func_call[func][inst_idx].begin(); it != func_call[func][inst_idx].end(); it++) 
                           setFuncArg(*func_pass[callee], it->first);
                      getVarType(*func_pass[callee]);
                  }
              }

              func_call_param[func][inst_idx] = std::vector<std::string>();
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   func_call_param[func][inst_idx].push_back(var_str);
                   if (func_var[func].find(var_str) != func_var[func].end() && ext_func_arg.find(callee) != ext_func_arg.end())
                       ext_func_arg[callee][i] = true;
              }
              // Find new target variables from callee return
              if (ret_var_str != "" && func_ret.find(callee) != func_ret.end()) {
                  if (func_var[func].find(ret_var_str) == func_var[func].end()) {
                      func_var[func][ret_var_str] = ret_type_str;
/*                      // Update taint src of return var
                      for (std::map<std::string, std::string>::iterator it = func_ret[callee].begin(); it != func_ret[callee].end(); it++) {
                           if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                               for (std::list<std::string>::iterator lt = func_taint[callee][it->first].begin(); lt != func_taint[callee][it->first].end(); lt++) {
                                    std::string callee_ret_taint = traceTaintSrc(callee, *lt);
                                    if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                        if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                            func_taint[func][ret_var_str] = std::list<std::string>();
                                        int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                        std::string caller_arg = getFuncArgByIdx(*func_pass[func], callee_arg_idx);
                                        func_taint[func][ret_var_str].push_back(caller_arg);
                                    }
                               }
                           }
                      }
                      std::string taint_str = "-1";
                      if (func_taint[func].find(ret_var_str) != func_taint[func].end())
                          taint_str = listToString(func_taint[func][ret_var_str]);
                      errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << taint_str << "\n";
*/
                      errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << "\n";
                      found = true;
                  }
              }
          }
          else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              std::string callee = getCallee(&II);
              // If callee is defined in current bc and not yet analyzed, create func_arg[callee] and analyze it by calling getVarType
              // If it is defined externally, add it in ext_func_arg
/*              if (func_call[func].find(inst_idx) != func_call[func].end() && func_pass.find(callee) == func_pass.end()) {
                  errs() << "NewFunc: " << callee << "\n";
                  if (ext_func_arg.find(callee) == ext_func_arg.end())
                      ext_func_arg[callee] = std::map<int, bool>();
              }
*/
              if (func_call[func].find(inst_idx) != func_call[func].end() && (func_arg.find(callee) == func_arg.end() || func_arg[callee].size() == 0)) {
                  if (func_pass.find(callee) == func_pass.end()) {
                      errs() << "NewFunc: " << callee << "\n";
                      if (ext_func_arg.find(callee) == ext_func_arg.end())
                          ext_func_arg[callee] = std::map<int, bool>();
                      for (std::map<int, bool>::iterator it = func_call[func][inst_idx].begin(); it != func_call[func][inst_idx].end(); it++)
                           ext_func_arg[callee][it->first] = true;
                  }
                  else {
                      initFuncProf(callee);
                      for (std::map<int, bool>::iterator it = func_call[func][inst_idx].begin(); it != func_call[func][inst_idx].end(); it++)
                           setFuncArg(*func_pass[callee], it->first);
                      getVarType(*func_pass[callee]);
                  }
              }

              func_call_param[func][inst_idx] = std::vector<std::string>();
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   func_call_param[func][inst_idx].push_back(var_str);
                   if (func_var[func].find(var_str) != func_var[func].end() && ext_func_arg.find(callee) != ext_func_arg.end())
                       ext_func_arg[callee][i] = true;
              }
              // Find new target variables from callee return
              if (ret_var_str != "" && func_ret.find(callee) != func_ret.end()) {
                  if (func_var[func].find(ret_var_str) == func_var[func].end()) {
                      func_var[func][ret_var_str] = ret_type_str;
/*                      // Update taint src of return var
                      for (std::map<std::string, std::string>::iterator it = func_ret[callee].begin(); it != func_ret[callee].end(); it++) {
                           if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                               for (std::list<std::string>::iterator lt = func_taint[callee][it->first].begin(); lt != func_taint[callee][it->first].end(); lt++) {
                                    std::string callee_ret_taint = traceTaintSrc(callee, *lt);
                                    if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                        if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                            func_taint[func][ret_var_str] = std::list<std::string>();
                                        int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                        std::string caller_arg = getFuncArgByIdx(*func_pass[func], callee_arg_idx);
                                        func_taint[func][ret_var_str].push_back(caller_arg);
                                    }
                               }
                           }
                      }
                      std::string taint_str = "-1";
                      if (func_taint[func].find(ret_var_str) != func_taint[func].end())
                          taint_str = listToString(func_taint[func][ret_var_str]);
                      errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << taint_str << "\n";
*/
                      errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << "\n";                      
                      found = true;
                  }
              }
          }
          inst_idx++;
    }
    if (found) {
       func_updated[F.getName()] = true;
       getVarType(F);
    }
//    errs() << "\n****************** END FUNC CALL FOR FUNCTION: " << F.getName() << " ******************\n\n";
  }

  void loadFuncProf(std::string func) {
    std::string file_name = func + ".prof";
    std::ifstream infile;
    std::string line, label;
    infile.open(file_name.c_str());
    if (!infile.is_open()) 
        return;
    while (!infile.eof()) {
       getline(infile, line);
       if (line.length() == 0)
           continue;
       label = line.substr(0, line.find(":"));
       line = line.substr(line.find(" ")+1);
       if (label == "FuncCall") {
           std::string callee_name = line.substr(line.find(":")+1);
           callee_name = callee_name.substr(0, callee_name.find(" "));
           std::string::size_type sz;
           int callee = std::stoi(line, &sz); 
           if (func_call[func].find(callee) == func_call[func].end())
               func_call[func][callee] = std::map<int, bool>();
           func_callee[func][callee] = callee_name;
           line = line.substr(line.find("=>")+3);
           int i;
           while (line.find(" ") != std::string::npos) {
              i = std::stoi(line, &sz); 
              func_call[func][callee][i] = true;
              line = line.substr(line.find(" ")+1);
           }
           if (line.length() > 0) {
              i = std::stoi(line, &sz);
              func_call[func][callee][i] = true;
           }     
       }
       else { 
          if (line.find(" ") == std::string::npos)
              break;
          std::string var_str = line.substr(0, line.find(" "));
          std::string type_str = line.substr(line.find("=>")+3);
          std::string taint_str = "";
          if (type_str.find("=>") != std::string::npos) {
              taint_str = type_str.substr(type_str.find("=>")+3);
              type_str = type_str.substr(0, type_str.find("=>")-1);
          }
          if (label == "Argument") {
              int idx = atoi(var_str.substr(var_str.find(",")+1).c_str());
              var_str = var_str.substr(0, var_str.find(","));
              func_arg_idx[func][var_str] = idx;
              func_arg[func][var_str] = type_str;
          }
          if (label == "Field") {
              std::string s1 = type_str.substr(0, type_str.find(";"));
              type_str = type_str.substr(type_str.find(";")+2);
              std::string s2 = type_str.substr(0, type_str.find(";"));
              std::string s3 = type_str.substr(type_str.find(";")+2);
              func_var_field[func][var_str] = std::make_tuple(s1, s2, stoi(s3));
              type_str = s1;
          }
          if (label == "Ret")
              func_ret[func][var_str] = type_str;
          // target vars
          func_var[func][var_str] = type_str;
          // taint src
          if (taint_str != "" && taint_str != "-1") {
              func_taint[func][var_str] = std::list<std::string>();
              while (taint_str.find(",") != std::string::npos) {
                 func_taint[func][var_str].push_back(taint_str.substr(0, taint_str.find(",")));
                 taint_str = taint_str.substr(taint_str.find(",")+1);
              }
              func_taint[func][var_str].push_back(taint_str);
          }
       }
    }
    infile.close();
  }

  std::string getCallee(Instruction* I) {
    Instruction &II = *I;
    if (CallInst* CI = dyn_cast<CallInst>(&II)) {
        Function* fun = CI->getCalledFunction();
        if (fun)
            return fun->getName();
        else
            return getFuncPointer(&II, CI->getNumArgOperands());
    }
    else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
        Function* fun = CI->getCalledFunction();
        if (fun)
            return fun->getName();
        else
            return getFuncPointer(&II, CI->getNumArgOperands());
    }
    return "";
  }

  std::string listToString(std::list<std::string> l) {
    std::string str = "";
    for (std::list<std::string>::iterator it = l.begin(); it != l.end(); it++)
        str += (*it + ",");
    if (str != "")
        return str.substr(0, str.length()-1);
    else
        return "-1";
  }

  void print_func_var(std::string func) {
    clearFuncProf(func);
    for (std::map<std::string, std::string>::iterator it = func_var[func].begin(); it != func_var[func].end(); ++it) {
        if (func_arg[func].find(it->first) != func_arg[func].end())
            setFuncProf(func, "Argument: " + it->first + "," + std::to_string(func_arg_idx[func][it->first]) + " => " + it->second);
        else if (func_var_field[func].find(it->first) != func_var_field[func].end())
            setFuncProf(func, "Field: " + it->first + " => " + std::get<0>(func_var_field[func][it->first])  + "; " + std::get<1>(func_var_field[func][it->first]) + "; " + std::to_string(std::get<2>(func_var_field[func][it->first])) + " => " + listToString(func_taint[func][it->first]));
        //else if (func_taint[func].find(it->first) == func_taint[func].end())
        //    setFuncProf(func, "Variable: " + it->first + " => " + it->second);
        else
            setFuncProf(func, "Variable: " + it->first + " => " + it->second + " => " + listToString(func_taint[func][it->first]));
    }
    for (std::map< int, std::map<int, bool> >::iterator it = func_call[func].begin(); it != func_call[func].end(); ++it) {
        // Do not put indirect funccall into profile
        if (func_callee[func][it->first].find("%") != std::string::npos || 
            func_callee[func][it->first] == "_ZNK6google8protobuf10TextFormat7Printer5PrintERKNS0_7MessageERNS2_13TextGeneratorE" || 
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_8LaneInfoENS1_13LineSegment2dEEEE24GetNearestObjectInternalERKNS1_5Vec2dEPdPPKS7_" || 
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_8LaneInfoENS1_13LineSegment2dEEEE13GetAllObjectsEPSt6vectorIPKS7_SaISB_EE" || 
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_10SignalInfoENS1_13LineSegment2dEEEE18GetObjectsInternalERKNS1_5Vec2dEddPSt6vectorIPKS7_SaISE_EE" || 
            func_callee[func][it->first] == func)
            continue;
        std::string line = "FuncCall: " + std::to_string(it->first) + ":" + func_callee[func][it->first] + " => ";
        for (std::map<int, bool>::iterator lt = it->second.begin(); lt != it->second.end(); ++lt)
            line += std::to_string(lt->first) + ":" + func_call_param[func][it->first][lt->first] + " ";
        setFuncProf(func, line);
    }
    for (std::map<std::string, std::string>::iterator it = func_ret[func].begin(); it != func_ret[func].end(); ++it)
        setFuncProf(func, "Ret: " + it->first + " => " + it->second);
  }

  void targetVarOnFunc() {
    for (std::map<std::string, Function*>::iterator it = func_pass.begin(); it != func_pass.end(); ++it) {
        // Update func_var, func_call, func_var_field
        getCallFunc(*(it->second));
    }
    for (std::map<std::string, bool>::iterator it = func_updated.begin(); it != func_updated.end(); ++it) {
        // Do not put indirect funccall into profile
        if (it->first.find("%") != std::string::npos)
            continue;
        print_func_var(it->first);
        errs() << "\n****************** UPDATE FUNC PROF FOR FUNCTION: " << it->first << " ******************\n\n";
    }
    for (std::map<std::string, std::map<int, bool> >::iterator it = ext_func_arg.begin(); it != ext_func_arg.end(); ++it) {
        // Do not put indirect funccall into profile
        if (it->first.find("%") != std::string::npos)
            continue;
        for (std::map<int, bool>::iterator lt = it->second.begin(); lt != it->second.end(); ++lt)
             setFuncProf(it->first, "Argument: " + std::to_string(lt->first));
        errs() << "\n****************** ADD FUNC PROF FOR FUNCTION: " << it->first << " ******************\n\n";
    }
  }

    virtual bool runOnFunction(Function &F) {
      if (func_cnt < 0)
        if (getFuncCnt("func.cnt") < 0)
            return false;
      if (targetMsg.size() == 0) 
        if (loadTargetMessage("msg.meta") < 0)
            return false;
      std::string func = F.getName().str();
      func_pass[func] = &F;
      // Load all func def in current bc file first
      if (func_pass.size() == func_cnt)
          targetVarOnFunc();
      return false;
    }
  };
}

char FuncRefPass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FuncRef.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FuncRefPass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FuncRef.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FuncRefPass> X("funcref", "FunctionRefine Pass");

