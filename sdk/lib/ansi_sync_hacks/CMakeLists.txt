
list(APPEND SOURCE
   sync_hacks.c)

add_library(ansi_sync_hacks STATIC ${SOURCE})
add_dependencies(ansi_sync_hacks psdk)
