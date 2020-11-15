# PAL asynchronous library for C++

[![Build](https://github.com/svens/pal/workflows/Build/badge.svg)](https://github.com/svens/pal/actions?query=workflow:Build)
[![Coverage](https://coveralls.io/repos/github/svens/pal/badge.svg)](https://coveralls.io/github/svens/pal)

PAL is portable library providing asynchronous syscall wrappers for Linux, MacOS and Windows. Intention is to cover networking, cryptography, logging, etc.

Library tooling:
* Linux/MacOS/Windows builds using g++, clang++ and MSVC
* CI: [GitHub Actions](https://github.com/features/actions)
* Unittesting: [Catch2](https://github.com/catchorg/Catch2)
* Code coverage: [gcov](https://gcc.gnu.org/onlinedocs/gcc/Gcov.html)/[lcov](https://github.com/linux-test-project/lcov), [coveralls](https://docs.coveralls.io)
* Documentation: [Doxygen](http://www.doxygen.nl)
* Benchmarking: [Google benchmark](https://github.com/google/benchmark)


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
