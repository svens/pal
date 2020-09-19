# PAL asynchronous library for C++

[![Build](https://github.com/svens/pal/workflows/Build/badge.svg)](https://github.com/svens/pal/actions?query=workflow:Build)
[![Coverage](https://coveralls.io/repos/github/svens/pal/badge.svg)](https://coveralls.io/github/svens/pal)

TODO


## Compiling and installing

    $ mkdir build && cd build
    $ cmake .. [-Dpal_unittests=yes|no] [-Dpal_benchmarks=yes|no] [-Dpal_docs=yes|no]
    $ make && make test && make install


## Source tree

The source tree is organised as follows:

    .               Root of source tree
    |- pal          Library ...
    |  `- module    ... per module headers/sources/tests
    |- bench        Benchmarks
    |- cmake        CMake modules
    `- extern       External code as git submodules
