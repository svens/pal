## Build and test all
all: gcc-debug gcc-release clang-debug clang-release

## Build and test gcc-debug
gcc-debug:
	cmake --workflow --preset $@

## Build and test gcc-release
gcc-release:
	cmake --workflow --preset $@

## Build and test clang-debug
clang-debug:
	cmake --workflow --preset $@

## Build and test clang-release
clang-release:
	cmake --workflow --preset $@

## Format sources
format:
	find pal -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

## Show available targets
help:
	@awk '/^## /{desc=substr($$0,4)} /^[a-zA-Z_-]+:/{if(desc){sub(/:$$/,"",$$1);printf "  %-16s %s\n",$$1,desc;desc=""}}' $(MAKEFILE_LIST)

.PHONY: all gcc-debug gcc-release clang-debug clang-release format help
