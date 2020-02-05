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
  struct FuncCombPass : public FunctionPass {
    static char ID;
    FuncCombPass() : FunctionPass(ID) {}

    // Target message types
    std::map<std::string, std::vector<bool> > targetMsg;
    // Target var for message fields <type, msg, field>
    std::map< std::string, std::map< std::string, std::tuple<std::string, std::string, int> > > func_var_field;
    // Target variables identified from input args or local variables in a function
    std::map< std::string, std::map<std::string, std::string> > func_var;
    // Function call instructions with args info (located by offset)
    std::map< std::string, std::map< int, std::vector<std::string> > > func_call_param;
    // Function args info
    std::map< std::string, std::map<std::string, std::string> > func_arg;
    // Function calls taking target vars as args (by its index)
    std::map< std::string, std::map< int, std::list<int> > > func_call;
    // Function ret vars info
    std::map< std::string, std::map<std::string, std::string> > func_ret;
    // Function callee info
    std::map< std::string, std::map<int, std::string> > func_callee;
    // Function arg list
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
      if (func_call.find(func) == func_call.end())
        func_call[func] = std::map< int, std::list<int> >();
      if (func_call_param.find(func) == func_call_param.end())
        func_call_param[func] = std::map< int, std::vector<std::string> >();
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
      // Read functions defined in an external bc file to be analyzed and their existing func prof containing target args
      std::ifstream myfile("profile.meta");
      std::string func;
      if (myfile.is_open()) {
        while (std::getline(myfile, func)) {
           initFuncProf(func);
           loadExtFuncProf(func);
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

  // Reach which arguments need to be analyzed for a target func defined in an external bc file
  void loadExtFuncProf(std::string func) {
    std::string file_name = func + ".prof";
    std::ifstream infile;
    std::string line, label;
    infile.open(file_name.c_str());
    if (!infile.is_open())
        return;
    if (ext_func_arg.find(func) == ext_func_arg.end())
        ext_func_arg[func] = std::map<int, bool>();
    while (!infile.eof()) {
       getline(infile, line);
       if (line.length() == 0)
           continue;
       label = line.substr(0, line.find(":"));
       line = line.substr(line.find(" ")+1);
       // TODO: validate no existence of FuncCall label
       if (label == "FuncCall") {
           std::string::size_type sz;
           std::string callee = line.substr(line.find(":")+1);
           callee = callee.substr(0, callee.find(" "));
           if (ext_func_arg.find(callee) == ext_func_arg.end())
               ext_func_arg[callee] = std::map<int, bool>();
           line = line.substr(line.find("=>")+3);
           while (line.find(" ") != std::string::npos) {
              int i = std::stoi(line, &sz);
              ext_func_arg[callee][i] = true;
              line = line.substr(line.find(" ")+1);
           }
           if (line.length() > 0) {
              int i = std::stoi(line, &sz);
              ext_func_arg[callee][i] = true;
           }
       }
       else {
           if (label == "Argument") {
              int i = atoi(line.c_str());
              ext_func_arg[func][i] = true;
              continue;
           }
           std::string var_str = line.substr(0, line.find(" "));
           std::string type_str = line.substr(line.find("=>")+3);
           std::string taint_str = "";
           if (type_str.find("=>") != std::string::npos) {
              taint_str = type_str.substr(type_str.find("=>")+3);
              type_str = type_str.substr(0, type_str.find("=>")-1);
           }
          // if (type_str != "")
          //    func_var[func][var_str] = type_str;
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
          if (type_str != "")
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

  std::string getFuncPointer(Instruction* inst, int num_arg) {
    int j = 0;
    std::string var_str = "";
    for (User::op_iterator i = inst->op_begin(), e = inst->op_end(); i != e; ++i) {
         // Function pointer case
         if ((isa<InvokeInst>(*inst) || isa<CallInst>(*inst)) && j >= num_arg) {
             if (Instruction *vi = dyn_cast<Instruction>(*i)) {
                 //errs() << "Define: " << *vi << " in " << *inst << "\n";
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

  bool getFuncArg(Function& F) {
    bool target_func = false;
    int i = 0;
    std::string func = F.getName().str();
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
          Argument& A = *arg;
          std::string arg_str = argToString(&A);
          std::string var_str = arg_str.substr(arg_str.find_last_of(" ")+1);
          std::string type_str = typeToString(A.getType());
          func_arg[func][var_str] = type_str;
          // Target variable ref/pointer found
          if (ext_func_arg[func].find(i) != ext_func_arg[func].end() || targetMsg.find(typeSanitizer(type_str)) != targetMsg.end()) {// && arg_str.find("* "+var_str) != std::string::npos) {
              target_func = true;
              // Target variable name, type
              func_var[func][var_str] = type_str;
              errs() << "Argument: " << arg_str << ", var: " << var_str << ", type: " << type_str << "\n";
          }
          // Bookkeep idx of arguments
          func_arg_idx[func][var_str] = i;
          i++;
    }
    return target_func;
  }

  bool getVarType(Function& F) {
    std::string ret_var_str, ret_type_str;
    std::string type_str, var_str;
    std::string func = F.getName().str();
    bool found = false;
    int inst_idx = 0;
//    errs() << "****************** START VAR TYPE FOR FUNCTION: " << F.getName() << " ******************\n\n";
    // Analyze function instructions
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          Instruction &II = *I;
          //errs() << "Type analysis: " << *I << "\n";
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
              if (var_str == "") {
                  std::string gstr = instrToString(&II);
                  gstr = gstr.substr(gstr.find_last_of('*')+1);
                  var_str = gstr.substr(0, gstr.find(','));
              }
              // Considering member access of a target variable and member recursively and accessed member as a target variable
              if (func_var[func].find(var_str) != func_var[func].end() || targetMsg.find(typeSanitizer(type_str)) != targetMsg.end() || targetMsg.find(typeSanitizer(ret_type_str)) != targetMsg.end()) {
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
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (AllocaInst* AI = dyn_cast<AllocaInst>(&II)) {
              ret_type_str = typeToString(AI->getAllocatedType());
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << " " << "\n";
          }
          else if (ReturnInst* RI = dyn_cast<ReturnInst>(&II)) {
              var_str = getOperandVar(&II);
              if (var_str == "")
                  type_str = "void";
              else
                  type_str = typeToString(RI->getReturnValue()->getType());
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  if (func_ret[func].find(var_str) == func_ret[func].end())
                      func_ret[func][var_str] = type_str;
                  errs() << "Ret: " << II << ", var: " << var_str << ", type: " << type_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << " " << "\n";
          }
          else if (LoadInst* LI = dyn_cast<LoadInst>(&II)) {
              ret_type_str = typeToString(LI->getType());
              var_str = getOperandVar(&II);
              type_str = typeToString(LI->getOperand(0)->getType());
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Pointer: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (BitCastInst* BI = dyn_cast<BitCastInst>(&II)) {
              ret_type_str = typeToString(BI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Cast: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (ZExtInst* ZI = dyn_cast<ZExtInst>(&II)) {
              ret_type_str = typeToString(ZI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Zext: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (SExtInst* SI = dyn_cast<SExtInst>(&II)) {
              ret_type_str = typeToString(SI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Sext: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (TruncInst* TI = dyn_cast<TruncInst>(&II)) {
              ret_type_str = typeToString(TI->getType());
              var_str = getOperandVar(&II);
              // ret_var_str not in func_var due to SSA
              if (func_var[func].find(var_str) != func_var[func].end()) {
                  func_var[func][ret_var_str] = ret_type_str;
                  if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                      func_taint[func][ret_var_str] = std::list<std::string>();
                  if (std::find(func_taint[func][ret_var_str].begin(), func_taint[func][ret_var_str].end(), var_str) == func_taint[func][ret_var_str].end())
                      func_taint[func][ret_var_str].push_back(var_str);
                  errs() << "Trunc: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << var_str << "\n";
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          else if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              func_call_param[func][inst_idx] = std::vector<std::string>();
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   func_call_param[func][inst_idx].push_back(var_str);
                   // Target variable involved
                   if (func_var[func].find(var_str) != func_var[func].end() || targetMsg.find(typeSanitizer(type_str)) != targetMsg.end()) {
                       if (func_var[func].find(var_str) == func_var[func].end())
                           func_var[func][var_str] = type_str;
                       // Only considering ref/pointer type
                       if (type_str.substr(type_str.length()-1) == "*") {
                           if (func_call[func].find(inst_idx) == func_call[func].end())
                               func_call[func][inst_idx] = std::list<int>();
                           if (func_callee[func].find(inst_idx) == func_callee[func].end())
                               func_callee[func][inst_idx] = getCallee(&II);
                           func_call[func][inst_idx].push_back(i);
                           errs() << "Parameter: " << II << ", var: " << var_str << ", type: " << type_str << "\n";
                       }
                       found = true;
                   }
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << " " << "\n";
          }
          else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              func_call_param[func][inst_idx] = std::vector<std::string>();
              for (int i = 0; i < CI->getNumArgOperands(); i++) {
                   type_str = typeToString(CI->getArgOperand(i)->getType());
                   if (CI->getArgOperand(i)->hasName())
                       var_str = "%"+CI->getArgOperand(i)->getName().str();
                   else
                       var_str = getUnamedVar(&II, i);
                   func_call_param[func][inst_idx].push_back(var_str);
                   // Target variable involved
                   if (func_var[func].find(var_str) != func_var[func].end() || targetMsg.find(typeSanitizer(type_str)) != targetMsg.end()) {
                       if (func_var[func].find(var_str) == func_var[func].end())
                           func_var[func][var_str] = type_str;
                       // Only considering reference/pointer type
                       if (type_str.substr(type_str.length()-1) == "*") {
                           if (func_call[func].find(inst_idx) == func_call[func].end())
                               func_call[func][inst_idx] = std::list<int>();                            
                           if (func_callee[func].find(inst_idx) == func_callee[func].end())
                               func_callee[func][inst_idx] = getCallee(&II);
                           func_call[func][inst_idx].push_back(i);
                           errs() << "Parameter: " << II << ", var: " << var_str << ", type: " << type_str << "\n";
                       }
                       found = true;
                   }   
              }
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << " " << "\n";
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
/*
              PHINode* PI = cast<PHINode>(&II);
              int incomingIdx = 0;
              for (User::op_iterator i = (&II)->op_begin(), e = (&II)->op_end(); i != e; ++i) {
                   Value* val = PI->getIncomingValue(incomingIdx);
                   var_str = "";
                   type_str = typeToString(val->getType());
                   if (isa<Instruction>(val) || isa<Argument>(val)) {
                       if (targetMsg.find(typeSanitizer(type_str)) != targetMsg.end()) {
                           if (Instruction *vi = dyn_cast<Instruction>(*i)) {
                               std::string str = instrToString(vi);
                               if (val->hasName())
                                   var_str = "%"+val->getName().str();
                               else if (str.find("=") != std::string::npos)
                                   var_str = str.substr(0, str.find("="));
                               if (func_var[func].find(var_str) == func_var[func].end())
                                   func_var[func][var_str] = type_str; 
                            }
                       }
                   }
                   incomingIdx++;
              }
*/
//              errs() << (&II)->getOpcodeName() << ": " << ret_var_str << " " << ret_type_str << "\n";
          }
          if (targetMsg.find(typeSanitizer(ret_type_str)) != targetMsg.end()) {
              if (func_var[func].find(ret_var_str) == func_var[func].end())
                  func_var[func][ret_var_str] = ret_type_str; 
              errs() << "LHS: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << "\n";
              found = true;
          }
          inst_idx++;
    }
//    errs() << "\n****************** END VAR TYPE FOR FUNCTION: " << F.getName() << " ******************\n\n";
    return found;
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
    for (std::map< int, std::list<int> >::iterator it = func_call[func].begin(); it != func_call[func].end(); ++it) {
        // Do not put indirect funccall into profile
        if (func_callee[func][it->first].find("%") != std::string::npos ||
            func_callee[func][it->first] == "_ZNK6google8protobuf10TextFormat7Printer5PrintERKNS0_7MessageERNS2_13TextGeneratorE" ||
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_8LaneInfoENS1_13LineSegment2dEEEE24GetNearestObjectInternalERKNS1_5Vec2dEPdPPKS7_" ||
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_8LaneInfoENS1_13LineSegment2dEEEE13GetAllObjectsEPSt6vectorIPKS7_SaISB_EE" ||
            func_callee[func][it->first] == "_ZNK6apollo6common4math17AABoxKDTree2dNodeINS_5hdmap15ObjectWithAABoxINS3_10SignalInfoENS1_13LineSegment2dEEEE18GetObjectsInternalERKNS1_5Vec2dEddPSt6vectorIPKS7_SaISE_EE" ||
            func_callee[func][it->first] == func)
            continue;
        std::string line = "FuncCall: " + std::to_string(it->first) + ":" + func_callee[func][it->first] + " => ";
        for (std::list<int>::iterator lt = it->second.begin(); lt != it->second.end(); ++lt)
            line += std::to_string(*lt) + ":" + func_call_param[func][it->first][*lt] + " ";
        setFuncProf(func, line);
    }
    for (std::map<std::string, std::string>::iterator it = func_ret[func].begin(); it != func_ret[func].end(); ++it)
        setFuncProf(func, "Ret: " + it->first + " => " + it->second);
  }

    virtual bool runOnFunction(Function &F) {
      if (targetMsg.size() == 0)
        if (loadTargetMessage("msg.meta") < 0)
            return false;
      std::string func = F.getName().str();
      if (ext_func_arg.find(func) == ext_func_arg.end())
          return false;
      bool found1 = getFuncArg(F); 
      bool found2 = getVarType(F);
      if (found1 || found2) {
        print_func_var(func);
        errs() << "\n****************** FOUND VAR TYPE FOR FUNCTION: " << F.getName() << " ******************\n\n";
      }
      return false;
    }
  };
}

char FuncCombPass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FuncComb.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FuncCombPass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FuncComb.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FuncCombPass> X("funccomb", "FunctionCombine Pass");

