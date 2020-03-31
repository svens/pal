# PAL asynchronous library for C++

[![Build Status](https://travis-ci.org/svens/pal.svg?branch=master)](https://travis-ci.org/svens/pal)
[![Coverage](https://coveralls.io/repos/github/svens/pal/badge.svg?branch=master)](https://coveralls.io/github/svens/pal?branch=master)

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
    |- extern       External code as git submodules
    `- scripts      Helper scripts
