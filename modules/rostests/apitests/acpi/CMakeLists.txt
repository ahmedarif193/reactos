
# TODO(ARM64): ACPI test includes driver source files directly which have
# missing declarations and type errors when compiled for arm64
if(ARCH STREQUAL "arm64")
    return()
endif()

include_directories(${REACTOS_SOURCE_DIR}/drivers/bus/acpi/acpica/include)

list(APPEND SOURCE
    Bus_PDO_EvalMethod.c
    Bus_PDO_QueryResourceRequirements.c
    testlist.c)

add_executable(acpi_apitest ${SOURCE})
set_module_type(acpi_apitest win32cui)
add_importlibs(acpi_apitest msvcrt kernel32 ntdll)

add_rostests_file(TARGET acpi_apitest)
