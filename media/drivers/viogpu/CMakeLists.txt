
# Red Hat VirtIO GPU Display Only Driver (viogpudo)
# Prebuilt Windows 10 amd64 DOD binary from virtio-win 0.1.285.
# This is the old display-only path. Vendored upstream sources for the newer
# guest-side 3D candidate live under .vendor/kvm-guest-drivers-windows-pr943.
# PCI device: VEN_1AF4&DEV_1050 (QEMU/KVM virtio-gpu)

# Driver binary -> reactos/system32/drivers/
add_cd_file(
    FILE ${CMAKE_CURRENT_SOURCE_DIR}/viogpudo.sys
    DESTINATION reactos/system32/drivers
    FOR livecd)

# Signing catalog -> reactos/inf/ (alongside the INF)
add_cd_file(
    FILE ${CMAKE_CURRENT_SOURCE_DIR}/viogpudo.cat
    DESTINATION reactos/inf
    FOR livecd)

# INF file: convert ANSI -> UTF-16LE, then place in reactos/inf/
set(_viogpudo_inf ${CMAKE_CURRENT_BINARY_DIR}/viogpudo.inf)
utf16le_convert(${CMAKE_CURRENT_SOURCE_DIR}/viogpudo.inf ${_viogpudo_inf})
add_custom_target(viogpudo_inf DEPENDS ${_viogpudo_inf})
add_cd_file(
    FILE ${_viogpudo_inf}
    TARGET viogpudo_inf
    DESTINATION reactos/inf
    FOR livecd)
