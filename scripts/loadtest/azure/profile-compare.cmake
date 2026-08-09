## Capture again and measure the difference from the baseline
include("${CMAKE_CURRENT_LIST_DIR}/profile_capture.cmake")

if(NOT EXISTS "${profile_state}")
	message(FATAL_ERROR
		"no ${profile_state}: take the reference first with\n"
		"$ ninja profile-baseline"
	)
endif()

find_program(difffolded_exe NAMES difffolded.pl)
find_program(flamegraph_exe NAMES flamegraph.pl)
if(NOT difffolded_exe OR NOT flamegraph_exe)
	message(FATAL_ERROR "difffolded.pl and flamegraph.pl not both on PATH: FlameGraph is a dependency, see docs/profile.md")
endif()

include("${profile_state}")
if(NOT results)
	message(FATAL_ERROR "${profile_state} holds no baseline capture: drop it with 'ninja profile-clear'")
endif()
set(state_deployment "${deployment}")
set(state_threads "${threads}")
set(state_sessions "${sessions}")
set(state_conntrack "${conntrack}")
set(state_results "${results}")

profile_preflight()

foreach(key deployment threads sessions conntrack)
	if(NOT "${${key}}" STREQUAL "${state_${key}}")
		message(FATAL_ERROR
			"${key} is '${${key}}' and the baseline was taken at '${state_${key}}', "
			"so the two do not describe the same work; start over with\n"
			"$ ninja profile-clear"
		)
	endif()
endforeach()

list(GET state_results -1 previous_stamp)
if(previous_stamp MATCHES "-exe_([0-9a-f]+)-" AND CMAKE_MATCH_1 STREQUAL binary_hash)
	message(STATUS "the shipped binary is the one ${previous_stamp} measured")
endif()

profile_capture(2)

file(APPEND "${profile_state}" "list(APPEND results \"${stamp}\")\n")
list(APPEND state_results "${stamp}")

execute_process(
	COMMAND "${CMAKE_COMMAND}"
		"-Dprofile_dir=${profile_dir}"
		"-Dstamps=${state_results}"
		-P "${loadtest_dir}/tools/profile_diff.cmake"
	RESULT_VARIABLE diff_status
)
if(diff_status)
	message(FATAL_ERROR "comparing against the baseline failed: ${diff_status}")
endif()

list(GET state_results 0 base_stamp)
set(diff_svg "${profile_dir}/${stamp}-vs-baseline.svg")
execute_process(
	COMMAND "${difffolded_exe}"
		"${profile_dir}/${base_stamp}-1.folded"
		"${profile_dir}/${stamp}-1.folded"
	COMMAND "${flamegraph_exe}"
		--title "${stamp}"
		--subtitle "against baseline ${base_stamp}"
	OUTPUT_FILE "${diff_svg}"
	RESULT_VARIABLE render_status
)
if(render_status)
	message(FATAL_ERROR "rendering ${diff_svg} failed: ${render_status}")
endif()
message(STATUS "difference ${diff_svg}")
