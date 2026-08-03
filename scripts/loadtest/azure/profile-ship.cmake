## Copy the profile build to the cluster
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

default(build_dir "${repo_root}/.build/linux-gcc-profile")
include("${CMAKE_CURRENT_LIST_DIR}/ship.cmake")
