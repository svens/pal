## Drop the profile baseline, keeping the captures it named
include("${CMAKE_CURRENT_LIST_DIR}/profile_capture.cmake")

if(NOT EXISTS "${profile_state}")
	message(STATUS "no ${profile_state}, nothing to clear")
	return()
endif()

include("${profile_state}")
file(REMOVE "${profile_state}")
message(STATUS "removed ${profile_state}")

set(superseded "")
foreach(entry IN LISTS results)
	file(GLOB artifacts "${profile_dir}/${entry}-*")
	list(APPEND superseded ${artifacts})
endforeach()
if(superseded)
	list(JOIN superseded "\n  " detail)
	message(STATUS "captures it named, now unreferenced:\n  ${detail}")
endif()
