
add_library(ntfslib_new_km km.cpp)
add_dependencies(ntfslib_new_km psdk)

target_include_directories(ntfslib_new_km
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include/internal
)
