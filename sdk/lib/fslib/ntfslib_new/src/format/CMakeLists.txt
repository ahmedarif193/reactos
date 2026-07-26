
list(APPEND SOURCE
    bootsect.cpp
    format.cpp
    record.cpp
    sysfiles.cpp
    tables.cpp
    formatint.h)

# Built as its own target rather than as part of ntfslib_new: the formatter is
# used from user/native mode (the fmifs provider and setuplib) and must stay
# clear of the kernel-only headers the rest of the library relies on.
add_library(ntfslib_format ${SOURCE})
add_dependencies(ntfslib_format psdk)

target_include_directories(ntfslib_format
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
