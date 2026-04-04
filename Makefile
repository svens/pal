#help|all|Build and test all
all: gcc-debug gcc-release clang-debug clang-release

#help|gcc-debug|Build and test gcc-debug
gcc-debug:
	cmake --workflow --preset $@

#help|gcc-release|Build and test gcc-release
gcc-release:
	cmake --workflow --preset $@

#help|clang-debug|Build and test clang-debug
clang-debug:
	cmake --workflow --preset $@

#help|clang-release|Build and test clang-release
clang-release:
	cmake --workflow --preset $@

#help|coverage|Generate coverage report
coverage:
	cmake --workflow --preset gcc-coverage
	@mkdir -p .build/gcc-coverage/coverage
	gcovr .build/gcc-coverage \
		--filter pal/ \
		-e '.*\.test\.cpp$$' \
		-e '.*\.bench\.cpp$$' \
		-e '.*/test\.(cpp|hpp)$$' \
		--html-details .build/gcc-coverage/coverage/index.html
	@echo "Coverage report: .build/gcc-coverage/coverage/index.html"

#help|tidy|Run clang-tidy
tidy:
	cmake --preset clang-debug
	cmake --build --preset clang-debug --target tidy

#help|format|Format sources
format:
	find pal -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

#help|format-check|Check formatting (dry-run)
format-check:
	find pal -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror

#help|help|Show available targets
help:
	@grep '^#help|' $(MAKEFILE_LIST) | awk -F'|' '{printf "  %-16s %s\n", $$2, $$3}'

.PHONY: all gcc-debug gcc-release clang-debug clang-release coverage tidy format format-check help
