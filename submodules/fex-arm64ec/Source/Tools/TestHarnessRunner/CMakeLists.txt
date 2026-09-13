list(APPEND LIBS FEXCore Common JemallocLibs ${PTHREAD_LIB})

set(SRCS TestHarnessRunner.cpp)
if (NOT MINGW)
  list(APPEND SRCS TestHarnessRunner/HostRunner.cpp)
  list(APPEND LIBS LinuxEmulation CommonTools)
endif()

add_executable(TestHarnessRunner ${SRCS})

if (ENABLE_VIXL_SIMULATOR)
  target_compile_definitions(TestHarnessRunner PRIVATE "-DVIXL_SIMULATOR=1")
endif()

target_include_directories(TestHarnessRunner PRIVATE ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(TestHarnessRunner PRIVATE ${LIBS})

if (MINGW)
  target_link_options(TestHarnessRunner PRIVATE -static -static-libgcc -static-libstdc++)
  target_include_directories(TestHarnessRunner PRIVATE "${CMAKE_SOURCE_DIR}/Source/Windows/include/" "${CMAKE_SOURCE_DIR}/Source/Windows/")
  # The executable uses the toolchain CRT. The extended native import library
  # also exports CRT names and would bind errno and setjmp to a different CRT.
  target_link_libraries(TestHarnessRunner PRIVATE CommonTools CommonWindows ntdll)
endif()
