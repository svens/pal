.PHONY: all # Build and test all
all: linux-gcc-debug linux-gcc-release linux-clang-debug linux-clang-release

.PHONY: linux-gcc-debug # Build and test linux-gcc-debug
linux-gcc-debug:
	cmake --workflow --preset $@

.PHONY: linux-gcc-release # Build and test linux-gcc-release
linux-gcc-release:
	cmake --workflow --preset $@

.PHONY: linux-clang-debug # Build and test linux-clang-debug
linux-clang-debug:
	cmake --workflow --preset $@

.PHONY: linux-clang-release # Build and test linux-clang-release
linux-clang-release:
	cmake --workflow --preset $@

.PHONY: sanitize # Build and test with ASan/UBSan
sanitize:
	cmake --workflow --preset linux-gcc-sanitize

.PHONY: tidy # Run clang-tidy
tidy:
	cmake --preset linux-clang-debug
	cmake --build --preset linux-clang-debug --target tidy

.PHONY: format # Format sources
format:
	clang-format -i pal/*pp pal/**/*pp

.PHONY: format-check # Check formatting (dry-run)
format-check:
	clang-format --dry-run --Werror pal/*pp pal/**/*pp

.PHONY: coverage # Generate coverage report
coverage:
	cmake --workflow --preset linux-gcc-coverage
	@mkdir -p .build/linux-gcc-coverage/coverage
	gcovr .build/linux-gcc-coverage \
		--filter pal/ \
		-e '.*\.test\.cpp$$' \
		-e '.*\.bench\.cpp$$' \
		-e '.*/test\.(cpp|hpp)$$' \
		--merge-mode-functions separate \
		--html-details .build/linux-gcc-coverage/coverage/index.html
	@echo "Coverage report: .build/linux-gcc-coverage/coverage/index.html"

.PHONY: bench-before # Run benchmarks (before) -> .build/bench-before.xml
bench-before: check-bench-filter linux-gcc-release
	.build/linux-gcc-release/pal_test $(FILTER) -r XML -o .build/bench-before.xml

.PHONY: bench-after # Run benchmarks (after) -> .build/bench-after.xml
bench-after: check-bench-filter linux-gcc-release
	.build/linux-gcc-release/pal_test $(FILTER) -r XML -o .build/bench-after.xml

.PHONY: check-bench-filter
check-bench-filter:
	$(if $(FILTER),,$(error FILTER is required, e.g. make bench-before FILTER=hash))

.PHONY: help # Show available targets
help:
	@grep '^\.PHONY: .* # ' $(MAKEFILE_LIST) | awk -F' # ' '{sub(/\.PHONY: /,"",$$1); printf "  %-20s %s\n", $$1, $$2}'
