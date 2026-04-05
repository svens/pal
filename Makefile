.PHONY: all # Build and test all
all: gcc-debug gcc-release clang-debug clang-release

.PHONY: gcc-debug # Build and test gcc-debug
gcc-debug:
	cmake --workflow --preset $@

.PHONY: gcc-release # Build and test gcc-release
gcc-release:
	cmake --workflow --preset $@

.PHONY: clang-debug # Build and test clang-debug
clang-debug:
	cmake --workflow --preset $@

.PHONY: clang-release # Build and test clang-release
clang-release:
	cmake --workflow --preset $@

.PHONY: sanitize # Build and test with ASan/UBSan
sanitize:
	cmake --workflow --preset gcc-sanitize

.PHONY: tidy # Run clang-tidy
tidy:
	cmake --preset clang-debug
	cmake --build --preset clang-debug --target tidy

.PHONY: format # Format sources
format:
	clang-format -i pal/*pp pal/**/*pp

.PHONY: format-check # Check formatting (dry-run)
format-check:
	clang-format --dry-run --Werror pal/*pp pal/**/*pp

.PHONY: coverage # Generate coverage report
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

.PHONY: bench-before # Run benchmarks (before) -> .build/bench-before.xml
bench-before: check-bench-filter gcc-release
	.build/gcc-release/pal_test $(FILTER) -r XML -o .build/bench-before.xml

.PHONY: bench-after # Run benchmarks (after) -> .build/bench-after.xml
bench-after: check-bench-filter gcc-release
	.build/gcc-release/pal_test $(FILTER) -r XML -o .build/bench-after.xml

.PHONY: check-bench-filter
check-bench-filter:
	$(if $(FILTER),,$(error FILTER is required, e.g. make bench-before FILTER=hash))

.PHONY: help # Show available targets
help:
	@grep '^\.PHONY: .* # ' $(MAKEFILE_LIST) | awk -F' # ' '{sub(/\.PHONY: /,"",$$1); printf "  %-16s %s\n", $$1, $$2}'
