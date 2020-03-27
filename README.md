# AVGuardian 

AVGuaridan is a static anlaysis and formal verification framework for AV software systems. 

Currently it is prootyped on top of a popular open-source AV software code base [Baidu Apollo](https://github.com/ApolloAuto/apollo/). 

There are two major parts in the current code base. 

The first one is a publish-subscribe message overprivilege detection tool in `overprivilege`

The second one is a safety driving rule compliance verification framework in `safety-verify`


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

2. Install a customized `whole-program-llvm` fro SRI-CSL 

    ```bash
    git clone https://github.com/SRI-CSL/whole-program-llvm.git
    cd whole-program-llvm
    sudo make develop
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

5. Or compile single module (e.g., `planning` module)

    ```bash
    ## Apollo 3.0
    bazel build --define ARCH=x86_64 --define CAN_CARD=fake_can --cxxopt=-DUSE_ESD_CAN=false --copt=-mavx2 --copt=-mno-sse3 --cxxopt=-DCPU_ONLY --crosstool_top=tools/wllvm:toolchain //modules/planning:planning --compilation_mode=opt
    ```

6. Extract bitcode file (e.g., `planning` module)

    ```bash
    cd /apollo/bazel-bin/modules/planning
    extract-bc planning

    # Check output
    file planning.bc
    llvm-dis planning.bc
    ```


## Tips

- ***!!! Do not use `dev_start.sh` to start dev docker after rebooting the machine***

    For the first time of starting Apollo dev docker, you can use `bash docker/scripts/dev_start.sh`. However, after rebooting, if you still use `dev_start.sh`, you will lose all previous modifications within the docker, because the dev docker container will be deleted. Using the following commands can avoid such issue.

    **Apollo 3.0**

    ```bash
    docker run -it -d --rm --name apollo_localization_volume apolloauto/apollo:localization_volume-x86_64-latest
    docker run -it -d --rm --name apollo_yolo3d_volume apolloauto/apollo:yolo3d_volume-x86_64-latest
    docker run -it -d --rm --name apollo_map_volume-sunnyvale_big_loop apolloauto/apollo:map_volume-sunnyvale_big_loop-latest
    docker run -it -d --rm --name apollo_map_volume-sunnyvale_loop apolloauto/apollo:map_volume-sunnyvale_loop-latest

    # Make sure that you can see `apollo_dev` in `docker ps -a` as `EXITED`
    docker start apollo_dev

    bash docker/scripts/dev_into.sh
    ```

- May need to delete inconsistent `gtest` header file
    ```bash
    sudo mv /usr/include/gtest /usr/include/gtest_bak
    ```

More detailed tool documentation and setup is in README of each folder.
