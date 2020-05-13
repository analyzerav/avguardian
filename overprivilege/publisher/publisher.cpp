#include "utils.h"
#include "publisher.h"

char Publisher::ID = 0;
static RegisterPass<Publisher> X("publisher", "Publisher", false, true);

bool Publisher::runOnModule(Module &M) {
  topic.insert("apollo::localization::LocalizationEstimate");
  topic.insert("apollo::localization::LocalizationStatus");
  topic.insert("apollo::localization::CorrectedImu");
  topic.insert("apollo::localization::Gps");
  topic.insert("apollo::common::PointENU");
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
  for (auto it = field_copy.begin(); it != field_copy.end(); it++) {
       if (it->second.size() > 1 || field_modify.find(it->first) != field_modify.end())
    	   continue;
       errs() << "Overprivielge: " << it->first << "\n";
  }

  return true;
}

void Publisher::initAnalyzeOrder(Module &M) {
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  //CallGraph &CG = getAnalysis<CallGraph>();
  std::map<std::string, std::set<std::string> > func_caller_dup;
  for (Function &func : M) {
        if (func.isDeclaration()) continue;
  //      std::string funcName = demangleFuncName(func);
  //      if (isStdFunction(funcName)) continue;
        // Init defined func call info
        std::string caller = func.getName();
        func_def[caller] = &func;
        if (func_caller.find(caller) == func_caller.end()) {
            func_caller[caller] = std::set<std::string>();
            func_caller_dup[caller] = std::set<std::string>();
        }
        if (func_callee.find(caller) == func_callee.end())
            func_callee[caller] = std::set<std::string>();
        for (CallGraphNode::CallRecord &CR : *CG[&func]) {
            if (Function *calledFunc = CR.second->getFunction()) {
                if (calledFunc) {
                   if (calledFunc->isDeclaration()) continue;
                   // Update func call info
                   std::string callee = calledFunc->getName();
                   func_caller[caller].insert(callee);
                   func_callee[callee].insert(caller);
                   func_caller_dup[caller].insert(callee);
                }
            } else {
                errs() << "external node\n";
            }
        }
  }
  // func_caller will be erased after this call, func_callee remains unchanged
  compute_caller_callee_order();
}

void Publisher::get_zero_callee_func(std::set<std::string>& proc_l) {
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

void Publisher::traverse_call_chain(std::string s, std::list<std::string>& call_chain) {
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

void Publisher::compute_caller_callee_order() {
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

bool Publisher::runOnFunction(Module &M, Function &F) {
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
	      if (pub_pts_set[&F].find(v0) != pub_pts_set[&F].end()) {
                  for (Value* v : pub_pts_set[&F][v0]) {
                       pub_alias_set[&F][v].insert(vi);
                       pub_alias_set[&F][vi].insert(v);
                       errs() << "Load pub alias: " << valueToStr(vi) << "\t" << valueToStr(v) << "\n";
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
                      // Add new field type
                      std::string type_str = typeToString((&*instr)->getType());
                      if (type_str.find("%") == 0) {
                          type_str = type_str.substr(type_str.find('"')+1);
                          type_str = type_str.substr(0, type_str.find('"'));
                          if (type_str.find("class.") == 0) {
                              type_str = type_str.substr(6);
                              topic.insert(type_str);
                              errs() << "Detect type: " << type_str << "\n";
                          }
			  else if (type_str.find("struct.") == 0) { 
                              type_str = type_str.substr(7);
                              topic.insert(type_str);
                              errs() << "Detect type: " << type_str << "\n";
                          }
                      }
                  }
	      }
              if (pub_msg_val[&F].find(v0) != pub_msg_val[&F].end() || pub_alias_set[&F].find(v0) != pub_alias_set[&F].end()) {
                  if (pub_msg_val[&F].find(&*instr) == pub_msg_val[&F].end()) {
                      errs() << "Detect pub field: " << valueToStr(v0) << "\t" << valueToStr(&*instr) << "\n";
		      pub_msg_val[&F].insert(&*instr);
                      TargetFunc[F.getName().str()] = true;
		      // Add new field type
                      std::string type_str = typeToString((&*instr)->getType());
                      if (type_str.find("%") == 0) {
                          type_str = type_str.substr(type_str.find('"')+1);
                          type_str = type_str.substr(0, type_str.find('"'));
                          if (type_str.find("class.") == 0) {
                              type_str = type_str.substr(6);
                              topic.insert(type_str);
                              errs() << "Detect type: " << type_str << "\n";
                          }
                          else if (type_str.find("struct.") == 0) {
                              type_str = type_str.substr(7);
                              topic.insert(type_str);
                              errs() << "Detect type: " << type_str << "\n";
                          }
                      }
                  }
              }                   
	  }
	  else if (isa<InvokeInst>(*instr) || isa<CallInst>(*instr)) {
              std::string callee_name = getCallee(&*instr);
	      if (callee_name != "") {
		  callee_name = demangle(callee_name.c_str());    
		  if (callee_name.find("GetLatestObserved(") != std::string::npos) {
		      errs() << "SUBSCRIBE: " << F.getName() << " " << typeToString((&*instr)->getType()) << " " << valueToStr(&*instr) << "\n";
                      if (msg_val[&F].find(&*instr) == msg_val[&F].end()) {
			  msg_val[&F].insert(&*instr);
                          TargetFunc[F.getName().str()] = true;
		      }
		  }
		  else if (callee_name.find("apollo::common::adapter::AdapterManager::Publish") != std::string::npos) {
                      Value* arg = getCalleeArg(&*instr, 0);
		      //std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, arg);
	              errs() << "PUBLISH: " << F.getName() << " " << typeToString(arg->getType()) << " " << valueToStr(&*instr) << "\t" << valueToStr(arg) << "\n";
                      if (pub_msg_val[&F].find(arg) == pub_msg_val[&F].end()) {
                          pub_msg_val[&F].insert(arg);
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
			   if (pub_msg_val[&F].find(def) != pub_msg_val[&F].end()) {
                               Value* v0 = getCalleeArg(&*instr, 0);
                               pub_alias_set[&F][def].insert(v0);
                               pub_alias_set[&F][v0].insert(def);
                               errs() << "Alias pub: " << valueToStr(v0) << "\n";
                           }
		      }
	          }
	          else {
		     // Detect msg API	
	             callee_name = callee_name.substr(0, callee_name.find("("));
	             std::string ns = callee_name.substr(0, callee_name.find_last_of("::")-1);
		     // Detect message variable
		     if (topic.find(ns) != topic.end()) {
			 Value* v0 = getCalleeArg((Value*)(&*instr), 0);
                         if (msg_val[&F].find(v0) != msg_val[&F].end() || alias_set[&F].find(v0) != alias_set[&F].end()) {
		             //for (Value* v : msg_val[&F]) 
			      //	 if (alias_set[&F][v0].find(v) != alias_set[&F][v0].end())
		                     errs() << "Detect field: " << callee_name << "\t" << valueToStr(&*instr) << "\n";
                            //         field_use[&F].insert(&*instr);
				     if (msg_val[&F].find(&*instr) == msg_val[&F].end()) {
				         msg_val[&F].insert(&*instr);
					 TargetFunc[F.getName().str()] = true;
					 // Add new field type
					 std::string type_str = typeToString((&*instr)->getType());
                                         if (type_str.find("%") == 0) {
				             type_str = type_str.substr(type_str.find('"')+1);
                                             type_str = type_str.substr(0, type_str.find('"'));
					     if (type_str.find("class.") == 0) {
						 type_str = type_str.substr(6);
						 topic.insert(type_str);
						 errs() << "Detect type: " << type_str << "\n";
					     }
					 }
                                     }
			 }
			 if (pub_msg_val[&F].find(v0) != pub_msg_val[&F].end() || pub_alias_set[&F].find(v0) != pub_alias_set[&F].end()) {
			     if (callee_name.find("::set_") != std::string::npos) {
				 //errs() << "Pub field: " << callee_name << "\t" << valueToStr(&*instr) << "\n";
                                 Value* v1 = getCalleeArg((Value*)(&*instr), 1);
			         if (msg_val[&F].find(v1) != msg_val[&F].end())	{
				     int level = 0;
				     bool reached = false;
				     Value* curr_v = v0;
				     std::string dst_field = "";
                                     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
					 for (auto def : def_set) {
                                              //errs() << "Detect pub field dest: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<LoadInst>(def)) {
                                                  reached = true;
						  std::string type_str = typeToString(def->getType());
                                                  type_str = type_str.substr(type_str.find('"')+1);
                                                  type_str = type_str.substr(0, type_str.find('"'));
                                                  dst_field += ("," + type_str);
						  errs() << "Detect pub field dest: " << level << " " << type_str << "\n";
					      }
                                              if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def); 
                                                  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
                                                  if (narg < 1)
						      reached = true;
						  else
						      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
                                                  if (field_str.find("::mutable_") != std::string::npos)
                                                      field_str = field_str.substr(field_str.find("::mutable_")+10);
                                                  else
                                                      field_str = field_str.substr(field_str.find_last_of("::")+1);
						  dst_field += ("," + field_str);
						  errs() << "Detect pub field dest: " << level << " " << called_func << "\n";
                                              }
                                         }
					 level++;
                                     }
				     level = 0;
				     reached = false;
				     curr_v = v1;
				     std::string src_field = "";
                                     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
                                         for (auto def : def_set) {
                                              //errs() << "Detect pub field src: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<LoadInst>(def)) {
                                                  reached = true;
						  std::string type_str = typeToString(def->getType());
						  type_str = type_str.substr(type_str.find('"')+1);
						  type_str = type_str.substr(0, type_str.find('"'));
						  src_field += ("," + type_str);
						  errs() << "Detect pub field src: " << level << " " << type_str << "\n";
                                              }
                                              if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def);
						  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
						  if (narg < 1 || called_func.find("GetLatestObserved(") != std::string::npos)
                                                      reached = true;
						  else 
						      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
						  if (field_str.find("::mutable_") != std::string::npos)
					              field_str = field_str.substr(field_str.find("::mutable_")+10);
						  else
						      field_str = field_str.substr(field_str.find_last_of("::")+1);  
						  src_field += ("," + field_str);
						  errs() << "Detect pub field src: " << level << " " << called_func << "\n";
                                              }
                                         }
					 level++;
                                     }
				     dst_field = callee_name.substr(callee_name.find("::set_")+6) + dst_field;
				     errs() << "Detect pub field copy: " << src_field << " -> " << dst_field << "\t" << valueToStr(v0) << "\t" << valueToStr(v1) << "\n";
				     field_copy[dst_field].insert(src_field.substr(src_field.find(",")+1));
                                 }
				 else {
			             int level = 0;
                                     bool reached = false;
                                     Value* curr_v = v0;
                                     std::string dst_field = "";
				     std::string type_str = "";
                                     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
                                         for (auto def : def_set) {
                                              //errs() << "Detect pub field dest: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<AllocaInst>(def)) 
						  reached = true;
					      else if (isa<LoadInst>(def)) {
						  Instruction* vi = dyn_cast<Instruction>(def); 
						  std::unordered_set<Value*> s1 = getDefinitions(&F, vi, vi->getOperand(0));
                                                  if (isa<StoreInst>(*(s1.begin())))
						      curr_v = dyn_cast<StoreInst>(*(s1.begin()))->getOperand(0);
						  else
						      reached = true;
						  type_str = typeToString(def->getType());
                                                  type_str = type_str.substr(type_str.find('"')+1);
                                                  type_str = type_str.substr(0, type_str.find('"'));
                                                  //dst_field += ("," + typeToString(def->getType()));
                                                  errs() << "Detect pub field dest: " << level << " " << type_str << "\n";                              
                                              }
					      else if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def);
                                                  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
                                                  if (narg < 1)
                                                      reached = true;
                                                  else
                                                      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
                                                  if (field_str.find("::mutable_") != std::string::npos)
                                                      field_str = field_str.substr(field_str.find("::mutable_")+10);
                                                  else
                                                      field_str = field_str.substr(field_str.find_last_of("::")+1);
                                                  dst_field += ("," + field_str);
                                                  errs() << "Detect pub field dest: " << level << " " << called_func << "\n";
                                              }
					      else
						  reached = true;
                                         }
                                         level++;
                                     }
				     dst_field = callee_name.substr(callee_name.find("::set_")+6) + dst_field;
				     dst_field += ("," + type_str);
			             errs() << "Detect pub field modify: " << dst_field << "\t" << valueToStr(v1) << "\n";
                                     field_modify.insert(dst_field);
				 }
                             }
			     else if (callee_name.find("::CopyFrom") != std::string::npos) {
                                 //errs() << "Pub field: " << callee_name << "\t" << valueToStr(&*instr) << "\n";
				 Value* v1 = getCalleeArg((Value*)(&*instr), 1);
                                 if (msg_val[&F].find(v1) != msg_val[&F].end()) {
                                     int level = 0;
                                     bool reached = false;
                                     Value* curr_v = v0;
				     std::string dst_field = "";
				     std::string type_str = "";
                                     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
                                         for (auto def : def_set) {
                                              //errs() << "Detect pub field dest: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<AllocaInst>(def))
                                                  reached = true;
                                              else if (isa<LoadInst>(def)) {
                                                  Instruction* vi = dyn_cast<Instruction>(def);
                                                  std::unordered_set<Value*> s1 = getDefinitions(&F, vi, vi->getOperand(0));
                                                  if (isa<StoreInst>(*(s1.begin())))
                                                      curr_v = dyn_cast<StoreInst>(*(s1.begin()))->getOperand(0);
                                                  else
                                                      reached = true;
						  type_str = typeToString(def->getType());
                                                  type_str = type_str.substr(type_str.find('"')+1);
                                                  type_str = type_str.substr(0, type_str.find('"'));
						  //dst_field += ("," + typeToString(def->getType()));
						  errs() << "Detect pub field dest: " << level << " " << type_str << "\n";
                                              }
					      else if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def);
                                                  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
                                                  if (narg < 1)
                                                      reached = true;
                                                  else
                                                      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
                                                  if (field_str.find("::mutable_") != std::string::npos)
                                                      field_str = field_str.substr(field_str.find("::mutable_")+10);
                                                  else
                                                      field_str = field_str.substr(field_str.find_last_of("::")+1);
						  dst_field += ("," + field_str);
						  errs() << "Detect pub field dest: " << level << " " << called_func << "\n";
                                              }
					      else
						  reached = true;
                                         }
                                         level++;
                                     }
				     dst_field += ("," + type_str);
                                     level = 0;
                                     reached = false;
                                     curr_v = v1;
				     std::string src_field = "";
				     type_str = "";
                                     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
                                         for (auto def : def_set) {
                                              //errs() << "Detect pub field src: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<AllocaInst>(def))
                                                  reached = true;
                                              else if (isa<LoadInst>(def)) {
                                                  Instruction* vi = dyn_cast<Instruction>(def);
                                                  std::unordered_set<Value*> s1 = getDefinitions(&F, vi, vi->getOperand(0));
                                                  if (isa<StoreInst>(*(s1.begin()))) 
                                                      curr_v = dyn_cast<StoreInst>(*(s1.begin()))->getOperand(0);
                                                  else
                                                      reached = true;
						  type_str = typeToString(def->getType());
                                                  type_str = type_str.substr(type_str.find('"')+1);
                                                  type_str = type_str.substr(0, type_str.find('"'));
						  //src_field += ("," + typeToString(def->getType()));
						  errs() << "Detect pub field src: " << level << " " << type_str << "\n";
                                              }
					      else if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def); 
                                                  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
                                                  if (narg < 1 || called_func.find("GetLatestObserved(") != std::string::npos)
                                                      reached = true;
                                                  else 
                                                      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
                                                  if (field_str.find("::mutable_") != std::string::npos)
                                                      field_str = field_str.substr(field_str.find("::mutable_")+10);
                                                  else
                                                      field_str = field_str.substr(field_str.find_last_of("::")+1);
						  src_field += ("," + field_str);
						  errs() << "Detect pub field src: " << level << " " << called_func << "\n";
                                              }
					      else
                                                  reached = true;
                                         }
                                         level++;
                                     }
				     src_field += ("," + type_str);
				     errs() << "Detect pub field copy: " << src_field << " -> " << dst_field << "\t" << valueToStr(v0) << "\t" << valueToStr(v1) << "\n";
				     field_copy[dst_field.substr(dst_field.find(",")+1)].insert(src_field.substr(src_field.find(",")+1));
				 }
				 else {
			             int level = 0;
                                     bool reached = false;
                                     Value* curr_v = v0;
                                     std::string dst_field = "";
                                     std::string type_str = "";
				     while (!reached) {
                                         std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, curr_v);
                                         for (auto def : def_set) {
                                              //errs() << "Detect pub field dest: " << level << " " << valueToStr(def) << "\n";
                                              if (isa<AllocaInst>(def))
                                                  reached = true;
					      else if (isa<LoadInst>(def)) {
						  Instruction* vi = dyn_cast<Instruction>(def); 
                                                  std::unordered_set<Value*> s1 = getDefinitions(&F, vi, vi->getOperand(0));    
						  if (isa<StoreInst>(*(s1.begin())))
                                                      curr_v = dyn_cast<StoreInst>(*(s1.begin()))->getOperand(0);
                                                  else
                                                      reached = true;
						  type_str = typeToString(def->getType());
                                                  type_str = type_str.substr(type_str.find('"')+1);
                                                  type_str = type_str.substr(0, type_str.find('"'));
                                                  //dst_field += ("," + typeToString(def->getType()));
                                                  errs() << "Detect pub field dest: " << level << " " << type_str << "\n";
                                              }
					      else if (isa<CallInst>(def) || isa<InvokeInst>(def)) {
                                                  int narg = getCalleeArgNum(def);
                                                  std::string called_func = getCallee(def);
                                                  called_func = demangle(called_func.c_str());
                                                  if (narg < 1)
                                                      reached = true;
                                                  else
                                                      curr_v = getCalleeArg(def, 0);
						  std::string field_str = called_func.substr(0, called_func.find("("));
                                                  if (field_str.find("::mutable_") != std::string::npos)
                                                      field_str = field_str.substr(field_str.find("::mutable_")+10);
                                                  else
                                                      field_str = field_str.substr(field_str.find_last_of("::")+1);
                                                  dst_field += ("," + field_str);
                                                  errs() << "Detect pub field dest: " << level << " " << called_func << "\n";
                                              }
					      else
						  reached = true;
                                         }
                                         level++;
                                     }
		                     dst_field += ("," + type_str);		     
			             errs() << "Detect pub field modify: " << dst_field << " " << valueToStr(v0) << "\t" << valueToStr(v1) << "\n";
                                     field_modify.insert(dst_field.substr(dst_field.find(",")+1));
				 }
			     }
			     else {
				 if (pub_msg_val[&F].find(&*instr) == pub_msg_val[&F].end()) {
                                     pub_msg_val[&F].insert(&*instr);
                                     TargetFunc[F.getName().str()] = true;
				     // Add new field type
                                     std::string type_str = typeToString((&*instr)->getType());
                                     if (type_str.find("%") == 0) {
                                         type_str = type_str.substr(type_str.find('"')+1);
                                         type_str = type_str.substr(0, type_str.find('"'));
                                         if (type_str.find("class.") == 0) {
                                             type_str = type_str.substr(6);
                                             topic.insert(type_str);
                                             errs() << "Detect type: " << type_str << "\n";
                                         }
                                     }
                                 }
                             }
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
                                     // Add new field type
                                     std::string type_str = typeToString((&*it)->getType());
                                     if (type_str.find("%") == 0) {
                                         type_str = type_str.substr(type_str.find('"')+1);
                                         type_str = type_str.substr(0, type_str.find('"'));
                                         if (type_str.find("class.") == 0) {
                                             type_str = type_str.substr(6);
                                             topic.insert(type_str);
                                             errs() << "Detect type: " << type_str << "\n";
                                         }
                                     }
				 }
    			     }
			     if (pub_msg_val[&F].find(arg) != pub_msg_val[&F].end() || pub_alias_set[&F].find(arg) != pub_alias_set[&F].end()) {
                                 if (pub_msg_val[fn].find(&*it) == pub_msg_val[fn].end()) {
                                     pub_msg_val[fn].insert(&*it);
			             TargetFunc[fn->getName().str()] = true;
				     errs() << "Detect pub function: " << callee_name << " " << valueToStr(&*it) << "\t" << valueToStr((Value*)(&*instr)) << "\n";
                                     // Add new field type
                                     std::string type_str = typeToString((&*it)->getType());
                                     if (type_str.find("%") == 0) {
                                         type_str = type_str.substr(type_str.find('"')+1);
                                         type_str = type_str.substr(0, type_str.find('"'));
                                         if (type_str.find("class.") == 0) {
                                             type_str = type_str.substr(6);
                                             topic.insert(type_str);
                                             errs() << "Detect type: " << type_str << "\n";
                                         }
                                     }
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
              Value* v0 = vi->getOperand(0);
	      std::unordered_set<Value*> def_set = getDefinitions(&F, &*instr, v0);
	      for (auto def : def_set) {
	           if (msg_val[&F].find(def) != msg_val[&F].end()) {
                       Value* v1 = vi->getOperand(1); 
		       pts_set[&F][v1].insert(def);
		       errs() << "Pts: " << valueToStr(v1) << "\n";
	 	   }
		   if (pub_msg_val[&F].find(def) != pub_msg_val[&F].end()) {
                       Value* v1 = vi->getOperand(1);
                       pub_pts_set[&F][v1].insert(def);
                       errs() << "Pts pub: " << valueToStr(v1) << "\n";
                   }
	      }
	      Value* v1 = vi->getOperand(1);
	      if (isa<GetElementPtrInst>(v1)) {
		  if (pub_msg_val[&F].find(v1) != pub_msg_val[&F].end()) {
                      if (isa<LoadInst>(v0)) {
			  Value* vpt = dyn_cast<LoadInst>(v0)->getOperand(0);
		      	  if (msg_val[&F].find(vpt) != pub_msg_val[&F].end()) {
	                      errs() << "Detect pub field copy (raw): " << valueToStr(&*instr) << "\t" << valueToStr(v1) << "\n";
                          }
			  else {
                              errs() << "Detect pub field modify (raw): " << valueToStr(&*instr) << "\t" << valueToStr(v1) << "\n";
			  }
		      }
		  }
	      }
	  }
     }
  }
}

std::unordered_set<Value*> Publisher::getDefinitions(Function *F, Instruction *I, Value *val) {
    std::unordered_set<Value*> results;
    // need reaching-definitions here
    ReachingDefinitions &RD = getAnalysis<ReachingDefinitions>();

    std::string funcName = F->getName(); //demangle(F->getName().str().c_str());
    for (Value* v : RD.func_reaching_def[funcName][I])
           if (val == valueToDefVar(v))
	       results.insert(v);   
    //for (Value* v : results)
    //     errs() << "RD: " << valueToStr(v) << "\n";

    return results;
}

