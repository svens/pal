#
# Threads
#
find_package(Threads REQUIRED)
list(APPEND PAL_DEP_LIBS
	Threads::Threads
)

#
# OpenSSL
#
find_package(OpenSSL REQUIRED)
list(FIND CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES OPENSSL_INCLUDE_DIR i)
if(${i} EQUAL -1)
	# not in include path, add
	include_directories(${OPENSSL_INCLUDE_DIR})
endif()
list(APPEND PAL_DEP_LIBS
	${OPENSSL_CRYPTO_LIBRARY}
)
