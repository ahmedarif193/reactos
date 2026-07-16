include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos)

list(APPEND CORE_SOURCE
    profiler_analysis.c
    profiler_baseline.c
    profiler_codec.c
    profiler_controller.c
    profiler_export.c
    profiler_jobs.c
    profiler_legacy_bridge.c
    profiler_model.c
    profiler_pe.c
    profiler_product.c
    profiler_recorder.c
    profiler_recorder_etw.c
    profiler_recorder_legacy.c
    profiler_recorder_rosprof.c
    profiler_symbolizer.c
    profiler_symbolizer_dbghelp.c
    profiler_viewmodel.c)

add_library(rosprofiler_core ${CORE_SOURCE})
target_link_libraries(rosprofiler_core rperf)

list(APPEND SOURCE
    capture.c
    flamegraph.c
    timeline.c
    rosprofiler.c
    rosprofiler.h)

set(ROSPROFILER_ICON
    ${CMAKE_CURRENT_SOURCE_DIR}/res/rosprofiler.ico)
add_rc_deps(rosprofiler.rc ${ROSPROFILER_ICON})
add_executable(rosprofiler ${SOURCE} rosprofiler.rc)
set_module_type(rosprofiler win32gui UNICODE)
target_link_libraries(rosprofiler rosprofiler_core)
add_importlibs(rosprofiler comctl32 comdlg32 dbghelp gdi32 user32 msvcrt kernel32 ntdll)
add_cd_file(TARGET rosprofiler DESTINATION reactos/system32 FOR all)

add_executable(rosprofiler_selftest EXCLUDE_FROM_ALL
    selftest.c
    capture.c
    rosprofiler.h)
set_module_type(rosprofiler_selftest win32cui UNICODE)
target_link_libraries(rosprofiler_selftest rosprofiler_core)
add_importlibs(rosprofiler_selftest dbghelp user32 msvcrt kernel32 ntdll)

add_executable(rosprofiler_guitest EXCLUDE_FROM_ALL
    guitest.c
    rosprofiler.h
    resource.h)
set_module_type(rosprofiler_guitest win32cui UNICODE)
add_importlibs(rosprofiler_guitest comctl32 user32 msvcrt kernel32)
add_dependencies(rosprofiler_guitest rosprofiler rosprofiler_selftest)
