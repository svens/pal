## Apply every host tuning stage on the relay, in order
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

# The verb is passed through, so the same action reads the host without changing
# it: -Dverb=show before -Dverb=apply is the check this suite is built around.
default(verb apply)
default(host relay-linux)

load_manifest()
host_field(os "${host}" os)
if(NOT os STREQUAL "linux")
	message(FATAL_ERROR "'${host}' runs ${os}: the tuning stages are Linux only")
endif()

remote_run("${host}" "sudo ${remote_dir}/host/tune ${verb}")
message(STATUS "${host}: tune ${verb} done")
