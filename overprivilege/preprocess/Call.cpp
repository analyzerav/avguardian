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

int load_def(const char* file_name) {
    ifstream myfile(file_name);
    string line;
    if (myfile.is_open()) {
       while (getline(myfile, line)) {
          line = line.substr(0, line.find(" "));
          if (func_caller.find(line) != func_caller.end())
              continue;
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
    load_def("funcdef.meta");
    load_call("funccall.meta");
    int c = 1, n = 1;
    while (func_caller.size() > 0 && n > 0) {
        list<string> l;
       // cout << "Iteration " << c << endl;
        get_zero_callee_func(l);
        for (list<string>::iterator it = l.begin(); it != l.end(); ++it)
             cout << c << "," << *it << endl;
        n = l.size();
        c++;
    }
    cout << "Left over" << endl;
    for (map<string, set<string> >::iterator it = func_caller.begin(); it != func_caller.end(); ++it) {
         cout << it->first << " ";
//         if ((it->second).size() > 5)
//             cout << (it->second).size();
//         else
             print_call_chain(*((it->second).begin()));
         cout << endl;
    }
    return 0;
}

