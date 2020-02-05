# Analysis on Apollo

## Steps

* Data flow (`reaching-definitions`)
* Call graph (`control-dependency`)
* Control dependency (`control-dependency`)
* Program slicing (`control-dependency`)
* Symbolic execution(`traffic-rule-info`)

## Get started

* Install z3 c++ API (`libz3.a`) following instructions [here](https://github.com/Z3Prover/z3#building-z3-using-make-and-gccclang), remember to add `--staticlib` option while running `python scripts/mk_make.py`
* Prepare a test directory, containing
    * `func.meta`: Demangled function name to be analyzed (including the source function but excluding the sink function).
    * `sink.meta`: Sink function name.
    * `source.meta`: Source function name.
    * `{dirname}.cpp/.bc`: A cpp file or compiled LLVM bitcode file; the filename must be the same as the directory name.

* Compile the pass

```bash
make
```

* Execute the pass

```bash
bash run.sh ${path_to_test_directory}
# e.g., bash run.sh test/crosswalk
```
