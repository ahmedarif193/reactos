add_library(rperf STATIC rperf.c)
target_include_directories(rperf PUBLIC ${REACTOS_SOURCE_DIR}/sdk/include)
