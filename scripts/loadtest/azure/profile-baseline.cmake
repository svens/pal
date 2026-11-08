## Capture the reference that later comparisons are measured against
include("${CMAKE_CURRENT_LIST_DIR}/profile_capture.cmake")

if(EXISTS "${profile_state}")
	message(FATAL_ERROR
		"${profile_state} already holds a baseline; measure against it with\n"
		"$ ninja profile-compare\n"
		"or drop it with\n"
		"$ ninja profile-clear"
	)
endif()

profile_preflight()
profile_capture(2)

file(WRITE "${profile_state}"
	"# written by 'ninja profile-baseline' -- drop it with 'ninja profile-clear'\n"
	"set(deployment \"${deployment}\")\n"
	"set(mode \"${mode}\")\n"
	"set(threads \"${threads}\")\n"
	"set(sessions \"${sessions}\")\n"
	"set(conntrack \"${conntrack}\")\n"
	"set(results \"\")\n"
	"list(APPEND results \"${stamp}\")\n"
)
message(STATUS "baseline ${stamp} in ${profile_state}")
