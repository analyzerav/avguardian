#include "utils.h"
#include "subscriber.h"

char Subscriber::ID = 0;
static RegisterPass<Subscriber> X("subscriber", "Subscriber", false, true);

bool Subscriber::runOnModule(Module &M) {
  topic.insert("apollo::localization::LocalizationEstimate");
  topic.insert("apollo::localization::LocalizationStatus");
  topic.insert("apollo::localization::CorrectedImu");
  topic.insert("apollo::localization::Gps");
  //record the function name
  std::ifstream infile("func.meta");
  if (infile.is_open()) {
      std::string func;
      while (std::getline(infile, func))
          TargetFunc[func] = true;
      infile.close();
  }
  bool converged = false;
  while (!converged) {
    converged = true;
    for (Function &func : M) {
      if (func.isDeclaration()) 
         continue;
      std::string funcName = demangle(func.getName().str().c_str());
      if (isStdFunction(funcName.c_str())) 
          continue;
      if (TargetFunc.find(func.getName().str()) != TargetFunc.end()) {
	  if (TargetFunc[func.getName().str()]) {
              converged = false;
	      TargetFunc[func.getName().str()] = false;
              runOnFunction(M, func);
	  }
      }       
    }
  }
  //output detected fields
  for (auto it = field_use.begin(); it != field_use.end(); it++) {
       errs() << "Func used field: " << it->first->getName();
       for (Value* v : it->second) {
	    std::string callee_name = demangle(getCallee(v).c_str());
            callee_name = callee_name.substr(0, callee_name.find("("));   
	    errs() << "\t" << callee_name;
       }
       errs() << "\n";
  }
  return true;
}

bool Subscriber::runOnFunction(Module &M, Function &F) {
  for (Function::iterator basicBlock = F.begin(); basicBlock != F.end(); ++basicBlock) {
     for (BasicBlock::iterator instr = basicBlock->begin(); instr != basicBlock->end(); ++instr) {
          if (!isa<Instruction>(*instr))
              continue;
	  if (isa<LoadInst>(*instr)) {
	      LoadInst* vi = dyn_cast<LoadInst>(&*instr);
	      Value* v0 = vi->getOperand(0);
	      std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, v0);
	      if (pts_set[&F].find(v0) != pts_set[&F].end()) {
		  for (Value* v : pts_set[&F][v0]) {
		       alias_set[&F][v].insert(vi);
		       alias_set[&F][vi].insert(v);
		       errs() << "Load alias: " << valueToStr(vi) << "\t" << valueToStr(v) << "\n";
		  }
	      }
	  }
	  else if (isa<GetElementPtrInst>(*instr)) {
              GetElementPtrInst* vi = dyn_cast<GetElementPtrInst>(&*instr);
              Value* v0 = vi->getOperand(0);
              if (msg_val[&F].find(v0) != msg_val[&F].end() || alias_set[&F].find(v0) != alias_set[&F].end()) {
                  errs() << "Detect field: " << valueToStr(v0) << "\t" << valueToStr(&*instr) << "\n";
                  if (msg_val[&F].find(&*instr) == msg_val[&F].end()) {
                      msg_val[&F].insert(&*instr);
                      TargetFunc[F.getName().str()] = true;
		  }
	      }
          }
	  else if (isa<InvokeInst>(*instr) || isa<CallInst>(*instr)) {
              std::string callee_name = getCallee(&*instr);
	      if (callee_name != "") {
		  callee_name = demangle(callee_name.c_str());    
		  if (callee_name.find("GetLatestObserved(") != std::string::npos) {
		      errs() << "Found: " << F.getName() << " " << typeToString((&*instr)->getType()) << " " << valueToStr(&*instr) << "\n";
                      if (msg_val[&F].find(&*instr) == msg_val[&F].end()) {
			  msg_val[&F].insert(&*instr);
                          TargetFunc[F.getName().str()] = true;
		      }
		  }
		  else if (callee_name.find("::operator=(") != std::string::npos) {
	              Value* v1 = getCalleeArg(&*instr, 1);
		      std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, v1);
		      for (auto def : def_set) {
                           if (msg_val[&F].find(def) != msg_val[&F].end()) {
			       Value* v0 = getCalleeArg(&*instr, 0); 	   
			       alias_set[&F][def].insert(v0);
			       alias_set[&F][v0].insert(def);
                               errs() << "Alias: " << valueToStr(v0) << "\n"; 
			   }
		      }
	          }
	          else {
	             callee_name = callee_name.substr(0, callee_name.find("("));
	             std::string ns = callee_name.substr(0, callee_name.find_last_of("::")-1);
		     if (topic.find(ns) != topic.end()) {
			 Value* v0 = getCalleeArg((Value*)(&*instr), 0);
                         if (msg_val[&F].find(v0) != msg_val[&F].end() || alias_set[&F].find(v0) != alias_set[&F].end()) {
		             //for (Value* v : msg_val[&F]) 
			      //	 if (alias_set[&F][v0].find(v) != alias_set[&F][v0].end())
		                     errs() << "Detect field: " << callee_name << "\t" << valueToStr(&*instr) << "\n";
                                     field_use[&F].insert(&*instr);
			 }
	             }
		     else {
		        int narg = getCalleeArgNum(&*instr);
			Function* fn = getCalledFunc(&*instr);
			auto it = fn->arg_begin();
			for (int i = 0; i < narg; i++) {
		             Value* arg = getCalleeArg(&*instr, i);		
		             if (msg_val[&F].find(arg) != msg_val[&F].end() || alias_set[&F].find(arg) != alias_set[&F].end()) {
			         if (msg_val[fn].find(&*it) == msg_val[fn].end()) {
			             msg_val[fn].insert(&*it); 
				     TargetFunc[fn->getName().str()] = true;
				     errs() << "Detect function: " << callee_name << " " << valueToStr(&*it) << "\t" << valueToStr((Value*)(&*instr)) << "\n";
                                 }
    			     }
			     it++;
			}    
		     }
	          }
              } 		  
	  }
	  else if (isa<StoreInst>(&*instr)) {
	      StoreInst* vi = dyn_cast<StoreInst>(&*instr);
              Value* v0 =vi->getOperand(0);
	      std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, v0);
	      for (auto def : def_set) {
	           if (msg_val[&F].find(def) != msg_val[&F].end()) {
                       Value* v1 = vi->getOperand(1); 
		       pts_set[&F][v1].insert(def);
		       errs() << "Pts: " << valueToStr(v1) << "\n";
	 	   }
	      }
	  }
     }
  }
}

std::unordered_set<Value*> Subscriber::getDefinitions(Function *F, Instruction *I, Value *val) {
    std::unordered_set<Value*> results;
    // need reaching-definitions here
    ReachingDefinitions &RD = getAnalysis<ReachingDefinitions>();

    std::string funcName = F->getName(); //demangle(F->getName().str().c_str());
    for (Value* v : RD.func_reaching_def[funcName][I])
           if (val == valueToDefVar(v))
	       results.insert(v);   
    //for (Value* v : results)
    //	   errs() << "RD: " << valueToStr(v) << "\n";

    return results;
}

