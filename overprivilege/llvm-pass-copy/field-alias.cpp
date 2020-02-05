#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/IR/Constants.h"

#include "dataflow.h"
#include "field-alias.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

char FieldAlias::ID = 1;
static RegisterPass<FieldAlias> X("cd-field-alias",
    "Field & alias analysis", false, true);
// INITIALIZE_PASS(FieldAlias, "cd-field-alias", "Field & alias analysis", false, true);
FieldAlias::FieldAlias() : ModulePass(ID) {
  // initializeFieldAliasPass(*PassRegistry::getPassRegistry());
}

void FieldAlias::get_zero_callee_func(std::set<std::string>& proc_l) {
   for (std::map<std::string, std::set<std::string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it)
        if ((it->second).size() == 0)
           proc_l.insert(it->first);
   for (std::map<std::string, std::set<std::string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it) {
        if (proc_l.find(it->first) == proc_l.end())
            continue;
        for (std::set<std::string>::iterator jt = func_callee[it->first].begin(); jt != func_callee[it->first].end(); ++jt)
             func_caller[*jt].erase(it->first);
        func_caller.erase(it);
   }
}

void FieldAlias::traverse_call_chain(std::string s, std::list<std::string>& call_chain) {
    std::map<std::string, bool> vis;
    while (vis.find(s) == vis.end()) {
        call_chain.push_back(s);
        errs() << s << " ";
        if (func_caller[s].size() == 0)
            break;
        vis[s] = true;
        s = *(func_caller[s].begin());
   }
   call_chain.push_back(s);
   errs() << s;
}

void FieldAlias::compute_caller_callee_order() {
    int c = 0;
    int n = 1;
    while (func_caller.size() > 0 && n > 0) {
        std::set<std::string> l;
        //errs() << "Iteration " << c << "\n";
        get_zero_callee_func(l);
        if (l.size() > 0)
            ordered_callee.push_back(std::list<std::string>());
        for (std::set<std::string>::iterator it = l.begin(); it != l.end(); ++it) {
             ordered_callee[c].push_back(*it);
             //errs() << c << "," << *it << "\n";
        }
        n = l.size();
        c++;
    }
    //errs() << "Left over" << "\n";
    for (std::map<std::string, std::set<std::string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it) {
         errs() << it->first << " ";
         recur_func[it->first] = std::list<std::string>();
         if (it->second.size() > 0)
             traverse_call_chain(*((it->second).begin()), recur_func[it->first]);
         errs() << "\n";
    }
}

void FieldAlias::updateAliasSet(std::string s1, std::string s2, std::vector<std::set<std::string> >& alias) {
    bool added = false;
    for (int k = 0; k < alias.size(); k++) {
         if (alias[k].find(s1) != alias[k].end()) {
             alias[k].insert(s2);
             added = true;
         }
         else if (alias[k].find(s2) != alias[k].end()) {
             alias[k].insert(s1);
             added = true;
         }
         if (added)
             break;
   }
   if (!added) {
       std::set<std::string> se;
       se.insert(s1);
       se.insert(s2);
       alias.push_back(se);
   }
}

void FieldAlias::updateArgAlias(Function& F) {
    errs() << "[+] updateArgAlias ...\n";
    std::string func = F.getName();
    for (Function::ArgumentListType::iterator arg1 = F.getArgumentList().begin(); arg1 != F.getArgumentList().end(); arg1++) {
         std::string type_str1 = typeToString(arg1->getType());
          if (type_str1.substr(type_str1.length()-2) != "**") 
             continue;
         std::string arg_str = argToString(&*arg1);
         std::string arg_str1 = arg_str.substr(arg_str.find_last_of(" ")+1);
         Function::ArgumentListType::iterator arg2 = arg1; 
         arg2++;
         while (arg2 != F.getArgumentList().end()) {
              std::string type_str2 = typeToString(arg2->getType());
              if (type_str2.substr(type_str2.length()-2) != "**") {
                  arg2++;
                  continue;
              }
              arg_str = argToString(&*arg2);
              std::string arg_str2 = arg_str.substr(arg_str.find_last_of(" ")+1);
              errs() << "[+] updateArgAlias inner " << arg_str << "\n";
              for (int i = 0; i < func_alias_set[func].size(); i++) {
                   std::set<std::string> se1;
                   std::set<std::string> se2;
                   for (std::set<std::string>::iterator it = func_alias_set[func][i].begin(); it != func_alias_set[func][i].end(); it++) {    
                        if (*it == arg_str1)
                            se1.insert(std::to_string(getFuncArgIdx(F, *it)));
                        else if ((*it).find(arg_str1+":") != std::string::npos) 
                            se1.insert(std::to_string(getFuncArgIdx(F, *it)) + (*it).substr((*it).find(":")));
                        else if (*it == arg_str2)
                            se2.insert(std::to_string(getFuncArgIdx(F, *it)));
                        else if ((*it).find(arg_str2+":") != std::string::npos)
                            se2.insert(std::to_string(getFuncArgIdx(F, *it)) + (*it).substr((*it).find(":")));
                   }
                   for (std::set<std::string>::iterator i1 = se1.begin(); i1 != se1.end(); i1++)
                        for (std::set<std::string>::iterator i2 = se2.begin(); i2!= se2.end(); i2++)
                             func_arg_alias[func].push_back(std::pair<std::string, std::string>(*i1, *i2));
              }
              arg2++;
         }
    }
    errs() << "[+] updateArgAlias done\n";
}

std::string FieldAlias::traceFuncRefArg(Function& F, std::string var_str)  {
    std::string func = F.getName();
    std::string taint_str = "";
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
         std::string type_str = typeToString(arg->getType());
         std::string arg_str = argToString(&*arg);
         arg_str = arg_str.substr(arg_str.find_last_of(" ")+1);
         if (type_str.substr(type_str.length()-1) == "*") {
             if (var_str == arg_str)
                 return arg_str;
             for (int i = 0; i < func_alias_set[func].size(); i++) {
                  if (func_alias_set[func][i].find(var_str) != func_alias_set[func][i].end()) {
                      for (std::set<std::string>::iterator it = func_alias_set[func][i].begin(); it != func_alias_set[func][i].end(); it++)
                           if (*it == arg_str || (*it).find(arg_str+":") != std::string::npos)
                               taint_str += (*it + ";"); 
                      break;
                  }
             }
         }
    }
    if (taint_str != "")
        taint_str = taint_str.substr(0, taint_str.length()-1);
    //errs() << "traceFuncRefArgs: " << var_str << ", taint_str: " << taint_str << "\n";
    return taint_str;
}

void FieldAlias::getOperandDefVals(Function& F, Instruction* inst, std::map<Value*, std::vector<Value*> >& reaching_def_instr, std::vector<std::set<std::string> >& alias, Instruction* prev_inst) {
  for (int i = 0; i < inst->getNumOperands(); i++) {
      Value* val = inst->getOperand(i);
      std::string operand = valueToDefinitionVarStr(val);
      if (operand == "")
          continue;
      std::string type_str = typeToString(val->getType());
      for (int j = 0; j < reaching_def_instr[inst].size(); j++) {
          if (valueToDefinitionVarStr(reaching_def_instr[inst][j]) == operand) {
              //errs() << "Reaching Defs (" << operand << ") : " << valueToStr(reaching_def_instr[inst][j]) << "\n";
              if (isa<LoadInst>(*inst) && type_str.substr(type_str.length()-2) == "**" && (isa<StoreInst>(*reaching_def_instr[inst][j]) || isa<GetElementPtrInst>(*reaching_def_instr[inst][j]) || isa<LoadInst>(*reaching_def_instr[inst][j]))) {
                  std::string s1 = valueToDefinitionVarStr(inst);
                  std::string s2 = "";
                  if (isa<StoreInst>(*reaching_def_instr[inst][j]))
                      s2 = valueToDefinitionVarStr(dyn_cast<StoreInst>(reaching_def_instr[inst][j])->getOperand(0));
                  else if (isa<GetElementPtrInst>(*reaching_def_instr[inst][j]) || isa<LoadInst>(*reaching_def_instr[inst][j]))
                      s2 = valueToDefinitionVarStr(reaching_def_instr[inst][j]);
                  updateAliasSet(s1, s2, alias); 
                  //errs() << "Store-Load pointer alias found " << s2 << ", " << s1 << "\n";
              }
              else if (isa<LoadInst>(*inst) && isa<GetElementPtrInst>(*reaching_def_instr[inst][j])) {
                  std::string s1 = valueToDefinitionVarStr(inst);
                  std::string s2 = valueToDefinitionVarStr(reaching_def_instr[inst][j]);
                  updateAliasSet(s1, s2, alias);
                  //errs() << "GetElement-Load pointer alias found " << s2 << ", " << s1 << "\n";
              }
          }
      }
      if (prev_inst == NULL || reaching_def_instr.find(prev_inst) == reaching_def_instr.end())
          continue;
      for (int j = 0; j < reaching_def_instr[prev_inst].size(); j++) {
          if (valueToDefinitionVarStr(reaching_def_instr[prev_inst][j]) == operand) {
              //errs() << "Prev Defs (" << operand << ") : " << valueToStr(reaching_def_instr[prev_inst][j]) << "\n";
              if (isa<StoreInst>(*inst) && i == 1 && type_str.substr(type_str.length()-2) == "**" && isa<StoreInst>(*reaching_def_instr[prev_inst][j])) {
                  std::string s1 = valueToDefinitionVarStr(dyn_cast<StoreInst>(inst)->getOperand(0));
                  std::string s2 = valueToDefinitionVarStr(dyn_cast<StoreInst>(reaching_def_instr[prev_inst][j])->getOperand(0));
                  updateAliasSet(s1, s2, alias);
                  //errs() << "Store-Store pointer alias found " << s2 << ", " << s1 << "\n";
              }
          }
      }
  }

  // Find variable's fields
  std::string func_name = F.getName();
  if (GetElementPtrInst* gepInst = dyn_cast<GetElementPtrInst>(inst)) {
      std::string var_str = valueToDefinitionVarStr(gepInst->getOperand(0));
      std::string ret_var_str = valueToDefinitionVarStr(gepInst);
      int offset = -1;
      if (gepInst->getNumOperands() > 2)
          if (ConstantInt* CI = dyn_cast<ConstantInt>(gepInst->getOperand(2)))
              offset = CI->getZExtValue();
      if (offset >= 0) {
          std::string arg_taint = traceFuncRefArg(F, var_str);
          if (func_name == "main" || arg_taint != "" || (func_field.find(func_name) != func_field.end() && func_field[func_name].find(var_str) != func_field[func_name].end())) {
                func_field[func_name][ret_var_str] = std::pair<std::string, int>((arg_taint != ""? arg_taint : var_str), offset);
                //errs() << "Field: " << *inst << ", ret: " << ret_var_str << ", var: " << (arg_taint != ""? arg_taint : var_str) << ":" << offset << "\n";
                while (arg_taint.find(";") != std::string::npos) {
                    std::string s = arg_taint.substr(0, arg_taint.find(";")) + std::to_string(offset);
                    updateAliasSet(s, ret_var_str, alias);
                    arg_taint = arg_taint.substr(arg_taint.find(";")+1);
                }
                std::string s = (arg_taint != ""? arg_taint : var_str) + ":" + std::to_string(offset);
                updateAliasSet(s, ret_var_str, alias);
            }
        }
    }
    else if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
        std::string callee = getCallee(inst);
        if (callee != "" && func_ret_alias.find(callee) != func_ret_alias.end()) {
            std::string ret_var_str = getLHSVar(inst); 
            if (ret_var_str != "" && func_ret_alias.find(callee) != func_ret_alias.end()) {
                for (std::set<std::string>::iterator it = func_ret_alias[callee].begin(); it != func_ret_alias[callee].end(); it++) {
                     std::string str = *it;
                     if ((*it).find(":") != std::string::npos)
                         str = (*it).substr(0, (*it).find(":"));    
                     str = getCalleeArg(inst, std::stoi(str));
                     if ((*it).find(":") != std::string::npos)
                         str = str + (*it).substr((*it).find(":"));
                     updateAliasSet(str, ret_var_str, alias);
                }
            }
            if (ret_var_str != "" && func_arg_alias.find(callee) != func_arg_alias.end()) {
                for (int i = 0; i < func_arg_alias[callee].size(); i++) {
                     std::string str1 = func_arg_alias[callee][i].first;
                     std::string str2 = func_arg_alias[callee][i].second;
                     if (func_arg_alias[callee][i].first.find(":") != std::string::npos)
                         str1 = func_arg_alias[callee][i].first.substr(0, func_arg_alias[callee][i].first.find(":"));
                     if (func_arg_alias[callee][i].second.find(":") != std::string::npos)
                         str2 = func_arg_alias[callee][i].second.substr(0, func_arg_alias[callee][i].second.find(":"));
                     str1 = getCalleeArg(inst, std::stoi(str1));
                     str2 = getCalleeArg(inst, std::stoi(str2));
                     if (func_arg_alias[callee][i].first.find(":") != std::string::npos)
                         str1 = str1 + func_arg_alias[callee][i].first.substr(func_arg_alias[callee][i].first.find(":"));
                     if (func_arg_alias[callee][i].second.find(":") != std::string::npos)
                         str2 = str2 + func_arg_alias[callee][i].second.substr(func_arg_alias[callee][i].second.find(":"));
                     updateAliasSet(str1, str2, alias);
                }
            }
        }
    }
    else if (ReturnInst* retInst = dyn_cast<ReturnInst>(inst)) {
        std::string var_str = "";
        Value* v = retInst->getReturnValue();
        if (v != 0)
            var_str = valueToDefinitionVarStr(v); 
        if (var_str != "") {
            std::string arg_taint = traceFuncRefArg(F, var_str);
            if (arg_taint != "") {
                //errs() << "Ret pointer alias found " << arg_taint << "\n";
                while (arg_taint.find(";") != std::string::npos) {
                    std::string s1 = arg_taint.substr(0, arg_taint.find(";"));
                    if (s1.find(":") != std::string::npos) {
                        std::string s2 = s1.substr(0, s1.find(":"));
                        func_ret_alias[func_name].insert(std::to_string(getFuncArgIdx(F, s2)) + s1.substr(s1.find(":")));
                    }
                    else {
                        func_ret_alias[func_name].insert(std::to_string(getFuncArgIdx(F, s1)));
                    }
                    arg_taint = arg_taint.substr(arg_taint.find(";")+1);
                }
                std::string s1 = arg_taint;
                if (s1.find(":") != std::string::npos) {
                    std::string s2 = s1.substr(0, s1.find(":"));
                    func_ret_alias[func_name].insert(std::to_string(getFuncArgIdx(F, s2)) + s1.substr(s1.find(":")));
                }
                else {
                    func_ret_alias[func_name].insert(std::to_string(getFuncArgIdx(F, s1)));
                }
                errs() << "Ret pointer alias: ";
                for (std::set<std::string>::iterator it = func_ret_alias[func_name].begin(); it != func_ret_alias[func_name].end(); it++)
                    errs() << *it << " ";
                errs() << "\n";
            }
        }
    }
    else if (isa<BitCastInst>(*inst) || isa<TruncInst>(*inst) || isa<ZExtInst>(*inst)) {
        Value* val = inst->getOperand(0);
        // use getLHSVar?
        std::string operand = valueToDefinitionVarStr(val);
        if (operand != "") {
            std::string ret_var_str = valueToDefinitionVarStr(inst);
            updateAliasSet(operand, ret_var_str, alias);
        }
   }
   //TODO: add more instruction types
   std::string dest = valueToDefinitionVarStr(inst);
   if (dest != "") {
     if (func_taint_copy[func_name].find(dest) == func_taint_copy[func_name].end())
         func_taint_copy[func_name][dest] = std::set<std::string>();
     if (func_taint_change[func_name].find(dest) == func_taint_change[func_name].end())
         func_taint_change[func_name][dest] = std::set<std::string>();
     if (isa<StoreInst>(*inst)) {
        std::string s1 = valueToDefinitionVarStr(inst->getOperand(0));
        std::string s2 = valueToDefinitionVarStr(inst->getOperand(1));
        if (s1 != "") {
            func_taint_copy[func_name][s2].insert(s1);
            if (func_taint_copy[func_name].find(s1) != func_taint_copy[func_name].end())
                func_taint_copy[func_name][s2].insert(func_taint_copy[func_name][s1].begin(), func_taint_copy[func_name][s1].end());
            if (func_taint_change[func_name].find(s1) != func_taint_change[func_name].end())
                func_taint_change[func_name][s2].insert(func_taint_change[func_name][s1].begin(), func_taint_change[func_name][s1].end());
            errs() << "TaintCopy (" << dest << "): " << s1 << "\t" << valueToStr(inst) << "\n";
        }
        else if (ConstantInt* CI = dyn_cast<ConstantInt>(inst->getOperand(0))) {
            func_taint_change[func_name][s2].insert(std::to_string(CI->getZExtValue()));
            errs() << "TaintChange (" << dest << "): " << s1 << "\t" << valueToStr(inst) << "\n";
        }
        for (int k = 0; k < alias.size(); k++) {
             if (alias[k].find(s1) != alias[k].end()) {
                 for (std::set<std::string>::iterator it = alias[k].begin(); it != alias[k].end(); it++)
                      if ((*it).find(":") != std::string::npos)
                          s1 = *it;
             }
        }
        for (int k = 0; k < alias.size(); k++) {
             if (alias[k].find(s2) != alias[k].end()) {
                 for (std::set<std::string>::iterator it = alias[k].begin(); it != alias[k].end(); it++)
                      if ((*it).find(":") != std::string::npos)
                          s2 = *it;
             }
        }      
        if (s1.find(":") != std::string::npos && s2.find(":") != std::string::npos)
            errs() << "CopyFrom: " << s1 << ", " << s2 << "\t" << valueToStr(inst) << "\n";
     //   else
     //       errs() << "ChangeFrom: " << s1 << ", " << s2 << "\t" << valueToStr(inst) << "\n";
     }
     else if (isa<LoadInst>(*inst) || isa<BitCastInst>(*inst) || isa<TruncInst>(*inst) || isa<ZExtInst>(*inst)) {
        std::string s1 = valueToDefinitionVarStr(inst->getOperand(0));
        if (s1 != "") {
            func_taint_copy[func_name][dest].insert(s1);    
            if (func_taint_copy[func_name].find(s1) != func_taint_copy[func_name].end())
                func_taint_copy[func_name][dest].insert(func_taint_copy[func_name][s1].begin(), func_taint_copy[func_name][s1].end());
            if (func_taint_change[func_name].find(s1) != func_taint_change[func_name].end())
                func_taint_change[func_name][dest].insert(func_taint_change[func_name][s1].begin(), func_taint_change[func_name][s1].end());
            errs() << "TaintCopy (" << dest << "): " << s1 << "\t" << valueToStr(inst) << "\n";
        }
     }
     else if (isa<CallInst>(*inst) || isa<InvokeInst>(*inst)) {
        std::string callee = getCallee(inst);
        errs() << "debug: " << callee << ", " << func_taint_copy[callee].size() << ", " << func_taint_change[callee].size() << "\n";
        for (std::map<std::string, std::set<std::string> >::iterator it = func_taint_copy[callee].begin(); it != func_taint_copy[callee].end(); it++) {
             if (it->second.size() == 0)
                 continue;
             errs() << "TaintCall Copy (" << it->first << "): ";
             for (std::set<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++)
                  errs() << *jt << " ";
             errs() << "\n";
        }
        for (std::map<std::string, std::set<std::string> >::iterator it = func_taint_change[callee].begin(); it != func_taint_change[callee].end(); it++) {
             if (it->second.size() == 0)
                 continue;
             errs() << "TaintCall Change (" << it->first << "): ";
             for (std::set<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++)
                  errs() << *jt << " ";
             errs() << "\n";
        }
     }
     else if (!isa<GetElementPtrInst>(*inst)) {
        for (int i = 0; i < inst->getNumOperands(); i++) {
             std::string operand = valueToDefinitionVarStr(inst->getOperand(i));    
             if (operand != "") {
                 func_taint_change[func_name][dest].insert(operand);
                 if (func_taint_copy[func_name].find(operand) != func_taint_copy[func_name].end())
                     func_taint_change[func_name][dest].insert(func_taint_copy[func_name][operand].begin(), func_taint_copy[func_name][operand].end());
                 if (func_taint_change[func_name].find(operand) != func_taint_change[func_name].end())
                     func_taint_change[func_name][dest].insert(func_taint_change[func_name][operand].begin(), func_taint_change[func_name][operand].end());
                 errs() << "TaintChange (" << dest << "): " << operand << "\t" << valueToStr(inst) << "\n";
             }
        }
     }
   }
}

void FieldAlias::initAnalyzeOrder(Module &M) {
  CallGraph &CG = getAnalysis<CallGraph>();
  for (Function &func : M) {
        if (func.isDeclaration()) continue;
  //      std::string funcName = demangleFuncName(func);
  //      if (isStdFunction(funcName)) continue;
        // Init defined func call info
        std::string caller = func.getName();
        func_def[caller] = &func;
        if (func_caller.find(caller) == func_caller.end())
            func_caller[caller] = std::set<std::string>();
        if (func_callee.find(caller) == func_callee.end())
            func_callee[caller] = std::set<std::string>();
        for (CallGraphNode::CallRecord &CR : *CG[&func]) {
            if (Function *calledFunc = CR.second->getFunction()) {
                if (calledFunc) {
                   if (calledFunc->isDeclaration()) { 
                       errs() << "Declared: " << calledFunc->getName() << "\n";
                       continue;
                   }
                   // Update func call info
                   std::string callee = calledFunc->getName();
                   func_caller[caller].insert(callee);
                   func_callee[callee].insert(caller);
                }
            } else {
                errs() << "external node\n";
            }
        }
  }
  compute_caller_callee_order();
  for (int i = 0; i < ordered_callee.size(); i++)
       for (std::list<std::string>::iterator it = ordered_callee[i].begin(); it != ordered_callee[i].end(); it++)
            errs() << "FieldAlias: " << i << ", " << *it << "\n";
}

bool FieldAlias::runOnModule(Module &M) {
  initAnalyzeOrder(M);    
  for (int i = 0; i < ordered_callee.size(); i++) 
       for (std::list<std::string>::iterator it = ordered_callee[i].begin(); it != ordered_callee[i].end(); it++)
            runOnFunction(*func_def[*it], M);
  return false;
}

void FieldAlias::runOnFunction(Function& F, Module &M) {
  //Set domain as a vector of definitions instr in the function
  std::vector<Value*> domain;
  for (Module::global_iterator global = M.global_begin(); global != M.global_end(); ++global) {
    if (global->isConstant()) continue;
    domain.push_back(global);
  }
  for (Function::arg_iterator arg = F.arg_begin(); arg != F.arg_end(); ++arg)
    domain.push_back(arg);
  for (inst_iterator instruction = inst_begin(F), e = inst_end(F); instruction != e; ++instruction) {
    //If instruction is nonempty when converted to a definition string, then it's a definition and belongs in our domain
    if (!valueToDefinitionStr(&*instruction).empty())
      domain.push_back(&*instruction);
  }

  int numVars = domain.size();

  //Init alias & RD set
  std::string func_name = F.getName();
  func_alias_set[func_name] = std::vector<std::set<std::string> >();
  func_arg_rd[func_name] =  std::set<std::string>(); //std::vector<std::string>();
  func_field[func_name] = std::map<std::string, std::pair<std::string, int> >();
  func_arg_alias[func_name] = std::vector<std::pair<std::string, std::string> >();
  func_ret_alias[func_name] = std::set<std::string>();

  func_taint_copy[func_name] = std::map<std::string, std::set<std::string> >();
  func_taint_change[func_name] = std::map<std::string, std::set<std::string> >();

  //Set the initial boundary dataflow value to be the set of input argument definitions for this function
  BitVector boundaryCond(numVars, false);
  for (int i = 0; i < domain.size(); i++)
    if (isa<Argument>(domain[i]) || isa<GlobalVariable>(domain[i]))
      boundaryCond.set(i);

  //Set interior initial dataflow values to be empty sets
  BitVector initInteriorCond(numVars, false);


  //Get dataflow values at IN and OUT points of each block
  FieldAliasDataFlow flow;
  DataFlowResult dataFlowResult = flow.run(F, domain, DataFlow::FORWARD, boundaryCond, initInteriorCond);

  //Then, extend those values into the interior points of each block, outputting the result along the way
   errs() << "\n****************** REACHING DEFINITIONS OUTPUT FOR FUNCTION: " << F.getName() << " *****************\n";
  // errs() << "Domain of values: " << setToStr(domain, BitVector(domain.size(), true), valueToDefinitionStr) << "\n";
  // errs() << "Variables: "   << setToStr(domain, BitVector(domain.size(), true), valueToDefinitionVarStr) << "\n";

  //Print function header (in hacky way... look for "definition" keyword in full printed function, then print rest of that line only)
  std::string funcStr = valueToStr(&F);
  int funcHeaderStartIdx = funcStr.find("define");
  int funcHeaderEndIdx = funcStr.find('{', funcHeaderStartIdx + 1);
  // errs() << funcStr.substr(funcHeaderStartIdx, funcHeaderEndIdx-funcHeaderStartIdx) << "\n";
   
  Instruction* prev_instr = NULL;

  //Now, use dataflow results to output reaching definitions at program points within each block
  for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
    DataFlowResultForBlock blockReachingDefVals = dataFlowResult.resultsByBlock[basicBlock];

    //Print just the header line of the block (in a hacky way... blocks start w/ newline, so look for first occurrence of newline beyond first char
    std::string basicBlockStr = valueToStr(basicBlock);
    // errs() << basicBlockStr.substr(0, basicBlockStr.find(':', 1) + 1) << "\n";
     
    if (basicBlock != F.begin() && pred_begin(basicBlock) == pred_end(basicBlock))
        continue;  

    //Initialize reaching definitions at the start of the block
    BitVector reachingDefVals = blockReachingDefVals.in;

    // std::vector<std::string> blockOutputLines;

    //Output reaching definitions at the IN point of this block (not strictly needed, but useful to see)
    // blockOutputLines.push_back("\nReaching Defs (BB IN): " + setToStr(domain, reachingDefVals, valueToDefinitionStr) + "\n");

    //Iterate forward through instructions of the block, updating and outputting reaching defs
    for (BasicBlock::iterator instruction = basicBlock->begin(); instruction != basicBlock->end(); ++instruction) {
      //Output the instruction contents
      // blockOutputLines.push_back(valueToStr(&*instruction));
      
      if (isa<UnreachableInst>(*instruction)) break; //continue;

      DenseMap<Value*, int>::const_iterator defIter;

      std::string currDefStr = valueToDefinitionVarStr(instruction);

      //Kill (unset) all existing defs for this variable
      //(is there a better way to do this than string comparison of the defined var names?)
      for (defIter = dataFlowResult.domainEntryToValueIdx.begin(); defIter != dataFlowResult.domainEntryToValueIdx.end(); ++defIter) {
        std::string prevDefStr = valueToDefinitionVarStr(defIter->first);
        if (prevDefStr == currDefStr)
          reachingDefVals.reset(defIter->second);
      }

      //Add this definition to the reaching set
      defIter = dataFlowResult.domainEntryToValueIdx.find(&*instruction);
      if (defIter != dataFlowResult.domainEntryToValueIdx.end())
        reachingDefVals.set((*defIter).second);

      //Output the set of reaching definitions at program point just past instruction
      //(but only if not a phi node... those aren't "real" instructions)
      if (!isa<PHINode>(instruction)) {
        reaching_def_instr[&*instruction] = std::vector<Value*>();
        for (int i = 0; i < domain.size(); i++)
            if (reachingDefVals[i])
                reaching_def_instr[&*instruction].push_back(domain[i]);
        //errs() << "Curr instr: " << valueToStr(&*instruction) << "\n";
        getOperandDefVals(F, &*instruction, reaching_def_instr, func_alias_set[func_name], prev_instr);
        prev_instr = &*instruction;
        //Debugging output
        //for (int i = 0; i < reaching_def_instr[&*instruction].size(); i++)
        //     errs() << "Reaching Defs: " << valueToStr(reaching_def_instr[&*instruction][i]) << "\n";
        // blockOutputLines.push_back("\nReaching Defs (" + valueToStr(&*instruction) + ", " + std::to_string(reaching_def_instr[&*instruction].size()) + "): " + setToStr(domain, reachingDefVals, valueToDefinitionStr) + "\n");
      }
    }

    //Debugging output
    // for (std::vector<std::string>::iterator i = blockOutputLines.begin(); i < blockOutputLines.end(); ++i)
    //  errs() << *i << "\n";
  }
  updateArgAlias(F);
  for (int i = 0; i < func_alias_set[func_name].size(); i++) {
       errs() << "Pointer alias: "; 
       for (std::set<std::string>::iterator it = func_alias_set[func_name][i].begin(); it != func_alias_set[func_name][i].end(); it++) 
            errs() << *it << " ";
       errs() << "\n";
  }
  for (std::map<std::string, std::set<std::string> >::iterator it = func_taint_copy[func_name].begin(); it != func_taint_copy[func_name].end(); it++) {
       if (it->second.size() == 0)
           continue;
       errs() << "FuncTaint Copy (" << it->first << "): ";
       for (std::set<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++)
            errs() << *jt << " ";
       errs() << "\n";
  }
  for (std::map<std::string, std::set<std::string> >::iterator it = func_taint_change[func_name].begin(); it != func_taint_change[func_name].end(); it++) {
       if (it->second.size() == 0)
           continue;
       errs() << "FuncTaint Change (" << it->first << "): ";
       for (std::set<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++)
            errs() << *jt << " ";
       errs() << "\n";
  }
   errs() << "****************** END REACHING DEFINITION OUTPUT FOR FUNCTION: " << F.getName() << " ******************\n\n";
}

void FieldAlias::getAnalysisUsage(AnalysisUsage& AU) const {
   AU.addRequired<CallGraph>();
   //AU.setPreservesCFG();
}

DEFINING_FILE_FOR(FieldAlias);
