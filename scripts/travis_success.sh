if test "${BUILD_TYPE}" = "Coverage"; then
	coveralls-lcov pal.info
fi

if test "${ENABLE_DOCS}" = "yes"; then
	cmake --build . --target pal-doc
fi
