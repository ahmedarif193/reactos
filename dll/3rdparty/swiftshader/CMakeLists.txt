set(SWIFTSHADER_ARTIFACT_DIR "${REACTOS_BINARY_DIR}/swiftshader-artifacts")

foreach(_swiftshader_file
    libvk_swiftshader.dll
    vk_swiftshader.dll
    libc++.dll
    libunwind.dll
    vk_swiftshader_icd.json)
    add_cd_file(
        FILE "${SWIFTSHADER_ARTIFACT_DIR}/${_swiftshader_file}"
        DESTINATION reactos/system32
        FOR all)
endforeach()
