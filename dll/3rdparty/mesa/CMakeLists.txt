
# Mesa GL-on-Vulkan (zink) ICD, cross-built from submodules/mesa
# (build-reactos-arm64: wgl frontend + zink gallium driver, ARM64 PE).
# Staged from the artifacts directory like the SwiftShader Vulkan ICD;
# registered as the custom OpenGL ICD via hivesft.inf/hivedef.inf.

set(MESA_ARTIFACT_DIR "${REACTOS_BINARY_DIR}/mesa-artifacts")

foreach(_mesa_file
    libgallium_wgl.dll
    libvulkan_broadcom.dll
    libgallium_wgl_v3d.dll
    libz-1.dll)
    add_cd_file(
        FILE "${MESA_ARTIFACT_DIR}/${_mesa_file}"
        DESTINATION reactos/system32
        FOR all)
endforeach()

# lavapipe: generic LLVMpipe-based software Vulkan ICD (has nullDescriptor, which
# zink requires and SwiftShader lacks) so opengl32->MesaZink->zink->lavapipe gives
# real GL 3.x/4.x to every app on QEMU-generic (no GPU). Registered as the Vulkan
# DriverName in hivesft.inf. LiveCD only (libLLVM is large; keep it out of the CAB).
foreach(_lvp_file
    vulkan_lvp.dll
    libLLVM-22.dll
    libSPIRV-Tools.dll
    libffi-8.dll
    libiconv-2.dll
    libxml2-16.dll
    libzstd.dll
    zlib1.dll
    libgallium_wgl26.dll
    libsystre-0.dll
    libtre-5.dll
    libintl-8.dll)
    add_cd_file(
        FILE "${MESA_ARTIFACT_DIR}/${_lvp_file}"
        DESTINATION reactos/system32
        FOR livecd preinstall)
endforeach()
