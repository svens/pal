## Rewrite .ssh/local.conf from the manifest, without touching Azure
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")

write_ssh_config()
