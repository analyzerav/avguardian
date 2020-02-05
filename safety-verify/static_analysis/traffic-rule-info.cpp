#include "llvm/ADT/Statistic.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include <fstream>
#include <list>
#include <ostream>
#include <set>
#include <sstream>

#include <cxxabi.h>
#include <sys/stat.h>

#include "traffic-rule-info.h"

namespace llvm {

char TrafficRuleInfo::ID = 0;
static RegisterPass<TrafficRuleInfo> X("traffic-rule-info", "TrafficRuleInfo", false, true);
unsigned Path::unnamedVarCnt = 0;

bool TrafficRuleInfo::runOnFunction(Function &F, ControlDependency &CD, z3::context &c) {
    if (F.isDeclaration())
        return false;

    if (CD.MCFG.find(&F) == CD.MCFG.end())
        return false;

    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();

    errs() << "Extracting paths in Function " << demangle(F.getName().str().c_str()) << "\n";

    Path::resetVarCnt();
    funcPaths[&F] = std::vector<Path>();
    Path initPath = Path(&F, c);
    // init vector status
    std::set<Value *> vectors;
    for (Instruction *I : CD.VectorDeps[&F]) {
        if (isa<CallBase>(I)) {
            CallBase *II = dyn_cast<CallBase>(I);
            vectors.insert(II->getArgOperandUse(0));
        }
    }
    initPath.vectorStatus.setSources(vectors);
    // set init path
    funcPaths[&F].push_back(initPath);

    extendedBlocks.clear();

    // init parameters
    for (auto arg = F.arg_begin(); arg != F.arg_end(); arg++) {
        newZ3Var(arg, c);
    }
    for (BasicBlock &BB : F) {
        MNode *mnode = CD.getMNode(&F, &BB);
        if (!mnode) {
            extendPaths(&F, &BB, std::set<EdgeType>(), nullptr, &LI, &DT, &CD, c);
        } else {
            extendPaths(&F, &BB, mnode->edges, mnode, &LI, &DT, &CD, c);
        }
    }
    cleanPaths(&F, &CD, c);

#ifdef DEBUG
    errs() << "Print paths of Function " << demangle(F.getName().str().c_str()) << "\n";
    printPaths(&F);
#endif

    return false;
}

bool TrafficRuleInfo::runOnModule(Module &M) {
    // init
    z3::context c;
    ControlDependency &CD = getAnalysis<ControlDependency>();
    interCalls = CD.InterCalls;
    vectorDeps = CD.VectorDeps;
    initRetExprs(CD, c);

    std::set<Function *> interSet;
    for (auto it = CD.InterCalls.begin(); it != CD.InterCalls.end(); it++) {
        if (interSet.find(&*(it->second)) == interSet.end()) {
            runOnFunction(*(it->second), CD, c);
            interSet.insert(&*(it->second));
        }
    }
    for (Function *F : CD.TargetFuncPtrs) {
        if (interSet.find(F) == interSet.end()) {
            runOnFunction(*F, CD, c);
        }
    }
    extractConstraint(CD, c);

    // print results
    errs() << "Final result:\n";
    errs() << result->simplify().to_string() << "\n";

    return false;
}

bool TrafficRuleInfo::doInitialization(Module &M) {
    return false;
}

bool TrafficRuleInfo::doFinalization(Module &M) {
    return false;
}

void TrafficRuleInfo::initRetExprs(ControlDependency &CD, z3::context &c) {
    for (auto it = interCalls.begin(); it != interCalls.end(); it++) {
        if (returnExprs.find(it->second) == returnExprs.end()) {
            z3::expr *e = newZ3DefaultConst(it->second->getReturnType(), c);
            if (e != nullptr) {
                returnExprs[it->second] = e;
            }
        }
    }
}

void TrafficRuleInfo::extractConstraint(ControlDependency &CD, z3::context &c) {
    result = new z3::expr(c.bool_val(false));
    for (auto chain : CD.CallChains) {
        z3::expr *tmpConstraint = new z3::expr(c.bool_val(true));
        Function *prev = nullptr;
        for (Function *F : chain) {
            if (prev != nullptr) {
                if (funcConstraints[prev][F] != nullptr) {
                    if (funcConstraints[prev][F]->is_arith()) {
                        funcConstraints[prev][F] = new z3::expr(*funcConstraints[prev][F] > 0);
                    }
                    if (funcConstraints[prev][F]->is_bool()) {
                        tmpConstraint = new z3::expr((*tmpConstraint && *(funcConstraints[prev][F])).simplify());
                    }
                }
            }
            prev = F;
        }
        result = new z3::expr(*result || *tmpConstraint);
    }
    // for (auto c : globalConstraints) {
    //     result = new z3::expr(*result || *c);
    // }
    result = new z3::expr(result->simplify());
}

void TrafficRuleInfo::extendPaths(Function *F, BasicBlock *BB, std::set<EdgeType> edges, MNode *N, LoopInfo *LI, DominatorTree *DT, ControlDependency *CD, z3::context &c) {
#ifdef DEBUG
    errs() << "BB " << BB->getName() << " processing\n";
#endif
    std::vector<Path> newPaths;
    std::map<BasicBlock *, std::set<BasicBlock *>> finalNextCache;
    auto pit = funcPaths[F].begin();
    while (pit != funcPaths[F].end()) {
        if (pit->next != BB) {
            pit++;
            continue;
        }

        // errs() << "enter: " << BB->getName() << "\n";
        // If it is a critical block, append it to paths
        if (N) {
            // errs() << "enter critical\n";
            executeBlock(&*pit, N, c);
        }
        pit->blocks.push_back(BB);

        // if already reach sink, stop extending
        SinkBBNode *snode = CD->getSinkBBNode(F, BB);
        if (snode) {
            pit++;
            continue;
        }

        // fork new paths if necessary
        std::set<BasicBlock *> nexts = std::set<BasicBlock *>();
        std::set<BasicBlock *> finalNexts = std::set<BasicBlock *>();

        if (finalNextCache.find(BB) != finalNextCache.end()) {
            finalNexts = finalNextCache[BB];
        } else {
            for (EdgeType E : edges) {
                switch (E) {
                    case EdgeType::TRUE:
                    case EdgeType::INVOKE_TRUE: {
                        nexts.insert(BB->getTerminator()->getSuccessor(0));
                        break;
                    }
                    case EdgeType::FALSE:
                    case EdgeType::INVOKE_FALSE: {
                        nexts.insert(BB->getTerminator()->getSuccessor(1));
                        break;
                    }
                    case EdgeType::UNKNOWN:
                    default:
                        break;
                }
            }

            if (nexts.size() == 0) {
                int succNum = BB->getTerminator()->getNumSuccessors();
                if (N) {
                    if (isa<CallBase>(N->BB->getTerminator())) {
                        nexts.insert(BB->getTerminator()->getSuccessor(0));
                    } else {
                        for (int i = 0; i < succNum; i++) {
                            nexts.insert(BB->getTerminator()->getSuccessor(i));
                        }
                    }
                } else if (succNum > 0) {
                    nexts.insert(BB->getTerminator()->getSuccessor(0));
                }
            }
            for (BasicBlock *next : nexts) {
                if (next == nullptr) continue;
                if (LI->isLoopHeader(next) && extendedBlocks.find(next) != extendedBlocks.end()) {
                    SmallVector<BasicBlock *, 2> exits;
                    LI->getLoopFor(next)->getExitBlocks(exits);
                    for (BasicBlock *e : exits) {
                        BasicBlock *exit = e;
                        // errs() << "exit: " << exit->getName() << "\n";
                        while (exit != nullptr && extendedBlocks.find(exit) != extendedBlocks.end()) {
                            // errs() << "nexit: " << exit->getName() << "\n";
                            if (exit->getSingleSuccessor() == nullptr) {
                                exit = nullptr;
                                break;
                            }
                            exit = exit->getTerminator()->getSuccessor(0);
                        }
                        if (exit != nullptr) {
                            // errs() << "fexit: " << exit->getName() << "\n";
                        }
                        if (exit != nullptr && exit->getName().find("end") != std::string::npos) {
                            finalNexts.insert(exit);
                        }
                    }
                } else if (extendedBlocks.find(next) == extendedBlocks.end()) {
                    finalNexts.insert(next);
                }
            }
        }

        for (BasicBlock *next : finalNexts) {
            Path newPath = Path(*pit);
            newPath.next = next;
            // errs() << "next: " << next->getName() << "\n";
            if (N) {
                executeBranch(&newPath, N, next, c);
            }
            newPaths.push_back(newPath);
        }

        if (newPaths.size() > 0) {
            pit = funcPaths[F].erase(pit);
        } else {
            pit++;
        }

        if (finalNextCache.find(BB) == finalNextCache.end()) {
            finalNextCache[BB] = finalNexts;
        }
    }

    for (Path P : newPaths) {
        funcPaths[F].push_back(P);
    }

    // remove impossible paths
    pit = funcPaths[F].begin();
    while (pit != funcPaths[F].end()) {
        if (pit->constraint->simplify().to_string() == "false") {
            pit = funcPaths[F].erase(pit);
        } else {
            pit++;
        }
    }

    extendedBlocks.insert(BB);
    // errs() << "num of paths: " << funcPaths[F].size() << "\n";
}

void TrafficRuleInfo::cleanPaths(Function *F, ControlDependency *CD, z3::context &c) {
    auto pit = funcPaths[F].begin();
    while (pit != funcPaths[F].end()) {
        if (pit->nodes.size() > 0) {
            BasicBlock *rear = pit->nodes.back()->BB;
            SinkBBNode *snode = CD->getSinkBBNode(F, rear);
            if (snode) {
                pit++;
            } else {
                pit = funcPaths[F].erase(pit);
            }
        } else {
            pit = funcPaths[F].erase(pit);
        }
    }
    std::map<Function *, z3::expr *> tmpConstraints;
    for (Path path : funcPaths[F]) {
        BasicBlock *sinkBB = path.nodes.back()->BB;
        SinkBBNode *snode = CD->getSinkBBNode(F, sinkBB);
        if (!snode) continue;
        Function *callee = snode->to;
        if (tmpConstraints.find(callee) == tmpConstraints.end()) {
            tmpConstraints[callee] = new z3::expr(*(path.constraint));
        } else {
            tmpConstraints[callee] = new z3::expr((*(tmpConstraints[callee]) || *(path.constraint)).simplify());
        }
    }
    for (auto it = tmpConstraints.begin(); it != tmpConstraints.end(); it++) {
        funcConstraints[it->first][F] = new z3::expr(it->second->simplify());
    }
}

void TrafficRuleInfo::printPaths(Function *F) {
    for (Path P : funcPaths[F]) {
        for (MNode *N : P.nodes) {
            errs() << N->BB->getName() << " ";
        }
        errs() << "\n";
        errs() << P.constraint->simplify().to_string() << "\n";
    }
}

void TrafficRuleInfo::executeBlock(Path *P, MNode *N, z3::context &c) {
    P->nodes.push_back(N);
    // execute path slides
    for (Instruction &I : *(N->BB)) {
        if (N->instrs.find(&I) != N->instrs.end()) {
            executeInstruction(P, N, &I, c);
        }
    }
}

void TrafficRuleInfo::executeBranch(Path *P, MNode *N, BasicBlock *next, z3::context &c) {
    Instruction *I = N->BB->getTerminator();

    z3::expr *latest = P->constraint;
    switch (I->getOpcode()) {
        case Instruction::Br: {
            BranchInst *II = dyn_cast<BranchInst>(I);
            z3::expr *condE = nullptr;
            if (II->isConditional()) {
                Value *cond = II->getCondition();
                Instruction *def = getUniqueDefinition(P, N, I, cond);
                condE = getZ3Expr(P, def);
            } else {
                condE = new z3::expr(c.bool_val(true));
            }
            // sanity check
            if (condE == nullptr) {
                errs() << "error BR " << *I << "\n";
                return;
            }
            if (condE->is_arith()) {
                condE = new z3::expr(*condE > 0);
            }
#ifdef DEBUG
            errs() << "  BRANCH " << *I << " " << condE->to_string() << "\n";
#endif
            int succNum = I->getNumSuccessors();
            if (succNum == 2) {
                if (next == I->getSuccessor(0)) {
                    // errs() << "cond " << next->getName() << " " << condE->to_string() << "\n";
                    P->constraint = new z3::expr((*latest && *condE).simplify());
                } else if (next == I->getSuccessor(1)) {
                    // errs() << "cond " << next->getName() << " not" << condE->to_string() << "\n";
                    P->constraint = new z3::expr((*latest && !(*condE)).simplify());
                }
            }
            break;
        }
        case Instruction::Call:
        case Instruction::Invoke: {
            CallBase *II = dyn_cast<CallBase>(I);
            for (auto it = II->arg_begin(); it != II->arg_end(); it++) {
                Value *op = dyn_cast<Value>(*it);
                Instruction *def = getUniqueDefinition(P, N, I, op);
                P->vectorStatus.propagate(I, def);
            }
#ifdef DEBUG
            errs() << "  INVOKE/CALL BR " << *I << "\n";
#endif
            break;
        }
    }

    // errs() << "CONDITION: " << P->constraint->to_string() << "\n";
}

void TrafficRuleInfo::executeInstruction(Path *P, MNode *N, Instruction *I, z3::context &c) {
    // fetch all operands
    std::vector<z3::expr *> ops;
    for (auto it = I->op_begin(); it != I->op_end(); it++) {
        Value *op = dyn_cast<Value>(*it);
        Type *T = op->getType();
        // sanity checks
        z3::expr *E = nullptr;
        if (isa<Constant>(op)) {
            Constant *C = dyn_cast<Constant>(op);
            E = newZ3Const(C, c);
        } else if (isa<Instruction>(op)) {
            Instruction *def = getUniqueDefinition(P, N, I, op);
            // vector
            P->vectorStatus.propagate(I, def);
            E = getZ3Expr(P, def);
        } else if (isa<Argument>(op)) {
            if (globalVars.find(op) != globalVars.end()) {
                E = globalVars[op];
            }
        }
        if (E == nullptr) {
            if (isa<Instruction>(op)) {
                E = newZ3Var(dyn_cast<Instruction>(op), c);
            }
        }
        ops.push_back(E);
    }

    // TODO: symbolically execute an instruction
    switch (I->getOpcode()) {
        // TODO: phi node
        case Instruction::PHI: {
            PHINode *II = dyn_cast<PHINode>(I);
            for (int i = 0; i < ops.size(); i++) {
                if (P->blocks.size() > 0 && II->getIncomingBlock(i) == P->blocks.back()) {
                    if (ops[i] == nullptr) {
                        errs() << "error PHI " << *I << "\n";
                        return;
                    }
                    P->vars[I] = ops[i];
                    break;
                }
            }
            if (P->vars.find(I) == P->vars.end()) {
                errs() << "error PHI " << *I << "\n";
                return;
            } else {
#ifdef DEBUG
                errs() << "  PHI " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            }
            break;
        }

        case Instruction::Alloca: {
            AllocaInst *II = dyn_cast<AllocaInst>(I);
            z3::expr *e = newZ3Var(I, c);
            if (e == nullptr) {
                errs() << "error ALLOCA " << *I << "\n";
                return;
            };
            P->vars[I] = e;

#ifdef DEBUG
            errs() << "  ALLOCA " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Call:
        case Instruction::Invoke: {
            CallBase *II = dyn_cast<CallBase>(I);
            Function *calledFunc = getCalledFunction(II);
            if (calledFunc == nullptr) {
                errs() << "error INVOKE/CALL " << *I << "\n";
                return;
            }

            auto vec = P->vectorStatus.tainted(I);

            std::string calledFuncName = demangle(calledFunc->getName().str().c_str());
            if (vec != nullptr) {
                if (calledFuncName.find("empty") != std::string::npos) {
                    P->vars[I] = new z3::expr(c.bool_val(!P->vectorStatus.getStatus(vec)));
                } else if (calledFuncName.find("operator!=") != std::string::npos) {
                    P->vars[I] = new z3::expr(c.bool_val(P->vectorStatus.getStatus(vec)));
                } else if (calledFuncName.find("push_back") != std::string::npos) {
                    P->vectorStatus.setStatus(vec, true);
                } else if (calledFuncName.find("emplace_back") != std::string::npos) {
                    P->vectorStatus.setStatus(vec, true);
                }
            } else {
                if (calledFuncName.find("empty") != std::string::npos) {
                    P->vars[I] = new z3::expr(c.bool_val(false));
                } else if (calledFuncName.find("operator!=") != std::string::npos) {
                    P->vars[I] = new z3::expr(c.bool_val(true));
                }
            }

            if (P->vars.find(I) == P->vars.end()) {
                if (std_function(calledFunc->getName().str().c_str())) {
                    P->vars[I] = new z3::expr(c.bool_val(true));
                } else if (returnExprs.find(calledFunc) != returnExprs.end()) {
                    P->vars[I] = returnExprs[calledFunc];
                    unsigned cnt = 0;
                    for (Argument *arg = calledFunc->arg_begin(); arg != calledFunc->arg_end(); arg++) {
                        if (globalVars.find(arg) != globalVars.end() && ops.size() > cnt && ops[cnt] != nullptr) {
                            z3::expr *c = new z3::expr(*(globalVars[arg]) == *(ops[cnt]));
                            // if (c->is_bool()) globalConstraints.push_back(c);
                            // errs() << "aaa " << globalVars[arg]->to_string() << " bbb " << ops[cnt]->to_string() << "\n"; 
                        }
                        cnt++;
                    }
                    
                } else {
                    z3::expr *e = newZ3Var(I, c);
                    if (e == nullptr) {
                        errs() << "error INVOKE/CALL " << *I << "\n";
                        return;
                    }
                    P->vars[I] = e;
                }
            }
#ifdef DEBUG
            errs() << "  INVOKE/CALL " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Load: {
            // LoadInst *II = dyn_cast<LoadInst>(I);
            z3::expr *e = ops[0];
            if (e == nullptr) {
                errs() << "error LOAD " << *I << "\n";
                return;
            }
            P->vars[I] = e;
#ifdef DEBUG
            errs() << "  LOAD " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Store: {
            z3::expr *e = ops[0];
            Value *to = I->getOperand(1);
            if (e == nullptr) {
                errs() << "error STORE " << *I << "\n";
                return;
            }
            if (isa<Instruction>(to)) {
                P->vars[dyn_cast<Instruction>(to)] = e;
            }
            P->vars[I] = e;
#ifdef DEBUG
            errs() << "  STORE " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Add:
        case Instruction::FAdd: {
            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_bool()) {
                leftE = new z3::expr(z3::ite(*leftE, c.int_val(1), c.int_val(0)));
            }
            if (rightE->is_bool()) {
                rightE = new z3::expr(z3::ite(*rightE, c.int_val(1), c.int_val(0)));
            }

            // sanity checks
            if (leftE == nullptr || rightE == nullptr) {
                errs() << "error ADD " << *I << "\n";
                return;
            }

            P->vars[I] = new z3::expr(*leftE + *rightE);
#ifdef DEBUG
            errs() << "  ADD " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Sub:
        case Instruction::FSub: {
            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_bool()) {
                leftE = new z3::expr(z3::ite(*leftE, c.int_val(1), c.int_val(0)));
            }
            if (rightE->is_bool()) {
                rightE = new z3::expr(z3::ite(*rightE, c.int_val(1), c.int_val(0)));
            }

            // sanity checks
            if (leftE == nullptr || rightE == nullptr) {
                errs() << "error SUB " << *I << "\n";
                return;
            }

            P->vars[I] = new z3::expr(*leftE - *rightE);
#ifdef DEBUG
            errs() << "  SUB " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::Mul:
        case Instruction::FMul: {
            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_bool()) {
                leftE = new z3::expr(z3::ite(*leftE, c.int_val(1), c.int_val(0)));
            }
            if (rightE->is_bool()) {
                rightE = new z3::expr(z3::ite(*rightE, c.int_val(1), c.int_val(0)));
            }

            // sanity checks
            if (leftE == nullptr || rightE == nullptr) {
                errs() << "error MUL " << *I << "\n";
                return;
            }

            P->vars[I] = new z3::expr(*leftE * *rightE);
#ifdef DEBUG
            errs() << "  MUL " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::UDiv:
        case Instruction::SDiv:
        case Instruction::FDiv: {
            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_bool()) {
                leftE = new z3::expr(z3::ite(*leftE, c.int_val(1), c.int_val(0)));
            }
            if (rightE->is_bool()) {
                rightE = new z3::expr(z3::ite(*rightE, c.int_val(1), c.int_val(0)));
            }

            // sanity checks
            if (leftE == nullptr || rightE == nullptr) {
                errs() << "error DIV " << *I << "\n";
                return;
            }

            P->vars[I] = new z3::expr(*leftE / *rightE);
#ifdef DEBUG
            errs() << "  DIV " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::ICmp:
        case Instruction::FCmp: {
            CmpInst *II = dyn_cast<CmpInst>(I);

            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_bool()) {
                leftE = new z3::expr(z3::ite(*leftE, c.int_val(1), c.int_val(0)));
            }
            if (rightE->is_bool()) {
                rightE = new z3::expr(z3::ite(*rightE, c.int_val(1), c.int_val(0)));
            }

            // sanity checks
            if (leftE == nullptr || rightE == nullptr) {
                errs() << "error CMP " << *I << "\n";
                return;
            }

            // switch predicates
            switch (II->getPredicate()) {
                case FCmpInst::FCMP_FALSE: {
                    P->vars[I] = new z3::expr(c.int_val(0));
                    break;
                }
                case FCmpInst::FCMP_TRUE: {
                    P->vars[I] = new z3::expr(c.int_val(1));
                    break;
                }
                case FCmpInst::FCMP_OEQ:
                case ICmpInst::ICMP_EQ:
                case FCmpInst::FCMP_UEQ: {
                    P->vars[I] = new z3::expr(*leftE == *rightE);
                    break;
                }
                case FCmpInst::FCMP_OGT:
                case ICmpInst::ICMP_SGT:
                case FCmpInst::FCMP_UGT:
                case ICmpInst::ICMP_UGT: {
                    P->vars[I] = new z3::expr(*leftE > *rightE);
                    break;
                }
                case FCmpInst::FCMP_OGE:
                case ICmpInst::ICMP_SGE:
                case FCmpInst::FCMP_UGE:
                case ICmpInst::ICMP_UGE: {
                    P->vars[I] = new z3::expr(*leftE >= *rightE);
                    break;
                }
                case FCmpInst::FCMP_OLT:
                case ICmpInst::ICMP_SLT:
                case FCmpInst::FCMP_ULT:
                case ICmpInst::ICMP_ULT: {
                    P->vars[I] = new z3::expr(*leftE < *rightE);
                    break;
                }
                case FCmpInst::FCMP_OLE:
                case ICmpInst::ICMP_SLE:
                case FCmpInst::FCMP_ULE:
                case ICmpInst::ICMP_ULE: {
                    P->vars[I] = new z3::expr(*leftE <= *rightE);
                    break;
                }
                case FCmpInst::FCMP_ONE:
                case FCmpInst::FCMP_UNE:
                case ICmpInst::ICMP_NE: {
                    P->vars[I] = new z3::expr(!(*leftE == *rightE));
                    break;
                }
                // case FCmpInst::FCMP_ORD:
                // case FCmpInst::FCMP_UNO:
                default:
                    break;
            }
#ifdef DEBUG
            if (P->vars.find(I) != P->vars.end()) {
                errs() << "  CMP " << *I << " " << P->vars[I]->to_string() << "\n";
            }
#endif
            break;
        }
        case Instruction::Ret: {
            // ReturnInst *II = dyn_cast<ReturnInst>(I);
            if (ops.size() == 0) return;

            z3::expr *e = ops[0];
            if (e == nullptr) {
                errs() << "error RET " << *I << "\n";
                return;
            }
            P->vars[I] = new z3::expr(*e);
#ifdef DEBUG
            errs() << "  RET " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            if (returnExprs.find(P->F) != returnExprs.end()) {
                // TODO: type match
                if (returnExprs[P->F]->is_bool() && P->vars[I]->is_arith()) {
                    P->vars[I] = new z3::expr(*(P->vars[I]) > 0);
                } else if (returnExprs[P->F]->is_arith() && P->vars[I]->is_bool()) {
                    P->vars[I] = new z3::expr(z3::ite(*P->vars[I], c.real_val(1), c.real_val(0)));
                }
                returnExprs[P->F] = new z3::expr(z3::ite(*(P->constraint), *(P->vars[I]), *(returnExprs[P->F])));
            }
            break;
        }
        case Instruction::Trunc: {
            z3::expr *e = ops[0];
            if (e == nullptr) {
                errs() << "error TRUNC " << *I << "\n";
                return;
            }
            P->vars[I] = e;
            // Type *T = I->getType();
            // if (e->is_arith() && T->isIntegerTy(1)) {
            //     P->vars[I] = new z3::expr(*e > 0);
            // } else {
            //     P->vars[I] = e;
            // }
#ifdef DEBUG
            errs() << "  TRUNC " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            break;
        }
        case Instruction::ZExt: {
            z3::expr *e = ops[0];
            if (e == nullptr) {
                errs() << "error ZEXT " << *I << "\n";
                return;
            }
            P->vars[I] = e;
            Type *T = I->getType();
            // if (e->is_bool() && T->isIntegerTy() && T->getIntegerBitWidth() > 1) {
            //     P->vars[I] = new z3::expr(z3::ite(*e, c.int_val(1), c.int_val(0)));
            // } else {
            //     P->vars[I] = e;
            // }
#ifdef DEBUG
            errs() << "  ZEXT " << *I << " " << P->vars[I]->to_string() << "\n";
#endif
            return;
        }
        case Instruction::Xor: {
            z3::expr *leftE = ops[0];
            z3::expr *rightE = ops[1];

            if (leftE->is_arith()) {
                leftE = new z3::expr(*leftE > 0);
            }
            if (rightE->is_arith()) {
                rightE = new z3::expr(*rightE > 0);
            }
            P->vars[I] = new z3::expr(*leftE != *rightE);
            break;
        }
        default:
            break;
    }
}

z3::expr *TrafficRuleInfo::newZ3Const(Constant *C, z3::context &c) {
    z3::expr *e = nullptr;
    Type *T = C->getType();
    switch (T->getTypeID()) {
        case Type::IntegerTyID: {
            ConstantInt *val = dyn_cast<ConstantInt>(C);
            if (T->isIntegerTy(1)) {
                e = new z3::expr(c.bool_val(val->getSExtValue() > 0 ? true : false));
            } else {
                e = new z3::expr(c.int_val(val->getSExtValue()));
            }
            break;
        }
        case Type::DoubleTyID:
        case Type::FloatTyID:
        case Type::HalfTyID: {
            ConstantFP *val = dyn_cast<ConstantFP>(C);
            e = new z3::expr(c.real_val(int(val->getValueAPF().convertToDouble())));
            break;
        }
        default: {
            e = new z3::expr(c.real_val(0));
            break;
        }
    }
    return e;
}

z3::expr *TrafficRuleInfo::newZ3DefaultConst(Type *T, z3::context &c) {
    z3::expr *e = nullptr;
    switch (T->getTypeID()) {
        case Type::IntegerTyID: {
            if (T->isIntegerTy(1)) {
                e = new z3::expr(c.bool_val(false));
            } else {
                e = new z3::expr(c.int_val(0));
            }
            break;
        }
        case Type::DoubleTyID:
        case Type::FloatTyID:
        case Type::HalfTyID:
        default: {
            e = new z3::expr(c.real_val(0));
            break;
        }
    }
    return e;
}

z3::expr *TrafficRuleInfo::newZ3Var(Value *I, z3::context &c) {
    if (globalVars.find(I) != globalVars.end()) {
        return globalVars[I];
    }
    Type *T = I->getType();
    std::string name = Path::getVarName(I);
    z3::expr *e = nullptr;
    switch (T->getTypeID()) {
        case Type::IntegerTyID: {
            if (T->isIntegerTy(1)) {
                e = new z3::expr(c.bool_const(name.c_str()));
            } else {
                e = new z3::expr(c.int_const(name.c_str()));
            }
            break;
        }
        case Type::DoubleTyID:
        case Type::FloatTyID:
        case Type::HalfTyID: {
            e = new z3::expr(c.real_const(name.c_str()));
            break;
        }
        default: {
            e = new z3::expr(c.real_const(name.c_str()));
            break;
        }
    }
    globalVars[I] = e;
    return e;
}

Instruction *TrafficRuleInfo::getUniqueDefinition(Path *P, MNode *N, Instruction *I, Value *V) {
    // sanity checks
    if (V == nullptr) {
        return nullptr;
    }
    Instruction *def = nullptr;
    auto VP = std::make_pair(I, V);
    if (N->UDs.find(VP) != N->UDs.end()) {
        auto defs = N->UDs[VP];
        bool found = false;
        for (auto nit = P->nodes.rbegin(); nit != P->nodes.rend(); nit++) {
            MNode *M = (*nit);
            BasicBlock *BB = M->BB;
            for (auto d : defs) {
                if (d->getParent() == BB) {
                    def = d;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    if (!def && isa<Instruction>(V)) {
        def = dyn_cast<Instruction>(V);
    }

    return def;
}

z3::expr *TrafficRuleInfo::getZ3Expr(Path *P, Instruction *def) {
    if (!def) {
        return nullptr;
    }

    if (P->vars.find(def) == P->vars.end()) {
        return nullptr;
    }
    z3::expr *expr = P->vars[def];
    return expr;
}

}  // namespace llvm
