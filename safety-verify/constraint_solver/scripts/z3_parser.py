"""
    z3_parser.py
    Parse z3 SMT2 format output to DSL
"""
import os, sys
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
import sys
import re

from av_solver.constraint_tree import Node

in_file = sys.argv[1]
out_file = sys.argv[2]

try:
    in_fd = open(in_file, "r")
    in_text = in_fd.read()
except:
    print("Open file {} failed".format(in_file))

try:
    out_fd = open(out_file, "w")
except:
    print("Open file {} failed".format(out_file))

# data = re.sub("\s+", "", in_text)
token_list = re.split('(\(|\)|,|\ |\n)', in_text)
token_list = list(filter(lambda x : x != "" and x != " " and x != "\n", token_list))
tmp_tokens = []
# print(token_list)

stack = []
root = Node("")
parent = root
prev = root
b = False
stack.append(root)
for token in token_list:
    if token == "(":
        if b:
            n = Node("blank")
            parent.add_son(n)
            parent = n
            stack.append(n)
        b = True
    elif token == ")":
        stack.pop()
        parent = stack[-1]
    else:
        if token == "=":
            token = "=="
        n = Node(token)
        parent.add_son(n)
        if b:
            stack.append(n)
            parent = n
            b = False
    # print(stack[-1].value)

cases = {}
def walk_action(self):
    if self.value == "let":
        for son in self.sons:
            if son.value == "blank":
                for grandson in self.sons[0].sons:
                    if grandson.value[:2] == "a!":
                        cases[grandson.value] = grandson
            elif son.value != "let":
                cases["root"] = son
root.walk(walk_action)

# print(cases)

changed = True
def assemble(self):
    global changed
    if self.value in cases.keys():
        key = self.value
        self.value = cases[key].sons[0].value
        self.sons = cases[key].sons[0].sons + []
        changed = True

while changed:
    changed = False
    cases["root"].walk(assemble)

out_text = cases["root"].printout()
# format
result = ""
tab = 0
for c in out_text:
    if c == "(":
        result += c
        tab += 1
        result += ("\n" + "  " * tab)
    elif c == ",":
        result += c
        result += ("\n" + "  " * tab)
    elif c == ")":
        tab -= 1
        result += ("\n" + "  " * tab)
        result += c
    else:
        result += c

out_fd.write(result)

names = []
def unique_names(self):
    if self.is_variable():
        if len(self.value) > 0 and (self.value[0] < '0' or self.value[0] > '9') and self.value not in names:
            names.append(self.value)

cases["root"].walk(unique_names)

print("Undefined variables:")
print(names)