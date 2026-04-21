
add_definitions(
    -D__WINESRC__
    -D_WINE
    -D_KERNELBASE_)

include_directories(BEFORE ${REACTOS_SOURCE_DIR}/sdk/include/wine)

#add_subdirectory(wine)
# TODO: ReactOS Isn't ready for Kernelbase.dll
