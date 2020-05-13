Apollo+LLVM Notes
===

## Download Apollo source code and docker container

1. Download Apollo 3.0 source code

    ```bash
    wget https://github.com/ApolloAuto/apollo/archive/v3.0.0.tar.gz
    tar xf https://github.com/ApolloAuto/apollo/archive/v3.0.0.tar.gz
    ```

2. Download and start Apollo docker container

    ```bash
    cd apollo-3.0.0/
    bash docker/scripts/dev_start.sh
    bash docker/scripts/dev_into.sh
    ```

3. Enter the docker container to compile Apollo
    ```bash
    bash docker/scripts/dev_into.sh
    ```


## Compile Apollo using LLVM

(All following commands are assumed to be executed in Apollo dev docker)

1. Install LLVM

    **LLVM 8 (Recommended)**

    ```bash
    # LLVM 8
    sudo apt install llvm-8 llvm-8-dev clang-8 libclang-8-dev

    # Create soft links
    sudo ln -sf /usr/bin/llvm-config-8 /usr/bin/llvm-config
    sudo ln -sf /usr/bin/llvm-link-8 /usr/bin/llvm-link
    ```

2. Install `whole-program-llvm`  

    ```bash
    sudo pip install wllvm
    ```

3. Copy `wllvm` directory to `/apollo/tools`

    ```bash
    cp -r wllvm /apollo/tools
    ```

4. Compile Apollo

    ```bash
    cd /apollo
    mkdir wllvm_bc  # This directory will contain all seperate bitcode files
    bash apollo.sh build --copt=-mavx2 --cxxopt=-mavx2 --copt=-mno-sse3 --crosstool_top=tools/wllvm:toolchain
    ```

    Those `copt` and `cxxopt` can be removed, if your machine supports the corresponding instruction sets.

5. Or compile single module (e.g., `localization` module)

    ```bash
    ## Apollo 3.0
    bazel build --define ARCH=x86_64 --define CAN_CARD=fake_can --cxxopt=-DUSE_ESD_CAN=false --copt=-mavx2 --copt=-mno-sse3 --cxxopt=-DCPU_ONLY --crosstool_top=tools/wllvm:toolchain //modules/localization:localization --compilation_mode=opt
    ```

6. Extract bitcode file (e.g., `localization` module)

    ```bash
    cd /apollo/bazel-bin/modules/localization
    extract-bc localization

    # Check output
    file localization.bc
    llvm-dis localization.bc
    ```


## Tips

- May need to delete inconsistent `gtest` header file
    ```bash
    sudo mv /usr/include/gtest /usr/include/gtest_bak
    ```

