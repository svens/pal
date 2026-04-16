# pal

C++23 platform abstraction library providing cross-platform utilities for networking, cryptography, and system-level operations.

[![CI](https://github.com/svens/pal/actions/workflows/ci.yml/badge.svg)](https://github.com/svens/pal/actions/workflows/ci.yml)

## Directory structure

    .                       Root of source tree
    |- cmake/               Build system support modules
    |  `- actions/          Cross-platform action scripts (invoked via ninja)
    |- pal/                 Library ...
    |  `- <module>          ... per module header, sources and tests
    |- CMakeLists.txt       Top-level CMake project
    |- CMakePresets.json    Compiler/config presets and workflows
    `- build.ninja          Convenience wrapper for cmake workflow presets

## Building

    ninja help              # show available targets
    ninja                   # build and test all compiler/config combinations (platform-specific)

Each target delegates to `cmake -P cmake/actions/<target>.cmake`.
