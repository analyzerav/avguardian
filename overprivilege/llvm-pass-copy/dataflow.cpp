#include <set>
#include <sstream>

#include "dataflow.h"

#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/CFG.h"

#include <fstream>

namespace llvm {

/* Var definition util */
Value* getDefinitionVar(Value* v) {
  // Definitions are assumed to be one of:
  // 1) Function arguments
  // 2) Store instructions (2nd argument is the variable being (re)defined)
  // 3) Instructions that start with "  %" (note the 2x spaces)
  //      Note that this is a pretty brittle and hacky way to catch what seems the most common definition type in LLVM.
  //      Unfortunately, we couldn't figure a better way to catch all definitions otherwise, as cases like
  //      "%0" and "%1" don't show up  when using "getName()" to identify definition instructions.
  //      There's got to be a better way, though...
  // 4) CallInst/InvokeInst, check existing summary to find any definition of pass-by-ref args and return a list defiend vars as "var1,var2,..."

  if (isa<Argument>(v)) {
    return v;
  }
  else if (isa<StoreInst>(v)) {
    return ((StoreInst*)v)->getPointerOperand();
  }
  else if (isa<Instruction>(v)){
    std::string str = valueToStr(v);
    const int VAR_NAME_START_IDX = 2;
    if (str.length() > VAR_NAME_START_IDX && str.substr(0,VAR_NAME_START_IDX+1) == "  %")
      return v;
    if (isa<CallInst>(v) || isa<InvokeInst>(v)) {
      std::string callee_func = getCallee(v);
      if (callee_func != "") {
        std::string def_arg = loadFuncRD(callee_func);
        if (def_arg != "")
            return v;
      }
    }
  } else if (isa<GlobalVariable>(v)) {
    std::string str = valueToStr(v);
    const int VAR_NAME_START_IDX = 0;
    if (str.length() > VAR_NAME_START_IDX && str.substr(0,VAR_NAME_START_IDX+1) == "@")
      return v;
  }
  return 0;
}

// Decode a series of vars separated by ";"
bool includeDefVarWithField(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      if (v1.size() > v2.size())
           return false;
      int c = 0;
      for (int i = 0; i < v1.size(); i++) {
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i].find(":") != std::string::npos && v1[i].find(v2[j]) != std::string::npos) {
                  c++;
                  break;
               }
               else if (v1[i] == v2[j]) {
                  c++;
                  break;
               }
          }
      }
      return (v1.size() == c);
}

// Decode a series of vars separated by ";"
bool includeDefVar(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      if (v1.size() > v2.size())
           return false;
      int c = 0;
      for (int i = 0; i < v1.size(); i++) {
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i] == v2[j])
                  c++;
          }
      }
      return (v1.size() == c);
}

std::string dedupDefVarWithField(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      std::string s = "";
      for (int i = 0; i < v1.size(); i++) {
          bool found = false;
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i].find(":") != std::string::npos && v1[i].find(v2[j]) != std::string::npos)
                   found = true;
          }
          if (!found)
              s += (v1[i] + ";");
      }
      if (s != "")
          s = s.substr(0, s.length()-1);
      return s;
}

std::string dedupDefVar(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      std::string s = "";
      for (int i = 0; i < v1.size(); i++) {
          bool found = false;
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i] == v2[j])
                   found = true;
          }
          if (!found)
              s += (v1[i] + ";");
      }
      if (s != "")
          s = s.substr(0, s.length()-1);
      return s;
}

// Decode a series of vars separated by ";"
bool overlapDefVar(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      for (int i = 0; i < v1.size(); i++) {
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i] == v2[j])
                  return true;
          }
      }
      return false;
}

std::vector<std::string> getOverlapDefVar(std::string s1, std::string s2) {
      std::vector<std::string> vo;
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      for (int i = 0; i < v1.size(); i++) {
          for (int j = 0; j < v2.size(); j++) {
               if (v1[i] == v2[j])
                   vo.push_back(v1[i]);
          }
      }
      return vo;
}

std::vector<std::string> getOverlapDefVarWithField(std::string s1, std::string s2) {
      std::vector<std::string> vo;
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(0, s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         } 
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(0, s2.find(";")));
            s2 = s2.substr(s2.find(";")+1);
         }
         v2.push_back(s2);
      }
      for (int i = 0; i < v1.size(); i++) {
          for (int j = 0; j < v2.size(); j++) {
               // %a:1;%d;%e ^ %a;%e => %a:1;%e, while %a:1;%e ^ %a:1:2;%e => %a:1:2;%e
               if (v1[i] == v2[j])
                   vo.push_back(v1[i]);
               else if (v1[i].find(v2[j]) != std::string::npos)
                   vo.push_back(v1[i]);
               else if (v2[j].find(v1[i]) != std::string::npos)
                   vo.push_back(v2[j]);
          }
      }
      return vo;
}

std::string getCallee(Value *v) {
    if (CallInst* CI = dyn_cast<CallInst>(v)) {
        Function* fun = CI->getCalledFunction();
        if (fun)
            return fun->getName();
    }
    else if (InvokeInst* CI = dyn_cast<InvokeInst>(v)) {
        Function* fun = CI->getCalledFunction();
        if (fun)
            return fun->getName();
    }
    return "";
}

std::string getCalleeArg(Value* v, int i) {
    if (CallInst* CI = dyn_cast<CallInst>(v)) {
        Value* val = CI->getArgOperand(i);
        if (val->hasName())
            return ("%"+val->getName().str());
        else
            return getLHSVar(dyn_cast<Instruction>(val));
    }
    else if (InvokeInst* CI = dyn_cast<InvokeInst>(v)) {
        Value* val = CI->getArgOperand(i);
        if (val->hasName())
            return ("%"+val->getName().str());
        else
            return getLHSVar(dyn_cast<Instruction>(val));
    }
    return "";
}

std::string augmentAlias(std::string s, std::vector<std::set<std::string> > alias_set) {
    std::set<std::string> def_vals;
    while (s.find(";") != std::string::npos) {
       std::string v = s.substr(0, s.find(";"));
       def_vals.insert(v);
       for (int i = 0; i < alias_set.size(); i++) {
            if (alias_set[i].find(v) != alias_set[i].end()) {
                for (std::set<std::string>::iterator it = alias_set[i].begin(); it != alias_set[i].end(); it++)
                     def_vals.insert(*it);
            }
       }
       s = s.substr(s.find(";")+1);
    }
    def_vals.insert(s);
    for (int i = 0; i < alias_set.size(); i++) {
         if (alias_set[i].find(s) != alias_set[i].end()) {
             for (std::set<std::string>::iterator it = alias_set[i].begin(); it != alias_set[i].end(); it++)
                  def_vals.insert(*it);
         }
    }
    std::string v = "";
    for (std::set<std::string>::iterator it = def_vals.begin(); it != def_vals.end(); it++)
         v += (*it + ";");
    if (v != "")
        v = v.substr(0, v.length()-1);
    return v;
}           

void getBBReachDef(DenseMap<Value*, int>& domainEntryToValueIdx, BasicBlock* block, int instr_limit, std::vector<std::set<std::string> >& alias, std::map<Value*, std::string>& def_set) {
      std::set<std::string> local_def, local_instr;
      int instr_cnt = 0;
      for (BasicBlock::iterator instruction = block->begin(); instruction != block->end(); ++instruction, instr_cnt++) {
        local_instr.insert(valueToStr(&*instruction));
        DenseMap<Value*, int>::const_iterator currDefIter = domainEntryToValueIdx.find(&*instruction);
        if (currDefIter != domainEntryToValueIdx.end()) {
          std::string currDefStr = valueToDefinitionVarStr(currDefIter->first);
          if (currDefStr == "")
              continue;
          while (currDefStr.find(";") != std::string::npos) {
              std::string curr_def_var = currDefStr.substr(0, currDefStr.find(";"));
              local_def.insert(curr_def_var);
              // Also include its alias
              for (int i = 0; i < alias.size(); i++) {
                   if (alias[i].find(curr_def_var) != alias[i].end()) {
                       for (std::set<std::string>::iterator it = alias[i].begin(); it != alias[i].end(); it++)
                            local_def.insert(*it);
                   }
              }
              currDefStr = currDefStr.substr(currDefStr.find(";")+1);
          }
          local_def.insert(currDefStr);
        }
        if (instr_cnt == instr_limit)
            break;
      }
      // Get all def vars in current BB 
      std::string currDefStr = "";
      for (std::set<std::string>::iterator it = local_def.begin(); it != local_def.end(); ++it)
           currDefStr += (*it + ";");
      if (currDefStr != "")
          currDefStr = currDefStr.substr(0, currDefStr.length()-1);
//      errs() << "getBBReach: currDefStr " << currDefStr << "\n";
      // Add reaching defs in other BBs
      for (DenseMap<Value*, int>::const_iterator prevDefIter = domainEntryToValueIdx.begin();
               prevDefIter != domainEntryToValueIdx.end();
               ++prevDefIter) {
          if (local_instr.find(valueToStr(prevDefIter->first)) == local_instr.end()) {
              std::string prevDefStr = valueToDefinitionVarStr(prevDefIter->first);
              // Augment alias set
              prevDefStr = augmentAlias(prevDefStr, alias); 
//              errs() << "getBBReach: aliased prevDefStr " << prevDefStr << "\n";
              if (!includeDefVarWithField(prevDefStr, currDefStr))
                  def_set.insert(std::pair<Value*, std::string>(prevDefIter->first, dedupDefVarWithField(prevDefStr, currDefStr)));
          }
      }
//      for (std::map<Value*, std::string>::iterator jt = def_set.begin(); jt != def_set.end(); jt++)
//           errs() << "getBBReach: def_set " << valueToStr(jt->first) << "\t" << jt->second << "\n";
      // Add reaching defs in current BB
      instr_cnt = 0;
      for (BasicBlock::iterator instruction = block->begin(); instruction != block->end(); ++instruction, instr_cnt++) {
          DenseMap<Value*, int>::const_iterator currDefIter = domainEntryToValueIdx.find(&*instruction);
          if (currDefIter != domainEntryToValueIdx.end()) {
             std::string currDefStr = valueToDefinitionVarStr(currDefIter->first);
             // Augment alias set
             currDefStr = augmentAlias(currDefStr, alias);
//             errs() << "getBBReach: aliased currDefStr " << valueToStr(&*instruction) << "\t" << currDefStr << "\n";
             def_set.insert(std::pair<Value*, std::string>(currDefIter->first, currDefStr));
             // Look at all instrs so far to current instr in current BB
             for (BasicBlock::iterator prevInst = block->begin(); prevInst != instruction; ++prevInst) {
                  DenseMap<Value*, int>::const_iterator prevDefIter = domainEntryToValueIdx.find(&*prevInst);
                  if (prevDefIter != domainEntryToValueIdx.end() && def_set.find(prevDefIter->first) != def_set.end()) {
                      std::string prevDefStr = def_set[prevDefIter->first];
                      if (includeDefVarWithField(prevDefStr, currDefStr))
                          def_set.erase(prevDefIter->first);
                      else if (overlapDefVar(prevDefStr, currDefStr))
                          def_set[prevDefIter->first] = dedupDefVarWithField(prevDefStr, currDefStr);
                  }
             }
          }
          if (instr_cnt == instr_limit)
              break;
      }
}

void getBBReachDef(DenseMap<Value*, int>& domainEntryToValueIdx, BasicBlock* block, int instr_limit, std::map<Value*, std::string>& def_set) {
      std::set<std::string> local_def, local_instr;
      int instr_cnt = 0;
      for (BasicBlock::iterator instruction = block->begin(); instruction != block->end(); ++instruction, instr_cnt++) {
        local_instr.insert(valueToStr(&*instruction));
        DenseMap<Value*, int>::const_iterator currDefIter = domainEntryToValueIdx.find(&*instruction);
        if (currDefIter != domainEntryToValueIdx.end()) {
          std::string currDefStr = valueToDefinitionVarStr(currDefIter->first);
          if (currDefStr == "")
              continue;
          while (currDefStr.find(";") != std::string::npos) {
              local_def.insert(currDefStr.substr(0, currDefStr.find(";")));
              currDefStr = currDefStr.substr(currDefStr.find(";")+1);
          }
          local_def.insert(currDefStr);
        }
        if (instr_cnt == instr_limit)
            break;
      }
      // Get all def vars in current BB 
      std::string currDefStr = "";
      for (std::set<std::string>::iterator it = local_def.begin(); it != local_def.end(); ++it)
           currDefStr += (*it + ";");
      if (currDefStr != "")
          currDefStr = currDefStr.substr(0, currDefStr.length()-1);
      //errs() << "getBBReach: currDefStr " << currDefStr << "\n";
      // Add reaching defs in other BBs
      for (DenseMap<Value*, int>::const_iterator prevDefIter = domainEntryToValueIdx.begin();
               prevDefIter != domainEntryToValueIdx.end();
               ++prevDefIter) {
          if (local_instr.find(valueToStr(prevDefIter->first)) == local_instr.end()) {
              std::string prevDefStr = valueToDefinitionVarStr(prevDefIter->first);
              if (!includeDefVarWithField(prevDefStr, currDefStr))
                  def_set.insert(std::pair<Value*, std::string>(prevDefIter->first, dedupDefVarWithField(prevDefStr, currDefStr)));
          }
      }
      //for (std::map<Value*, std::string>::iterator jt = def_set.begin(); jt != def_set.end(); jt++)
      //     errs() << "getBBReach: def_set " << valueToStr(jt->first) << "\t" << jt->second << "\n";
      // Add reaching defs in current BB
      instr_cnt = 0;
      for (BasicBlock::iterator instruction = block->begin(); instruction != block->end(); ++instruction, instr_cnt++) {
          DenseMap<Value*, int>::const_iterator currDefIter = domainEntryToValueIdx.find(&*instruction);
          if (currDefIter != domainEntryToValueIdx.end()) {
             std::string currDefStr = valueToDefinitionVarStr(currDefIter->first);
             def_set.insert(std::pair<Value*, std::string>(currDefIter->first, currDefStr));
             for (BasicBlock::iterator prevInst = block->begin(); prevInst != instruction; ++prevInst) {
                  DenseMap<Value*, int>::const_iterator prevDefIter = domainEntryToValueIdx.find(&*prevInst);
                  if (prevDefIter != domainEntryToValueIdx.end() && def_set.find(prevDefIter->first) != def_set.end()) {
                      std::string prevDefStr = def_set[prevDefIter->first];
                      if (includeDefVarWithField(prevDefStr, currDefStr))
                          def_set.erase(prevDefIter->first);
                      else if (overlapDefVar(prevDefStr, currDefStr))
                          def_set[prevDefIter->first] = dedupDefVarWithField(prevDefStr, currDefStr);
                  }
             }
          }
          if (instr_cnt == instr_limit)
              break;
      }
}

std::vector<Value*> getPrevInstr(BasicBlock::iterator& inst, BasicBlock* basicBlock) {
    std::vector<Value*> prevInstr;
    BasicBlock::iterator prev_inst;
    if (inst == basicBlock->begin()) { 
        if (pred_begin(basicBlock) == pred_end(basicBlock))
            return prevInstr;
        for (auto it = pred_begin(basicBlock), et = pred_end(basicBlock); it != et; ++it) {
             for (BasicBlock::iterator instruction = (*it)->begin(); instruction != (*it)->end(); ++instruction) 
                  prev_inst = instruction;
             prevInstr.push_back(&*prev_inst);
       }
       return prevInstr;
    }
    for (BasicBlock::iterator instruction = basicBlock->begin(); instruction != basicBlock->end(); ++instruction) {
         if (inst == instruction) {
             prevInstr.push_back(&*prev_inst);
             return prevInstr;
         }
         prev_inst = instruction;
    }     
}

std::string getLHSVar(Instruction* inst) {
    std::string str = instrToString(inst);
    return str.substr(0, str.find("="));
}

std::string typeToString(Type* t) {
     std::string type_str;
     raw_string_ostream rso(type_str);
     t->print(rso);
     return rso.str();
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

int getFuncArgIdx(Function& F, std::string var_str)  {
    int arg_id = 0;
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
         Argument& A = *arg;
         std::string arg_str = argToString(&A);
         arg_str = arg_str.substr(arg_str.find_last_of(" ")+1);
         if (var_str == arg_str)
             return arg_id;
         arg_id++;
    }
    return -1;
}


void setFuncRD(std::string func, std::vector<std::string>& idx) {
    std::string file_name = func + ".df";
    std::ofstream outfile;
    outfile.open(file_name.c_str());
    if (outfile.is_open()) {
        for (int i = 0; i < idx.size(); i++)
             outfile << idx[i] << "\n";
        outfile.close();
    }
}

std::string loadFuncRD(std::string func) {
    std::string file_name = func + ".df";
    std::ifstream infile;
    std::string def_var = "";
    std::string line;
    infile.open(file_name.c_str());
    if (!infile.is_open())
        return "";
    while (!infile.eof()) {
       getline(infile, line);
       if (line.length() == 0)
           continue;
       def_var += (line + ";");
    }
    infile.close();
    if (def_var != "")
        def_var = def_var.substr(0, def_var.length()-1);
    return def_var;
}

/******************************************************************************************
 * String output utilities */
std::string bitVectorToStr(const BitVector& bv) {
  std::string str(bv.size(), '0');
  for (int i = 0; i < bv.size(); i++)
    str[i] = bv[i] ? '1' : '0';
  return str;
}

std::string valueToStr(const Value* value) {
  std::string instStr; llvm::raw_string_ostream rso(instStr);
  value->print(rso);
  return instStr;
}

const int VAR_NAME_START_IDX = 2;

std::string valueToDefinitionStr(Value* v) {
  //Verify it's a definition first
  Value* def = getDefinitionVar(v);
  if (def == 0)
    return "";

  std::string str = valueToStr(v);
  if (isa<Argument>(v) || isa<GlobalVariable>(v)) {
    return str;
  }
  else {
      str = str.substr(VAR_NAME_START_IDX);
      return str;
  }

  return "";
}

std::string valueToDefinitionVarStr(Value* v) {
  //Similar to valueToDefinitionStr, but we return just the defined var rather than the whole definition

  Value* def = getDefinitionVar(v);
  if (def == 0)
    return "";

  if (isa<Argument>(def) || isa<StoreInst>(def)) {
    return "%" + def->getName().str();
  }
  else if (isa<CallInst>(v) || isa<InvokeInst>(v)) {
    // Read callee's dataflow profile to get any defined args and LHS (if any)
    std::string def_str = "";
    std::string callee_func = getCallee(v);
    if (callee_func != "") {
        std::string def_arg = loadFuncRD(callee_func);
        //errs() << "Debug: " << callee_func << "###" << def_arg << "\n";
        if (def_arg != "") {
            while (def_arg.find(";") != std::string::npos) {
               std::string curr = def_arg.substr(0, def_arg.find(";"));
               int idx = -1;
               std::string field_idx = "";
               if (curr.find(":") != std::string::npos) {
                   idx = atoi(curr.substr(0, curr.find(":")).c_str());
                   field_idx = curr.substr(curr.find(":"));
               }
               else  {
                   idx = atoi(curr.c_str());
               }
               std::string arg_name = getCalleeArg(v, idx);
               def_str += (arg_name + field_idx + ";");
               def_arg = def_arg.substr(def_arg.find(";")+1);
            }
            std::string curr = def_arg;
            int idx = -1;
            std::string field_idx = "";
            if (curr.find(":") != std::string::npos) {
                idx = atoi(curr.substr(0, curr.find(":")).c_str());
                field_idx = curr.substr(curr.find(":"));
            }
            else  {
                idx = atoi(curr.c_str());
            }
            std::string arg_name = getCalleeArg(v, idx);
            def_str += (arg_name + field_idx);
        }
    }
    std::string str = valueToStr(def);
    if (str.find("=") != std::string::npos) {
        int varNameEndIdx = str.find(' ',VAR_NAME_START_IDX);
        str = str.substr(VAR_NAME_START_IDX,varNameEndIdx-VAR_NAME_START_IDX);
        if (def_str == "")
            def_str = str;
        else
            def_str += (";" + str);
    }
    return def_str;
  }
  else if (isa<GlobalVariable>(def)) {
    std::string str = valueToStr(def);
    int varNameEndIdx = str.find(' ', 0);
    str = str.substr(0, varNameEndIdx);
    return str;
  }
  else {
    std::string str = valueToStr(def);
    int varNameEndIdx = str.find(' ',VAR_NAME_START_IDX);
    str = str.substr(VAR_NAME_START_IDX,varNameEndIdx-VAR_NAME_START_IDX);
    return str;
  }
}

std::string setToStr(std::vector<Value*> domain, const BitVector& includedInSet, std::string (*valFormatFunc)(Value*)) {
  std::stringstream ss;
  ss << "{\n";
  int numInSet = 0;
  for (int i = 0; i < domain.size(); i++) {
    if (includedInSet[i]) {
      if (numInSet > 0) ss << " \n";
      numInSet++;
      ss << "    " << valFormatFunc(domain[i]);
    }
  }
  ss << "}";
  return ss.str();
}

/* End string output utilities *
******************************************************************************************/


DataFlowResult DataFlow::run(Function& F,
                              std::vector<Value*> domain,
                              Direction direction,
                              BitVector boundaryCond,
                              BitVector initInteriorCond) {
  DenseMap<BasicBlock*, DataFlowResultForBlock> resultsByBlock;
  bool analysisConverged = false;

  //Create mapping from domain entries to linear indices
  //(simplifies updating bitvector entries given a particular domain element)
  DenseMap<Value*, int> domainEntryToValueIdx;
  for (int i = 0; i < domain.size(); i++)
    domainEntryToValueIdx[domain[i]] = i;

  //Set initial val for boundary blocks, which depend on direction of analysis
  std::set<BasicBlock*> boundaryBlocks;
  switch (direction) {
    case FORWARD:
      boundaryBlocks.insert(&F.front()); //post-"entry" block = first in list
      break;
    case BACKWARD:
      //Pre-"exit" blocks = those that have a return statement
      for(Function::iterator I = F.begin(), E = F.end(); I != E; ++I)
        if (isa<ReturnInst>(I->getTerminator()))
          boundaryBlocks.insert(I);
      break;
  }
  for (std::set<BasicBlock*>::iterator boundaryBlock = boundaryBlocks.begin(); boundaryBlock != boundaryBlocks.end(); boundaryBlock++) {
    DataFlowResultForBlock boundaryResult = DataFlowResultForBlock();
    //Set either the "IN" of post-entry blocks or the "OUT" of pre-exit blocks (since entry/exit blocks don't actually exist...)
    BitVector* boundaryVal = (direction == FORWARD) ? &boundaryResult.in : &boundaryResult.out;
    *boundaryVal = boundaryCond;
    boundaryResult.currTransferResult.baseValue = boundaryCond;
    resultsByBlock[*boundaryBlock] = boundaryResult;
  }

  //Set initial vals for interior blocks (either OUTs for fwd analysis or INs for bwd analysis)
  for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
    if (boundaryBlocks.find((BasicBlock*)basicBlock) == boundaryBlocks.end()) {
      DataFlowResultForBlock interiorInitResult = DataFlowResultForBlock();
      BitVector* interiorInitVal = (direction == FORWARD) ? &interiorInitResult.out : &interiorInitResult.in;
      *interiorInitVal = initInteriorCond;
      interiorInitResult.currTransferResult.baseValue = initInteriorCond;
      resultsByBlock[basicBlock] = interiorInitResult;
    }
  }

  //Generate analysis "predecessor" list for each block (depending on direction of analysis)
  //Will be used to drive the meet inputs.
  DenseMap<BasicBlock*, std::vector<BasicBlock*> > analysisPredsByBlock;
  for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
      std::vector<BasicBlock*> analysisPreds;
      switch (direction) {
        case FORWARD:
          for (pred_iterator predBlock = pred_begin(basicBlock), E = pred_end(basicBlock); predBlock != E; ++predBlock)
            analysisPreds.push_back(*predBlock);
          break;
        case BACKWARD:
          for (succ_iterator succBlock = succ_begin(basicBlock), E = succ_end(basicBlock); succBlock != E; ++succBlock)
            analysisPreds.push_back(*succBlock);
          break;
      }

      analysisPredsByBlock[basicBlock] = analysisPreds;
  }

  //Iterate over blocks in function until convergence of output sets for all blocks
  while (!analysisConverged) {
    analysisConverged = true; //assume converged until proven otherwise during this iteration

    //TODO: if analysis is backwards, may want instead to iterate from back-to-front of blocks list

    for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
      DataFlowResultForBlock& blockVals = resultsByBlock[basicBlock];

      //Store old output before applying this analysis pass to the block (depends on analysis dir)
      DataFlowResultForBlock oldBlockVals = blockVals;
      BitVector oldPassOut = (direction == FORWARD) ? blockVals.out : blockVals.in;

      //If any analysis predecessors have outputs ready, apply meet operator to generate updated input set for this block
      BitVector* passInPtr = (direction == FORWARD) ? &blockVals.in : &blockVals.out;
      std::vector<BasicBlock*> analysisPreds = analysisPredsByBlock[basicBlock];
      std::vector<BitVector> meetInputs;
      //Iterate over analysis predecessors in order to generate meet inputs for this block
      for (std::vector<BasicBlock*>::iterator analysisPred = analysisPreds.begin(); analysisPred < analysisPreds.end(); ++analysisPred) {
        DataFlowResultForBlock& predVals = resultsByBlock[*analysisPred];

        BitVector meetInput = predVals.currTransferResult.baseValue;

        //If this pred matches a predecessor-specific value for the current block, union that value into value set
        DenseMap<BasicBlock*, BitVector>::iterator predSpecificValueEntry = predVals.currTransferResult.predSpecificValues.find(basicBlock);
        if (predSpecificValueEntry != predVals.currTransferResult.predSpecificValues.end()) {
//            errs() << "Pred-specific meet input from " << (*analysisPred)->getName() << ": " <<bitVectorToStr(predSpecificValueEntry->second) << "\n";
            meetInput |= predSpecificValueEntry->second;
        }

        meetInputs.push_back(meetInput);
      }
      if (!meetInputs.empty())
        *passInPtr = applyMeet(meetInputs);

      //Apply transfer function to input set in order to get output set for this iteration
      blockVals.currTransferResult = applyTransfer(*passInPtr, domainEntryToValueIdx, basicBlock);
      BitVector* passOutPtr = (direction == FORWARD) ? &blockVals.out : &blockVals.in;
      *passOutPtr = blockVals.currTransferResult.baseValue;

      //Update convergence: if the output set for this block has changed, then we've not converged for this iteration
      if (analysisConverged) {
        if (*passOutPtr != oldPassOut)
          analysisConverged = false;
        else if (blockVals.currTransferResult.predSpecificValues.size() != oldBlockVals.currTransferResult.predSpecificValues.size())
          analysisConverged = false;
        //(should really check whether contents of pred-specific values changed as well, but
        // that doesn't happen when the pred-specific values are just a result of phi-nodes)
      }
    }
  }

  DataFlowResult result;
  result.domainEntryToValueIdx = domainEntryToValueIdx;
  result.resultsByBlock = resultsByBlock;
  return result;
}

void DataFlow::PrintInstructionOps(raw_ostream& O, const Instruction* I) {
  O << "\nOps: {";
  if (I != NULL) {
    for (Instruction::const_op_iterator OI = I->op_begin(), OE = I->op_end();
        OI != OE; ++OI) {
      const Value* v = OI->get();
      v->print(O);
      O << ";";
    }
  }
  O << "}\n";
}

void DataFlow::ExampleFunctionPrinter(raw_ostream& O, const Function& F) {
  for (Function::const_iterator FI = F.begin(), FE = F.end(); FI != FE; ++FI) {
    const BasicBlock* block = FI;
    O << block->getName() << ":\n";
    const Value* blockValue = block;
    PrintInstructionOps(O, NULL);
    for (BasicBlock::const_iterator BI = block->begin(), BE = block->end();
        BI != BE; ++BI) {
      BI->print(O);
      PrintInstructionOps(O, &(*BI));
    }
  }
}

}
