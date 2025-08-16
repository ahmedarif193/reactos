# ARM64 Stub Toolchain for Testing Bootloader Build
# This uses the Linux ARM64 toolchain as a temporary workaround

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Use Linux ARM64 toolchain
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_OBJCOPY aarch64-linux-gnu-objcopy)

# Force static linking
set(CMAKE_EXE_LINKER_FLAGS "-static")

# Define architecture
set(ARCH "arm64" CACHE STRING "Architecture")

# Disable features that require Windows toolchain
set(NO_ROSSYM TRUE)
set(USE_DUMMY_PSEH TRUE)

# UEFI application settings
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffreestanding -fno-stack-protector -fno-builtin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffreestanding -fno-stack-protector -fno-builtin")

# Suppress missing tools warnings
macro(require_program varname execname)
    find_program(${varname} ${execname})
    if(NOT ${varname})
        message(WARNING "${execname} not found - using stub")
        set(${varname} "echo")
    endif()
endmacro()

# Stub for Windows-specific tools
set(CMAKE_MC_COMPILER echo)
set(CMAKE_RC_COMPILER echo)
set(CMAKE_DLLTOOL echo)

message(WARNING "Using ARM64 stub toolchain - this is for testing only!")
message(WARNING "The resulting binaries may not be proper UEFI/PE executables")