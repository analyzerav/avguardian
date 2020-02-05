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
  struct FuncTaintPass : public FunctionPass {
    static char ID;
    FuncTaintPass() : FunctionPass(ID) {}

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
    // Profiled func
    std::map<std::string, bool> func_profile;

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
      }
      else {
          return -1;
      }
      std::ifstream cfile("profile.meta");
      if (cfile.is_open()) {
        while (std::getline(cfile, func)) 
           func_profile[func] = true;
        cfile.close();
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

  std::string listToString(std::list<std::string> l) {
    std::string str = "";
    for (std::list<std::string>::iterator it = l.begin(); it != l.end(); it++)
        str += (*it + ",");
    if (str != "")
        return str.substr(0, str.length()-1);
    else
        return "-1";
  }

/*
  std::string traceTaintSrc(std::string func, std::string targetVar) {
    std::string curr_var = targetVar;
    std::string idx = "";
    while (func_taint[func].find(curr_var) != func_taint[func].end()) {
        if (idx == "")
            idx = std::to_string(std::get<2>(func_var_field[func][curr_var]));
        else
            idx = std::to_string(std::get<2>(func_var_field[func][curr_var])) + ";" + idx;
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

  std::string getCallerArg(Value *v) {
    std::string arg = valueToStr(v);
    if (arg.find("=") != std::string::npos)
        arg = arg.substr(0, arg.find("="));
    else if (arg.find(" ") != std::string::npos)
        arg = arg.substr(arg.find_last_of(" ")+1);
    return arg;
  }

  bool updateCallLHSTaint(Function& F) {
    std::string ret_var_str, ret_type_str;
    std::string func = F.getName().str();
    bool found = false;
//    errs() << "****************** START CALLEE TAINT FOR FUNCTION: " << F.getName() << " ******************\n\n";
    // Analyze function instructions
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
          Instruction &II = *I;
          ret_type_str = typeToString(II.getType());
          ret_var_str = "";
          // Infer return type
          std::string str = instrToString(&II);
          if (str.find("=") != std::string::npos) 
              ret_var_str = str.substr(0, str.find("="));
          if (ret_var_str == "")
              continue;
          // Find taint src for ret var
          if (CallInst* CI = dyn_cast<CallInst>(&II)) {
              std::string callee = getCallee(&II);
              // Callee targeted
              if (func_var[func].find(ret_var_str) != func_var[func].end()) {
                  if (func_ret.find(callee) == func_ret.end()) {
                      if (func_profile.find(callee) == func_profile.end())  
                          continue;
                      initFuncProf(callee);
                      loadFuncProf(callee);
                  }
                  // Update taint src of return var
                  for (std::map<std::string, std::string>::iterator it = func_ret[callee].begin(); it != func_ret[callee].end(); it++) {
                       /*if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                           for (std::list<std::string>::iterator lt = func_taint[callee][it->first].begin(); lt != func_taint[callee][it->first].end(); lt++) {
                                std::string callee_ret = traceTaintSrc(callee, *lt);
                                std::string callee_ret_taint = callee_ret;
                                std::string offset = "";
                                if (callee_ret_taint.find(":") != std::string::npos) {
                                    callee_ret_taint = callee_ret_taint.substr(0, callee_ret_taint.find(":"));
                                    offset = callee_ret.substr(callee_ret.find(":"));
                                }
                                if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                    if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                        func_taint[func][ret_var_str] = std::list<std::string>();
                                    int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                    std::string caller_arg = getCallerArg(CI->getArgOperand(callee_arg_idx));
                                    errs() << "Tainting " << func << ", " << callee << ", " <<  ret_var_str << ", " << (caller_arg+offset) << "\n";
                                    func_taint[func][ret_var_str].push_back(caller_arg+offset);
                                }
                           }
                       }*/
                         if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                                std::string callee_ret = update_first_taint(callee, it->first);
                                std::string callee_ret_taint = callee_ret;
                                std::string offset = "";
                                if (callee_ret_taint.find(":") != std::string::npos) {
                                    callee_ret_taint = callee_ret_taint.substr(0, callee_ret_taint.find(":"));
                                    offset = callee_ret.substr(callee_ret.find(":"));
                                }
                                if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                    if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                        func_taint[func][ret_var_str] = std::list<std::string>();
                                    int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                    std::string caller_arg = getCallerArg(CI->getArgOperand(callee_arg_idx));
                                    errs() << "Tainting " << func << ", " << callee << ", " <<  ret_var_str << ", " << (caller_arg+offset) << "\n";
                                    func_taint[func][ret_var_str].push_back(caller_arg+offset);
                                }
                          }
                  }
                  std::string taint_str = "-1";
                  if (func_taint[func].find(ret_var_str) != func_taint[func].end())
                      taint_str = listToString(func_taint[func][ret_var_str]);
                  errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << taint_str << "\n";
                  found = (taint_str != "-1");
              }
          }
          else if (InvokeInst* CI = dyn_cast<InvokeInst>(&II)) {
              std::string callee = getCallee(&II);
              // Callee targeted
              if (func_var[func].find(ret_var_str) != func_var[func].end()) {
                  if (func_ret.find(callee) == func_ret.end()) {
                      if (func_profile.find(callee) == func_profile.end()) 
                          continue;
                      initFuncProf(callee);
                      loadFuncProf(callee);
                  }
                  // Update taint src of return var
                  for (std::map<std::string, std::string>::iterator it = func_ret[callee].begin(); it != func_ret[callee].end(); it++) {
                       /*if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                           for (std::list<std::string>::iterator lt = func_taint[callee][it->first].begin(); lt != func_taint[callee][it->first].end(); lt++) {
                                std::string callee_ret = traceTaintSrc(callee, *lt);
                                std::string callee_ret_taint = callee_ret;
                                std::string offset = "";
                                if (callee_ret_taint.find(":") != std::string::npos) {
                                    callee_ret_taint = callee_ret_taint.substr(0, callee_ret_taint.find(":"));
                                    offset = callee_ret.substr(callee_ret.find(":"));
                                }
                                if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                    if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                        func_taint[func][ret_var_str] = std::list<std::string>();
                                    int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                    std::string caller_arg = getCallerArg(CI->getArgOperand(callee_arg_idx));
                                    errs() << "Tainting " << func << ", " << callee << ", " << ret_var_str <<  ", " << (caller_arg+offset) << "\n";
                                    func_taint[func][ret_var_str].push_back(caller_arg+offset);
                                }
                           }
                       }*/
                         if (func_taint[callee].find(it->first) != func_taint[callee].end()) {
                                std::string callee_ret = update_first_taint(callee, it->first);
                                std::string callee_ret_taint = callee_ret;
                                std::string offset = "";
                                if (callee_ret_taint.find(":") != std::string::npos) {
                                    callee_ret_taint = callee_ret_taint.substr(0, callee_ret_taint.find(":"));
                                    offset = callee_ret.substr(callee_ret.find(":"));
                                } 
                                if (func_arg[callee].find(callee_ret_taint) != func_arg[callee].end()) {
                                    if (func_taint[func].find(ret_var_str) == func_taint[func].end())
                                        func_taint[func][ret_var_str] = std::list<std::string>();
                                    int callee_arg_idx = func_arg_idx[callee][callee_ret_taint];
                                    std::string caller_arg = getCallerArg(CI->getArgOperand(callee_arg_idx));
                                    errs() << "Tainting " << func << ", " << callee << ", " <<  ret_var_str << ", " << (caller_arg+offset) << "\n";
                                    func_taint[func][ret_var_str].push_back(caller_arg+offset);
                                }
                          }
                  }
                  std::string taint_str = "-1";
                  if (func_taint[func].find(ret_var_str) != func_taint[func].end())
                      taint_str = listToString(func_taint[func][ret_var_str]);
                  errs() << "FuncRet: " << II << ", var: " << ret_var_str << ", type: " << ret_type_str << ", taint: " << taint_str << "\n";
                  found = (taint_str != "-1");
              }
          }
    }
//    errs() << "\n****************** END CALLEE TAINT FOR FUNCTION: " << F.getName() << " ******************\n\n";
    return found;
  }

  std::string update_first_taint(std::string func, std::string var) {
     if (func_taint[func][var].size() == 0)
        return "-1";
     std::string curr_var = func_taint[func][var].front();
     std::string idx = "";
     if (func_var_field[func].find(var) != func_var_field[func].end()) 
         idx = std::to_string(std::get<2>(func_var_field[func][var]));
     errs() << "Debug: first_taint " << func << ", " << var << ", " << curr_var << ", " << idx << "\n";
     if (curr_var.find(":") != std::string::npos) {
         idx = curr_var.substr(curr_var.find(":")+1);
         curr_var = curr_var.substr(0, curr_var.find(":"));
     }
     while (func_taint[func].find(curr_var) != func_taint[func].end()) {
        errs() << "Debug: mid_taint " << func << ", " << var << ", " << curr_var << ", " << func_taint[func][curr_var].size() << "\n";
        if (func_var_field[func].find(curr_var) != func_var_field[func].end()) {
            if (idx == "")
                idx = std::to_string(std::get<2>(func_var_field[func][curr_var]));
            else
                idx = std::to_string(std::get<2>(func_var_field[func][curr_var])) + ";" + idx;
            if (func_taint[func][curr_var].size() > 1)
                errs() << "Debug: getFieldIdx " << curr_var << " " << func_taint[func][curr_var].size() << "\n";
            curr_var = func_taint[func][curr_var].front();
            //if (func_var_field[func].find(curr_var) == func_var_field[func].end())
            //    break;
        }
        else {    
            if (func_taint[func][curr_var].size() == 0)
                break;
            if (func_taint[func][curr_var].size() > 1)
                errs() << "Debug: getFieldIdx " << curr_var << " " << func_taint[func][curr_var].size() << "\n";
            curr_var = func_taint[func][curr_var].front();
            if (curr_var.find(":") != std::string::npos) {
                if (idx == "")
                    idx = curr_var.substr(curr_var.find(":")+1);
                else 
                    idx = curr_var.substr(curr_var.find(":")+1) + ";" + idx;
                curr_var = curr_var.substr(0, curr_var.find(":"));
            }
        }
     }
     if (idx != "") {
         errs() << "Debug: final_taint " << func << ", " << var << ", " << (curr_var + ":" + idx) << "\n";
         return (curr_var + ":" + idx); 
     }
     else {
         errs() << "Debug: final_taint " << func << ", " << var << ", " << curr_var << "\n";
         return curr_var; 
     }
     //return listToString(func_taint[func][var]);
  }

  void print_func_var(std::string func) {
    clearFuncProf(func);
    for (std::map<std::string, std::string>::iterator it = func_var[func].begin(); it != func_var[func].end(); ++it) {
        if (func_arg[func].find(it->first) != func_arg[func].end())
            setFuncProf(func, "Argument: " + it->first + "," + std::to_string(func_arg_idx[func][it->first]) + " => " + it->second);
        else if (func_var_field[func].find(it->first) != func_var_field[func].end())
            setFuncProf(func, "Field: " + it->first + " => " + std::get<0>(func_var_field[func][it->first])  + "; " + std::get<1>(func_var_field[func][it->first]) + "; " + std::to_string(std::get<2>(func_var_field[func][it->first])) + " => " + update_first_taint(func, it->first)); //listToString(func_taint[func][it->first]));
        //else if (func_taint[func].find(it->first) == func_taint[func].end())
        //    setFuncProf(func, "Variable: " + it->first + " => " + it->second);
        else
            setFuncProf(func, "Variable: " + it->first + " => " + it->second + " => " + update_first_taint(func, it->first)); //listToString(func_taint[func][it->first]));
    }
    for (std::map< int, std::map<int, bool> >::iterator it = func_call[func].begin(); it != func_call[func].end(); ++it) {
        // Do not put indirect funccall into profile
        if (func_callee[func][it->first].find("%") != std::string::npos)
            continue;
        std::string line = "FuncCall: " + std::to_string(it->first) + ":" + func_callee[func][it->first] + " => ";
        for (std::map<int, bool>::iterator lt = it->second.begin(); lt != it->second.end(); ++lt)
            line += std::to_string(lt->first) + ":" + func_call_param[func][it->first][lt->first] + " ";
        setFuncProf(func, line);
    }
    for (std::map<std::string, std::string>::iterator it = func_ret[func].begin(); it != func_ret[func].end(); ++it)
        setFuncProf(func, "Ret: " + it->first + " => " + it->second);
  }

  virtual bool runOnFunction(Function &F) {
      if (targetFunc.size() == 0)
          if (loadTargetFunc("func.meta") < 0)
              return false;
      std::string func = F.getName().str();
      if (targetFunc.find(func) == targetFunc.end())
          return false;
      if (updateCallLHSTaint(F)) {
          errs() << "\n****************** UPDATE CALLEE TAINT FOR FUNCTION: " << F.getName() << " ******************\n\n";
      }
      print_func_var(func);
      return false;
    }
  };
}

char FuncTaintPass::ID = 0;

// Register this pass to be used by language front ends.
// This allows this pass to be called using the command:
//    clang -c -Xclang -load -Xclang ./FuncTaint.so sum.c
static void registerMyPass(const PassManagerBuilder &,
                           PassManagerBase &PM) {
    PM.add(new FuncTaintPass());
}
RegisterStandardPasses
    RegisterMyPass(PassManagerBuilder::EP_EarlyAsPossible,
                   registerMyPass);

// Register the pass name to allow it to be called with opt:
//    clang -c -emit-llvm loop.c
//    opt -load ./FuncTaint.so -defuse sum.bc > /dev/null
// See http://llvm.org/releases/3.4/docs/WritingAnLLVMPass.html#running-a-pass-with-opt for more info.
RegisterPass<FuncTaintPass> X("functaint", "FunctionTaint Pass");

