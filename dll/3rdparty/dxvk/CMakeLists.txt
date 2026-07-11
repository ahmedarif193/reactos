set(DXVK_ARTIFACT_DIR "${REACTOS_BINARY_DIR}/dxvk-artifacts")

# The system dxgi.dll must be the native dxgkrnl-backed one (dll/directx/wine/
# dxgi), NOT DXVK's Vulkan-based dxgi: shipping the latter as system32\dxgi.dll
# created a dxgi->Vulkan->driver cycle (v3dv WSI self-deadlock). DXVK stays the
# D3D11/D3D10 provider for now (it is coupled to its own dxgi via a shared
# DxvkInstance, and DX11 apps aren't required for boot/desktop; full parity is
# per-app DXVK beside native system dxgi).
foreach(_dxvk_file
    d3d11.dll
    d3d10core.dll)
    add_cd_file(
        FILE "${DXVK_ARTIFACT_DIR}/${_dxvk_file}"
        DESTINATION reactos/system32
        FOR all)
endforeach()
