
include(../ntos.cmake)

include_directories(${REACTOS_SOURCE_DIR}/ntoskrnl)
spec2def(ntkrnlmp.exe ../ntoskrnl.spec)

if(MSVC_IDE)
    set_source_files_properties(${NTKRNLMP_ASM_SOURCE} PROPERTIES COMPILE_DEFINITIONS "CONFIG_SMP")
endif()

add_asm_files(ntkrnlmp_asm ${NTKRNLMP_ASM_SOURCE})

if(NT_ARCH STREQUAL "i386" OR NT_ARCH STREQUAL "amd64")
    list(APPEND NTKRNLMP_SOURCE
        ${NTOS_ARCH_DIR}/ke/mproc.c)
elseif(NT_ARCH STREQUAL "arm" OR NT_ARCH STREQUAL "arm64")
    #TBD
endif()

list(APPEND NTKRNLMP_PCH_SKIP_SOURCE
    ${REACTOS_SOURCE_DIR}/ntoskrnl/guid.c)

add_executable(ntkrnlmp
    ${ntkrnlmp_asm}
    ${NTKRNLMP_SOURCE}
    ${NTKRNLMP_PCH_SKIP_SOURCE}
    ${REACTOS_SOURCE_DIR}/ntoskrnl/ntoskrnl.rc
    ${CMAKE_CURRENT_BINARY_DIR}/ntkrnlmp.def)
target_sources(ntkrnlmp PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/rtl/intrin_llshift.c)
if((NT_ARCH STREQUAL "i386" OR NT_ARCH STREQUAL "amd64") AND
   (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang"))
    target_sources(ntkrnlmp PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/rtl/intrin_msr.c)
endif()
set_module_type(ntkrnlmp kernel)
target_compile_definitions(ntkrnlmp PRIVATE CONFIG_SMP)

# dynamic analysis switches
if(STACK_PROTECTOR)
    target_sources(ntkrnlmp PRIVATE $<TARGET_OBJECTS:gcc_ssp_nt>)
endif()

target_link_libraries(ntkrnlmp cportlib csq ${PSEH_LIB} arbiter cmlib ntlsalib rtl ${ROSSYM_LIB} libcntpr setjmp wdmguid poguid ioevent)
add_importlibs(ntkrnlmp hal kdcom bootvid)
add_pch(ntkrnlmp ${REACTOS_SOURCE_DIR}/ntoskrnl/include/ntoskrnl.h "${NTKRNLMP_PCH_SKIP_SOURCE}")
add_dependencies(ntkrnlmp psdk asm)
add_cd_file(TARGET ntkrnlmp DESTINATION reactos/system32 NO_CAB FOR all)
