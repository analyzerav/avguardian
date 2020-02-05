import sys
import os
import logging
import argparse
from .variable_controller import VariableController
from .constraint_tree import Node, ConstraintTree
import json

logging.basicConfig(level=logging.INFO)

def main():
    parser = argparse.ArgumentParser(description='Solve constraints of decision making in Apollo')
    parser.add_argument('--code', type=str, help="filename containing policy extracted from Apollo code (split by comma)")
    parser.add_argument('--spec', type=str, help="filename containing user's policy")
    parser.add_argument('--output', type=str, help="filename of output test case")
    parser.add_argument('--action', type=int, default=0, help="Do action or do not action {0, 1}")
    parser.add_argument('--scenario', type=str, default="crosswalk", help="Scenario type {crosswalk, intersection, traffic_light, stop_sign}")

    args = parser.parse_args()

    var_control = VariableController(action_mode=args.action, scenario_type=args.scenario)

    
    for code in args.code.split(','):
        code_tree = ConstraintTree(var_control, "code")
        with open(args.code, "r") as f:
            constraint_str = f.read()
        code_tree.build(constraint_str)
        logging.debug("code side: {}".format(code_tree.constraint))

    user_tree = ConstraintTree(var_control, "user")
    with open(args.spec, "r") as f:
        constraint_str = f.read()
    user_tree.build(constraint_str)
    logging.debug("user side: {}".format(user_tree.constraint))

    data = var_control.solve()
    with open(args.output, "w") as f:
        json.dump(data, f, indent=2)

    for side in ["model", "code", "user"]:
        logging.info("{}'s {}: {}".format(side, "variables", len(var_control.var_map[side])))
        logging.info("{}'s {}: {}".format(side, "constraints", len(var_control.constraints[side])))


if __name__ == "__main__":
    main()