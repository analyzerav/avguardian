# Apollo Constraint Solver

# Requirements

* Python (3.6+ preferred)
```
python -m pip install -r requirements.txt
```

# Get started

```
python -m av_solver --help
```

# Notes

* `variable_controller` defines code/model/user side variables.
* `constraint_tree` transforms plain input (DSL) to a structured tree.
* `model` basic geometric objects.
* `objects` traffic objects.
* `scenarios` defines multiple scenarios including crosswalk and intersection.
* `scripts/z3_parser.py` transforms SMT2 constraints to DSL.