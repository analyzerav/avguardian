//===- CD.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"

#include <sys/stat.h>
#include <cxxabi.h>
#include <unordered_map>
#include <vector>
#include <stack>
#include <unordered_set>

#include <fstream>
#include <algorithm>

#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/CFG.h"

using namespace llvm;

#define DEBUG_TYPE "CD"

namespace {
    enum EdgeType {TRUE, FALSE, INVOKE_TRUE, INVOKE_FALSE, OTHER };
    struct CDNode {
        BasicBlock* From;
        BasicBlock* To;
        std::string edge;
        CDNode(BasicBlock* a, BasicBlock* b, EdgeType e){
            From = a;
            To = b;
            if(e == EdgeType::TRUE)
                edge = "taken";
            else if(e == EdgeType::FALSE)
                edge = "not taken";
            else if(e == EdgeType::INVOKE_TRUE)
                edge = "Invoke taken";
            else if(e == EdgeType::INVOKE_FALSE)
                edge = "Invoke not taken";
            else
                edge = "unknown";
        }
    };

    struct SinkBBNode{
        BasicBlock* SinkBB;
        std::string SinkFuncName;
        SinkBBNode(BasicBlock* bb, std::string name){
            SinkBB = bb;
            SinkFuncName = name;
        }
    };

    // Target Functions: 0 -- never checked, 1 -- being checked, 2 -- finished checked
    std::map<std::string, int> TargetSinks;
    std::set<std::string> TargetSources;
    // Sink Information for given function
    std::unordered_map<std::string, std::vector<SinkBBNode>> FunctionData;

    struct CD : public FunctionPass {
        static char ID; // Pass identification, replacement for typeid

        CD() : FunctionPass(ID) {}

        bool isInFunctionData(std::vector<SinkBBNode>& function_data, BasicBlock* bb) {
             for (int i = 0; i < function_data.size(); i++)
                  if (function_data[i].SinkBB == bb)
                      return true;
             return false;
        }
 
        bool doInitialization(Module &M) override {

          //record the source function name
          std::ifstream infile("source.meta");
          if (infile.is_open()) {
              std::string func;
              while (std::getline(infile, func))
                 TargetSources.insert(func);
              infile.close();
          }

          //record the sink function name
          std::ifstream myfile("func.meta");
          if (myfile.is_open()) {
              std::string func;
              while (std::getline(myfile, func)) 
                 TargetSinks[func] = 0;
              myfile.close();
          }
    
          int num_new_sinks = TargetSinks.size();
          while (num_new_sinks > 0) {
            int added_sinks = 0;
            for (std::map<std::string, int>::iterator it = TargetSinks.begin(); it != TargetSinks.end(); it++)
                 if (it->second == 0)
                     it->second = 1;
            for(Module::iterator func_inter = M.begin(); func_inter != M.end(); ++func_inter){
                Function* func = &(*func_inter);
                if(func->isDeclaration())
                    continue;
                for(Function::iterator bb_iter = func->begin(); bb_iter != func->end(); ++bb_iter) {
                    BasicBlock *bb = &(*bb_iter);
                    for(BasicBlock::iterator i_iter = bb->begin(); i_iter != bb->end(); ++i_iter) {
                        Instruction *I = &(*i_iter);
                        if(I->getOpcode() == Instruction::Invoke){
                            if (const InvokeInst *invoke = dyn_cast<InvokeInst>(I)) {
                                Function* calledfunc = (Function*)invoke->getCalledFunction(); 
                                if(calledfunc){
                                    for(std::map<std::string, int>::iterator it = TargetSinks.begin(); it != TargetSinks.end(); it++) {
                                        if (it->second == 2) continue;
                                        //if (calledfunc->getName().str().find("_ZN6apollo8planning12PathObstacle23AddLongitudinalDecisionERKSsRKNS0_18ObjectDecisionTypeE") != std::string::npos && it->first.find("AddLongitudinalDecision") != std::string::npos)
                                       //     errs() << "Debug: INVOKE-" << func->getName() << "-" << demangle(func->getName().str().c_str()) << "-" << calledfunc->getName() << "-" << demangle(calledfunc->getName().str().c_str())  << "-" << bb->getName() << " " << it->first << " " << demangle(calledfunc->getName().str().c_str()).find(it->first) << "\n";
                                        if (demangle(calledfunc->getName().str().c_str()).find(it->first) == 0) { //!= std::string::npos){
                                            if (isInFunctionData(FunctionData[demangle(func->getName().str().c_str())], bb))
                                                continue;
                                            errs() << "INVOKE-" << func->getName() << "-" << demangle(func->getName().str().c_str()) << "-" << calledfunc->getName() << "-" << demangle(calledfunc->getName().str().c_str())  << "-" << bb->getName() << "\n";
                                            FunctionData[demangle(func->getName().str().c_str())].push_back(SinkBBNode(bb, demangle(calledfunc->getName().str().c_str())));
                                            if (TargetSinks.find(demangle(func->getName().str().c_str())) == TargetSinks.end()) {
                                                bool source_reach = false;
                                                for (std::set<std::string>::iterator st = TargetSources.begin(); st != TargetSources.end(); st++)
                                                    if (demangle(func->getName().str().c_str()).find(*st) != std::string::npos) 
                                                        source_reach = true;
                                                if (!source_reach) {
                                                    TargetSinks[demangle(func->getName().str().c_str())] = 0;
                                                    added_sinks++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if(I->getOpcode() == Instruction::Call){
                            if (const CallInst *invoke = dyn_cast<CallInst>(I)) {
                                Function* calledfunc = (Function*)invoke->getCalledFunction();
                                if(calledfunc){
                                    for(std::map<std::string, int>::iterator it = TargetSinks.begin(); it != TargetSinks.end(); it++) {
                                        if (it->second == 2) continue;
                                        if(demangle(calledfunc->getName().str().c_str()).find(it->first) == 0) { //!= std::string::npos){
                                            if (isInFunctionData(FunctionData[demangle(func->getName().str().c_str())], bb))
                                                continue;
                                            errs() << "CALL-" << func->getName() << "-" << demangle(func->getName().str().c_str()) << "-" << calledfunc->getName() << "-" << demangle(calledfunc->getName().str().c_str())  << "-" << bb->getName() << "\n";
                                            FunctionData[demangle(func->getName().str().c_str())].push_back(SinkBBNode(bb, demangle(calledfunc->getName().str().c_str())));
                                            if (TargetSinks.find(demangle(func->getName().str().c_str())) == TargetSinks.end()) {
                                                bool source_reach = false;
                                                for (std::set<std::string>::iterator st = TargetSources.begin(); st != TargetSources.end(); st++)
                                                    if (demangle(func->getName().str().c_str()).find(*st) != std::string::npos) 
                                                        source_reach = true;
                                                if (!source_reach) {
                                                    TargetSinks[demangle(func->getName().str().c_str())] = 0;
                                                    added_sinks++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (I->getOpcode() == Instruction::Call || I->getOpcode() == Instruction::Invoke) {
                            
                        }
                    }
                }
            }
            for (std::map<std::string, int>::iterator it = TargetSinks.begin(); it != TargetSinks.end(); it++)
                 if (it->second == 1)
                     it->second = 2;
            num_new_sinks = added_sinks;
          }
          //for (std::map<std::string, int>::iterator it = TargetSinks.begin(); it != TargetSinks.end(); it++)
          //     errs() << "Sink Func: " << it->first << "\n";
          return false;
        }

        bool doFinalization(Module &M) override {
            /*for(auto a : FunctionData){
                errs() << a.first << "\n";
                for(auto b : a.second){
                    errs() << "     " << (b.SinkBB)->getName() << "---" << b.SinkFuncName << "\n";
                }
            }*/
            errs() << "Total number of functions analyzed: " << FunctionData.size() << "\n";
            return false;
        }
 
        // Need to make sure the keys in pred_list are CD BBs, the old version does not consider branch1->inovke->branch2, only branch1->branch2
/*
        void extractBoolExpr(BasicBlock* curr, std::map<std::string, std::list<std::string>>& pred_list, std::unordered_map<BasicBlock*, std::unordered_set<std::string>>& Result, std::unordered_map<BasicBlock*, std::vector<CDNode>>& CDMap) {
            for (int ii = 0; ii < CDMap[curr].size(); ii++) {
                 BasicBlock* pred = CDMap[curr][ii].From;
                 if (Result.find(pred) != Result.end()) {
                     if (pred_list.find(curr->getName()) == pred_list.end())
                         pred_list[curr->getName()] = std::list<std::string>();
                     pred_list[curr->getName()].push_back(std::string(pred->getName())+","+CDMap[curr][ii].edge);
                     errs() << "Found CD " << pred->getName() << "--" << CDMap[curr][ii].edge << "--" << curr->getName() << "\n";
                 }
                 else {
                     extractBoolExpr(pred, pred_list, Result, CDMap);
                 }
            }
        }
*/
        void extractBoolExpr(BasicBlock* curr, BasicBlock* curr_cd, std::map<std::string, std::list<std::string>>& pred_list, std::unordered_map<BasicBlock*, std::unordered_set<std::string>>& Result, std::unordered_map<BasicBlock*, std::vector<CDNode>>& CDMap, LoopInfo &LI) {
            for (int ii = 0; ii < CDMap[curr].size(); ii++) {
                 BasicBlock* pred = CDMap[curr][ii].From;
                 if (curr_cd == pred)
                     continue;
                 if (CDMap[curr][ii].edge.find("unknown") == 0)
                     continue;
                 //errs() << "Debug: CD of " << curr->getName() << ": " << pred->getName() << "--" << CDMap[curr][ii].edge << "--" << CDMap[curr][ii].To->getName() << " " << curr_cd->getName() << "\n";
                 if (Result.find(pred) != Result.end()) {
                     bool not_pred = false;
                     if (LI.isLoopHeader(curr_cd)) {
                         Loop* curr_L = LI.getLoopFor(curr_cd);
                         not_pred = curr_L->contains(pred);        
                     }
                     if (!not_pred) {
                         if (pred_list.find(curr_cd->getName()) == pred_list.end())
                             pred_list[curr_cd->getName()] = std::list<std::string>();
                         pred_list[curr_cd->getName()].push_back(std::string(pred->getName())+","+CDMap[curr][ii].edge);
                         errs() << "Found CD " << pred->getName() << "--" << CDMap[curr][ii].edge << "--" << curr->getName() << "--" << curr_cd->getName() << "\n";
                     } 
                }
                 else {
                     extractBoolExpr(pred, curr_cd, pred_list, Result, CDMap, LI);
                 }
            }
        }

        void printAllPred(std::string curr_bb, std::map<std::string, std::list<std::string>>& pred_list, std::string expr) {
             if (pred_list.find(curr_bb) == pred_list.end() || pred_list[curr_bb].size() == 0) {
                 errs() << "Boolean expr: " << expr << "\n";
                 return;
             }
             for (std::list<std::string>::iterator it = pred_list[curr_bb].begin(); it != pred_list[curr_bb].end(); it++) {
                 std::string bb = (*it).substr(0, (*it).find(","));
                 if ((*it).find(",taken") != std::string::npos)
                     printAllPred(bb, pred_list, expr + " AND " + bb + "==T");
                 else if ((*it).find(",not taken") != std::string::npos)
                     printAllPred(bb, pred_list, expr + " AND " + bb + "==F");
             }
        }

        bool isPredCD(std::string curr_bb, std::string cand_pred, std::map<std::string, std::list<std::string>>& pred_list) {
             if (pred_list.find(curr_bb) == pred_list.end())
                 return false;
             if (curr_bb == cand_pred || std::find(pred_list[curr_bb].begin(), pred_list[curr_bb].end(), cand_pred) != pred_list[curr_bb].end())
                 return true;
             for (std::list<std::string>::iterator it = pred_list[curr_bb].begin(); it != pred_list[curr_bb].end(); it++) {
                 std::string bb = (*it).substr(0, (*it).find(","));
                 //errs() << " Checked " << bb << "->" << cand_pred << "  "; //errs() << " " << bb;
                 if (isPredCD(bb, cand_pred, pred_list))
                     return true;
             }
             return false;
        }

        bool runOnFunction(Function &F) override {

            //Loop Header
            std::unordered_set<BasicBlock*> LoopHeader;

            std::unordered_map<BasicBlock*, std::vector<CDNode>> CDMap;

            //if(demangle(F.getName().str().c_str()) != "apollo::planning::Crosswalk::BuildStopDecision(apollo::planning::Frame*, apollo::planning::ReferenceLineInfo*, apollo::hdmap::PathOverlap*, std::vector<std::string, std::allocator<std::string> >)")
            //    return false;

            if(F.isDeclaration())
                return false;

            PostDominatorTree &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
            LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

            if(FunctionData.find(demangle(F.getName().str().c_str())) != FunctionData.end()){
                LLVM_DEBUG(dbgs() << demangle(F.getName().str().c_str()) << "\n");
                // starting control dependency analysis
                for (Function::iterator b = F.begin(); b != F.end(); b++) {
                    BasicBlock *A = &(*b);
                    if(PDT[A]->getIDom()->getBlock())
                        LLVM_DEBUG(dbgs() << A->getName() << " (" << PDT[A]->getIDom()->getBlock()->getName() << ")\n");
                    else
                        LLVM_DEBUG(dbgs() << A->getName() << "\n");
                    for (succ_iterator succ = succ_begin(A), end = succ_end(A); succ != end; ++succ) {
                        BasicBlock *B = *succ;
                        LLVM_DEBUG(dbgs() << "  ----" << B->getName() << '\n');
                        assert(A && B);
                        if(A == B || !PDT.dominates(B, A)){
                            BasicBlock *lub = PDT[A]->getIDom()->getBlock();
                            LLVM_DEBUG(dbgs() << "      ----Current edge type: "<< getEdgeType(A, B) << '\n');
                            BasicBlock *tmp = B;
                            while(tmp != lub){
                                CDNode curNode(A, B, getEdgeType(A, B));
                                CDMap[tmp].push_back(curNode);
                                tmp =  PDT[tmp]->getIDom()->getBlock();
                            }
                        }

                    }

                    if(LI.isLoopHeader(A)){
                        LoopHeader.insert(A);
                    }

                    // Get the BB Name for the sink
                    /*for(BasicBlock::iterator i_iter = A->begin(); i_iter != A->end(); ++i_iter) {
                        Instruction *I = &(*i_iter);
                        //TODO-Consider Call Insn as well?
                        if(I->getOpcode() == Instruction::Invoke){
                            if (const InvokeInst *invoke = dyn_cast<InvokeInst>(I)) {
                                Function* calledfunc = (Function*)invoke->getOperand(invoke->getNumOperands() - 3);
                                if(calledfunc){
                                    if(demangle(calledfunc->getName().str().c_str()).find("apollo::planning::SignalLight::BuildStopDecision") != std::string::npos)
                                        errs() << "!!!!!Sink BB Nmae: " << A->getName() << "\n";
                                }
                            }
                        }
                    }*/
                }

                //finish the control dependency analysis, print out the result.
                /*errs() << "##### CD Rsult for function: " << demangle(F.getName().str().c_str()) << " \n";
                for (Function::iterator b = F.begin(); b != F.end(); b++) {
                    BasicBlock *BB = &(*b);
                    errs() << BB->getName() << ":\n";
                    for(int i = 0; i < CDMap[BB].size(); ++i){
                        errs() << "     ---" << CDMap[BB][i].From->getName()
                               << "---" << CDMap[BB][i].edge
                               << "---" << CDMap[BB][i].To->getName()
                               << "\n";
                    }
                }*/

                // print the loop info
                /*for(auto headerBB : LoopHeader){
                    errs() << "$$$$$" << headerBB->getName() << "\n";
                    Loop* curLoop = LI.getLoopFor(headerBB);
                    for(auto loopBB : curLoop->getBlocksVector()){
                        errs() << "     ^^^" << loopBB->getName() << "\n";
                    }

                }*/

                errs() << "##### CD Rsult for function: " << demangle(F.getName().str().c_str()) << "\n";
                for(auto curSinkBBNode : FunctionData[demangle(F.getName().str().c_str())]){
                    errs() << "***** Sink Function: "
                           << curSinkBBNode.SinkFuncName << "---"
                           << (curSinkBBNode.SinkBB)->getName() << "\n";

                    std::stack<CDNode> WorkList;
                    std::unordered_map<BasicBlock*,  std::unordered_set<std::string>> Result;
                    for(auto curNode : CDMap[curSinkBBNode.SinkBB]){
                            WorkList.push(curNode);
                    }

                    while(!WorkList.empty()){
                        BasicBlock* ParentName = WorkList.top().From;
                        std::string EdgeType = WorkList.top().edge;
                        if(EdgeType != "Invoke taken" && EdgeType != "Invoke not taken" && EdgeType != "unknown"){
                            if(LoopHeader.find(ParentName) != LoopHeader.end()){
                                EdgeType = EdgeType + " (from loop)";
                            }
                            BasicBlock* prev = ParentName->getUniquePredecessor();
                            if(prev && prev->getTerminator()->getOpcode() == Instruction::Invoke){
                                if(LoopHeader.find(prev) != LoopHeader.end()){
                                    EdgeType = EdgeType + " (from loop)";
                                }
                            }
                            Result[ParentName].insert(EdgeType);
                            //errs() << "Found " << ParentName->getName() << " " << EdgeType;
                            //for (int ii = 0; ii < CDMap[ParentName].size(); ii++)
                            //     errs() << " " << CDMap[ParentName][ii].From->getName() << "--" << CDMap[ParentName][ii].edge << "--" << CDMap[ParentName][ii].To->getName();
                            //errs() << "\n";
                        }


                        WorkList.pop();
                        if(LoopHeader.find(ParentName) == LoopHeader.end()){
                            for(auto ParentNode : CDMap[ParentName]){
                                WorkList.push(ParentNode);
                            }
                        }
                        else{
                            for(auto ParentNode : CDMap[ParentName]){
                                BasicBlock* target = ParentNode.From;
                                if(!((LI.getLoopFor(ParentName))->contains(target)))
                                    WorkList.push(ParentNode);
                            }
                        }
                    }

                    std::map<std::string, std::list<std::string>> pred_list;
                    std::set<std::string> cd_sink_edge;
                    std::set<std::string> non_taken_loop;
                    // Assume taken from loop is control dependency, not-taken from loop is not control dependency
                    for(auto curResult : Result){
                        errs() << "^^^^^ " << curResult.first->getName() << "\n";
                        bool taken = false;
                        bool not_taken = false;
                        bool is_loop = false;
                        for(auto curEdge : curResult.second){
                            errs() << "     ^^^^^ " << curEdge << "\n";   
                            if (curEdge.find("(from loop)") != std::string::npos && curEdge.find("not taken") == std::string::npos)
                               cd_sink_edge.insert(std::string(curResult.first->getName())+","+curEdge.substr(0, curEdge.find(" (from loop)")));
                            else
                               cd_sink_edge.insert(std::string(curResult.first->getName())+","+curEdge);
                            not_taken |= (curEdge.find("not taken") == 0);
                            taken |= (curEdge.find("taken") == 0);
                            is_loop = (curEdge.find("(from loop)") != std::string::npos);
                        }
                        if (!is_loop || is_loop && taken)
                            extractBoolExpr(curResult.first, curResult.first, pred_list, Result, CDMap, LI); 
                        //if (is_loop && taken) {
                         //   for (std::list<std::string>::iterator it = pred_list[curResult.first].begin(); it++)
                        //    for (int ii = 0; ii < CDMap[curResult.first].size(); ii++) 
                        //         errs() << "Debug loop CDMap " << curResult.first->getName() << " " << CDMap[curResult.first][ii].From->getName() << " " << CDMap[curResult.first][ii].edge << "-" << CDMap[curResult.first][ii].To->getName() << "\n";
                        //}
                        if (taken && not_taken) 
                            continue;
                        if (is_loop && not_taken)
                            non_taken_loop.insert(curResult.first->getName());
                        errs() << "CD: " << F.getName().str() << ":" << (curSinkBBNode.SinkBB)->getName() << "," << F.getName().str() << "-" << curResult.first->getName() << '\n';
                    }
                    // Remove those in pred_list[bb] that bb is predecessor of
                    for (std::map<std::string, std::list<std::string>>::iterator it = pred_list.begin(); it != pred_list.end(); it++) {    
                         errs() << "Debug pred_list: ";
                         for (std::list<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++) {
                              //errs() << " " << *jt;
                              std::string bb = (*jt).substr(0, (*jt).find(","));
                              errs() << " Checked " << bb << "->" << it->first << " ";
                              // Exclude non-taken loop cond from boolean expressions
                              if (non_taken_loop.find(bb) != non_taken_loop.end()) {
                                  //if (cd_sink_edge.find(*jt) != cd_sink_edge.end())
                                  //    cd_sink_edge.erase(*jt);
                                  it->second.erase(jt);
                                  jt--;
                              }
                              // Check if bb -> it->first
                              else if (isPredCD(bb, it->first, pred_list)) {
                                  errs() << ",remove "; 
                                  it->second.erase(jt);
                                  jt--;
                              }
                         }
                         errs() << it->first << ":";
                         for (std::list<std::string>::iterator jt = it->second.begin(); jt != it->second.end(); jt++) {
                              if (cd_sink_edge.find(*jt) != cd_sink_edge.end())
                                  cd_sink_edge.erase(*jt); 
                              errs() << " " << *jt;
                         }
                         errs() << "\n";
                    }
                    for (std::set<std::string>::iterator it = cd_sink_edge.begin(); it != cd_sink_edge.end(); it++) {
                         // Double-check exclusion of non-taken loop cond from boolean expressions
                         //std::string bb = (*it).substr(0, (*it).find(","));
                         //if (non_taken_loop.find(bb) != non_taken_loop.end()) {
                         //    cd_sink_edge.erase(*it);
                         //    it--;
                         //}
                         errs() << "CD edge towards sink: " << *it << "\n";
                    }
                    //for (std::map<std::string, std::list<std::string>>::iterator it = pred_list.begin(); it != pred_list.end(); it++) {
                    //     if (cd_sink_edge.find(it->first+",taken") != cd_sink_edge.end()) 
                    //         printAllPred(it->first, pred_list, it->first + "==T");
                    //     else if (cd_sink_edge.find(it->first+",not taken") != cd_sink_edge.end()) 
                    //         printAllPred(it->first, pred_list, it->first + "==F"); 
                    //}        
                    for (auto curResult : Result) {
                         std::string bb = curResult.first->getName().str();
                         if (cd_sink_edge.find(bb+",taken") != cd_sink_edge.end()) 
                             printAllPred(bb, pred_list, bb + "==T");
                         else if (cd_sink_edge.find(bb+",not taken") != cd_sink_edge.end())
                             printAllPred(bb, pred_list, bb + "==F");
                    }
                }

                errs() << '\n';
                errs() << '\n';
            }

            return false;
        }

        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<PostDominatorTreeWrapperPass>();
            AU.addRequired<LoopInfoWrapperPass>();
            AU.setPreservesAll();
        }

    private:
        std::string demangle(const char* name);
        bool std_function(const char* name);
        EdgeType getEdgeType(const BasicBlock *A, const BasicBlock *B);
    };

}

char CD::ID = 0;
static RegisterPass<CD> X("cd", "control dependency analysis on given function");


std::string CD::demangle(const char* name){
        int status = -1; 

        std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
        return (status == 0) ? res.get() : std::string(name);
}

bool CD::std_function(const char* name){
  std::string func_name = demangle(name);
  if(func_name.find("std::") != std::string::npos ||
     func_name.find("cxx") != std::string::npos ||
     func_name.find("gnu") != std::string::npos ||
     func_name.find("__") != std::string::npos){
    return true;
  }
  return false;
}

EdgeType CD::getEdgeType(const BasicBlock *A, const BasicBlock *B) {
    if (const BranchInst *i = dyn_cast<BranchInst>(A->getTerminator())) {
        if (i->isConditional()) {
            if (i->getSuccessor(0) == B) {
                return EdgeType::TRUE;
            } else if (i->getSuccessor(1) == B) {
                return EdgeType::FALSE;
            } else {
                assert(false && "Asking for edge type between unconnected basic blocks!");
            }
        }
    }
    else if (const InvokeInst *i = dyn_cast<InvokeInst>(A->getTerminator())) {
        if (i->getNormalDest() == B) {
            return EdgeType::INVOKE_TRUE;
        } else if (i->getUnwindDest() == B) {
            return EdgeType::INVOKE_FALSE;
        } else {
            assert(false && "Asking for edge type between unconnected basic blocks!");
        }
    }
    return EdgeType::OTHER;
}

