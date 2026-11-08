## Capture the relay's per-packet time budget at the standard point
include("${CMAKE_CURRENT_LIST_DIR}/profile_capture.cmake")

find_program(flamegraph_exe NAMES flamegraph.pl)
if(NOT flamegraph_exe)
	message(FATAL_ERROR "flamegraph.pl not on PATH: FlameGraph is a dependency, see docs/profile.md")
endif()

profile_preflight()
profile_capture(1)

execute_process(
	COMMAND "${flamegraph_exe}"
		--title "${stamp}"
		--subtitle "${ns_per_packet} ns/packet"
		"${profile_dir}/${stamp}-1.folded"
	OUTPUT_FILE "${profile_dir}/${stamp}-1.svg"
	RESULT_VARIABLE flamegraph_status
)
if(flamegraph_status)
	message(FATAL_ERROR "rendering ${stamp}-1.svg failed: ${flamegraph_status}")
endif()
message(STATUS "flamegraph ${profile_dir}/${stamp}-1.svg")
