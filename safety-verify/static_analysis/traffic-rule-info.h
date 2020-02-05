#ifndef __TRAFFIC_RULE_INFO_H__
#define __TRAFFIC_RULE_INFO_H__

#include "control-dependency.h"
#include "utils.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"

// Option 1: use KLEE to directly perform module level symbolic execution
// #include "klee/klee.h"

// Option 2: Use z3 as our theorem solver to perform a simple symbolic execution
#include "z3++.h"

#include <map>
#include <vector>

namespace llvm {

class VectorStatus {
  public:
    // vector analysis
    std::map<Value *, std::set<Value *>> vectorTaints;
    std::set<Value *> vectorSources;
    std::map<Value *, bool> vectorStatus;

    VectorStatus() {}
    VectorStatus(VectorStatus const &other) {
        vectorTaints = other.vectorTaints;
        vectorSources = other.vectorSources;
        vectorStatus = other.vectorStatus;
    }

    void setSources(std::set<Value *> vectors) {
        vectorSources = vectors;
        for (Value *vec : vectorSources) {
            vectorStatus[vec] = false;
            vectorTaints[vec] = std::set<Value *>();
            vectorTaints[vec].insert(dyn_cast<Instruction>(vec));
        }
    }
    
    void propagate(Instruction *I, Instruction *def) {
        for (auto it = vectorTaints.begin(); it != vectorTaints.end(); it++) {
            if (it->second.find(def) != it->second.end()) {
                if (isa<StoreInst>(I)) {
                    it->second.insert(I->getOperand(1));
                } else {
                    it->second.insert(I);
                }
            }
        }
    }

    Value *tainted(Instruction *I) {
        for (auto it = vectorTaints.begin(); it != vectorTaints.end(); it++) {
            if (it->second.find(I) != it->second.end()) {
                return it->first;
            }
        }
        return nullptr;
    }

    bool getStatus(Value *vec) {
        if (vectorStatus.find(vec) != vectorStatus.end()) {
            return vectorStatus[vec];
        } else {
            return true;
        }
    }

    void setStatus(Value *vec, bool status) {
        vectorStatus[vec] = status;
    }
};

class Path {
   public:
    static unsigned unnamedVarCnt;

    // vector analysis
    VectorStatus vectorStatus;
    // z3 expr has no default constructors...
    z3::expr *constraint;
    std::map<Instruction *, z3::expr *> vars;
    std::vector<MNode *> nodes;
    std::vector<BasicBlock *> blocks;
    Function *F;
    BasicBlock *next;

    Path(Function *F, z3::context &c) : F(F) {
        constraint = new z3::expr(c.bool_val(true));
        next = &(F->getEntryBlock());
    }

    Path(Path const &other) {
        vectorStatus = VectorStatus(other.vectorStatus);
        constraint = other.constraint;
        nodes = other.nodes;
        blocks = other.blocks;
        vars = other.vars;
        F = other.F;
        next = other.next;
    }

    static std::string getVarName(Value *V) {
        std::string name;
        Function *func = nullptr;
        if (isa<Instruction>(V)) {
            Instruction *I = dyn_cast<Instruction>(V);
            if (isa<CallBase>(I)) {
                if (I->getName().size() > 0 && I->getName().find("call") != 0) {
                    name = I->getName();
                } else {
                    CallBase *II = dyn_cast<CallBase>(I);
                    Function *func = getCalledFunction(II);
                    if (func == nullptr) {
                        name = I->getName();
                    } else {
                        name = beautyFuncName(func);
                    }
                }
            } else {
                name = I->getName();
            }
            // Function* func = I->getParent()->getParent();
        } else if (isa<Argument>(V)) {
            Argument *I = dyn_cast<Argument>(V);
            name = I->getName();
            // Function* func = I->getParent();
        }
        // std::string funcName = beautyFuncName(func);
        if (name.size() == 0) {
            name = std::string("var_") + std::to_string(unnamedVarCnt);
        } else {
            name = name + "_" + std::to_string(unnamedVarCnt);
        }
        unnamedVarCnt++;
        return name;
    }

    static void resetVarCnt() {
        unnamedVarCnt = 0;
    }
};

class TrafficRuleInfo : public ModulePass {
   public:
    static char ID;

    // Function => vector push_back
    std::map<Function *, std::set<Instruction *>> vectorDeps;
    // instr => func
    std::map<Instruction *, Function *> interCalls;
    // func => path[]
    std::map<Function *, std::vector<Path>> funcPaths;
    // func => return expr,
    std::map<Function *, z3::expr *> returnExprs;
    // callee => caller => constraint
    std::map<Function *, std::map<Function *, z3::expr *>> funcConstraints;
    // global var map
    std::map<Value *, z3::expr *> globalVars;
    // final result
    z3::expr *result;

    // depracated!

    // // func => return expr => constraint,
    // std::map<Function *, std::map<z3::expr *, z3::expr *>> returnMultiExprs;
    // // global constraints among z3 expressions
    // std::vector<z3::expr *> globalConstraints;

    std::set<BasicBlock *> extendedBlocks;

    TrafficRuleInfo() : ModulePass(ID) {}

    bool runOnFunction(Function &F, ControlDependency &CD, z3::context &c);

    bool runOnModule(Module &M);

    virtual bool doInitialization(Module &M);

    virtual bool doFinalization(Module &M);

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        // add dependencies here
        AU.addRequired<ControlDependency>();
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<DominatorTreeWrapperPass>();
        // AU.setPreservesAll();
    }

   private:
    void extractConstraint(ControlDependency &CD, z3::context &c);

    void extendPaths(Function *F, BasicBlock *BB, std::set<EdgeType> edges, MNode *N, LoopInfo *LI, DominatorTree *DT, ControlDependency *CD, z3::context &c);
    void cleanPaths(Function *F, ControlDependency *CD, z3::context &c);
    void printPaths(Function *F);

    void executeBlock(Path *P, MNode *N, z3::context &c);
    void executeBranch(Path *P, MNode *N, BasicBlock *next, z3::context &c);
    void executeInstruction(Path *P, MNode *N, Instruction *I, z3::context &c);

    z3::expr *newZ3Const(Constant *C, z3::context &c);
    z3::expr *newZ3DefaultConst(Type *T, z3::context &c);
    z3::expr *newZ3Var(Value *V, z3::context &c);
    z3::expr *getZ3Expr(Path *P, Instruction *def);

    void initRetExprs(ControlDependency &CD, z3::context &c);

    std::string valueToStr(const Value *value);
    std::string getValDefVar(const Value *def);
    std::string getPredicateName(CmpInst::Predicate Pred);
    std::string getOpType(Value *I, std::string operand);
    Instruction *getUniqueDefinition(Path *P, MNode *N, Instruction *I, Value *V);
};

}  // namespace llvm

#endif  // __TRAFFIC_RULE_INFO_H__