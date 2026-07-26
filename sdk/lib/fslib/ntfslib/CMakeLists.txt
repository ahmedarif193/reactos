
add_library(ntfslib ntfslib.c)
add_dependencies(ntfslib psdk)
target_link_libraries(ntfslib ntfslib_format)
