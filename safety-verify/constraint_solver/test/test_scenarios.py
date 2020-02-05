import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
from av_solver.scenarios import *
import logging

logging.basicConfig(level=logging.DEBUG)

def test():
    s = CrosswalkScenario()
    for name, var in s.get_variables():
        print(name, ":", var)
    for c in s.get_constraints():
        print(c)

if __name__ == "__main__":
    test()