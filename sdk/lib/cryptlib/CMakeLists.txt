
list(APPEND SOURCE
    aes.c
    des.c
    md4.c
    md5.c
    mvAesAlg.c
    rc4.c
    sha1.c
    util.c)

add_library(cryptlib ${SOURCE})
add_dependencies(cryptlib xdk)
if(ARCH STREQUAL "arm64ec" AND COMMAND set_arm64ec_native_target)
    add_library(cryptlib_native ${SOURCE})
    set_arm64ec_native_target(cryptlib_native)
    add_dependencies(cryptlib_native xdk)
endif()
