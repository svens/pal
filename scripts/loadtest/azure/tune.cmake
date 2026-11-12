## Apply every host tuning stage on the relay, in order
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

# -Dverb=show reads the host without changing it.
default(verb apply)
default(host ${relay_host})

load_manifest()
remote_run("${host}" "sudo ${remote_dir}/host/tune ${verb}")
message(STATUS "${host}: tune ${verb} done")
