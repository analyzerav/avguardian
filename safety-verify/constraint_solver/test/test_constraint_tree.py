import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
from av_solver.constraint_tree import Node, ConstraintTree
import logging

logging.basicConfig(level=logging.DEBUG)

def test():
    tree = ConstraintTree()
    tree.build("and(>=(pedestrian_pos_x, 10), or(is_path_cross, <(pedestrian_pos_y, 0)))")
    print(tree.constraint)

if __name__ == "__main__":
    test()