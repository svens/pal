# pal

C++26 platform abstraction library providing cross-platform utilities for networking, cryptography, and system-level operations.

## Directory structure

    .                       Root of source tree
    |- cmake/               Build system support modules
    |- pal/                 Library ...
    |  `- <module>          ... per module header, sources and tests
    |- CMakeLists.txt       Top-level CMake project
    |- CMakePresets.json    Compiler/config presets and workflows
    `- Makefile             Convenience wrapper for cmake workflow presets

## Building

    make help               # show available targets
    make all                # build and test all compiler/config combinations
    make gcc-debug          # build and test single preset

Each preset runs the full cmake workflow: configure, build, test. Build artifacts go to `.build/<preset>/`.
