
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
  // 4) CallInst/InvokeInst, return v

  if (isa<Argument>(v)) {
    return v;
  }
  else if (isa<StoreInst>(v)) {
    return ((StoreInst*)v)->getPointerOperand();
  }
  else if (isa<Instruction>(v)){
    std::string str = valueToStr(v);
    const int VAR_NAME_START_IDX = 2;
    if ((str.length() > VAR_NAME_START_IDX && str.substr(0,VAR_NAME_START_IDX+1) == "  %"))
      return v;
    if (isa<CallInst>(v) || isa<InvokeInst>(v)) {
      std::string callee_func = getCallee(v);
      if (callee_func != "") {
        std::string def_arg = loadFuncRD(callee_func);
        if (def_arg != "")
            return v;
      }
    }
  }
  return 0;
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
  if (isa<Argument>(v)) {
    return str;
  }
  else {
      str = str.substr(VAR_NAME_START_IDX);
      return str;
  }

  return "";
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
            return valueToStr(val);
    }
    else if (InvokeInst* CI = dyn_cast<InvokeInst>(v)) {
        Value* val = CI->getArgOperand(i);
        if (val->hasName())
            return ("%"+val->getName().str());
        else
            return valueToStr(val);
    }
    return "";
}

   // Decode a series of vars separated by ";"
   bool overlapDefVar(std::string s1, std::string s2) {
      std::vector<std::string> v1;
      if (s1 != "") {
         while (s1.find(";") != std::string::npos) {
            v1.push_back(s1.substr(s1.find(";")));
            s1 = s1.substr(s1.find(";")+1);
         }
         v1.push_back(s1);
      }
      std::vector<std::string> v2;
      if (s2 != "") {
         while (s2.find(";") != std::string::npos) {
            v2.push_back(s2.substr(s2.find(";")));
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

std::string loadFuncRD(std::string func) {
    std::map<std::string, std::string> callee_arg;
    std::map<std::string, std::string> var_taint; 
    std::string file_name = func + ".df";
    std::ifstream infile;
    std::string line, label;
    infile.open(file_name.c_str());
    if (!infile.is_open())
        return "";
    while (!infile.eof()) {
       getline(infile, line);
       if (line.length() == 0)
           continue;
       label = line.substr(0, line.find(":"));
       if (label == "Define") {
          std::string var_name = line.substr(line.find(":")+2);
          if (line.find(", taint: ") != std::string::npos) {
              std::string taint_str = var_name.substr(var_name.find(":")+2);
              var_name = var_name.substr(0, var_name.find(","));
              var_taint[var_name] = taint_str;
          }
          else {
              var_taint[var_name] = "";
          } 
       }
       else if (label == "Argument") {
          std::string var_name = line.substr(line.find(":")+2);
          std::string pos = var_name.substr(var_name.find(",")+1);
          var_name = var_name.substr(0, var_name.find(","));
          callee_arg[var_name] = pos;
       }
    }
    infile.close();
    std::string def_var = "";
    for (std::map<std::string, std::string>::iterator it = var_taint.begin(); it != var_taint.end(); it++) {
       std::string var_name = it->first;
       std::string taint_str = it->second;
       std::string taint_var = (taint_str == ""? taint_str : taint_str.substr(0, taint_str.find(":")));
       if (callee_arg.find(var_name) != callee_arg.end())
           def_var += (callee_arg[var_name] + ";");
       else if (callee_arg.find(taint_var) != callee_arg.end())
           def_var += (callee_arg[taint_var] + taint_str.substr(taint_str.find(":")) + ";");
    }
    if (def_var != "")
        def_var = def_var.substr(0, def_var.length()-1);
    return def_var;
}

std::string valueToDefinitionVarStr(Value* v) {
  //Similar to valueToDefinitionStr, but we return just the defined var rather than the whole definition

  Value* def = getDefinitionVar(v);
  if (def == 0)
    return "";
  if (isa<Argument>(def) || isa<StoreInst>(def)) {
    if (def->hasName())
       return "%" + def->getName().str();
    // Cover operand in StoreInst without a name
    std::string str = valueToStr(def);
    if (isa<Argument>(def))
       return str.substr(str.find(" ")+1);
  }
  else if (isa<CallInst>(v) || isa<InvokeInst>(v)) {
    // Read callee's dataflow profile to get any defined args and LHS (if any)
    std::string def_str = "";
    std::string callee_func = getCallee(v); 
    if (callee_func != "") {
        std::string def_arg = loadFuncRD(callee_func); 
        if (def_arg != "") {
            while (def_arg.find(";") != std::string::npos) {
               int idx = atoi(def_arg.substr(0, def_arg.find(":")).c_str());
               std::string arg_name = getCalleeArg(v, idx);
               std::string s = def_arg.substr(0, def_arg.find(";"));
               def_str += (arg_name + s.substr(s.find(":")) + ";");
               def_arg = def_arg.substr(def_arg.find(";")+1);
            }
            int idx = atoi(def_arg.substr(0, def_arg.find(":")).c_str());
            std::string arg_name = getCalleeArg(v, idx);
            std::string s = def_arg.substr(0, def_arg.find(";"));
            def_str += (arg_name + s.substr(s.find(":")));
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
