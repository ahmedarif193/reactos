
if(FREELDR_HAS_BIOS_BOOT)
    add_subdirectory(bootsect)
    add_subdirectory(fdebug)
endif()
add_subdirectory(freeldr)
if(FREELDR_HAS_BIOS_BOOT)
    add_subdirectory(install)
endif()
