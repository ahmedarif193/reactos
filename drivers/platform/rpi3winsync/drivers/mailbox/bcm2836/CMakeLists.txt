# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build the unchanged Raspberry Pi Windows mailbox driver sources
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

set(RPIQ_SOURCE
    device.c
    driver.c
    init.c
    interrupt.c
    ioctl.c
    mailbox.c
    rpiq.rc)

add_library(rpiq MODULE ${RPIQ_SOURCE})
set_module_type(rpiq kmdfdriver)

get_filename_component(_rpi3winsync_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
target_include_directories(rpiq BEFORE PRIVATE
    ${_rpi3winsync_root}/reactos-compat/mailbox
    ${_rpi3winsync_root}/reactos-compat
    ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(rpiq PRIVATE
        -Wno-unknown-pragmas
        -Wno-microsoft-anon-tag
        -Wno-missing-braces)
endif()

target_link_libraries(rpiq ntoskrnl_vista libcntpr ${PSEH_LIB})
add_importlibs(rpiq ntoskrnl hal)

add_cd_file(TARGET rpiq DESTINATION reactos/system32/drivers FOR all)

# StampInf normally expands the architecture token in the synchronized INF.
# Generate the equivalent ReactOS ARM64 install metadata in the output tree.
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rpiq.inf.in" CONTENT [=[
[Version]
Signature = "$Windows NT$"
Class = System
ClassGuid = {4d36e97d-e325-11ce-bfc1-08002be10318}
Provider = %ProviderString%

[DestinationDirs]
DefaultDestDir = 12

[Manufacturer]
%MSFT% = Standard,NTarm64

[Standard.NTarm64]
%rpiq.DeviceDesc% = rpiq_Device, ACPI\BCM2849
%rpiq.DeviceDesc% = rpiq_Device, ACPI\RPIQ

[rpiq_Device.NT]
CopyFiles = Drivers_Dir

[Drivers_Dir]
rpiq.sys

[rpiq_Device.NT.Services]
AddService = rpiq,0x00000002,rpiq_Service_Inst

[rpiq_Service_Inst]
DisplayName = %rpiq.SVCDESC%
ServiceType = 1
StartType = 0
ErrorControl = 1
ServiceBinary = %12%\rpiq.sys
LoadOrderGroup = Boot Bus Extender

[Strings]
MSFT = "Microsoft"
ProviderString = "MS-IOT"
rpiq.DeviceDesc = "RPIQ Driver"
rpiq.SVCDESC = "RPIQ Service"
]=])
utf16le_convert(
    "${CMAKE_CURRENT_BINARY_DIR}/rpiq.inf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/rpiq.inf")
add_custom_target(rpiq_inf_files DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/rpiq.inf")
add_cd_file(
    FILE "${CMAKE_CURRENT_BINARY_DIR}/rpiq.inf"
    TARGET rpiq_inf_files
    DESTINATION reactos/inf
    FOR all)

set_target_properties(rpiq PROPERTIES FOLDER "Drivers/Platform/Raspberry Pi 3")
