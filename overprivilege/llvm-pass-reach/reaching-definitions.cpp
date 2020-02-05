// Given a function and its bitcode file as input, compute its reaching definition
// Inside a function, if a called function is defined somewhere, then retrive its dataflow profile 
// Adapted from https://github.com/bhumbers/optcomp/tree/master/asst2/ClassicalDataflow
////////////////////////////////////////////////////////////////////////////////

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "ExtAPI.h"

#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <list>

#include "dataflow.h"

using namespace llvm;

namespace {

//////////////////////////////////////////////////////////////////////////////////////////////
//Dataflow analysis
class ReachingDefinitionsDataFlow : public DataFlow {

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
            // For call/invoke instr, if some var in its define set is redefined, kill it
            if (overlapDefVar(prevDefStr, currDefStr)) { //if (prevDefStr == currDefStr) {
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

class ReachingDefinitions : public FunctionPass {
    // Target functions
    std::map<std::string, bool> targetFunc;
    // Reachable indicator for var 
    std::map< std::string, std::map<std::string, bool> > func_reach;
    // Variable define points: func_def[func][var] = list of instr (idx) that defines var in func
    std::map< std::string, std::map< std::string, std::list<std::string> > > func_def;
    // Target var for message fields <type, msg, field>
    std::map< std::string, std::map< std::string, std::tuple<std::string, std::string, int> > > func_var_field;
    // Target variables identified from input args or local variables in a function
    std::map< std::string, std::map<std::string, std::string> > func_var;
    // Function call instructions with args info (located by offset)
    std::map< std::string, std::map< int, std::vector<std::string> > > func_call_param;
    // Function args as target var
    std::map< std::string, std::map<std::string, std::string> > func_arg;
    // Function calls taking target vars as args (by its index)
    std::map< std::string, std::map< int, std::map<int, bool> > > func_call;
    // Function ret vars info
    std::map< std::string, std::map<std::string, std::string> > func_ret;
    // Function arg index
    std::map< std::string, std::map<std::string, int> > func_arg_idx;
    // Tainted source
    std::map<std::string, std::map< std::string, std::list<std::string> > > func_taint;
    // Function pass pointer
    std::map<std::string, Function*> func_pass;

 public:
  static char ID;

  ReachingDefinitions() : FunctionPass(ID) { }

  static ExtAPI ext;

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

  std::string typeSanitizer(std::string str) {
    if (str.find('"') != std::string::npos) {
        str = str.substr(str.find('"')+1);
        str = str.substr(0, str.find('"'));
    }
    return str;
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

  int checkExtAPI(std::string func) {
    return ext.getExtAPI()->get_type_str(func);
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

  void getDefineInstr(Function& F) {
    // Iterate each instr: if it is a define instr, get its defined var and add it into func_def
    for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
        for (BasicBlock::iterator instruction = basicBlock->begin(); instruction != basicBlock->end(); ++instruction) {
             Value* val = &(*instruction);
             std::string instr = valueToDefinitionStr(val); 
             std::string var = valueToDefinitionVarStr(val); 
             if (instr != "")
                 errs() << "Def: " << instr << "..." << var << "\n";
        }
     }
  }

  std::string getFieldIdx(std::string func, std::string targetVar) {
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
    return (curr_var + ":" + idx);
  }

  void getTargetVarReach(Function& F, std::vector<Value*>& domain, DataFlowResult& dataFlowResult) {
    std::string func = F.getName().str();
    for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
        if (succ_begin(&(*basicBlock)) == succ_end(&(*basicBlock))) {
            DataFlowResultForBlock blockReachingDefVals = dataFlowResult.resultsByBlock[basicBlock];
            //std::string basicBlockStr = valueToStr(basicBlock);
            //errs() << "Exit BB (" << (*basicBlock).size() << "): " << basicBlockStr << "\n"; //.substr(0, basicBlockStr.find(':', 1) + 1) << "\n";
            BitVector reachingDefVals = blockReachingDefVals.out;
            // Extract def var from each instruction in RD set and check if it has been defined previously
            for (int i = 0; i < domain.size(); i++) {
                if (reachingDefVals[i]) {
                    //errs() << "RD: " << valueToDefinitionStr(domain[i]) << "\t" << valueToDefinitionVarStr(domain[i]) << "\n";
                    std::string var_name = valueToDefinitionVarStr(domain[i]);
                    if (func_reach[func].find(var_name) != func_reach[func].end()) {
                        bool predefined = false;
                        if (isa<Instruction>(domain[i])) {
                            Instruction *instr = dyn_cast<Instruction>(domain[i]);
                            for (User::op_iterator i = instr->op_begin(), e = instr->op_end(); i != e; ++i) {
                                if (Instruction *vi = dyn_cast<Instruction>(*i)) {
                                   if (valueToDefinitionVarStr(instr) == valueToDefinitionVarStr(vi)) 
                                       predefined = true;
                                }
                            } 
                       }
                       if (!predefined)
                           func_reach[func][var_name] = true;
                    }
                }
            }
        }
    }
    getDefineInstr(*func_pass[func]);
    // Set function profile summary
    for (std::map<std::string, bool>::iterator it = func_reach[func].begin(); it != func_reach[func].end(); ++it) {
        if (!it->second) {
            errs() << "Define: " << it->first;
            if (func_var_field[func].find(it->first) != func_var_field[func].end())
                errs() << ", taint: " << getFieldIdx(func, it->first);
            errs() << "\n";
            continue;
        }
        if (func_arg[func].find(it->first) != func_arg[func].end())
            errs() << "Argument: " << it->first << "," << func_arg_idx[func][it->first] << "\n";
        else if (func_ret[func].find(it->first) != func_ret[func].end())
            errs() << "Ret: " << it->first << "\n";   
        else if (func_var_field[func].find(it->first) != func_var_field[func].end())
            errs() << "Field: " << it->first << ", taint: " << getFieldIdx(func, it->first) << "\n";
        else
            errs() << "Variable: " << it->first << ", taint: " << listToString(func_taint[func][it->first]) << "\n"; 
    }
  }

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
      if (func_taint.find(func) == func_taint.end())
          func_taint[func] = std::map< std::string, std::list<std::string> >();
  }

  void getFuncArgIdx(Function& F) {
    int i = 0;
    std::string func = F.getName().str();
    for (Function::ArgumentListType::iterator arg = F.getArgumentList().begin(); arg != F.getArgumentList().end(); arg++) {
          Argument& A = *arg;
          std::string arg_str = argToString(&A);
          std::string var_str = arg_str.substr(arg_str.find_last_of(" ")+1);
          func_arg_idx[func][var_str] = i;
          i++;
    }
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
           std::string::size_type sz;
           //int callee = atoi(line.substr(line.find(":")).c_str()); 
           int callee = std::stoi(line, &sz); //std::string callee = line.substr(0, line.find(" "));
           if (func_call[func].find(callee) == func_call[func].end())
               func_call[func][callee] = std::map<int, bool>();
           line = line.substr(line.find(">")+2);
           int i;
           while (line.find(" ") != std::string::npos) {
              //i = atoi(line.substr(line.find(":")).c_str()); 
              i = std::stoi(line, &sz);
              func_call[func][callee][i] = true;
              line = line.substr(line.find(" ")+1);
           }
           if (line.length() > 0) {
              //i = atoi(line.substr(line.find(":")).c_str()); 
              i = std::stoi(line, &sz);
              func_call[func][callee][i] = true;
           }
       }
       else {
          std::string var_str = line.substr(0, line.find(" "));
          std::string type_str = line.substr(line.find(">")+2);
          std::string taint_str = "";
          if (type_str.find(">") != std::string::npos) {
              taint_str = type_str.substr(type_str.find(">")+2);
              type_str = type_str.substr(0, type_str.find(">")-2);
          }
          //func_var[func][var_str] = type_str;
          if (label == "Argument")
              func_arg[func][var_str] = type_str;
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

  virtual bool runOnFunction(Function& F) {
    if (targetFunc.size() == 0)
        if (loadTargetFunc("func.meta") < 0)
            return false;
    std::string func = F.getName().str();
    if (targetFunc.find(func) == targetFunc.end())
        return false;
    
    // Init func arg index
    if (func_arg_idx.find(func) == func_arg_idx.end())
        func_arg_idx[func] = std::map<std::string, int>();
    //else
    //    return false;
    func_pass[func] = &F;
    getFuncArgIdx(F);
    
    // Init reachability set
    if (func_reach.find(func) == func_reach.end())
        func_reach[func] = std::map<std::string, bool>();
    if (func_def.find(func) == func_def.end())
        func_def[func] = std::map< std::string, std::list<std::string> >();
    for (std::map<std::string, std::string>::iterator it = func_var[func].begin(); it != func_var[func].end(); ++it)
        func_reach[func][it->first] = false; 

    //Set domain = definitions in the function
    std::vector<Value*> domain;
    for (Function::arg_iterator arg = F.arg_begin(); arg != F.arg_end(); ++arg)
      domain.push_back(arg);
    for (inst_iterator instruction = inst_begin(F), e = inst_end(F); instruction != e; ++instruction) {
      //If instruction is nonempty when converted to a definition string, then it's a definition and belongs in our domain
      if (!valueToDefinitionStr(&*instruction).empty())
        domain.push_back(&*instruction);
    }

    int numVars = domain.size();

    //Set the initial boundary dataflow value to be the set of input argument definitions for this function
    BitVector boundaryCond(numVars, false);
    for (int i = 0; i < domain.size(); i++)
      if (isa<Argument>(domain[i]))
        boundaryCond.set(i);

    //Set interior initial dataflow values to be empty sets
    BitVector initInteriorCond(numVars, false);

    //Get dataflow values at IN and OUT points of each block
    ReachingDefinitionsDataFlow flow;
    DataFlowResult dataFlowResult = flow.run(F, domain, DataFlow::FORWARD, boundaryCond, initInteriorCond);

    //Then, extend those values into the interior points of each block, outputting the result along the way
/*
    errs() << "\n****************** REACHING DEFINITIONS OUTPUT FOR FUNCTION: " << F.getName() << " *****************\n";
    errs() << "Domain of values: " << setToStr(domain, BitVector(domain.size(), true), valueToDefinitionStr) << "\n";
    errs() << "Variables: "   << setToStr(domain, BitVector(domain.size(), true), valueToDefinitionVarStr) << "\n";
*/
    //Print function header (in hacky way... look for "definition" keyword in full printed function, then print rest of that line only)
    std::string funcStr = valueToStr(&F);
    int funcHeaderStartIdx = funcStr.find("define");
    int funcHeaderEndIdx = funcStr.find('{', funcHeaderStartIdx + 1);
//    errs() << funcStr.substr(funcHeaderStartIdx, funcHeaderEndIdx-funcHeaderStartIdx) << "\n";

    //Now, use dataflow results to output reaching definitions at program points within each block
    for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
      DataFlowResultForBlock blockReachingDefVals = dataFlowResult.resultsByBlock[basicBlock];

      //Print just the header line of the block (in a hacky way... blocks start w/ newline, so look for first occurrence of newline beyond first char
      std::string basicBlockStr = valueToStr(basicBlock);
//      errs() << basicBlockStr.substr(0, basicBlockStr.find(':', 1) + 1) << "\n";

      //Initialize reaching definitions at the start of the block
      BitVector reachingDefVals = blockReachingDefVals.in;

//      std::vector<std::string> blockOutputLines;

      //Output reaching definitions at the IN point of this block (not strictly needed, but useful to see)
//      blockOutputLines.push_back("\nReaching Defs: " + setToStr(domain, reachingDefVals, valueToDefinitionStr) + "\n");

      //Iterate forward through instructions of the block, updating and outputting reaching defs
      for (BasicBlock::iterator instruction = basicBlock->begin(); instruction != basicBlock->end(); ++instruction) {
        //Output the instruction contents
//        blockOutputLines.push_back(valueToStr(&*instruction));

        DenseMap<Value*, int>::const_iterator defIter;

        std::string currDefStr = valueToDefinitionVarStr(instruction);

        //Kill (unset) all existing defs for this variable
        //(is there a better way to do this than string comparison of the defined var names?)
        for (defIter = dataFlowResult.domainEntryToValueIdx.begin(); defIter != dataFlowResult.domainEntryToValueIdx.end(); ++defIter) {
          std::string prevDefStr = valueToDefinitionVarStr(defIter->first);
          // For call/invoke instr, if some var in its define set is redefined, kill it
          if (overlapDefVar(prevDefStr, currDefStr))  //if (prevDefStr == currDefStr)
            reachingDefVals.reset(defIter->second);
        }

        //Add this definition to the reaching set
        defIter = dataFlowResult.domainEntryToValueIdx.find(&*instruction);
        if (defIter != dataFlowResult.domainEntryToValueIdx.end())
          reachingDefVals.set((*defIter).second);

        //Output the set of reaching definitions at program point just past instruction
        //(but only if not a phi node... those aren't "real" instructions)
//        if (!isa<PHINode>(instruction))
//          blockOutputLines.push_back("\nReaching Defs: " + setToStr(domain, reachingDefVals, valueToDefinitionStr) + "\n");
      }

      // Print out reaching definiton contents
//      for (std::vector<std::string>::iterator i = blockOutputLines.begin(); i < blockOutputLines.end(); ++i)
//        errs() << *i << "\n";
    }
//    errs() << "****************** END REACHING DEFINITION OUTPUT FOR FUNCTION: " << F.getName() << " ******************\n\n";

    // Only interested in tainted variables at exit BBs
    getTargetVarReach(F, domain, dataFlowResult);

    // Did not modify the incoming Function.
    return false;
  }

  virtual void getAnalysisUsage(AnalysisUsage& AU) const {
    AU.setPreservesCFG();
  }

 private:
};

char ReachingDefinitions::ID = 0;
RegisterPass<ReachingDefinitions> X("cd-reaching-definitions",
    "ReachingDefinitions");

}
