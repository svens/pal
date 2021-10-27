set(cxx_max_warning_flags -Werror -Wall -Wextra -Weffc++ -pedantic)

add_compile_options(-pipe)
if("${CMAKE_GENERATOR}" STREQUAL "Ninja")
	# Ninja redirects build output and prints it only on error
	# Redirection strips colorization, so let's force it here
	add_compile_options(-fdiagnostics-color)
endif()
