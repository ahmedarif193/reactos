# Hardware Resources Arbiter Library
# Provides resource arbitration functionality for kernel drivers

add_definitions(-D_NTSYSTEM_)

list(APPEND SOURCE
    arbiter.c
    arbiter.h)

add_library(arbiter ${SOURCE})
add_dependencies(arbiter bugcodes xdk)

# Ensure proper include paths for NDK headers
target_include_directories(arbiter PRIVATE ${REACTOS_SOURCE_DIR}/sdk/include)
