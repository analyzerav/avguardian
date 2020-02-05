import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
from av_solver.objects import *
import logging

logging.basicConfig(level=logging.DEBUG)

def test():
    v = Vehicle("0")
    for var in v.get_variables():
        print(var)

if __name__ == "__main__":
    test()