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
  struct DefUsePass : public FunctionPass {
    static char ID;
    DefUsePass() : FunctionPass(ID) {}

    // Target functions
    std::map<std::string, bool> targetFunc;
    // Target var for message fields <type, msg, field>
    std::map< std::string, std::map< std::string, std::tuple<std::string, std::string, int> > > func_var_field;
    // Target variables identified from input args or local variables in a function
    std::map< std::string, std::map<std::string, std::string> > func_var;
    // Function call instructions with args info (located by offset)
    std::map< std::string, std::map< int, std::map<int, std::string> > > func_call_param;
    // Function args info
    std::map< std::string, std::map<std::string, std::string> > func_arg;
    // Function calls taking target vars as args (by its index)
    std::map< std::string, std::map< int, std::map<int, bool> > > func_call;
    // Function ret vars info
    std::map< std::string, std::map<std::string, std::string> > func_ret;
    // Function callee info
    std::map< std::string, std::map<int, std::string> > func_callee;
    // Tainted source
    std::map<std::string, std::map< std::string, std::list<std::string> > > func_taint;
    // Function arg index
    std::map< std::string, std::map<std::string, int> > func_arg_idx;
    // Function var use info: <func, var:field, type>
    std::map< std::string, std::map<std::string, std::string> > func_use;

    void initFuncProf(std::string func) {
      if (func_arg.find(func) == func_arg.end())
        func_arg[func] = std::map<std::string, std::string>();
      if (func_var.find(func) == func_var.end())
        func_var[func] = std::map<std::string, std::string>();
      if (func_call.find(func) == func_call.end())
          func_call[func] = std::map<int, std::map<int, bool> >();
      if (func_call_param.find(func) == func_call_param.end())
        func_call_param[func] = std::map< int, std::map<int, std::string> >();
      if (func_ret.find(func) == func_ret.end())
        func_ret[func] = std::map<std::string, std::string>();
      if (func_var_field.find(func) == func_var_field.end())
        func_var_field[func] = std::map< std::string, std::tuple<std::string, std::string, int> >();
      if (func_callee.find(func) == func_callee.end())
        func_callee[func] = std::map<int, std::string>();
      if (func_taint.find(func) == func_taint.end())
        func_taint[func] = std::map< std::string, std::list<std::string> >();
      if (func_arg_idx.find(func) == func_arg_idx.end())
        func_arg_idx[func] = std::map<std::string, int>();
      if (func_use.find(func) == func_use.end())
        func_use[func] = std::map<std::string, std::string>();
    }

    int loadTargetFunc(const char* file_name) {
      std::ifstream myfile(file_name);
      std::string func;
      if (myfile.is_open()) {
          while (std::getline(myfile, func)) {
             targetFunc[func] = true;
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

  void clearUseProf(std::string func) {
    std::string file_name = func + ".uf";
    std::ofstream myfile;
    myfile.open(file_name.c_str(), std::fstream::out);
    myfile.close();
  }

  void setUseProf(std::string func, std::string line) {
    std::string file_name = func + ".uf";
    std::ofstream myfile;
    myfile.open(file_name.c_str(), std::fstream::out | std::fstream::app);
    myfile << line << "\n";
    myfile.close();
  }

  void loadUseProf(std::string func) {
    std::string file_name = func + ".uf";
    std::ifstream infile;
    std::string line, label;
    if (func_use.find(func) == func_use.end())
        func_use[func] = std::map<std::string, std::string>();
    infile.open(file_name.c_str());
    if (!infile.is_open())
        return;
    while (!infile.eof()) {
       getline(infile, line);
       if (line.length() == 0)
           continue;
       label = line.substr(0, line.find(":"));
       line = line.substr(line.find(" ")+1);
       std::string var_str = line.substr(0, line.find(" "));
       std::string type_str = line.substr(line.find("=>")+3);
       if (label == "Argument") {
           int idx = atoi(var_str.substr(var_str.find(",")+1).c_str());
           var_str = var_str.substr(0, var_str.find(","));
           func_arg_idx[func][var_str] = idx;
           func_arg[func][var_str] = type_str;
       }
       else if (label == "Use") {
           if (type_str.find("=>") != std::string::npos)
               type_str = type_str.substr(0, type_str.find("=>")-1);
           func_use[func][var_str] = type_str;
       }
    }
    infile.close();
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
           func_call_param[func][callee] = std::map<int, std::string>();
           while (line.find(" ") != std::string::npos) {
              i = std::stoi(line, &sz);
              func_call[func][callee][i] = true;
              line = line.substr(sz);
              func_call_param[func][callee][i] = line.substr(1, line.find(" ")-1);
              line = line.substr(line.find(" ")+1);
           }
           if (line.length() > 0) {
              i = std::stoi(line, &sz);
              func_call[func][callee][i] = true;
              line = line.substr(sz);
              func_call_param[func][callee][i] = line.substr(1, line.find(" ")-1);
           }
       }
       else {
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
            return "";
    }
    else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
        Function* fun = CI->getCalledFunction();
        if (fun)
            return fun->getName();
        else
            return "";
    }
  }

/*
  std::string listToString(std::list<std::string> l) {
    std::string str = "";
    for (std::list<std::string>::iterator it = l.begin(); it != l.end(); it++)
        str += (*it + ",");
    if (str != "")
        return str.substr(0, str.length()-1);
    else
        return "-1";
  }
*/

  void getUseVar(Function& F) {
    std::string ret_var_str, ret_type_str;
    std::string func = F.getName().str(); 
    int inst_idx = 0;
//    errs() << "****************** START DEF USE FOR FUNCTION: " << F.getName() << " ******************\n\n";
    // Analyze function instructions
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          Instruction &II = *I;
          ret_type_str = typeToString(II.getType());
          ret_var_str = "";
          // Infer return type
          std::string str = instrToString(&II);
          if (str.find("=") != std::string::npos) 
              ret_var_str = str.substr(0, str.find("="));
          if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              std::string callee = getCallee(&II);
              // If callee targeted, read its use info, add into use set for func
              if (func_call[func].find(inst_idx) != func_call[func].end()) {
                  if (func_use.find(callee) == func_use.end())
                      loadUseProf(callee);
                  // for each caller's target arg, check if it or its taint dest is used in callee
                  for (std::map<std::string, std::string>::iterator it = func_use[callee].begin(); it != func_use[callee].end(); it++) {
                      std::string var_str = it->first;
                      std::string offset = "";
                      if (var_str.find(":") != std::string::npos) {
                          offset = var_str.substr(var_str.find(":"));
                          var_str = var_str.substr(0, var_str.find(":"));
                      } 
                      if (func_arg[callee].find(var_str) != func_arg[callee].end()) {
                          int callee_arg_idx = func_arg_idx[callee][var_str];
                          std::string caller_arg = valueToStr(CI->getArgOperand(callee_arg_idx));
                          if (caller_arg.find("=") != std::string::npos)
                              caller_arg = caller_arg.substr(0, caller_arg.find("="));
                          else if (caller_arg.find(" ") != std::string::npos)
                              caller_arg = caller_arg.substr(caller_arg.find_last_of(" ")+1);
                          func_use[func][caller_arg+offset] = it->second;
                          errs() << "Debug: " << func << ", " << callee << ", " << (caller_arg+offset) << "\n";
                      } 
                  }
              }
          }
          else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              std::string callee = getCallee(&II);
              // If callee targeted, read its use info, add into use set for func
              if (func_call[func].find(inst_idx) != func_call[func].end()) {
                  if (func_use.find(callee) == func_use.end())
                      loadUseProf(callee);
                  // for each caller's target arg, check if it or its taint dest is used in callee
                  for (std::map<std::string, std::string>::iterator it = func_use[callee].begin(); it != func_use[callee].end(); it++) {
                      std::string var_str = it->first;
                      std::string offset = "";
                      if (var_str.find(":") != std::string::npos) {
                          offset = var_str.substr(var_str.find(":"));
                          var_str = var_str.substr(0, var_str.find(":"));
                      }        
                      if (func_arg[callee].find(var_str) != func_arg[callee].end()) {
                          int callee_arg_idx = func_arg_idx[callee][var_str];
                          std::string caller_arg = valueToStr(CI->getArgOperand(callee_arg_idx));
                          if (caller_arg.find("=") != std::string::npos)
                              caller_arg = caller_arg.substr(0, caller_arg.find("="));
                          else if (caller_arg.find(" ") != std::string::npos)
                              caller_arg = caller_arg.substr(caller_arg.find_last_of(" ")+1);
                          func_use[func][caller_arg+offset] = it->second;
                          errs() << "Debug: " << func << ", " << callee << ", " << (caller_arg+offset) << "\n";
                      } 
                  }
              }
          }
          if (ret_var_str != "" && func_var[func].find(ret_var_str) != func_var[func].end()) {
              Instruction* instr = &II;
              for (Value::use_iterator i = instr->use_begin(), ie = instr->use_end(); i!=ie; ++i) {
                  if (Instruction *vi = dyn_cast<Instruction>(*i)) {
                      if (func_taint[func].find(ret_var_str) != func_taint[func].end()) {
                          if (func_taint[func][ret_var_str].size() > 0) {
                              std::string taint_str = func_taint[func][ret_var_str].front();
                              func_use[func][taint_str] = ret_type_str;
                              errs() << "Debug: normal " << func << ", " << ret_var_str << ", " << taint_str << "\n";
                          }
                          else {
                              func_use[func][ret_var_str] = ret_type_str;
                          }
                      }
                      else {
                          func_use[func][ret_var_str] = ret_type_str;
                      }
                      errs() << "Debug: normal " << func << ", " << ret_var_str << "\n";
                      break;
                  }
              }
          }
          inst_idx++;
    }
//    errs() << "\n****************** END DEF USE FOR FUNCTION: " << F.getName() << " ******************\n\n";
  }

  void print_func_var(std::string func) {
    clearUseProf(func);
    for (std::map<std::string, std::string>::iterator it = func_arg[func].begin(); it != func_arg[func].end(); ++it) 
         setUseProf(func, "Argument: " + it->first + "," + std::to_string(func_arg_idx[func][it->first]) + " => " + it->second);
    for (std::map<std::string, std::string>::iterator it = func_use[func].begin(); it != func_use[func].end(); ++it) {
         std::string var_str = it->first;
         if (var_str.find(":") != std::string::npos)
             var_str = var_str.substr(0, var_str.find(":"));
         if (func_var[func].find(var_str) != func_var[func].end())
             setUseProf(func, "Use: " + it->first + " => " + it->second + " => " + func_var[func][var_str]);
    }
  }

  virtual bool runOnFunction(Function &F) {
      if (targetFunc.size() == 0)
          if (loadTargetFunc("func.meta") < 0)
              return false;
      std::string func = F.getName().str();
      if (targetFunc.find(func) == targetFunc.end())
          return false;
      getUseVar(F);
      print_func_var(func);
      errs() << "\n****************** UPDATE DEF USE FOR FUNCTION: " << F.getName() << " ******************\n\n";
      return false;
    }
  };
}

char DefUsePass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./DefUse.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new DefUsePass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./DefUse.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<DefUsePass> X("defuse", "DefUse Pass");

