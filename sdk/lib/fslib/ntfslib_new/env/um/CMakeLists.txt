
add_library(ntfslib_new_um um.cpp)
add_dependencies(ntfslib_new_um psdk)

target_include_directories(ntfslib_new_um
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
)
