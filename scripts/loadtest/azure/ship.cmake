## Copy binaries and on-VM scripts to the cluster
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

# Any preset turns PAL_SAMPLES on, so a preset build has the binaries; a bare cmake
# invocation does not, because the option defaults to OFF.
default(build_dir "${repo_root}/.build/linux-gcc-release")

file(REAL_PATH "~/.tmux.conf" home_tmux_conf EXPAND_TILDE)
default(tmux_conf "${home_tmux_conf}")

foreach(binary ${relay_process} loadtest)
	if(NOT EXISTS "${build_dir}/${binary}")
		message(FATAL_ERROR
			"${build_dir}/${binary} not found: build it first, e.g.\n"
			"  cmake --preset linux-gcc-release && ninja -C ${build_dir}\n"
			"or point elsewhere with -Dbuild_dir=<path>"
		)
	endif()
endforeach()

load_manifest()

# Unlinked rather than overwritten: scp cannot open a running executable for
# writing. The running process keeps the old inode and carries on; the new file
# lands beside it and is what the next start picks up.
message(STATUS "${relay_host}")
remote_run(${relay_host} "mkdir -p ${remote_dir} && rm -f ${remote_dir}/${relay_process}")
remote_copy(${relay_host} "${remote_dir}/"
	"${build_dir}/${relay_process}"
	"${loadtest_dir}/host"
	"${loadtest_dir}/perf"
)

foreach(client IN LISTS cluster_clients)
	message(STATUS "${client}")
	remote_run("${client}" "mkdir -p ${remote_dir} && rm -f ${remote_dir}/loadtest")
	remote_copy("${client}" "${remote_dir}/"
		"${build_dir}/loadtest"
		"${loadtest_dir}/host"
	)
endforeach()

# Absent is the normal case and not an error.
if(EXISTS "${tmux_conf}")
	foreach(host ${relay_host} ${cluster_clients})
		remote_copy("${host}" ".tmux.conf" "${tmux_conf}")
	endforeach()
	message(STATUS "shipped ${tmux_conf}")
endif()
