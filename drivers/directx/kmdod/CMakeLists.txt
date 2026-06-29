# kmdod.sys -- Microsoft Kernel Mode Display-Only Driver sample
#
# Adapted for the ReactOS build system from the official Microsoft
# Windows driver samples repository:
#   https://github.com/microsoft/Windows-driver-samples/tree/main/video/KMDOD
#
# This is a WDDM 1.2 (Win8) display-only driver that renders to the
# POST device frame buffer.  It calls DxgkInitializeDisplayOnlyDriver
# which is exported by our dxgkrnl.sys.
#
# Build order in drivers/directx:
#   1. dxgmms1   -- no dxgkrnl dependency
#   2. dxgkrnl   -- exports DxgkInitializeDisplayOnlyDriver
#   3. kmdod     -- links DxgkInitializeDisplayOnlyDriver from dxgkrnl

spec2def(kmdod.sys kmdod.spec ADD_IMPORTLIB)

# Target Windows 8 (WDDM 1.2) -- DOD requires at least Win8.
remove_definitions(-D_WIN32_WINNT=0x502)
remove_definitions(-DWINVER=0x502)
add_definitions(-D_WIN32_WINNT=0x0602 -DWINVER=0x0602)
add_definitions(-DNTDDI_VERSION=0x06020000)

# The sample uses C++ (operator new, classes)
set(CMAKE_CXX_STANDARD 14)

# Suppress warnings in the unmodified Microsoft sample code
add_compile_options(-Wno-unused-parameter -Wno-missing-field-initializers)

list(APPEND SOURCE
    bdd.cxx
    bdd_ddi.cxx
    bdd_dmm.cxx
    bdd_util.cxx
    bltfuncs.cxx
    blthw.cxx
    memory.cxx)

add_library(kmdod MODULE
    ${SOURCE}
    kmdod.rc
    ${CMAKE_CURRENT_BINARY_DIR}/kmdod.def)

# Force-include the ReactOS adaptation header before all source files.
# This provides shims for NonPagedPoolNx, ExAllocatePool2, MmMapIoSpaceEx, etc.
target_compile_options(kmdod PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/kmdod_reactos.h>")

# Kernel-mode C++ flags: no exceptions, no RTTI
target_compile_options(kmdod PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>")

# Suppress warnings and allow const mismatches from the original Microsoft code
target_compile_options(kmdod PRIVATE -Wno-write-strings -Wno-narrowing -fpermissive)

set_module_type(kmdod kernelmodedriver)
add_importlibs(kmdod dxgkrnl ntoskrnl hal)
add_cd_file(TARGET kmdod DESTINATION reactos/system32/drivers FOR all)
