#include <string>
#include <iostream>
#include <fstream>
#include <stdlib.h> 
#include <map>
#include <vector>
#include <list>

// Target functions
std::list<std::string> targetFunc;
// Functions to be analyzed for target functions
std::map<std::string, int> func_name;
// Bitcode files where a function is defined
std::map<std::string, std::string> func_def;
// Order of functions that are called
std::map<std::string, int> func_order;
// Function caller & callee info
std::map< std::string, std::list<std::string> > func_call;

void setFuncCall(std::list<std::string>& l) {
    for (std::list<std::string>::iterator it = l.begin(); it != l.end(); ++it) {
        if (func_name.find(*it) != func_name.end())
            continue;
        func_name[*it] = (func_order.find(*it) == func_order.end()? -1 : func_order[*it]);
        if (func_name[*it] < 0)
            continue;
        func_name[*it] = (func_order.find(*it) == func_order.end()? -1 : func_order[*it]);
        if (func_call.find(*it)!= func_call.end())
            setFuncCall(func_call[*it]);
    }
}

// Recursively extract all called functions from a target function
void findTargetFunc() {
    for (std::list<std::string>::iterator it = targetFunc.begin(); it != targetFunc.end(); ++it) {
        if (func_name.find(*it) != func_name.end())
            continue;
        func_name[*it] = (func_order.find(*it) == func_order.end()? -1 : func_order[*it]);
        if (func_name[*it] < 0)
            continue;
        if (func_call.find(*it) != func_call.end())
            setFuncCall(func_call[*it]);
    }
    for (std::map<std::string, int>::iterator it = func_name.begin(); it != func_name.end(); ++it) {
        std::cout << it->first << " order " << it->second;
        if (it->second != -1)
            std::cout << " file " << func_def[it->first] << "\n";
        else if (func_def.find(it->first) == func_def.end())
            std::cout << " extern " << "\n";
        else
            std::cout << " recursive " << "\n";
    }
}

int loadTargetFunc(const char* file_name) {
    std::ifstream myfile(file_name);
    std::string line;
    if (myfile.is_open()) {
       while (std::getline(myfile, line))
          targetFunc.push_back(line);
       myfile.close();
       findTargetFunc();
       return 0;
    }
    return -1;
}

int loadFuncDef(const char* file_name) {
    std::ifstream myfile(file_name);
    std::string line, func, loc;
    if (myfile.is_open()) {
       while (std::getline(myfile, line)) {
          func = line.substr(0, line.find(" "));
          loc = line.substr(line.find(" ")+1);
          func_def[func] = loc;
       }
       myfile.close();
       return 0;
    }
    return -1;
}

int loadFuncCall(const char* file_name) {
    std::ifstream myfile(file_name);
    std::string line, caller, callee;
    if (myfile.is_open()) {
       while (std::getline(myfile, line)) {
          caller = line.substr(0, line.find(" "));
          callee = line.substr(line.find(" ")+1);
          if (func_call.find(caller) == func_call.end())
             func_call[caller] = std::list<std::string>();
          func_call[caller].push_back(callee);
       }
       myfile.close();
       return 0;
    }
    return -1;
}

int loadFuncOrder(const char* file_name) {
    std::ifstream myfile(file_name);
    std::string line, order, func;
    if (myfile.is_open()) {
       while (std::getline(myfile, line)) {
          order = line.substr(0, line.find(","));
          func = line.substr(line.find(",")+1);
          func_order[func] = atoi(order.c_str());
       }
       myfile.close();
       return 0;
    }
    return -1;
}

bool computeFuncDep() {
    if (func_def.size() == 0)
        if (loadFuncDef("funcdef.meta") < 0)
            return false;
    if (func_call.size() == 0)
        if (loadFuncCall("funccall.meta") < 0)
            return false;
    if (func_order.size() == 0)
        if (loadFuncOrder("funcorder.meta") < 0)
            return false;
    if (func_name.size() == 0)
        if (loadTargetFunc("func.meta") < 0)
            return false;
    return true;
}

int main() {
    return (computeFuncDep()? 0 : -1);
}
