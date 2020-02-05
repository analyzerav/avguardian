// Input file: func.meta, funcdef.meta, funccall.meta
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <list>

using namespace std;

map<string, set<string> > func_caller;
map<string, set<string> > func_callee;
map<string, bool> func_prof;

int load_profile_func(const char* file_name) {
    ifstream myfile(file_name);
    string line;
    if (myfile.is_open()) {
       while (getline(myfile, line)) 
          func_prof[line] = true;
       myfile.close();
       return 0;
    }
    return -1;
}

int load_def(const char* file_name) {
    ifstream myfile(file_name);
    string line;
    if (myfile.is_open()) {
       while (getline(myfile, line)) {
          line = line.substr(0, line.find(" "));
          if (func_prof.find(line) == func_prof.end() || func_caller.find(line) != func_caller.end())
              continue;
          // Only take defined function for order analysis
          func_caller[line] = set<string>();
          func_callee[line] = set<string>();
       }
       myfile.close();
       return 0;
    }
    return -1;
}

int load_call(const char* file_name) {
    ifstream myfile(file_name);
    string line, caller, callee;
    if (myfile.is_open()) {
       while (getline(myfile, line)) {
          caller = line.substr(0, line.find(" "));
          callee = line.substr(line.find(" ") + 1);
          if (func_prof.find(caller) == func_prof.end() || func_prof.find(callee) == func_prof.end())
              continue;
          // If callee not defined, do not include them
          if (func_caller.find(callee) != func_caller.end()) {
              func_caller[caller].insert(callee);
              func_callee[callee].insert(caller);
          }
       }
       myfile.close();
       return 0;
    }
    return -1;
}

void get_zero_callee_func(list<string>& proc_l) {
   for (map<string, set<string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it) {
        if ((it->second).size() == 0) {
           proc_l.push_back(it->first);
           for (set<string>::iterator jt = func_callee[it->first].begin(); jt != func_callee[it->first].end(); ++jt) {
                func_caller[*jt].erase(it->first);
           }
           func_caller.erase(it);
        }
   } 
}

void print_call_chain(string s) {
    map<string, bool> vis;
    while (vis.find(s) == vis.end()) {
        cout << s << " ";
        if (func_caller[s].size() == 0) 
            break;
        vis[s] = true;
        s = *(func_caller[s].begin());
   }
   cout << s;
}

int main() {
    load_profile_func("func.meta");
    load_def("funcdef.meta");
    load_call("funccall.meta");
    int c = 1, n = 1;
    while (func_caller.size() > 0 && n > 0) {
        list<string> l;
        get_zero_callee_func(l);
        for (list<string>::iterator it = l.begin(); it != l.end(); ++it)
             cout << c << "," << *it << endl;
        n = l.size();
        c++;
    }
    if (func_caller.size() == 0)
        return 0;
    cout << "Left over" << endl;
    for (map<string, set<string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it) {
         cout << it->first << " ";
         print_call_chain(*((it->second).begin()));
         cout << endl;
    }
    return 0;
}

