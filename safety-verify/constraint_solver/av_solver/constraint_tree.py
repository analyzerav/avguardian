from z3 import *
import argparse
import re
from .variable_controller import VariableController
from .model import *
from .api import *
import logging


class Node:
    
    def __init__(self, value=None):
        self.value = value
        self.sons = []
        self.parent = None
        self.constraint = None

    def set_sons(self, sons):
        assert(isinstance(sons, list))
        for son in sons:
            assert(isinstance(son, Node))
        self.sons = sons

    def add_son(self, son):
        assert(isinstance(son, Node))
        self.sons.append(son)
        son.parent = self

    def set_parent(self, parent):
        assert(isinstance(parent, Node))
        self.parent = parent
        parent.sons.append(self)

    def remove(self):
        self.parent.sons.remove(self)

    def is_root(self):
        return self.parent == None

    def is_leaf(self):
        return len(self.sons) == 0

    def is_variable(self):
        return self.is_leaf()

    def is_operator(self):
        return not self.is_leaf()

    def walk(self, action):
        # Post-Order Traversal
        for son in self.sons:
            son.walk(action)
        action(self)

    def __repr__(self):
        return str(self.value)

    def printout(self, result=""):
        result += self.value
        if len(self.sons) > 0:
            result += "("
            for son in self.sons:
                result += son.printout()
                if son != self.sons[-1]:
                    result += ","
            result += ")"
        return result

class ConstraintTree:

    def __init__(self, var_control, space="model"):
        self.root = None
        self.var_control = var_control
        self.operator_list = ["and", "or", "not", "==", ">", ">=", "<", "<=", "!=", "in", "cross", "+", "-", "*", "/", "abs", "is_color", "is_direction", "st_cross", "st_below", "st_above"]
        self.macro_list = ["red", "yellow", "green", "unknown", "left", "straight", "right"]
        self.space = space

    def build(self, data):
        data = re.sub("\s+", "", data)
        token_list = re.split('(\(|\)|,)', data)
        # a fix
        token_list = list(filter(lambda x : x != "", token_list))
        # FSM and stack
        state = 0
        node_stack = []
        for token in token_list:
            logging.debug("parse {} with stack {}".format(token, node_stack))

            if len(node_stack) > 0:
                cur_node = node_stack[-1]
            else:
                cur_node = None
            
            if state == 0:
                if self.is_operator(token):
                    new_node = Node()
                    if self.root == None:
                        self.root = new_node
                    else:
                        new_node.set_parent(cur_node)
                    new_node.value = token
                    node_stack.append(new_node)
                    state = 1
                elif self.is_literal(token):
                    son_node = Node()
                    son_node.set_parent(cur_node)
                    son_node.value = self.str_to_literal(token)
                    son_node.constraint = son_node.value
                    state = 2
                elif self.is_macro(token):
                    son_node = Node()
                    son_node.set_parent(cur_node)
                    son_node.value = token
                    son_node.constraint = son_node.value
                    state = 2
                elif self.is_var(token):
                    var = self.var_control.get_var(None, token)
                    son_node = Node()
                    son_node.set_parent(cur_node)
                    son_node.value = var
                    son_node.constraint = son_node.value
                    state = 2
                else:
                    raise Exception("build failed")
            elif state == 1:
                assert(token == "(")
                state = 0
            elif state == 2:
                if token == ",":
                    state = 0
                elif token == ")":
                    node_stack.pop()
                    state = 2
            else:
                raise Exception("build failed")

        def constraint_action(node):
            logging.debug("walking {} with sons {}".format(node, node.sons))
            
            if not node.is_operator():
                return
            if node.value == "and":
                node.constraint = And(*[son.constraint for son in node.sons])
            elif node.value == "or":
                node.constraint = Or(*[son.constraint for son in node.sons])
            elif node.value == "not":
                assert(len(node.sons) == 1)
                node.constraint = Not(node.sons[0].constraint)
            elif node.value == "==":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint == node.sons[1].constraint
            elif node.value == "!=":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint != node.sons[1].constraint
            elif node.value == ">":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint > node.sons[1].constraint
            elif node.value == ">=":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint >= node.sons[1].constraint
            elif node.value == "<":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint < node.sons[1].constraint
            elif node.value == "<=":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint <= node.sons[1].constraint
            elif node.value == "+":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint + node.sons[1].constraint
            elif node.value == "-":
                assert(len(node.sons) == 1 or len(node.sons) == 2)
                if len(node.sons) == 1:
                    node.constraint = -node.sons[0].constraint
                if len(node.sons) == 2:
                    node.constraint = node.sons[0].constraint - node.sons[1].constraint
            elif node.value == "*":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint * node.sons[1].constraint
            elif node.value == "/":
                assert(len(node.sons) == 2)
                node.constraint = node.sons[0].constraint / node.sons[1].constraint
            elif node.value == "abs":
                assert(len(node.sons) == 1)
                node.constraint = z3_abs(node.sons[0].constraint)
            # self-defined operator IN
            elif node.value == "in":
                assert(len(node.sons) == 2)
                a, b = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_in(a, b)
            # self-defined operator CROSS
            elif node.value == "cross":
                assert(len(node.sons) == 2)
                a, b = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_cross(a, b)
            # self-defined operator for traffic lights IS_COLOR
            elif node.value == "is_color":
                assert(len(node.sons) == 2)
                traffic_light, color = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_is_color(traffic_light, color)
            # self-defined operator for lanes IS_DIRECTION
            elif node.value == "is_direction":
                assert(len(node.sons) == 2)
                lane, direction = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_is_direction(lane, direction)
            # self-defined operator for lanes ST_CROSS
            elif node.value == "st_cross":
                assert(len(node.sons) == 2)
                ego, obs = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_st_cross(ego, obs)
            # self-defined operator for lanes ST_BELOW
            elif node.value == "st_below":
                assert(len(node.sons) == 2)
                ego, obs = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_st_below(ego, obs)
            # self-defined operator for lanes ST_ABOVE
            elif node.value == "st_above":
                assert(len(node.sons) == 2)
                ego, obs = node.sons[0].constraint, node.sons[1].constraint
                node.constraint = api_st_above(ego, obs)


        self.root.walk(constraint_action)
        self.var_control.add_constraint(self.space, simplify(self.constraint))
                
    def is_operator(self, data):
        return isinstance(data, str) and data in self.operator_list

    def is_var(self, data):
        return isinstance(data, str) and self.var_control.get_var(None, data) != None

    def is_literal(self, data):
        return self.str_to_literal(data) != None

    def is_macro(self, data):
        return isinstance(data, str) and data in self.macro_list

    def str_to_literal(self, data):
        if not isinstance(data, str):
            return None
        elif re.match("^[0-9]+$", data):
            return int(data)
        elif re.match("^[0-9]+\.[0-9]+$", data):
            return float(data)
        elif re.match("^(True|False)$", data):
            return data == "True"
        else:
            return None

    @property
    def constraint(self):
        assert(self.root != None)
        return self.root.constraint