#ifndef __UTILS_H__
#define __UTILS_H__

#include <string>
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

std::string demangle(const char *name);

std::string get_func_name(const char *name);

bool isStdFunction(const char *name);

Value* valueToDefVar(Value* v);

Function *getCalledFunction(CallBase *call);

std::string getTypeName(Value *V);

}  // namespace llvm

#endif  
